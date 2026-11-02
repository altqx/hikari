#include "hikari/app/application.h"

#include "hikari/core/export.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStringList>
#include <QUrl>

#include <algorithm>
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
    {"tmp", ExportFormat::TMPlayer, "txt"},
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

QJsonObject rememberedExport(const QStringList &rows, const QString &key)
{
    for (const QString &wanted : {key, QString()})
        for (const QString &row : rows)
            if (row.contains(u'\t') && rowKey(row) == wanted)
                return QJsonDocument::fromJson(row.mid(row.indexOf(u'\t') + 1).toUtf8()).object();
    return {};
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
        return QCoreApplication::translate("HikariSub.Export", "Font, size, scale or spacing tags removed");
    case ExportLoss::Outline:
        return QCoreApplication::translate("HikariSub.Export", "Border, shadow, blur, transparency or other colours removed");
    case ExportLoss::Colour:
        return QCoreApplication::translate("HikariSub.Export", "Text colour removed");
    case ExportLoss::Formatting:
        return QCoreApplication::translate("HikariSub.Export", "Some bold, italic, underline or strikeout removed");
    case ExportLoss::Position:
        return QCoreApplication::translate("HikariSub.Export", "Position or alignment removed");
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
    const QJsonObject saved = rememberedExport(m_settings->list("export.choices"), key);

    const core::ExportOptions defaults;
    QVariantMap options;
    for (const auto &[name, flag] : kExportFlags) {
        const QJsonValue v = saved.value(QLatin1String(name));
        options.insert(QLatin1String(name), v.isBool() ? v.toBool() : defaults.*flag);
    }
    const QString lines = saved.value(QStringLiteral("lines")).toString();
    options.insert(QStringLiteral("lines"), lines == QLatin1String("selected") || lines == QLatin1String("shown")
                                                ? lines
                                                : QStringLiteral("all"));
    const auto &video = m_video->session();
    const float frameMs =
        video.state() == application::VideoSession::State::Ready ? video.legacyTimebase().frameDuration() : 0.f;
    const double videoFps = frameMs > 0.f ? 1000.0 / frameMs : 0.0;
    const double savedFps = saved.value(QStringLiteral("fps")).toDouble();
    options.insert(QStringLiteral("fps"), savedFps >= 1 ? savedFps : videoFps >= 1 ? videoFps : defaults.fps);

    int all = 0, selected = 0, shown = 0;
    const auto shownIds = m_shell->actionLines();
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
        return problem(QCoreApplication::translate("HikariSub.Export", "%1 is a folder.").arg(QDir::toNativeSeparators(path)));
    if (QFileInfo::exists(path) && !values.value(QStringLiteral("replace")).toBool())
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("exists"), true},
                {QStringLiteral("name"), QFileInfo(path).fileName()}};

    core::ExportOptions options;
    for (const auto &[name, flag] : kExportFlags)
        if (const auto v = values.constFind(QLatin1String(name)); v != values.cend())
            options.*flag = v->toBool();
    const double fps = values.value(QStringLiteral("fps")).toDouble();
    if (format->format == ExportFormat::MicroDvd && (!std::isfinite(fps) || fps < 1 || fps > 1000))
        return problem(QCoreApplication::translate("HikariSub.Export", "Enter a frame rate between 1 and 1000."));
    if (fps >= 1)
        options.fps = fps;
    const QString lines = values.value(QStringLiteral("lines")).toString();
    if (lines == QLatin1String("selected")) {
        options.lines = session->selection().selected;
    } else if (lines == QLatin1String("shown")) {
        const auto shown = m_shell->actionLines();
        options.lines = std::set<core::LineId>(shown.begin(), shown.end());
    }

    const auto draft = m_editorDocument == target ? m_editor->draftDocument() : std::nullopt;
    const core::Document &document = draft ? *draft : session->document();
    const core::ExportResult result = core::exportDocument(document, format->format, options);

    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) ||
        out.write(reinterpret_cast<const char *>(result.bytes.data()), qint64(result.bytes.size())) !=
            qint64(result.bytes.size()) ||
        !out.commit())
        return problem(QCoreApplication::translate("HikariSub.Export", "Couldn't write %1: %2").arg(QFileInfo(path).fileName(), out.errorString()));

    QJsonObject choice{{QStringLiteral("format"), formatId},
                       {QStringLiteral("lines"), lines.isEmpty() ? QStringLiteral("all") : lines}};
    for (const auto &[name, flag] : kExportFlags)
        choice.insert(QLatin1String(name), options.*flag);
    if (format->format == ExportFormat::MicroDvd)
        choice.insert(QStringLiteral("fps"), options.fps);
    else if (const double kept = rememberedExport(m_settings->list("export.choices"), exportKey(*target))
                                     .value(QStringLiteral("fps"))
                                     .toDouble();
             kept >= 1)
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
            {QStringLiteral("notes"), notes}};
}

}
