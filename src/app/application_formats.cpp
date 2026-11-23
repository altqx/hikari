#include "hikari/app/application.h"

#include "hikari/api/formats.h"
#include "hikari/application/edit_session.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QSaveFile>

#include <memory>

namespace hikari::app {

namespace {

constexpr qint64 kMaxFormatBytes = 48 << 20;

QString qs(const std::string &text)
{
    return QString::fromStdString(text);
}

std::string lowerSuffix(const QString &path)
{
    return QFileInfo(path).suffix().toLower().toStdString();
}

std::string absolute(const QString &path)
{
    return QFileInfo(path).absoluteFilePath().toStdString();
}

bool hasExtension(const plugins::FormatContribution &format, const std::string &extension)
{
    return std::ranges::find(format.extensions, extension) != format.extensions.end();
}

QString withExtension(QString path, const plugins::FormatContribution &format)
{
    if (!hasExtension(format, lowerSuffix(path)))
        path += u'.' + qs(format.extensions.front());
    return path;
}

std::optional<api::Json> callNow(api::Api &api, std::string_view operation, const api::Json &params)
{
    std::optional<api::Json> out;
    api.call(api::Caller{"hikarisub.formats", "Formats", true}, operation, params, [&](api::Result result) {
        if (result)
            out = std::move(*result);
    });
    return out;
}

std::vector<std::byte> bytesOf(const std::string &text)
{
    std::vector<std::byte> out(text.size());
    std::memcpy(out.data(), text.data(), text.size());
    return out;
}

}

QString Application::formatText(const PluginRuntime::Format &format)
{
    return QCoreApplication::translate("HikariSub.Documents", "%1 (%2)").arg(qs(format.format.title), qs(format.pluginName));
}

QString Application::formatFilter(const PluginRuntime::Format &format)
{
    QStringList patterns;
    for (const auto &extension : format.format.extensions)
        patterns << QStringLiteral("*.") + qs(extension);
    return QStringLiteral("%1 (%2)").arg(formatText(format), patterns.join(u' '));
}

std::vector<PluginRuntime::Format> Application::pluginFormats(bool imports) const
{
    std::vector<PluginRuntime::Format> out;
    if (m_pluginRuntime)
        for (auto &format : m_pluginRuntime->formats())
            if (imports ? format.format.imports : format.format.exports)
                out.push_back(std::move(format));
    return out;
}

QStringList Application::pluginOpenFilters() const
{
    QStringList filters;
    for (const auto &format : pluginFormats(true))
        filters << formatFilter(format);
    return filters;
}

QStringList Application::pluginOpenFormats() const
{
    QStringList ids;
    for (const auto &format : pluginFormats(true))
        ids << qs(format.id);
    return ids;
}

std::vector<std::string> Application::pluginImportExtensions() const
{
    std::vector<std::string> out;
    for (const auto &format : pluginFormats(true))
        std::ranges::copy(format.format.extensions, std::back_inserter(out));
    return out;
}

std::optional<std::string> Application::fileFormat(application::DocumentId document) const
{
    const auto destination = m_files->destination(document);
    if (!destination)
        return std::nullopt;
    const auto it = m_fileFormats.find(destination->value);
    return it == m_fileFormats.end() ? std::nullopt : std::optional(it->second);
}

std::optional<PluginRuntime::Format> Application::exportingFormat(application::DocumentId document) const
{
    const auto id = fileFormat(document);
    auto format = id && m_pluginRuntime ? m_pluginRuntime->format(*id) : std::nullopt;
    return format && format->format.exports ? format : std::nullopt;
}

std::optional<api::Json> Application::formatSnapshot(application::DocumentId document,
                                                     const std::optional<std::set<core::LineId>> &only)
{
    api::Api &api = m_apiHost->api();
    const api::Json target = api::Json::object({{"document", api::Json(document.value)}});
    auto summary = callNow(api, "documents.get", target);
    auto styles = callNow(api, "styles.list", target);
    auto info = callNow(api, "info.list", target);
    if (!summary || !styles || !info)
        return std::nullopt;
    api::Json lines = api::Json::array();
    for (std::int64_t offset = 0;;) {
        auto page = callNow(api, "lines.list",
                            api::Json::object({{"document", api::Json(document.value)}, {"offset", api::Json(offset)},
                                               {"limit", api::Json(5000)}}));
        if (!page)
            return std::nullopt;
        const auto &rows = page->find("lines")->asArray();
        for (const api::Json &line : rows)
            if (!only || only->contains(core::LineId{std::uint64_t(line.find("id")->asInt())}))
                lines.push(line);
        offset += std::int64_t(rows.size());
        if (rows.empty() || offset >= page->find("total")->asInt())
            break;
    }
    return api::Json::object({{"document", api::Json(document.value)},
                              {"revision", *summary->find("revision")},
                              {"title", *summary->find("title")},
                              {"format", *summary->find("format")},
                              {"lines", std::move(lines)},
                              {"styles", *styles->find("styles")},
                              {"info", *info->find("info")}});
}

api::Json Application::formatOptions(double frameRate, const QString &path) const
{
    const auto &video = activeMedia().video->session();
    const bool ready = video.state() == application::VideoSession::State::Ready;
    if (frameRate < 1 && ready && video.legacyTimebase().frameDuration() > 0.f)
        frameRate = 1000.0 / video.legacyTimebase().frameDuration();
    return api::Json::object({{"frameRate", frameRate >= 1 ? api::Json(frameRate) : api::Json()},
                              {"video", ready ? api::Json(video.path()) : api::Json()},
                              {"path", api::Json(path.toStdString())}});
}

QVariantMap Application::exportThroughPlugin(application::DocumentId document, const PluginRuntime::Format &format,
                                             const QString &path, const std::optional<std::set<core::LineId>> &lines,
                                             double frameRate)
{
    const auto problem = [](const QString &text) {
        return QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("problem"), text}};
    };
    auto snapshot = formatSnapshot(document, lines);
    if (!snapshot)
        return problem(QCoreApplication::translate("HikariSub.Export", "There is nothing to export."));
    const int written = int(snapshot->find("lines")->asArray().size());
    const QString plugin = qs(format.pluginName);
    QPointer<Application> self(this);
    const bool started = m_pluginRuntime->exportDocument(
        format, *snapshot, formatOptions(frameRate, path),
        [self, path, plugin, document, written, problem](std::expected<std::string, QString> bytes) {
            if (!self)
                return;
            if (!bytes) {
                emit self->pluginExportFinished(problem(
                    QCoreApplication::translate("HikariSub.Export", "%1 couldn’t export this file: %2").arg(plugin, bytes.error())));
                return;
            }
            QSaveFile out(path);
            if (!out.open(QIODevice::WriteOnly) || out.write(bytes->data(), qint64(bytes->size())) != qint64(bytes->size()) ||
                !out.commit()) {
                emit self->pluginExportFinished(problem(
                    QCoreApplication::translate("HikariSub.Export", "Couldn’t write %1: %2").arg(QFileInfo(path).fileName(), out.errorString())));
                return;
            }
            emit self->pluginExportFinished({{QStringLiteral("ok"), true},
                                             {QStringLiteral("file"), QUrl::fromLocalFile(path)},
                                             {QStringLiteral("document"), QVariant::fromValue<qulonglong>(document.value)},
                                             {QStringLiteral("written"), written},
                                             {QStringLiteral("comments"), 0},
                                             {QStringLiteral("empty"), 0},
                                             {QStringLiteral("srv3Styles"), 0},
                                             {QStringLiteral("notes"), QVariantList()}});
        });
    if (!started)
        return problem(QCoreApplication::translate("HikariSub.Export", "%1 isn’t running, so it can’t export.").arg(plugin));
    return {{QStringLiteral("ok"), false}, {QStringLiteral("pending"), true}};
}

