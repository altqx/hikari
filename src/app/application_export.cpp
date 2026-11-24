#include "hikari/app/application.h"

#include "hikari/application/dialog_memory.h"
#include "hikari/application/edit_session.h"
#include "hikari/application/export_steps.h"
#include "hikari/application/macro_transaction.h"
#include "hikari/application/export_format_ids.h"
#include "hikari/application/style_manager.h"
#include "hikari/backends/text_encoding.h"
#include "hikari/core/export.h"
#include "hikari/core/srv3.h"
#include "hikari/core/text_projection.h"
#include "automation_dialog_controller.h"
#include "project_file.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringList>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <memory>
#include <set>

namespace hikari::app {

namespace {

using core::ExportFormat;
using core::ExportLoss;

struct ExportFormatRow {
    const char *id;
    ExportFormat format;
    const char *extension;
};

constexpr ExportFormatRow kExportFormats[] = {
    {"srt", ExportFormat::Srt, "srt"},           {"vtt", ExportFormat::WebVtt, "vtt"},
    {"sbv", ExportFormat::Sbv, "sbv"},           {"text", ExportFormat::PlainText, "txt"},
    {"microdvd", ExportFormat::MicroDvd, "sub"}, {"mpl2", ExportFormat::Mpl2, "txt"},
    {"tmplayer", ExportFormat::TMPlayer, "txt"}, {"ttml", ExportFormat::Ttml, "ttml"},
    {"dfxp", ExportFormat::Dfxp, "dfxp"},        {"srv3", ExportFormat::Srv3, "ytt"},
};

constexpr std::pair<const char *, bool core::ExportOptions::*> kExportFlags[] = {
    {"mergeOverlaps", &core::ExportOptions::mergeOverlaps},
    {"srtFontColour", &core::ExportOptions::srtFontColour},
    {"srtAlignmentTags", &core::ExportOptions::srtAlignmentTags},
    {"vttPositions", &core::ExportOptions::vttPositions},
    {"vttStyles", &core::ExportOptions::vttStyles},
    {"vttKaraoke", &core::ExportOptions::vttKaraoke},
    {"textTimes", &core::ExportOptions::textTimes},
    {"textActors", &core::ExportOptions::textActors},
    {"ttmlFrames", &core::ExportOptions::ttmlFrames},
};

constexpr int kRememberedExports = 100;

bool takesAnEncoding(ExportFormat format)
{
    switch (format) {
    case ExportFormat::Srt:
    case ExportFormat::Sbv:
    case ExportFormat::PlainText:
    case ExportFormat::MicroDvd:
    case ExportFormat::Mpl2:
    case ExportFormat::TMPlayer: return true;
    default: return false;
    }
}

bool offeredEncoding(const QString &name)
{
    const std::string wanted = name.toStdString();
    return std::ranges::any_of(hikari::backends::reopenEncodings(), [&](const auto &e) { return e.name == wanted; });
}

const ExportFormatRow *exportFormatNamed(const QString &id)
{
    const auto it = std::ranges::find_if(kExportFormats, [&](const auto &row) { return id == QLatin1String(row.id); });
    return it == std::end(kExportFormats) ? nullptr : &*it;
}

QString rowKey(const QString &row)
{
    return row.left(row.indexOf(u'\t'));
}

QStringList currentChoices(QStringList rows)
{
    for (QString &row : rows) {
        const qsizetype tab = row.indexOf(u'\t');
        QJsonObject choice = QJsonDocument::fromJson(row.mid(tab + 1).toUtf8()).object();
        const QString format = choice.value(QStringLiteral("format")).toString();
        const std::string current(application::currentExportFormat(format.toStdString()));
        if (tab < 0 || current == format.toStdString())
            continue;
        choice.insert(QStringLiteral("format"), QString::fromStdString(current));
        row = row.left(tab + 1) + QString::fromUtf8(QJsonDocument(choice).toJson(QJsonDocument::Compact));
    }
    return rows;
}

QJsonObject rememberedRow(const QStringList &rows, const QString &key)
{
    for (const QString &row : rows)
        if (row.contains(u'\t') && rowKey(row) == key)
            return QJsonDocument::fromJson(row.mid(row.indexOf(u'\t') + 1).toUtf8()).object();
    return {};
}

QJsonObject rememberedExport(const QStringList &rows, const QString &key)
{
    const QJsonObject exact = rememberedRow(rows, key);
    return exact.isEmpty() ? rememberedRow(rows, {}) : exact;
}

QJsonObject overlaid(QJsonObject under, const QJsonObject &over)
{
    for (auto it = over.begin(); it != over.end(); ++it)
        under.insert(it.key(), it.value());
    return under;
}

// This file's choice, then the Project's `[export]`, then the last export.
QJsonObject startingExport(const QStringList &rows, const QString &key, const std::string &project)
{
    const QJsonObject projectDefault = QJsonDocument::fromJson(QByteArray::fromStdString(project)).object();
    const QJsonObject exact = key.isEmpty() ? QJsonObject() : rememberedRow(rows, key);
    return overlaid(overlaid(rememberedRow(rows, {}), projectDefault), exact);
}

QStringList rememberedWith(const QStringList &rows, const QString &key, const QJsonObject &choice)
{
    const QString json = QString::fromUtf8(QJsonDocument(choice).toJson(QJsonDocument::Compact));
    QStringList out;
    if (!key.isEmpty())
        out << key + u'\t' + json;
    out << u'\t' + json;
    for (const QString &row : rows)
        if (out.size() < kRememberedExports && rowKey(row) != key && !rowKey(row).isEmpty())
            out << row;
    return out;
}

bool isTtml(ExportFormat format)
{
    return format == ExportFormat::Ttml || format == ExportFormat::Dfxp;
}

bool framesIn(ExportFormat format, const core::ExportOptions &options)
{
    return format == ExportFormat::MicroDvd ||
           (isTtml(format) && (options.ttmlFrames || options.ttmlProfile == core::TtmlProfile::Netflix));
}

bool languageTag(const QString &tag)
{
    static const QRegularExpression pattern(QStringLiteral("^([A-Za-z]{1,8}(-[A-Za-z0-9]{1,8})*)?$"));
    return pattern.match(tag).hasMatch();
}

QString lossText(ExportLoss loss, ExportFormat format)
{
    switch (loss) {
    case ExportLoss::Drawing:
        return QCoreApplication::translate("HikariSub.Export", "Drawing removed");
    case ExportLoss::Clip:
        return QCoreApplication::translate("HikariSub.Export", "Clip removed");
    case ExportLoss::Transform:
        return QCoreApplication::translate("HikariSub.Export", "Animation (\\t) removed");
    case ExportLoss::Movement:
        return format == ExportFormat::WebVtt ? QCoreApplication::translate("HikariSub.Export", "Movement kept as its start position") : QCoreApplication::translate("HikariSub.Export", "Movement removed");
    case ExportLoss::Fade:
        return QCoreApplication::translate("HikariSub.Export", "Fade removed");
    case ExportLoss::Rotation:
        return QCoreApplication::translate("HikariSub.Export", "Rotation or shearing removed");
    case ExportLoss::Font:
        return format == ExportFormat::Srv3 ? QCoreApplication::translate("HikariSub.Export", "Scale or spacing tags removed") : QCoreApplication::translate("HikariSub.Export", "Font, size, scale or spacing tags removed");
    case ExportLoss::Outline:
        return format == ExportFormat::Srv3 ? QCoreApplication::translate("HikariSub.Export", "Border, shadow or blur tags removed") : QCoreApplication::translate("HikariSub.Export", "Border, shadow, blur, transparency or other colours removed");
    case ExportLoss::Colour:
        return QCoreApplication::translate("HikariSub.Export", "Text colour removed");
    case ExportLoss::Formatting:
        return format == ExportFormat::Srv3 ? QCoreApplication::translate("HikariSub.Export", "Strikeout removed") : QCoreApplication::translate("HikariSub.Export", "Some bold, italic, underline or strikeout removed");
    case ExportLoss::Position:
        return isTtml(format) ? QCoreApplication::translate("HikariSub.Export", "Position approximated by the nearest region") : QCoreApplication::translate("HikariSub.Export", "Position or alignment removed");
    case ExportLoss::Karaoke:
        return QCoreApplication::translate("HikariSub.Export", "Karaoke timing removed");
    case ExportLoss::Effect:
        return QCoreApplication::translate("HikariSub.Export", "Scrolling effect removed");
    case ExportLoss::Merged:
        return QCoreApplication::translate("HikariSub.Export", "Merged with overlapping lines");
    }
    return {};
}

QString lossKind(ExportLoss loss)
{
    switch (loss) {
    case ExportLoss::Drawing:
        return QCoreApplication::translate("HikariSub.Export", "Drawing");
    case ExportLoss::Clip:
        return QCoreApplication::translate("HikariSub.Export", "Clip");
    case ExportLoss::Transform:
        return QCoreApplication::translate("HikariSub.Export", "Transform");
    case ExportLoss::Movement:
        return QCoreApplication::translate("HikariSub.Export", "Movement");
    case ExportLoss::Fade:
        return QCoreApplication::translate("HikariSub.Export", "Fade");
    case ExportLoss::Rotation:
        return QCoreApplication::translate("HikariSub.Export", "Rotation");
    case ExportLoss::Font:
        return QCoreApplication::translate("HikariSub.Export", "Font");
    case ExportLoss::Outline:
        return QCoreApplication::translate("HikariSub.Export", "Outline");
    case ExportLoss::Colour:
        return QCoreApplication::translate("HikariSub.Export", "Colour");
    case ExportLoss::Formatting:
        return QCoreApplication::translate("HikariSub.Export", "Formatting");
    case ExportLoss::Position:
        return QCoreApplication::translate("HikariSub.Export", "Position");
    case ExportLoss::Karaoke:
        return QCoreApplication::translate("HikariSub.Export", "Karaoke");
    case ExportLoss::Effect:
        return QCoreApplication::translate("HikariSub.Export", "Effect");
    case ExportLoss::Merged:
        return QCoreApplication::translate("HikariSub.Export", "Merged");
    }
    return {};
}

constexpr ExportLoss kLossOrder[] = {
    ExportLoss::Drawing,  ExportLoss::Clip,    ExportLoss::Transform,  ExportLoss::Movement, ExportLoss::Fade,
    ExportLoss::Rotation, ExportLoss::Font,    ExportLoss::Outline,    ExportLoss::Colour,   ExportLoss::Formatting,
    ExportLoss::Position, ExportLoss::Karaoke, ExportLoss::Effect,     ExportLoss::Merged,
};

QString srv3Colour(const std::optional<std::uint32_t> &rgb)
{
    return rgb ? u'#' + QStringLiteral("%1").arg(*rgb, 6, 16, QLatin1Char('0')).toUpper() : QString();
}

std::optional<std::uint32_t> srv3Colour(const QVariant &value)
{
    const QString text = value.toString();
    bool ok = false;
    const uint rgb = text.mid(1).toUInt(&ok, 16);
    return text.size() == 7 && text.startsWith(u'#') && ok ? std::optional<std::uint32_t>(rgb) : std::nullopt;
}

QVariantMap srv3Values(const core::Srv3StyleOptions &o)
{
    return {{QStringLiteral("shadows"), int(o.shadows)},
            {QStringLiteral("karaoke"), o.karaoke},
            {QStringLiteral("text"), srv3Colour(o.currentWordText)},
            {QStringLiteral("outline"), srv3Colour(o.currentWordOutline)},
            {QStringLiteral("shadow"), srv3Colour(o.currentWordShadow)}};
}

core::Srv3StyleOptions srv3Options(const std::u16string &style, const QVariantMap &values)
{
    return {.style = style,
            .shadows = static_cast<std::uint8_t>(values.value(QStringLiteral("shadows")).toInt() & 0xF),
            .karaoke = values.value(QStringLiteral("karaoke")).toBool(),
            .currentWordText = srv3Colour(values.value(QStringLiteral("text"))),
            .currentWordOutline = srv3Colour(values.value(QStringLiteral("outline"))),
            .currentWordShadow = srv3Colour(values.value(QStringLiteral("shadow")))};
}

std::vector<std::u8string> styleNames(const application::EditSession &session)
{
    std::vector<std::u8string> names;
    if (const auto styles = application::documentStyles(session))
        for (const auto &style : *styles)
            if (std::ranges::find(names, style.name) == names.end())
                names.push_back(style.name);
    return names;
}

QVariantList srv3Rows(const application::EditSession &session, const std::set<core::LineId> &shown)
{
    const auto styles = application::documentStyles(session);
    if (!styles)
        return {};
    std::set<std::u8string> inAll, inSelected, inShown;
    for (const auto *line : session.document().lines()) {
        if (line->comment || line->unparsed)
            continue;
        inAll.insert(line->style);
        if (session.selection().selected.contains(line->id))
            inSelected.insert(line->style);
        if (shown.contains(line->id))
            inShown.insert(line->style);
    }
    const auto saved = core::srv3StyleOptions(session.document());
    QVariantList rows;
    for (const auto &name : styleNames(session)) {
        const auto &style = *std::ranges::find(*styles, name, &core::StyleValues::name);
        const std::u16string wide = core::toUtf16(name);
        const auto defaults = core::srv3DefaultOptions(wide);
        const auto it = std::ranges::find(saved, wide, &core::Srv3StyleOptions::style);
        rows << QVariantMap{{QStringLiteral("name"), QString::fromStdU16String(wide)},
                            {QStringLiteral("builtIn"), core::isSrv3BuiltInStyle(wide)},
                            {QStringLiteral("hasShadow"), QString::fromUtf8(reinterpret_cast<const char *>(style.shadow.data()),
                                                                            qsizetype(style.shadow.size())).toDouble() > 0},
                            {QStringLiteral("all"), inAll.contains(name)},
                            {QStringLiteral("selected"), inSelected.contains(name)},
                            {QStringLiteral("shown"), inShown.contains(name)},
                            {QStringLiteral("value"), srv3Values(it != saved.end() ? *it : defaults)},
                            {QStringLiteral("default"), srv3Values(defaults)}};
    }
    return rows;
}

std::vector<core::Srv3StyleOptions> srv3Wanted(const application::EditSession &session, const QVariantList &rows,
                                               const std::vector<core::Srv3StyleOptions> &saved)
{
    const auto names = styleNames(session);
    std::vector<core::Srv3StyleOptions> wanted;
    for (const auto &name : names) {
        const std::u16string wide = core::toUtf16(name);
        const auto row = std::ranges::find_if(rows, [&](const QVariant &r) {
            return r.toMap().value(QStringLiteral("name")).toString() == QString::fromStdU16String(wide);
        });
        const auto savedIt = std::ranges::find(saved, wide, &core::Srv3StyleOptions::style);
        if (row == rows.end()) {
            if (savedIt != saved.end())
                wanted.push_back(*savedIt);
            continue;
        }
        auto o = srv3Options(wide, row->toMap().value(QStringLiteral("value")).toMap());
        if (o != core::srv3DefaultOptions(wide))
            wanted.push_back(std::move(o));
    }
    for (const auto &o : saved)
        if (std::ranges::find(names, core::toUtf8(o.style)) == names.end())
            wanted.push_back(o);
    return wanted;
}

int srv3StylesChanged(const std::vector<core::Srv3StyleOptions> &before, const std::vector<core::Srv3StyleOptions> &after)
{
    const auto entry = [](const std::vector<core::Srv3StyleOptions> &list, const std::u16string &style) {
        const auto it = std::ranges::find(list, style, &core::Srv3StyleOptions::style);
        return it == list.end() ? std::nullopt : std::optional(*it);
    };
    std::set<std::u16string> styles;
    for (const auto *list : {&before, &after})
        for (const auto &o : *list)
            styles.insert(o.style);
    return static_cast<int>(std::ranges::count_if(styles, [&](const auto &style) { return entry(before, style) != entry(after, style); }));
}

QVariantMap problem(const QString &text)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("problem"), text}};
}

