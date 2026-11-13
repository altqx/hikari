#include "hikari/app/application.h"

#include "hikari/application/edit_session.h"
#include "hikari/application/style_manager.h"
#include "hikari/core/export.h"
#include "hikari/core/srv3.h"
#include "hikari/core/text_projection.h"
#include "project_file.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringList>
#include <QUrl>

#include <algorithm>
#include <set>
#include <cmath>

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
    {"srt", ExportFormat::Srt, "srt"},      {"vtt", ExportFormat::WebVtt, "vtt"},
    {"sbv", ExportFormat::Sbv, "sbv"},      {"txt", ExportFormat::PlainText, "txt"},
    {"mdvd", ExportFormat::MicroDvd, "sub"}, {"mpl2", ExportFormat::Mpl2, "txt"},
    {"tmp", ExportFormat::TMPlayer, "txt"}, {"ttml", ExportFormat::Ttml, "ttml"},
    {"dfxp", ExportFormat::Dfxp, "dfxp"},   {"srv3", ExportFormat::Srv3, "ytt"},
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

const ExportFormatRow *exportFormatNamed(const QString &id)
{
    const auto it = std::ranges::find_if(kExportFormats, [&](const auto &row) { return id == QLatin1String(row.id); });
    return it == std::end(kExportFormats) ? nullptr : &*it;
}

QString rowKey(const QString &row)
{
    return row.left(row.indexOf(u'\t'));
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

}

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
        startingExport(m_settings->list("export.choices"), key, project ? project->exportJson() : std::string());

    const core::ExportOptions defaults;
    QVariantMap options;
    for (const auto &[name, flag] : kExportFlags) {
        const QJsonValue v = saved.value(QLatin1String(name));
        options.insert(QLatin1String(name), v.isBool() ? v.toBool() : defaults.*flag);
    }
    options.insert(QStringLiteral("ttmlNetflix"), saved.value(QStringLiteral("ttmlNetflix")).toBool());
    options.insert(QStringLiteral("language"), saved.value(QStringLiteral("language")).toString());
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

    QString format = saved.value(QStringLiteral("format")).toString();
    if (!exportFormatNamed(format))
        format = QStringLiteral("srt");
    QVariantMap extensions;
    for (const auto &row : kExportFormats)
        extensions.insert(QLatin1String(row.id), QLatin1String(row.extension));
    const QFileInfo file(key);
    return {{QStringLiteral("format"), format},
            {QStringLiteral("options"), options},
            {QStringLiteral("extensions"), extensions},
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
    QString path = file.toLocalFile();
    if (!session || !format || path.isEmpty())
        return problem(QCoreApplication::translate("HikariSub.Export", "There is nothing to export."));
    if (QFileInfo(path).isRelative())
        return problem(QCoreApplication::translate("HikariSub.Export", "Enter the file’s full path, or choose it with Browse…"));
    if (QFileInfo(path).suffix().isEmpty())
        path += u'.' + QLatin1String(format->extension);
    if (QFileInfo(path).isDir())
        return problem(QCoreApplication::translate("HikariSub.Export", "“%1” is a folder.").arg(QDir::toNativeSeparators(path)));
    if (QFileInfo::exists(path) && !values.value(QStringLiteral("replace")).toBool())
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("exists"), true},
                {QStringLiteral("name"), QFileInfo(path).fileName()}};

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
    const QString lines = values.value(QStringLiteral("lines")).toString();
    if (lines == QLatin1String("selected")) {
        options.lines = session->selection().selected;
    } else if (lines == QLatin1String("shown")) {
        const auto shown = shell().actionLines();
        options.lines = std::set<core::LineId>(shown.begin(), shown.end());
    }

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
    const core::ExportResult result = core::exportDocument(document, format->format, options);
    if (!result.refusal.empty())
        return problem(QCoreApplication::translate("HikariSub.Export", "YouTube timed text couldn’t be made from this file: %1")
                           .arg(QString::fromStdString(result.refusal)));

    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) ||
        out.write(reinterpret_cast<const char *>(result.bytes.data()), qint64(result.bytes.size())) !=
            qint64(result.bytes.size()) ||
        !out.commit())
        return problem(QCoreApplication::translate("HikariSub.Export", "Couldn’t write %1: %2").arg(QFileInfo(path).fileName(), out.errorString()));

    int srv3Styles = 0;
    if (srv3Changed) {
        auto *editable = m_files->session(*target);
        const auto ran = editable->run(application::Command{
            "Set srv3 style options", editable->revision(), {},
            [&](core::Document &d) { return core::setSrv3StyleOptions(d, *srv3Changed); }});
        if (ran)
            srv3Styles = srv3StylesChanged(srv3Saved, *srv3Changed);
    }

    QJsonObject choice{{QStringLiteral("format"), formatId},
                       {QStringLiteral("lines"), lines.isEmpty() ? QStringLiteral("all") : lines}};
    for (const auto &[name, flag] : kExportFlags)
        choice.insert(QLatin1String(name), options.*flag);
    choice.insert(QStringLiteral("ttmlNetflix"), options.ttmlProfile == core::TtmlProfile::Netflix);
    const QJsonObject before = rememberedExport(m_settings->list("export.choices"), exportKey(*target));
    choice.insert(QStringLiteral("language"),
                  isTtml(format->format) ? language : before.value(QStringLiteral("language")).toString());
    if (framesIn(format->format, options))
        choice.insert(QStringLiteral("fps"), options.fps);
    else if (const double kept = before.value(QStringLiteral("fps")).toDouble(); kept >= 1)
        choice.insert(QStringLiteral("fps"), kept);
    m_settings->setValue(QStringLiteral("export.choices"),
                         rememberedWith(m_settings->list("export.choices"), exportKey(*target), choice));

    std::map<std::uint64_t, int> rows;
    int row = 0;
    for (const auto *line : document.lines())
        rows.emplace(line->id.value, ++row);
    QVariantList notes;
    for (const auto &note : result.notes) {
        QStringList losses;
        for (const ExportLoss loss : kLossOrder)
            if (note.has(loss))
                losses << lossText(loss, format->format);
        notes << QVariantMap{{QStringLiteral("line"), QVariant::fromValue<qulonglong>(note.line.value)},
                             {QStringLiteral("number"), rows[note.line.value]},
                             {QStringLiteral("losses"), losses.join(QStringLiteral("; "))}};
    }
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("file"), QUrl::fromLocalFile(path)},
            {QStringLiteral("document"), QVariant::fromValue<qulonglong>(target->value)},
            {QStringLiteral("written"), result.linesWritten},
            {QStringLiteral("comments"), result.commentsSkipped},
            {QStringLiteral("empty"), result.emptySkipped},
            {QStringLiteral("srv3Styles"), srv3Styles},
            {QStringLiteral("notes"), notes}};
}

}