std::optional<application::DeferredBytes> Application::pluginSaveBytes(application::DocumentId document,
                                                                       const application::DestinationKey &destination)
{
    const auto id = m_fileFormats.find(destination.value);
    if (id == m_fileFormats.end())
        return std::nullopt;
    const std::string formatId = id->second;
    QPointer<Application> self(this);
    return [self, document, formatId, path = qs(destination.value)](application::BytesReady ready) {
        if (!self)
            return ready(std::nullopt);
        const auto fail = [&](const QString &why) {
            const std::string *title = self->m_workspace.title(document);
            self->m_log->notice(ui::LogController::Error, QCoreApplication::translate("HikariSub.Documents", "Save"),
                                QCoreApplication::translate("HikariSub.Documents", "%1 could not be saved: %2")
                                    .arg(title ? qs(*title) : QFileInfo(path).fileName(), why));
            ready(std::nullopt);
        };
        const auto format = self->m_pluginRuntime ? self->m_pluginRuntime->format(formatId) : std::nullopt;
        if (!format || !format->format.exports)
            return fail(QCoreApplication::translate("HikariSub.Documents", "the plugin that writes this format isn’t running."));
        auto snapshot = self->formatSnapshot(document, std::nullopt);
        if (!snapshot)
            return fail(QCoreApplication::translate("HikariSub.Documents", "the document couldn’t be read."));
        auto shared = std::make_shared<application::BytesReady>(std::move(ready));
        const QString plugin = qs(format->pluginName);
        const bool started = self->m_pluginRuntime->exportDocument(
            *format, *snapshot, self->formatOptions(0, path), [self, document, path, plugin, shared](std::expected<std::string, QString> bytes) {
                if (bytes)
                    return (*shared)(bytesOf(*bytes));
                if (self) {
                    const std::string *title = self->m_workspace.title(document);
                    self->m_log->notice(ui::LogController::Error, QCoreApplication::translate("HikariSub.Documents", "Save"),
                                        QCoreApplication::translate("HikariSub.Documents", "%1 could not be saved: %2")
                                            .arg(title ? qs(*title) : QFileInfo(path).fileName(),
                                                 QStringLiteral("%1: %2").arg(plugin, bytes.error())));
                }
                (*shared)(std::nullopt);
            });
        if (!started) {
            (*shared)(std::nullopt);
            if (self)
                self->m_log->notice(ui::LogController::Error, QCoreApplication::translate("HikariSub.Documents", "Save"),
                                    QCoreApplication::translate("HikariSub.Documents", "%1 isn’t running, so it can’t save.").arg(plugin));
        }
    };
}