QString stateText(application::ExportStepOutcome::State state)
{
    using State = application::ExportStepOutcome::State;
    switch (state) {
    case State::Done:
        return QStringLiteral("done");
    case State::Failed:
        return QStringLiteral("failed");
    case State::NotRun:
        return QStringLiteral("notRun");
    case State::Skipped:
        return QStringLiteral("skipped");
    case State::Stopped:
        return QStringLiteral("stopped");
    }
    return {};
}

QString problemText(application::ExportStepOutcome::Problem problem)
{
    using Problem = application::ExportStepOutcome::Problem;
    switch (problem) {
    case Problem::None:
        return {};
    case Problem::ScriptError:
        return QStringLiteral("scriptError");
    case Problem::NotApplied:
        return QStringLiteral("notApplied");
    case Problem::NoTimecodes:
        return QStringLiteral("noTimecodes");
    case Problem::BadFrameRate:
        return QStringLiteral("badFrameRate");
    case Problem::Busy:
        return QStringLiteral("busy");
    }
    return {};
}

QVariantList stepRows(const std::vector<application::ExportStepOutcome> &steps)
{
    QVariantList rows;
    for (const auto &step : steps)
        rows << QVariantMap{{QStringLiteral("id"), QString::fromStdString(step.id)},
                            {QStringLiteral("name"), QString::fromStdString(step.name)},
                            {QStringLiteral("state"), stateText(step.state)},
                            {QStringLiteral("lines"), step.lines},
                            {QStringLiteral("problem"), problemText(step.problem)},
                            {QStringLiteral("error"), QString::fromStdString(step.error)}};
    return rows;
}

}

struct Application::ExportWrite {
    QString path, formatId;
    ExportFormat format;
    core::ExportOptions options;
    QString lines, language;
    std::string encoding;
    bool writeUnencodable = false;
    std::vector<core::Srv3StyleOptions> srv3Saved;
    std::optional<std::vector<core::Srv3StyleOptions>> srv3Changed;
};

QString Application::exportKey(application::DocumentId document) const
{
    if (const auto destination = m_files->destination(document); destination && !destination->value.empty())
        return QFileInfo(QString::fromStdString(destination->value)).absoluteFilePath();
    if (const auto import = m_importPaths.find(document.value); import != m_importPaths.end())
        return QFileInfo(import->second).absoluteFilePath();
    return {};
}

QVariantMap Application::exportSetup() const
{
    const auto target = m_workspace.editingTarget();
    const auto *session = target ? m_files->session(*target) : nullptr;
    if (!session)
        return {};
    const QString key = exportKey(*target);
    const auto *project = views(m_workspace.window(*target).value_or(m_workspace.activeWindow())).settings->project();
    const QJsonObject saved =
        startingExport(currentChoices(m_settings->list("export.choices")), key, project ? project->exportJson() : std::string());

    const core::ExportOptions defaults;
    QVariantMap options;
    for (const auto &[name, flag] : kExportFlags) {
        const QJsonValue v = saved.value(QLatin1String(name));
        options.insert(QLatin1String(name), v.isBool() ? v.toBool() : defaults.*flag);
    }
    options.insert(QStringLiteral("ttmlNetflix"), saved.value(QStringLiteral("ttmlNetflix")).toBool());
    options.insert(QStringLiteral("language"), saved.value(QStringLiteral("language")).toString());
    const QString encoding = saved.value(QStringLiteral("encoding")).toString();
    options.insert(QStringLiteral("encoding"), offeredEncoding(encoding) ? encoding : QStringLiteral("UTF-8"));
    const QString lines = saved.value(QStringLiteral("lines")).toString();
    options.insert(QStringLiteral("lines"), lines == QLatin1String("selected") || lines == QLatin1String("shown")
                                                ? lines
                                                : QStringLiteral("all"));
    const auto &video = activeMedia().video->session();
    const float frameMs =
        video.state() == application::VideoSession::State::Ready ? video.legacyTimebase().frameDuration() : 0.f;
    const double videoFps = frameMs > 0.f ? 1000.0 / frameMs : 0.0;
    const double savedFps = saved.value(QStringLiteral("fps")).toDouble();
    options.insert(QStringLiteral("fps"), savedFps >= 1 ? savedFps : videoFps >= 1 ? videoFps : defaults.fps);

    int all = 0, selected = 0, shown = 0;
    const auto shownIds = shell().actionLines();
    const std::set<core::LineId> shownSet(shownIds.begin(), shownIds.end());
    for (const auto *line : session->document().lines()) {
        if (line->comment || line->unparsed)
            continue;
        ++all;
        selected += session->selection().selected.contains(line->id) ? 1 : 0;
        shown += shownSet.contains(line->id) ? 1 : 0;
    }

    QVariantMap extensions;
    QStringList encodingFormats;
    for (const auto &row : kExportFormats) {
        extensions.insert(QLatin1String(row.id), QLatin1String(row.extension));
        if (takesAnEncoding(row.format))
            encodingFormats << QLatin1String(row.id);
    }
    QVariantList encodings;
    for (const auto &e : backends::reopenEncodings())
        encodings << QVariantMap{{QStringLiteral("name"), QString::fromUtf8(e.name.data(), qsizetype(e.name.size()))},
                                 {QStringLiteral("label"), QString::fromUtf8(e.label.data(), qsizetype(e.label.size()))}};
    QVariantList plugins;
    for (const auto &f : pluginFormats(false)) {
        const QString id = QString::fromStdString(f.id);
        extensions.insert(id, QString::fromStdString(f.format.extensions.front()));
        plugins << QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("text"), formatText(f)}};
    }
    QString format = saved.value(QStringLiteral("format")).toString();
    if (!extensions.contains(format))
        format = QStringLiteral("srt");
    const QFileInfo file(key);
    return {{QStringLiteral("format"), format},
            {QStringLiteral("options"), options},
            {QStringLiteral("extensions"), extensions},
            {QStringLiteral("pluginFormats"), plugins},
            {QStringLiteral("encodings"), encodings},
            {QStringLiteral("encodingFormats"), encodingFormats},
            {QStringLiteral("all"), all},
            {QStringLiteral("selected"), selected},
            {QStringLiteral("shown"), shown},
            {QStringLiteral("videoFps"), videoFps},
            {QStringLiteral("srv3Styles"), srv3Rows(*session, shownSet)},
            {QStringLiteral("folder"), key.isEmpty() ? QUrl() : QUrl::fromLocalFile(file.absolutePath())},
            {QStringLiteral("name"), key.isEmpty() ? QString() : file.completeBaseName()}};
}