void Application::importThroughPlugins(const QString &path, std::vector<PluginRuntime::Format> candidates, QString firstProblem,
                                       std::function<void(std::expected<application::StagedOpen, QString>)> done)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return done(std::unexpected(firstProblem.isEmpty() ? QCoreApplication::translate("HikariSub.Documents", "%1 couldn’t be read: %2")
                                                                 .arg(QFileInfo(path).fileName(), file.errorString())
                                                           : firstProblem));
    if (file.size() > kMaxFormatBytes)
        return done(std::unexpected(QCoreApplication::translate("HikariSub.Documents", "%1 is larger than 48 MiB, so no plugin can open it.")
                                        .arg(QFileInfo(path).fileName())));
    const QByteArray bytes = file.readAll();
    struct Attempt {
        QString path;
        std::string bytes;
        std::vector<PluginRuntime::Format> candidates;
        std::size_t next = 0;
        QString problem;
        std::function<void(std::expected<application::StagedOpen, QString>)> done;
    };
    auto attempt = std::make_shared<Attempt>(Attempt{path, bytes.toStdString(), std::move(candidates), 0, std::move(firstProblem), std::move(done)});
    auto tryNext = std::make_shared<std::function<void()>>();
    QPointer<Application> self(this);
    *tryNext = [self, attempt, weak = std::weak_ptr(tryNext)] {
        const auto tryNext = weak.lock();
        if (!self || attempt->next == attempt->candidates.size())
            return attempt->done(std::unexpected(attempt->problem));
        const PluginRuntime::Format format = attempt->candidates[attempt->next++];
        const QString plugin = qs(format.pluginName);
        const auto failed = [attempt, tryNext, plugin](const QString &why) {
            if (attempt->problem.isEmpty())
                attempt->problem = QCoreApplication::translate("HikariSub.Documents", "%1 couldn’t open this file: %2").arg(plugin, why);
            (*tryNext)();
        };
        const bool started = self->m_pluginRuntime->importFile(
            format, attempt->bytes, QFileInfo(attempt->path).fileName().toStdString(),
            [self, attempt, tryNext, format, failed](std::expected<std::string, QString> json) {
                if (!self)
                    return failed(QString());
                if (!json)
                    return failed(json.error());
                const auto parsed = api::Json::parse(*json);
                auto document = parsed ? api::documentFromImport(*parsed) : std::unexpected(std::string("not JSON"));
                if (!document)
                    return failed(qs(document.error()));
                application::StagedOpen staged{application::DestinationKey{absolute(attempt->path)},
                                               core::LoadResult{std::move(*document), {}},
                                               std::nullopt,
                                               0,
                                               0,
                                               false,
                                               bytesOf(attempt->bytes)};
                self->m_fileFormats[staged.destination.value] = format.id;
                attempt->done(std::move(staged));
            });
        if (!started)
            failed(QCoreApplication::translate("HikariSub.Documents", "it isn’t running."));
    };
    (*tryNext)();
}