QVariantMap Application::exportTo(const QUrl &file, const QString &formatId, const QVariantMap &values)
{
    const auto target = m_workspace.editingTarget();
    const auto *session = target ? m_files->session(*target) : nullptr;
    const auto *format = exportFormatNamed(formatId);
    const auto plugin = format || !m_pluginRuntime ? std::nullopt : m_pluginRuntime->format(formatId.toStdString());
    QString path = file.toLocalFile();
    if (!session || (!format && !(plugin && plugin->format.exports)) || path.isEmpty())
        return problem(QCoreApplication::translate("HikariSub.Export", "There is nothing to export."));
    if (QFileInfo(path).isRelative())
        return problem(QCoreApplication::translate("HikariSub.Export", "Enter the file’s full path, or choose it with Browse…"));
    if (QFileInfo(path).suffix().isEmpty())
        path += u'.' + (format ? QLatin1String(format->extension) : QString::fromStdString(plugin->format.extensions.front()));
    if (QFileInfo(path).isDir())
        return problem(QCoreApplication::translate("HikariSub.Export", "“%1” is a folder.").arg(QDir::toNativeSeparators(path)));
    if (QFileInfo::exists(path) && !values.value(QStringLiteral("replace")).toBool())
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("exists"), true},
                {QStringLiteral("name"), QFileInfo(path).fileName()}};
    if (plugin)
        return exportPluginFormat(*target, *plugin, path, values);

    core::ExportOptions options;
    for (const auto &[name, flag] : kExportFlags)
        if (const auto v = values.constFind(QLatin1String(name)); v != values.cend())
            options.*flag = v->toBool();
    if (values.value(QStringLiteral("ttmlNetflix")).toBool())
        options.ttmlProfile = core::TtmlProfile::Netflix;
    const QString language = values.value(QStringLiteral("language")).toString().trimmed();
    if (isTtml(format->format) && !languageTag(language))
        return problem(QCoreApplication::translate("HikariSub.Export", "Enter a language code such as en or pt-BR, or leave it empty."));
    const QByteArray languageUtf8 = language.toUtf8();
    options.language.assign(reinterpret_cast<const char8_t *>(languageUtf8.constData()), languageUtf8.size());
    const double fps = values.value(QStringLiteral("fps")).toDouble();
    if (framesIn(format->format, options) && (!std::isfinite(fps) || fps < 1 || fps > 1000))
        return problem(QCoreApplication::translate("HikariSub.Export", "Enter a frame rate between 1 and 1000."));
    if (fps >= 1)
        options.fps = fps;
    QString encoding = values.value(QStringLiteral("encoding"), QStringLiteral("UTF-8")).toString();
    if (!takesAnEncoding(format->format))
        encoding = QStringLiteral("UTF-8");
    if (!offeredEncoding(encoding))
        return problem(QCoreApplication::translate("HikariSub.Export", "Choose an encoding from the list."));
    const QString lines = values.value(QStringLiteral("lines")).toString();
    if (lines == QLatin1String("selected")) {
        options.lines = session->selection().selected;
    } else if (lines == QLatin1String("shown")) {
        const auto shown = shell().actionLines();
        options.lines = std::set<core::LineId>(shown.begin(), shown.end());
    }

    const auto recipe = ui::ExportRecipes::recipeOf(values.value(QStringLiteral("steps")).toList());
    const bool withSteps = std::ranges::any_of(recipe.steps, &application::ExportStep::on);
    if (withSteps)
        commitTemplates();
    auto draft = activeViews().editorDocument == target ? editor().draftDocument() : std::nullopt;
    const auto srv3Saved = core::srv3StyleOptions(session->document());
    std::optional<std::vector<core::Srv3StyleOptions>> srv3Changed;
    if (format->format == ExportFormat::Srv3 && values.contains(QStringLiteral("srv3Styles"))) {
        auto wanted = srv3Wanted(*session, values.value(QStringLiteral("srv3Styles")).toList(), srv3Saved);
        if (wanted != srv3Saved) {
            if (!draft)
                draft = session->document();
            if (!core::setSrv3StyleOptions(*draft, wanted))
                return problem(QCoreApplication::translate("HikariSub.Export", "The style options can’t be saved in this file."));
            srv3Changed = std::move(wanted);
        }
    }
    const core::Document &document = draft ? *draft : session->document();
    auto write = std::make_shared<ExportWrite>(ExportWrite{path, formatId, format->format, options, lines, language,
                                                           encoding.toStdString(),
                                                           values.value(QStringLiteral("writeUnencodable")).toBool(),
                                                           srv3Saved, std::move(srv3Changed)});
    if (!withSteps)
        return writeExport(*target, *write, document);
    return exportAfterSteps(*target, std::move(write), recipe, document);
}