std::vector<PluginRuntime::Format> Application::importCandidates(const QString &path, const QString &only) const
{
    std::vector<PluginRuntime::Format> out;
    const std::string extension = lowerSuffix(path);
    for (auto &format : pluginFormats(true))
        if (only.isEmpty() ? hasExtension(format.format, extension) : qs(format.id) == only)
            out.push_back(std::move(format));
    return out;
}

bool Application::builtInExtension(const QString &path)
{
    const std::string extension = lowerSuffix(path);
    return std::ranges::any_of(api::builtInFormats(), [&](const api::FormatInfo &format) {
        return format.imports && std::ranges::find(format.extensions, extension) != format.extensions.end();
    });
}

QVariantMap Application::reviewOpenAs(const QString &path, const QString &format)
{
    auto candidates = importCandidates(path, format);
    if (candidates.empty())
        return reviewOpen(path);
    openThroughPlugins(path, false, std::move(candidates), {});
    return {{QStringLiteral("ok"), true}, {QStringLiteral("done"), true}};
}

void Application::openThroughPlugins(const QString &path, bool here, std::vector<PluginRuntime::Format> candidates,
                                     QString firstProblem)
{
    const int window = m_workspace.activeWindow();
    QPointer<Application> self(this);
    importThroughPlugins(path, std::move(candidates), std::move(firstProblem),
                         [self, path, here, window](std::expected<application::StagedOpen, QString> staged) {
                             if (!self)
                                 return;
                             if (!staged) {
                                 self->m_log->notice(ui::LogController::Error,
                                                     QCoreApplication::translate("HikariSub.Video", "Open subtitles"), staged.error());
                                 return;
                             }
                             emit self->pluginOpenReviewed(window, self->continueOpen(std::move(*staged), path, here));
                         });
}

void Application::openInTabThroughPlugins(const QString &path)
{
    QPointer<Application> self(this);
    importThroughPlugins(path, importCandidates(path, {}), {}, [self, path](std::expected<application::StagedOpen, QString> staged) {
        if (!self)
            return;
        if (!staged)
            self->m_log->notice(ui::LogController::Error, QCoreApplication::translate("HikariSub.Video", "Open subtitles"), staged.error());
        else
            self->openInNewTab(std::move(*staged), path);
    });
}

}