QVariantMap Application::writeExport(application::DocumentId target, const ExportWrite &write,
                                     const core::Document &document)
{
    const core::ExportResult result = core::exportDocument(document, write.format, write.options);
    if (!result.refusal.empty())
        return problem(QCoreApplication::translate("HikariSub.Export", "YouTube timed text couldn’t be made from this file: %1")
                           .arg(QString::fromStdString(result.refusal)));

    std::vector<std::byte> bytes = result.bytes;
    if (write.encoding != "UTF-8") {
        const QString label = QString::fromStdString(backends::encodingLabel(write.encoding));
        auto encoded = backends::encodeUtf8(result.bytes, write.encoding);
        if (!encoded)
            return problem(QCoreApplication::translate("HikariSub.Export", "The file couldn’t be written in %1.").arg(label));
        if (!encoded->unencodable.empty() && !write.writeUnencodable)
            return {{QStringLiteral("ok"), false},
                    {QStringLiteral("unencodable"),
                     QString::fromUcs4(encoded->unencodable.data(), qsizetype(encoded->unencodable.size()))},
                    {QStringLiteral("encoding"), label}};
        bytes = std::move(encoded->bytes);
    }
    QSaveFile out(write.path);
    if (!out.open(QIODevice::WriteOnly) ||
        out.write(reinterpret_cast<const char *>(bytes.data()), qint64(bytes.size())) != qint64(bytes.size()) ||
        !out.commit()) {
        QVariantMap failed = problem(QCoreApplication::translate("HikariSub.Export", "Couldn’t write %1: %2")
                                         .arg(QFileInfo(write.path).fileName(), out.errorString()));
        failed.insert(QStringLiteral("writeFailed"), true);
        failed.insert(QStringLiteral("details"), QDir::toNativeSeparators(write.path) + u'\n' + out.errorString());
        return failed;
    }

    int srv3Styles = 0;
    if (auto *editable = m_files->session(target); editable && write.srv3Changed) {
        const auto ran = editable->run(application::Command{
            "Set srv3 style options", editable->revision(), {},
            [&](core::Document &d) { return core::setSrv3StyleOptions(d, *write.srv3Changed); }});
        if (ran)
            srv3Styles = srv3StylesChanged(write.srv3Saved, *write.srv3Changed);
    }

    QJsonObject choice{{QStringLiteral("format"), write.formatId},
                       {QStringLiteral("lines"), write.lines.isEmpty() ? QStringLiteral("all") : write.lines}};
    for (const auto &[name, flag] : kExportFlags)
        choice.insert(QLatin1String(name), write.options.*flag);
    choice.insert(QStringLiteral("ttmlNetflix"), write.options.ttmlProfile == core::TtmlProfile::Netflix);
    const QJsonObject before = rememberedExport(currentChoices(m_settings->list("export.choices")), exportKey(target));
    choice.insert(QStringLiteral("language"),
                  isTtml(write.format) ? write.language : before.value(QStringLiteral("language")).toString());
    if (takesAnEncoding(write.format))
        choice.insert(QStringLiteral("encoding"), QString::fromStdString(write.encoding));
    else if (const QString kept = before.value(QStringLiteral("encoding")).toString(); !kept.isEmpty())
        choice.insert(QStringLiteral("encoding"), kept);
    if (framesIn(write.format, write.options))
        choice.insert(QStringLiteral("fps"), write.options.fps);
    else if (const double kept = before.value(QStringLiteral("fps")).toDouble(); kept >= 1)
        choice.insert(QStringLiteral("fps"), kept);
    m_settings->setValue(QStringLiteral("export.choices"),
                         rememberedWith(currentChoices(m_settings->list("export.choices")), exportKey(target), choice));

    std::map<std::uint64_t, int> rows;
    int row = 0;
    for (const auto *line : document.lines())
        rows.emplace(line->id.value, ++row);
    QVariantList notes;
    for (const auto &note : result.notes) {
        QStringList losses;
        for (const ExportLoss loss : kLossOrder)
            if (note.has(loss))
                losses << lossText(loss, write.format);
        notes << QVariantMap{{QStringLiteral("line"), QVariant::fromValue<qulonglong>(note.line.value)},
                             {QStringLiteral("number"), rows[note.line.value]},
                             {QStringLiteral("losses"), losses.join(QStringLiteral("; "))}};
    }
    QVariantList lost;
    for (const ExportLoss loss : kLossOrder) {
        const auto count = std::ranges::count_if(result.notes, [loss](const auto &note) { return note.has(loss); });
        if (count == 0)
            continue;
        const QString group = QStringLiteral("%1 · %2").arg(lossKind(loss)).arg(count);
        for (const auto &note : result.notes)
            if (note.has(loss))
                lost << QVariantMap{{QStringLiteral("group"), group},
                                    {QStringLiteral("line"), QVariant::fromValue<qulonglong>(note.line.value)},
                                    {QStringLiteral("number"), rows[note.line.value]},
                                    {QStringLiteral("text"), lossText(loss, write.format)}};
    }
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("file"), QUrl::fromLocalFile(write.path)},
            {QStringLiteral("document"), QVariant::fromValue<qulonglong>(target.value)},
            {QStringLiteral("written"), result.linesWritten},
            {QStringLiteral("comments"), result.commentsSkipped},
            {QStringLiteral("empty"), result.emptySkipped},
            {QStringLiteral("srv3Styles"), srv3Styles},
            {QStringLiteral("notes"), notes},
            {QStringLiteral("lost"), lost}};
}

QVariantMap Application::exportAfterSteps(application::DocumentId target, std::shared_ptr<ExportWrite> write,
                                          const application::ExportRecipe &recipe, const core::Document &document)
{
    const auto available = application::availableSteps(m_automation->scripts());
    std::set<core::LineId> before;
    for (const auto *line : document.lines())
        before.insert(line->id);
    auto returned = std::make_shared<bool>(false);
    auto answer = std::make_shared<std::optional<QVariantMap>>();
    QPointer<Application> self(this);
    const auto finished = [self, target, write, recipe, available, before = std::move(before), returned,
                           answer](application::ExportStepsResult result) {
        if (!self)
            return;
        self->m_stopExport = {};
        QVariantMap out;
        if (result.failed) {
            out = {{QStringLiteral("ok"), false}, {QStringLiteral("stepsFailed"), true}};
        } else if (result.stopped) {
            out = {{QStringLiteral("ok"), false}, {QStringLiteral("stopped"), true}};
        } else {
            if (write->options.lines)
                for (const auto *line : result.document.lines())
                    if (!before.contains(line->id))
                        write->options.lines->insert(line->id);
            out = self->writeExport(target, *write, result.document);
            if (out.value(QStringLiteral("ok")).toBool())
                self->m_exportFiltersToSave[target.value] = application::exportFiltersLine(recipe, available);
        }
        out.insert(QStringLiteral("steps"), stepRows(result.steps));
        if (*returned)
            emit self->pluginExportFinished(out);
        else
            *answer = std::move(out);
    };
    application::ExportStepsContext context{activeMedia().video->session().aegisubFrameTimes(), m_luaExportFilters.get()};
    context.started = [self](std::size_t step, std::size_t count, const std::string &name) {
        if (self)
            emit self->exportStepStarted(QString::fromStdString(name), int(step) + 1, int(count));
    };
    auto stop = application::runExportSteps(document, recipe, available, context, finished);
    *returned = true;
    if (*answer)
        return **answer;
    m_stopExport = std::move(stop);
    return {{QStringLiteral("ok"), false}, {QStringLiteral("pending"), true}};
}

void Application::stopExport()
{
    if (m_stopExport)
        m_stopExport();
}

QVariantMap Application::exportSteps() const
{
    const auto target = m_workspace.editingTarget();
    const auto *session = target ? m_files->session(*target) : nullptr;
    if (!session)
        return {};
    const auto available = application::availableSteps(m_automation->scripts());
    QVariantList steps;
    for (const auto &step : available)
        steps << QVariantMap{{QStringLiteral("id"), QString::fromStdString(step.id)},
                             {QStringLiteral("name"), QString::fromStdString(step.name)},
                             {QStringLiteral("priority"), step.priority},
                             {QStringLiteral("hasConfig"), step.hasConfig},
                             {QStringLiteral("script"), QString::fromStdString(step.script)}};
    const auto recorded = m_exportFiltersToSave.find(target->value);
    const auto names = recorded != m_exportFiltersToSave.end() ? application::exportFilterNames(recorded->second)
                                                              : application::exportFilterNames(session->document());
    return {{QStringLiteral("available"), steps},
            {QStringLiteral("newList"), ui::ExportRecipes::stepsOf(application::newStepList(available))},
            {QStringLiteral("fromFile"),
             names.empty() ? QVariantList()
                           : ui::ExportRecipes::stepsOf(application::recipeFromExportFilters(names, available))},
            {QStringLiteral("recipes"), m_exportRecipes->rows()},
            {QStringLiteral("hasProject"), m_exportRecipes->hasProject()}};
}

bool Application::askExportStepDialog(const QString &id, const QVariantMap &settings)
{
    const auto target = m_workspace.editingTarget();
    const auto *session = target ? m_files->session(*target) : nullptr;
    const auto parts = application::luaStepParts(id.toStdString());
    if (!session || !parts)
        return false;
    application::EditSession copy(session->document());
    const auto snapshot = application::snapshotForMacro(copy);
    if (!snapshot)
        return false;
    const auto saved = ui::ExportRecipes::recipeOf(
        QVariantList{QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("settings"), settings}}});
    QPointer<Application> self(this);
    return m_luaExportFilters->configure(
        parts->first, parts->second, *snapshot,
        [self, id, settings = saved.steps.front().settings](std::expected<application::DialogRequest, std::string> dialog) {
            if (!self)
                return;
            if (!dialog) {
                emit self->exportStepDialogReady(id, {{QStringLiteral("problem"), QString::fromStdString(dialog.error())}});
                return;
            }
            auto shown = application::initialDialogResult(*dialog);
            for (std::size_t i = 0; i < dialog->controls.size(); ++i)
                if (const auto it = settings.find(dialog->controls[i].name); it != settings.end())
                    if (const auto v = application::dialogValueOf(dialog->controls[i], it->second))
                        shown.values[i] = *v;
            int columns = 1;
            const QVariantList controls = ui::dialogControlRows(*dialog, shown, &columns);
            self->m_exportStepDialogs[id] = std::move(*dialog);
            emit self->exportStepDialogReady(id, {{QStringLiteral("controls"), controls}, {QStringLiteral("columns"), columns}});
        });
}

QVariantMap Application::exportStepSettings(const QString &id, const QVariantList &values) const
{
    const auto dialog = m_exportStepDialogs.find(id);
    if (dialog == m_exportStepDialogs.end())
        return {};
    const auto initial = application::initialDialogResult(dialog->second);
    application::ExportStep step{id.toStdString()};
    for (std::size_t i = 0; i < dialog->second.controls.size() && i < std::size_t(values.size()); ++i) {
        const auto &control = dialog->second.controls[i];
        if (control.name.empty() || application::dialogValueType(control.kind) == application::DialogValueType::None)
            continue;
        step.settings[control.name] = ui::dialogValueFrom(control, values[qsizetype(i)], initial.values[i]);
    }
    return ui::ExportRecipes::stepsOf({{step}}).value(0).toMap().value(QStringLiteral("settings")).toMap();
}

QVariantList Application::exportRecipe(const QString &name, const QString &tier) const
{
    const auto recipe = m_exportRecipes->recipe(name, ui::tierFromText(tier));
    return recipe ? ui::ExportRecipes::stepsOf(*recipe) : QVariantList();
}

bool Application::saveExportRecipe(const QString &name, const QString &tier, const QVariantList &steps)
{
    const auto t = ui::tierFromText(tier);
    return t && m_exportRecipes->save(name, *t, ui::ExportRecipes::recipeOf(steps));
}

bool Application::removeExportRecipe(const QString &name, const QString &tier)
{
    const auto t = ui::tierFromText(tier);
    return t && m_exportRecipes->remove(name, *t);
}

bool Application::renameExportRecipe(const QString &tier, const QString &from, const QString &to)
{
    const auto t = ui::tierFromText(tier);
    return t && m_exportRecipes->rename(*t, from, to);
}

QVariantMap Application::exportPluginFormat(application::DocumentId target, const PluginRuntime::Format &format,
                                            const QString &path, const QVariantMap &values)
{
    const auto *session = m_files->session(target);
    const QString lines = values.value(QStringLiteral("lines")).toString();
    std::optional<std::set<core::LineId>> only;
    if (lines == QLatin1String("selected")) {
        only = session->selection().selected;
    } else if (lines == QLatin1String("shown")) {
        const auto shown = shell().actionLines();
        only = std::set<core::LineId>(shown.begin(), shown.end());
    }
    QJsonObject choice = rememberedExport(currentChoices(m_settings->list("export.choices")), exportKey(target));
    choice.insert(QStringLiteral("format"), QString::fromStdString(format.id));
    choice.insert(QStringLiteral("lines"), lines.isEmpty() ? QStringLiteral("all") : lines);
    m_settings->setValue(QStringLiteral("export.choices"),
                         rememberedWith(currentChoices(m_settings->list("export.choices")), exportKey(target), choice));
    return exportThroughPlugin(target, format, path, only, values.value(QStringLiteral("fps")).toDouble());
}

}
