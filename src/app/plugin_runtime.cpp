#include "hikari/app/plugin_runtime.h"

#include "hikari/app/plugin_content.h"
#include "hikari/app/plugin_nodes.h"
#include "hikari/backends/program_plugin_link.h"
#include "hikari/backends/program_process.h"

#include "log_controller.h"

#include <QCollator>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>

#include <algorithm>
#include <iterator>

namespace hikari::app {

Q_LOGGING_CATEGORY(lcPluginsSandbox, "hikari.plugins.sandbox")

namespace {

namespace fs = std::filesystem;
using backends::PluginProcess;
namespace protocol = backends::plugin;

constexpr std::string_view kCommandPrefix = "plugin.";
const std::string kLockBusy = "another plugin is applying edits; try again when it is done";

QString qs(std::string_view s)
{
    return QString::fromUtf8(s.data(), qsizetype(s.size()));
}

fs::path path(const QString &text)
{
    return fs::path(text.toStdU16String());
}

std::string utf8(const fs::path &p)
{
    const std::u8string text = p.u8string();
    return std::string(text.begin(), text.end());
}

ui::LogController::Severity severityOf(protocol::Level level)
{
    switch (level) {
    case protocol::Level::Warning: return ui::LogController::Warning;
    case protocol::Level::Error: return ui::LogController::Error;
    case protocol::Level::Info: break;
    }
    return ui::LogController::Info;
}

QString analyticsFeature(std::string_view operation)
{
    if (operation == "automation.run_macro")
        return QStringLiteral("run_macro");
    if (operation == "video.set_keyframes")
        return QStringLiteral("set_keyframes");
    if (operation == "video.frames")
        return QStringLiteral("frames");
    if (operation == "marks.set")
        return QStringLiteral("row_marks");
    return {};
}

bool hasCode(const plugins::Manifest &manifest)
{
    return !manifest.main.empty() || !manifest.mainProgram.empty();
}

QString stopReason(PluginProcess::StopReason reason)
{
    switch (reason) {
    case PluginProcess::StopReason::Crashed:
        return QCoreApplication::translate("HikariSub.Plugins", "It stopped unexpectedly.");
    case PluginProcess::StopReason::ForceStopped:
        break;
    }
    return QCoreApplication::translate("HikariSub.Plugins", "It was force stopped.");
}

QString conditionPhrase(std::string_view condition)
{
    const char *phrase = nullptr;
    if (condition == "document")
        phrase = QT_TRANSLATE_NOOP("HikariSub.Plugins", "Needs an open file");
    else if (condition == "!document")
        phrase = QT_TRANSLATE_NOOP("HikariSub.Plugins", "Works only without an open file");
    else if (condition == "selection")
        phrase = QT_TRANSLATE_NOOP("HikariSub.Plugins", "Needs a selection");
    else if (condition == "!selection")
        phrase = QT_TRANSLATE_NOOP("HikariSub.Plugins", "Works only without a selection");
    else if (condition == "multipleLines")
        phrase = QT_TRANSLATE_NOOP("HikariSub.Plugins", "Needs two or more selected lines");
    else if (condition == "!multipleLines")
        phrase = QT_TRANSLATE_NOOP("HikariSub.Plugins", "Works only with one line selected or none");
    else if (condition == "video")
        phrase = QT_TRANSLATE_NOOP("HikariSub.Plugins", "Needs an open video");
    else if (condition == "!video")
        phrase = QT_TRANSLATE_NOOP("HikariSub.Plugins", "Works only without an open video");
    else if (condition == "audio")
        phrase = QT_TRANSLATE_NOOP("HikariSub.Plugins", "Needs open audio");
    else if (condition == "!audio")
        phrase = QT_TRANSLATE_NOOP("HikariSub.Plugins", "Works only without open audio");
    else if (condition == "karaoke")
        phrase = QT_TRANSLATE_NOOP("HikariSub.Plugins", "Needs a karaoke line");
    else if (condition == "!karaoke")
        phrase = QT_TRANSLATE_NOOP("HikariSub.Plugins", "Works only on a line without karaoke");
    else if (condition == "translation")
        phrase = QT_TRANSLATE_NOOP("HikariSub.Plugins", "Needs Translation mode");
    else
        phrase = QT_TRANSLATE_NOOP("HikariSub.Plugins", "Works only outside Translation mode");
    return QCoreApplication::translate("HikariSub.Plugins", phrase);
}

QString menuLocation(plugins::MenuLocation location)
{
    switch (location) {
    case plugins::MenuLocation::Grid: return QStringLiteral("grid.context");
    case plugins::MenuLocation::LineEditor: return QStringLiteral("lineEditor.context");
    case plugins::MenuLocation::MenuBar: break;
    }
    return QStringLiteral("menuBar");
}

std::string envelope(const api::Result &result)
{
    if (result)
        return api::Json::object({{"result", *result}}).dump();
    return api::Json::object(
               {{"error", api::Json::object({{"code", result.error().code}, {"message", result.error().message}, {"data", result.error().data}})}})
        .dump();
}

}

struct PluginRuntime::Running {
    std::string id;
    plugins::Manifest manifest;
    api::Caller caller;
    PluginFiles::Plugin files;
    std::unique_ptr<PluginProcess> process;
    std::vector<std::uint64_t> subscriptions;
    struct Status {
        plugins::StatusItem item; // with the plugin's latest text, icon and tooltip
        QString tone = QStringLiteral("normal");
        bool visible = true;
    };
    std::vector<Status> status;
    bool loaded = false;
    void resetStatus()
    {
        status.clear();
        for (const plugins::StatusItem &item : manifest.statusItems)
            status.push_back({item});
    }
    bool held = false;
};

namespace {

QString commandTitle(const plugins::Manifest &manifest, const QString &command)
{
    for (const auto &c : manifest.commands)
        if (qs(c.id) == command)
            return qs(c.title);
    return command;
}

}

PluginRuntime::PluginRuntime(Wiring wiring, QObject *parent) : QObject(parent), m_wiring(std::move(wiring))
{
    m_ui = new PluginUi(
        [this](const std::string &plugin, const std::string &event, const std::string &json, const std::string &key) {
            const auto it = m_plugins.find(plugin);
            return it != m_plugins.end() && it->second->process->deliver(event, json, key);
        },
        this);
    m_ui->popups()->setUserAsked([this](const std::string &plugin) {
        const auto it = m_plugins.find(plugin);
        return it != m_plugins.end() && it->second->process->answersUser();
    });
    const auto &f = m_wiring.folders;
    if (!f.plugins.isEmpty())
        m_installer.emplace(plugins::PluginFolders{path(f.plugins), path(f.data), path(f.cache), path(f.registry)},
                            api::kApiVersion);
    if (!m_wiring.network) {
        m_ownNetwork = std::make_unique<QNetworkAccessManager>();
        m_wiring.network = m_ownNetwork.get();
    }
    if (m_wiring.sandbox) {
        const backends::sandbox::Coverage coverage = backends::sandbox::systemCoverage(backends::sandbox::Network::None);
        if (!coverage.complete())
            qCWarning(lcPluginsSandbox) << "Restricted plugins run without a complete sandbox:" << coverage.missing;
    }
    m_network = new PluginNetwork(*m_wiring.network, m_wiring.networkAllowed, this);
    m_downloads = std::make_unique<PluginProgramDownloads>(
        PluginProgramDownloads::Wiring{f.programs, m_wiring.network, m_wiring.networkAllowed});
    connect(m_downloads.get(), &PluginProgramDownloads::changed, this, [this](const QString &pluginId) {
        ++m_programsRevision;
        emit programsChanged();
        const auto it = m_plugins.find(pluginId.toStdString());
        if (it != m_plugins.end() && !it->second->manifest.mainProgram.empty() && !it->second->held &&
            it->second->process->state() == PluginProcess::State::LoadFailed && m_downloads->missing(it->first, it->second->manifest).empty())
            it->second->process->start();
    });
    m_files = std::make_unique<PluginFiles>(m_wiring.api, m_installer ? &*m_installer : nullptr, *m_ui, m_wiring.project);
    m_ui->setGrantDrop([this](const std::string &plugin, const QString &file) {
        const auto it = m_plugins.find(plugin);
        return it == m_plugins.end() ? api::Json() : m_files->grantDropped(it->second->files, file);
    });
    wireQmlPanels();
    wirePrograms();
}

void PluginRuntime::wirePrograms()
{
    if (!m_wiring.programs)
        return;
    m_wiring.programs->bind({
        [this](const std::string &id) -> std::optional<PluginPrograms::Plugin> {
            const auto it = m_plugins.find(id);
            if (it == m_plugins.end() || !m_installer)
                return std::nullopt;
            const plugins::PluginFolders &f = m_installer->folders();
            const fs::path own(std::u8string(id.begin(), id.end()));
            auto folder = [&own](const fs::path &base) { return QString::fromStdU16String((base / own).u16string()); };
            return PluginPrograms::Plugin{&it->second->manifest, folder(f.plugins), folder(f.data), folder(f.cache),
                                          plugins::settingValues(it->second->manifest, storedSettings(id))};
        },
        m_wiring.networkAllowed,
        [this](const std::string &id, const QString &line) {
            const auto it = m_plugins.find(id);
            if (m_wiring.log && it != m_plugins.end())
                m_wiring.log->log(line, ui::LogController::Info, qs(it->second->manifest.name));
        },
        m_wiring.api,
        m_wiring.lowRightsHelper,
        [this](const std::string &id, const plugins::ProgramRun &run) { return m_downloads->file(id, run); },
    });
}

void PluginRuntime::wireQmlPanels()
{
    m_ui->setQmlHooks({
        [this](const std::string &plugin, const std::string &operation, const api::Json &params,
               std::function<void(api::Result)> done) {
            const auto it = m_plugins.find(plugin);
            if (it == m_plugins.end() || !m_wiring.api) {
                done(api::fail(api::errors::kUnavailable, "the Hikari API isn't available here"));
                return;
            }
            m_wiring.api->call(it->second->caller, operation, params, std::move(done));
        },
        [this](const std::string &plugin, const std::string &setting, const api::Json &value) {
            return setSetting(plugin, setting, value);
        },
        [this](const std::string &plugin) {
            const auto it = m_plugins.find(plugin);
            if (it == m_plugins.end() || !m_installer)
                return api::Json::object();
            return plugins::settingValues(it->second->manifest, storedSettings(plugin));
        },
        [this] { return m_wiring.language ? m_wiring.language() : QString(); },
        [this](const QString &pluginName, const QString &text, bool error) {
            if (m_wiring.log)
                m_wiring.log->log(text, error ? ui::LogController::Error : ui::LogController::Warning, pluginName);
        },
    });
    if (!m_wiring.folders.registry.isEmpty())
        m_ui->keepLoadedPanelsIn(QFileInfo(m_wiring.folders.registry).dir().filePath(QStringLiteral("plugin-panels.json")));
}

std::optional<std::string> PluginRuntime::setSetting(const std::string &plugin, const std::string &setting,
                                                     const api::Json &value)
{
    if (m_wiring.setSetting) {
        const QString refused = m_wiring.setSetting(qs(plugin), qs(setting), qs(value.dump()));
        return refused.isEmpty() ? std::nullopt : std::optional(refused.toStdString());
    }
    if (!m_installer)
        return "HikariSub can’t keep settings here";
    if (const auto kept = m_installer->setSetting(plugin, setting, value); !kept)
        return kept.error();
    settingChanged(qs(plugin), qs(setting), qs(value.dump()));
    return std::nullopt;
}

PluginRuntime::~PluginRuntime()
{
    m_lockWaiters.clear();
    for (auto &[id, plugin] : m_plugins) {
        unsubscribe(*plugin);
        endAccess(*plugin);
    }
    delete m_network;
}

const plugins::ProjectPlugins *PluginRuntime::windowProject(int shellWindow) const
{
    const auto found = m_windows.find(shellWindow);
    return found == m_windows.end() || !found->second ? nullptr : &*found->second;
}

api::Json PluginRuntime::storedSettings(const std::string &plugin) const
{
    api::Json stored = api::Json::object();
    if (!m_installer)
        return stored;
    if (const auto registry = m_installer->registry(); registry && registry->contains(plugin))
        stored = registry->at(plugin).settings;
    return plugins::projectSettings(plugin, stored, windowProject(m_wiring.activeWindow ? m_wiring.activeWindow() : 0));
}

bool PluginRuntime::onIn(const QString &pluginId, int shellWindow) const
{
    const auto found = m_onIn.find(shellWindow);
    const auto &on = found == m_onIn.end() ? m_userOn : found->second;
    return on.contains(pluginId.toStdString());
}

QStringList PluginRuntime::missingIn(int shellWindow) const
{
    const auto *project = windowProject(shellWindow);
    QStringList out;
    if (!project)
        return out;
    const auto registry = m_installer ? m_installer->registry() : plugins::Registry();
    if (registry)
        for (const auto &id : plugins::missingPlugins(*project, *registry))
            out << qs(id);
    return out;
}

void PluginRuntime::projectsChanged()
{
    auto windows = m_wiring.windowPlugins ? m_wiring.windowPlugins() : decltype(m_windows)();
    const bool same = windows.size() == m_windows.size()
                      && std::ranges::equal(windows, m_windows, [](const auto &a, const auto &b) {
                             return a.first == b.first && a.second.has_value() == b.second.has_value()
                                    && (!a.second
                                        || (a.second->enabled == b.second->enabled && a.second->disabled == b.second->disabled
                                            && a.second->settings == b.second->settings));
                         });
    if (same)
        return;
    m_windows = std::move(windows);
    ++m_windowsRevision;
    sync();
    emit windowsChanged();
}

void PluginRuntime::sync()
{
    std::vector<const plugins::ProjectPlugins *> windows;
    for (const auto &[window, project] : m_windows)
        windows.push_back(project ? &*project : nullptr);
    std::set<std::string> userOn;
    std::map<int, std::set<std::string>> onIn;
    for (const auto &[window, project] : m_windows)
        onIn[window];
    std::map<std::string, plugins::Manifest> wanted;
    std::vector<std::pair<std::int64_t, std::string>> order;
    std::vector<PluginUi::Plugin> installed;
    std::vector<application::PluginNode> nodes;
    std::map<std::string, std::string> unavailable;
    application::PluginContent content;
    if (m_installer) {
        std::set<std::string> installedIds;
        const auto registry = m_installer->registry();
        if (registry)
            for (const auto &[id, entry] : *registry) {
                installedIds.insert(id);
                auto manifest = m_installer->installedManifest(id);
                if (!manifest) {
                    if (entry.enabled && m_wiring.log)
                        m_wiring.log->log(QCoreApplication::translate("HikariSub.Plugins", "Can’t start: %1").arg(qs(manifest.error())),
                                          ui::LogController::Error, qs(id));
                    continue;
                }
                const fs::path root = m_installer->folders().plugins / fs::path(std::u8string(id.begin(), id.end()));
                const bool runs = plugins::pluginRunsAnywhere(id, entry, windows);
                if (plugins::pluginOn(id, entry, nullptr))
                    userOn.insert(id);
                for (const auto &[window, project] : m_windows)
                    if (plugins::pluginOn(id, entry, project ? &*project : nullptr))
                        onIn[window].insert(id);
                installed.push_back({id, manifest->name, runs, manifest->panels,
                                     QUrl::fromLocalFile(QString::fromStdU16String(root.u16string())),
                                     manifest->tier == plugins::Tier::Unrestricted, manifest->version});
                if (pluginContentShows(entry, *manifest))
                    addPluginContent(content, id, *manifest, root);
                if (!manifest->nodes.empty()) {
                    std::expected<std::vector<application::PluginNode>, std::string> own = std::unexpected(std::string());
                    if (pluginNodesRun(entry, *manifest))
                        own = pluginNodesOf(*manifest, root);
                    if (own)
                        std::ranges::move(*own, std::back_inserter(nodes));
                    else
                        unavailable.emplace(id, manifest->name);
                    if (!own && !own.error().empty() && m_wiring.log)
                        m_wiring.log->log(QCoreApplication::translate("HikariSub.Plugins", "Its nodes can’t run: %1").arg(qs(own.error())),
                                          ui::LogController::Error, qs(manifest->name));
                }
                std::set<std::string> kept = PluginProgramDownloads::hashes(*manifest);
                if (const auto previous = m_installer->previousManifest(id))
                    kept.merge(PluginProgramDownloads::hashes(*previous));
                m_downloads->keepOnly(id, kept);
                if (runs) {
                    m_downloads->fetch(id, *manifest);
                    order.emplace_back(entry.installed, id);
                    wanted.emplace(id, std::move(*manifest));
                }
            }
        if (registry)
            m_downloads->keepPlugins(installedIds);
    }
    for (auto it = m_plugins.begin(); it != m_plugins.end();) {
        const auto keep = wanted.find(it->first);
        if (keep != wanted.end() && keep->second.version == it->second->manifest.version) {
            ++it;
            continue;
        }
        unsubscribe(*it->second);
        releaseLock(*it->second);
        endAccess(*it->second);
        PluginProcess *process = it->second->process.get();
        disconnect(process, nullptr, this, nullptr);
        process->end();
        it = m_plugins.erase(it);
    }
    for (auto &[id, manifest] : wanted)
        if (!m_plugins.contains(id)) {
            Running &plugin = *m_plugins.emplace(id, launch(id, std::move(manifest))).first->second;
            if (hasCode(plugin.manifest))
                plugin.process->start();
        }
    std::ranges::sort(order);
    m_installOrder.clear();
    for (auto &[installed, id] : order)
        m_installOrder.push_back(std::move(id));
    m_userOn = std::move(userOn);
    m_onIn = std::move(onIn);
    application::setPluginNodes(std::move(nodes), std::move(unavailable));
    application::setPluginContent(std::move(content));
    m_ui->setPlugins(std::move(installed));
    relist();
}

std::vector<PluginRuntime::Format> PluginRuntime::formats() const
{
    std::vector<Format> out;
    for (const std::string &id : m_installOrder) {
        const auto it = m_plugins.find(id);
        if (it == m_plugins.end())
            continue;
        const plugins::Manifest &m = it->second->manifest;
        for (const auto &format : m.formats)
            out.push_back({"plugin:" + id + "/" + format.id, id, m.name, format});
    }
    return out;
}

std::optional<PluginRuntime::Format> PluginRuntime::format(const std::string &id) const
{
    for (Format &f : formats())
        if (f.id == id)
            return std::move(f);
    return std::nullopt;
}

bool PluginRuntime::exportDocument(const Format &format, const api::Json &snapshot, const api::Json &options,
                                   PluginProcess::FormatDone done)
{
    const auto it = m_plugins.find(format.plugin);
    return it != m_plugins.end() && format.format.exports &&
           it->second->process->exportDocument(format.format.id, snapshot.dump(), options.dump(), std::move(done));
}

bool PluginRuntime::importFile(const Format &format, std::string bytes, const std::string &fileName,
                               PluginProcess::FormatDone done)
{
    const auto it = m_plugins.find(format.plugin);
    return it != m_plugins.end() && format.format.imports &&
           it->second->process->importFile(format.format.id, std::move(bytes), fileName, std::move(done));
}

std::unique_ptr<PluginRuntime::Running> PluginRuntime::launch(const std::string &id, plugins::Manifest manifest)
{
    auto plugin = std::make_unique<Running>();
    Running *raw = plugin.get();
    raw->id = id;
    raw->manifest = std::move(manifest);
    raw->caller = api::Caller{"plugin:" + id, raw->manifest.name, raw->manifest.permissions.allDocuments.has_value()};
    raw->caller.vcsRead = raw->manifest.permissions.vcsRead.has_value();
    raw->caller.programs = raw->manifest.permissions.programs.has_value();
    raw->caller.presence = raw->manifest.permissions.presence.has_value();
    raw->files = PluginFiles::Plugin{id, raw->caller.id, raw->manifest.permissions.files};
    auto load = [this, raw] {
        protocol::LoadRequest request;
        const plugins::Manifest &m = raw->manifest;
        request.root = utf8(m_installer->folders().plugins / fs::path(std::u8string(raw->id.begin(), raw->id.end())));
        request.main = m.main;
        request.id = raw->id;
        request.version = m.version;
        request.tier = m.tier == plugins::Tier::Unrestricted ? protocol::Tier::Unrestricted : protocol::Tier::Restricted;
        request.memoryLimit = static_cast<std::int64_t>(m_wiring.memoryLimit);
        request.locale = m_wiring.language ? m_wiring.language().toStdString() : std::string();
        request.settings = plugins::settingValues(m, storedSettings(raw->id)).dump();
        for (const auto &command : m.commands)
            request.commands.push_back(command.id);
        for (const auto &format : m.formats) {
            if (format.exports)
                request.exporters.push_back(format.id);
            if (format.imports)
                request.importers.push_back(format.id);
        }
        return request;
    };
    raw->process = std::make_unique<PluginProcess>(m_wiring.helperPath, load, m_wiring.limits);
    PluginProcess *process = raw->process.get();
    process->setBackground(raw->manifest.permissions.background.has_value());
    if (m_wiring.sandbox && m_installer && raw->manifest.tier == plugins::Tier::Restricted) {
        const fs::path root = m_installer->folders().plugins / fs::path(std::u8string(id.begin(), id.end()));
        process->setSandbox(backends::pluginProfile(m_wiring.helperPath, QString::fromStdU16String(root.u16string()), id));
    }
    if (!raw->manifest.mainProgram.empty())
        process->setLinkFactory([this, raw]() -> std::expected<std::unique_ptr<backends::PluginLink>, QString> {
            const plugins::Program *program = raw->manifest.program(raw->manifest.mainProgram);
            if (!m_wiring.programs)
                return std::unexpected(QStringLiteral("HikariSub can’t run programs here."));
            auto prepared = m_wiring.programs->prepare(raw->id, raw->manifest.mainProgram);
            if (!prepared)
                return std::unexpected(qs(prepared.error().message));
            auto link = std::make_unique<backends::ProgramPluginLink>(std::move(*prepared),
                                                                      raw->manifest.permissions.background.has_value());
            link->setName(qs(program->title));
            connect(link.get(), &backends::ProgramPluginLink::logLine, this,
                    [this, id = raw->id](const QString &line) { m_wiring.programs->logLine(id, line); });
            return link;
        });
    process->setServiceHandler([this, raw](const PluginProcess::ServiceRequest &request, PluginProcess::ServiceReply reply) {
        serve(*raw, request, std::move(reply));
    });
    const QString name = qs(raw->manifest.name);
    auto log = [this] { return m_wiring.log; };
    connect(process, &PluginProcess::unitEnded, this, [this, raw] {
        releaseLock(*raw);
        emit idle(qs(raw->id));
    });
    connect(process, &PluginProcess::stopped, this, [this, raw] {
        emit idle(qs(raw->id));
        rework();
    });
    connect(process, &PluginProcess::workChanged, this, &PluginRuntime::rework);
    connect(process, &PluginProcess::jobBegan, this, [this, raw](const QString &command, const QString &title) {
        if (m_wiring.featureUsed)
            m_wiring.featureUsed(QStringLiteral("job"));
        const QString what = command.isEmpty() ? QCoreApplication::translate("HikariSub.Plugins", "A timer")
                                               : commandTitle(raw->manifest, command);
        Job job{raw->id, qs(raw->manifest.name), title.isEmpty() ? what : title, {}, -1, false, false, {}};
        endJob(raw->id);
        m_jobs.push_back(std::move(job));
        emit jobsChanged();
    });
    connect(process, &PluginProcess::jobProgress, this, [this, raw](double fraction, const QString &text) {
        for (Job &job : m_jobs)
            if (job.plugin == raw->id) {
                job.fraction = fraction;
                job.text = text;
                emit jobsChanged();
            }
    });
    connect(process, &PluginProcess::jobEnded, this, [this, raw] { endJob(raw->id); });
    raw->resetStatus();
    connect(process, &PluginProcess::loaded, this, [this, raw] {
        m_ui->running(raw->id);
        raw->loaded = true;
        emit statusItemsChanged();
    });
    const auto hideStatus = [this, raw] {
        raw->loaded = false;
        raw->resetStatus();
        emit statusItemsChanged();
    };
    connect(process, &PluginProcess::loadFailed, this, hideStatus);
    connect(process, &PluginProcess::stopped, this, hideStatus);
    connect(process, &PluginProcess::loadFailed, this, [this, raw, name, log](const QString &message) {
        unsubscribe(*raw);
        releaseLock(*raw);
        endAccess(*raw);
        m_ui->stopped(raw->id, QStringLiteral("failed"), message);
        if (log())
            log()->notice(ui::LogController::Error, name, QCoreApplication::translate("HikariSub.Plugins", "The plugin didn’t start."), message);
    });
    connect(process, &PluginProcess::commandFailed, this, [raw, name, log](const QString &command, const QString &message) {
        if (log())
            log()->notice(ui::LogController::Error, name,
                          QCoreApplication::translate("HikariSub.Plugins", "“%1” failed.").arg(commandTitle(raw->manifest, command)),
                          message);
    });
    connect(process, &PluginProcess::eventFailed, this, [name, log](const QString &event, const QString &message) {
        if (log())
            log()->log(QCoreApplication::translate("HikariSub.Plugins", "Its %1 handler failed: %2").arg(event, message), ui::LogController::Error, name);
    });
    connect(process, &PluginProcess::stopped, this, [this, raw, name, log](PluginProcess::StopReason reason) {
        unsubscribe(*raw);
        releaseLock(*raw);
        endAccess(*raw);
        m_ui->stopped(raw->id, QStringLiteral("stopped"), stopReason(reason));
        if (!log())
            return;
        switch (reason) {
        case PluginProcess::StopReason::Crashed:
            log()->notice(ui::LogController::Error, name,
                          QCoreApplication::translate("HikariSub.Plugins", "The plugin stopped unexpectedly. Its next command starts it again."),
                          QString::fromUtf8(raw->process->diagnostics()));
            break;
        case PluginProcess::StopReason::ForceStopped:
            log()->log(QCoreApplication::translate("HikariSub.Plugins", "Force stopped."), ui::LogController::Info, name);
            break;
        }
    });
    return plugin;
}

void PluginRuntime::serve(Running &plugin, const PluginProcess::ServiceRequest &request, PluginProcess::ServiceReply reply)
{
    switch (request.service) {
    case protocol::Service::Notify:
        if (m_wiring.log) {
            const QString text = QString::fromStdString(request.text);
            const QString name = qs(plugin.manifest.name);
            if (request.toast)
                m_wiring.log->notice(severityOf(request.level), name, text);
            else
                m_wiring.log->log(text, severityOf(request.level), name);
        }
        reply({});
        return;
    case protocol::Service::Call: {
        auto params = api::Json::parse(request.json);
        if (!m_wiring.api || !params) {
            reply(envelope(api::fail(api::errors::kUnavailable, "the Hikari API isn't available here")));
            return;
        }
        if (m_wiring.featureUsed)
            if (const QString feature = analyticsFeature(request.name); !feature.isEmpty())
                m_wiring.featureUsed(feature);
        m_wiring.api->call(plugin.caller, request.name, *params, [reply](api::Result result) { reply(envelope(result)); });
        return;
    }
    case protocol::Service::SetSetting: {
        auto value = api::Json::parse(request.json);
        if (!value || !m_installer) {
            reply("HikariSub can’t keep settings here");
            return;
        }
        const auto kept = m_installer->setSetting(plugin.id, request.name, *value);
        if (kept)
            m_ui->settingsChanged(plugin.id);
        reply(kept ? std::string() : kept.error());
        return;
    }
    case protocol::Service::Subscribe: {
        if (!m_wiring.api) {
            reply("the Hikari API isn't available here");
            return;
        }
        PluginProcess *process = plugin.process.get();
        const std::uint64_t subscription =
            m_wiring.api->subscribe(plugin.caller, {request.name}, [this, process, id = plugin.id](const api::Event &event) {
                if (event.name.starts_with("program."))
                    process->deliver(event.name, event.data.dump(), {});
                else if (onIn(qs(id), m_wiring.activeWindow ? m_wiring.activeWindow() : 0))
                    process->push(event.name, event.data.dump());
            });
        if (subscription == 0) {
            reply("there is no event \xE2\x80\x9C" + request.name + "\xE2\x80\x9D");
            return;
        }
        plugin.subscriptions.push_back(subscription);
        reply({});
        return;
    }
    case protocol::Service::Lock:
        lock(plugin, std::move(reply));
        return;
    case protocol::Service::Unlock:
        unlock(plugin);
        reply({});
        return;
    case protocol::Service::Ui: {
        auto params = api::Json::parse(request.json);
        if (!params || !params->isObject()) {
            reply(envelope(api::fail(api::errors::kInvalidParams, "the parameters must be a JSON object")));
            return;
        }
        m_ui->serve(plugin.id, request.name, *params, std::move(reply));
        return;
    }
    case protocol::Service::Files:
    case protocol::Service::Net: {
        auto params = api::Json::parse(request.json);
        if (!params || !params->isObject()) {
            reply(envelope(api::fail(api::errors::kInvalidParams, "the parameters must be a JSON object")));
            return;
        }
        auto answer = [reply](api::Result result) { reply(envelope(result)); };
        if (request.service == protocol::Service::Files)
            m_files->serve(plugin.files, request.name, *params, std::move(answer));
        else if (request.name == "net.fetch")
            m_network->fetch(plugin.id, plugin.manifest.permissions.networkHosts, *params, std::move(answer));
        else
            answer(api::fail(api::errors::kUnknownOperation, "there is no operation \xE2\x80\x9C" + request.name + "\xE2\x80\x9D"));
        return;
    }
    }
    reply({});
}

void PluginRuntime::lock(Running &plugin, PluginProcess::ServiceReply reply)
{
    if (!m_lockHolder || m_lockHolder == &plugin) {
        m_lockHolder = &plugin;
        reply({});
        return;
    }
    auto timeout = std::make_unique<QTimer>();
    timeout->setSingleShot(true);
    QTimer *timer = timeout.get();
    connect(timer, &QTimer::timeout, this, [this, timer] {
        const auto it = std::ranges::find_if(m_lockWaiters, [timer](const LockWaiter &w) { return w.timeout.get() == timer; });
        if (it == m_lockWaiters.end())
            return;
        const PluginProcess::ServiceReply refuse = std::move(it->reply);
        it->timeout.release()->deleteLater();
        m_lockWaiters.erase(it);
        refuse(kLockBusy);
    });
    timer->start(m_wiring.transactionWaitMs);
    m_lockWaiters.push_back({&plugin, std::move(reply), std::move(timeout)});
}

void PluginRuntime::unlock(Running &plugin)
{
    if (m_lockHolder != &plugin)
        return;
    m_lockHolder = nullptr;
    if (m_lockWaiters.empty())
        return;
    LockWaiter next = std::move(m_lockWaiters.front());
    m_lockWaiters.pop_front();
    m_lockHolder = next.plugin;
    next.reply({});
}

void PluginRuntime::releaseLock(Running &plugin)
{
    std::erase_if(m_lockWaiters, [&plugin](const LockWaiter &w) { return w.plugin == &plugin; });
    unlock(plugin);
    if (m_wiring.api)
        m_wiring.api->endGroup(plugin.caller.id);
}

QVariantList PluginRuntime::jobs() const
{
    QVariantList out;
    for (auto it = m_jobs.rbegin(); it != m_jobs.rend(); ++it)
        out.push_back(QVariantMap{{QStringLiteral("plugin"), qs(it->plugin)},
                                  {QStringLiteral("name"), it->name},
                                  {QStringLiteral("title"), it->title},
                                  {QStringLiteral("fraction"), it->fraction},
                                  {QStringLiteral("text"), it->text},
                                  {QStringLiteral("cancelling"), it->cancelling},
                                  {QStringLiteral("stuck"), it->stuck}});
    return out;
}

void PluginRuntime::cancelJob(const QString &pluginId)
{
    const std::string id = pluginId.toStdString();
    const auto job = std::ranges::find(m_jobs, id, &Job::plugin);
    const auto running = m_plugins.find(id);
    if (job == m_jobs.end() || job->cancelling || running == m_plugins.end())
        return;
    running->second->process->cancelJob();
    job->cancelling = true;
    job->grace = std::make_unique<QTimer>();
    job->grace->setSingleShot(true);
    connect(job->grace.get(), &QTimer::timeout, this, [this, id] {
        if (const auto stuck = std::ranges::find(m_jobs, id, &Job::plugin); stuck != m_jobs.end()) {
            stuck->stuck = true;
            emit jobsChanged();
        }
    });
    job->grace->start(m_wiring.jobGraceMs);
    emit jobsChanged();
}

QVariantList PluginRuntime::marks(const QString &pluginId) const
{
    QVariantList out;
    if (!m_wiring.marks)
        return out;
    static const QStringList roles{QStringLiteral("accent"), QStringLiteral("warning"), QStringLiteral("danger"),
                                   QStringLiteral("success"), QStringLiteral("muted")};
    const std::string owner = "plugin:" + pluginId.toStdString();
    for (const auto &[document, count] : m_wiring.marks->counts(owner))
        out.push_back(QVariantMap{
            {QStringLiteral("document"), double(document)},
            {QStringLiteral("colour"), roles.value(int(m_wiring.marks->of(owner, application::DocumentId{document}).front().colour))},
            {QStringLiteral("file"), m_wiring.documentTitle ? m_wiring.documentTitle(application::DocumentId{document}) : QString()},
            {QStringLiteral("count"), qulonglong(count)}});
    return out;
}

QVariantList PluginRuntime::programs(const QString &pluginId) const
{
    QVariantList out;
    const std::string id = pluginId.toStdString();
    const auto manifest = m_installer ? m_installer->installedManifest(id) : std::unexpected(std::string());
    if (!manifest)
        return out;
    const std::vector<const plugins::Program *> missing = m_downloads->missing(id, *manifest);
    for (const plugins::Program &program : manifest->programs) {
        QString state = QStringLiteral("ready");
        if (!program.platforms.contains(std::string(plugins::thisPlatform())))
            state = QStringLiteral("unavailable");
        else if (std::ranges::find(missing, &program) != missing.end())
            state = m_downloads->fetching(id) ? QStringLiteral("downloading") : QStringLiteral("missing");
        out.push_back(QVariantMap{{QStringLiteral("title"), qs(program.title)},
                                  {QStringLiteral("state"), state},
                                  {QStringLiteral("problem"), state == QStringLiteral("missing") ? m_downloads->problem(id) : QString()}});
    }
    return out;
}

void PluginRuntime::downloadPrograms(const QString &pluginId)
{
    const std::string id = pluginId.toStdString();
    if (const auto manifest = m_installer ? m_installer->installedManifest(id) : std::unexpected(std::string()))
        m_downloads->fetch(id, *manifest);
}

void PluginRuntime::clearMarks(const QString &pluginId, double document)
{
    if (m_wiring.marks)
        m_wiring.marks->clear("plugin:" + pluginId.toStdString(), application::DocumentId{std::uint64_t(document)});
}

void PluginRuntime::endJob(const std::string &plugin)
{
    if (std::erase_if(m_jobs, [&](const Job &job) { return job.plugin == plugin; }) > 0)
        emit jobsChanged();
}

void PluginRuntime::endAccess(Running &plugin)
{
    m_network->cancel(plugin.id);
    m_files->end(plugin.id, plugin.caller.id);
    if (m_wiring.programs)
        m_wiring.programs->end(plugin.id);
    if (m_wiring.marks)
        m_wiring.marks->clearOwner(plugin.caller.id);
    if (m_wiring.problems)
        m_wiring.problems->clearOwner(plugin.caller.id);
    endJob(plugin.id);
}

void PluginRuntime::endFileAccess(const QString &pluginId)
{
    if (const auto it = m_plugins.find(pluginId.toStdString()); it != m_plugins.end())
        m_files->end(it->first, it->second->caller.id);
}

void PluginRuntime::unsubscribe(Running &plugin)
{
    if (m_wiring.api)
        for (const std::uint64_t subscription : plugin.subscriptions)
            m_wiring.api->unsubscribe(subscription);
    plugin.subscriptions.clear();
}

std::vector<const PluginRuntime::Running *> PluginRuntime::byName() const
{
    std::vector<const Running *> order;
    for (const auto &[id, plugin] : m_plugins)
        order.push_back(plugin.get());
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::ranges::stable_sort(order, [&](const Running *a, const Running *b) {
        return collator.compare(qs(a->manifest.name), qs(b->manifest.name)) < 0;
    });
    return order;
}

void PluginRuntime::rework()
{
    QVariantList working;
    for (const Running *plugin : byName()) {
        const PluginProcess &process = *plugin->process;
        const bool background = plugin->manifest.permissions.background.has_value();
        if (!process.stillWorking() && !(background && (process.inTimer() || process.inJob())))
            continue;
        QString what;
        const QString work = process.work();
        if (const auto job = std::ranges::find(m_jobs, plugin->id, &Job::plugin); job != m_jobs.end())
            what = job->title;
        else if (work == QStringLiteral("load"))
            what = QCoreApplication::translate("HikariSub.Plugins", "Starting");
        else if (work == QStringLiteral("timer"))
            what = QCoreApplication::translate("HikariSub.Plugins", "A timer");
        else
            what = commandTitle(plugin->manifest, work);
        working << QVariantMap{{QStringLiteral("plugin"), qs(plugin->id)},
                               {QStringLiteral("name"), qs(plugin->manifest.name)},
                               {QStringLiteral("what"), what},
                               {QStringLiteral("background"), background},
                               {QStringLiteral("stillWorking"), process.stillWorking()}};
    }
    if (working == m_working)
        return;
    m_working = std::move(working);
    emit workingChanged();
}

void PluginRuntime::relist()
{
    QVariantList commands, menuItems;
    for (const Running *plugin : byName()) {
        const QString name = qs(plugin->manifest.name);
        const auto idOf = [&](const std::string &command) { return qs(std::string(kCommandPrefix) + plugin->id + "." + command); };
        for (const auto &command : plugin->manifest.commands)
            commands << QVariantMap{{QStringLiteral("id"), idOf(command.id)},
                                    {QStringLiteral("plugin"), qs(plugin->id)},
                                    {QStringLiteral("pluginName"), name},
                                    {QStringLiteral("title"), qs(command.title)},
                                    {QStringLiteral("titles"), QStringList{name, qs(command.title)}},
                                    {QStringLiteral("icon"), qs(command.icon)},
                                    {QStringLiteral("description"), qs(command.description)},
                                    {QStringLiteral("palette"), command.palette}};
        for (const plugins::MenuPlacement &placement : plugin->manifest.menus) {
            const auto command = std::ranges::find(plugin->manifest.commands, placement.command, &plugins::Command::id);
            menuItems << QVariantMap{{QStringLiteral("id"), idOf(placement.command)},
                                     {QStringLiteral("plugin"), qs(plugin->id)},
                                     {QStringLiteral("pluginName"), name},
                                     {QStringLiteral("title"), qs(command->title)},
                                     {QStringLiteral("icon"), qs(command->icon)},
                                     {QStringLiteral("location"), menuLocation(placement.location)},
                                     {QStringLiteral("group"), qs(placement.group)},
                                     {QStringLiteral("name"), qs("pluginMenu_" + plugin->id + "_" + placement.command)}};
        }
    }
    if (commands == m_commands && menuItems == m_menuItems)
        return;
    m_commands = std::move(commands);
    m_menuItems = std::move(menuItems);
    emit commandsChanged();
    emit statusItemsChanged();
}

const plugins::Command *PluginRuntime::command(const QString &commandId, Running **plugin) const
{
    const std::string id = commandId.toStdString();
    const auto cut = id.rfind('.');
    if (!id.starts_with(kCommandPrefix) || cut == std::string::npos || cut <= kCommandPrefix.size())
        return nullptr;
    const auto it = m_plugins.find(id.substr(kCommandPrefix.size(), cut - kCommandPrefix.size()));
    if (it == m_plugins.end())
        return nullptr;
    const auto &declared = it->second->manifest.commands;
    const auto found = std::ranges::find(declared, id.substr(cut + 1), &plugins::Command::id);
    if (found == declared.end())
        return nullptr;
    if (plugin)
        *plugin = it->second.get();
    return &*found;
}

void PluginRuntime::setConditions(int shellWindow, const QVariantMap &facts)
{
    const plugins::ConditionFacts next{facts.value(QStringLiteral("document")).toBool(),
                                       facts.value(QStringLiteral("selectedLines")).toInt(),
                                       facts.value(QStringLiteral("video")).toBool(),
                                       facts.value(QStringLiteral("audio")).toBool(),
                                       facts.value(QStringLiteral("karaoke")).toBool(),
                                       facts.value(QStringLiteral("translation")).toBool()};
    const auto it = m_conditions.find(shellWindow);
    if (it != m_conditions.end() && it->second.document == next.document &&
        it->second.selectedLines == next.selectedLines && it->second.video == next.video &&
        it->second.audio == next.audio && it->second.karaoke == next.karaoke && it->second.translation == next.translation)
        return;
    m_conditions[shellWindow] = next;
    ++m_conditionsRevision;
    emit conditionsChanged();
}

void PluginRuntime::forgetWindow(int shellWindow)
{
    if (m_conditions.erase(shellWindow)) {
        ++m_conditionsRevision;
        emit conditionsChanged();
    }
}

QString PluginRuntime::disabledReason(const QString &commandId, int shellWindow) const
{
    const plugins::Command *found = command(commandId);
    if (!found || found->when.empty())
        return {};
    if (shellWindow < 0 && m_wiring.activeWindow)
        shellWindow = m_wiring.activeWindow();
    const auto facts = m_conditions.find(shellWindow);
    const auto failing = plugins::failingCondition(found->when, facts == m_conditions.end() ? plugins::ConditionFacts{} : facts->second);
    return failing ? conditionPhrase(*failing) : QString();
}

bool PluginRuntime::run(const QString &commandId, const QString &via)
{
    Running *plugin = nullptr;
    const plugins::Command *found = command(commandId, &plugin);
    if (!found || !disabledReason(commandId).isEmpty())
        return false;
    PluginProcess &process = *plugin->process;
    if (plugin->held) {
        if (m_wiring.log)
            m_wiring.log->notice(ui::LogController::Info, qs(plugin->manifest.name),
                                 QCoreApplication::translate("HikariSub.Plugins", "The plugin is stopped. Start it again in its details."));
        return false;
    }
    if (process.state() == PluginProcess::State::LoadFailed) {
        if (m_wiring.log)
            m_wiring.log->notice(ui::LogController::Error, qs(plugin->manifest.name), QCoreApplication::translate("HikariSub.Plugins", "The plugin isn’t running."),
                                 process.loadError());
        return false;
    }
    const api::Json call = api::Json::object(
        {{"args", api::Json::object()}, {"context", api::Json::object({{"via", api::Json(via.toStdString())}})}});
    return process.run(found->id, call.dump());
}

api::StatusItemSet PluginRuntime::setStatusItem(const std::string &pluginId, const api::StatusItemChange &change)
{
    const auto it = m_plugins.find(pluginId);
    if (it == m_plugins.end() || !it->second->loaded)
        return api::StatusItemSet::NotRunning;
    auto &status = it->second->status;
    const auto found = std::ranges::find_if(status, [&](const Running::Status &s) { return s.item.id == change.item; });
    if (found == status.end())
        return api::StatusItemSet::UnknownItem;
    Running::Status next = *found;
    if (change.text)
        next.item.text = *change.text;
    if (change.icon)
        next.item.icon = *change.icon;
    if (change.tooltip)
        next.item.tooltip = *change.tooltip;
    if (change.tone)
        next.tone = qs(*change.tone);
    if (change.visible)
        next.visible = *change.visible;
    if (next.item.text.empty() && next.item.icon.empty())
        return api::StatusItemSet::Empty;
    *found = std::move(next);
    emit statusItemsChanged();
    return api::StatusItemSet::Set;
}

QVariantList PluginRuntime::statusItems() const
{
    QVariantList out;
    for (const Running *running : byName()) {
        const Running &plugin = *running;
        if (!plugin.loaded)
            continue;
        for (const Running::Status &s : plugin.status)
            if (s.visible)
                out << QVariantMap{{QStringLiteral("plugin"), qs(plugin.id)},
                                   {QStringLiteral("pluginName"), qs(plugin.manifest.name)},
                                   {QStringLiteral("item"), qs(s.item.id)},
                                   {QStringLiteral("key"), qs(plugin.id + "/" + s.item.id)},
                                   {QStringLiteral("title"), qs(s.item.title)},
                                   {QStringLiteral("text"), qs(s.item.text)},
                                   {QStringLiteral("icon"), qs(s.item.icon)},
                                   {QStringLiteral("tooltip"), qs(s.item.tooltip)},
                                   {QStringLiteral("tone"), s.tone},
                                   {QStringLiteral("command"),
                                    s.item.command.empty() ? QString() : qs(std::string(kCommandPrefix) + plugin.id + "." + s.item.command)}};
    }
    return out;
}

QVariantList PluginRuntime::keybindings() const
{
    QVariantList out;
    for (const std::string &id : m_installOrder) {
        const auto it = m_plugins.find(id);
        if (it == m_plugins.end())
            continue;
        const Running *plugin = it->second.get();
        for (const plugins::Keybinding &binding : plugin->manifest.keybindings)
            out << QVariantMap{{QStringLiteral("id"), qs(std::string(kCommandPrefix) + plugin->id + "." + binding.command)},
                               {QStringLiteral("keys"), qs(binding.key)},
                               {QStringLiteral("pluginName"), qs(plugin->manifest.name)}};
    }
    return out;
}

QVariantList PluginRuntime::statusItemTitles() const
{
    QVariantList out;
    for (const Running *running : byName()) {
        const Running &plugin = *running;
        if (!plugin.loaded)
            continue;
        for (const Running::Status &s : plugin.status)
            out << QVariantMap{{QStringLiteral("plugin"), qs(plugin.id)},
                               {QStringLiteral("key"), qs(plugin.id + "/" + s.item.id)},
                               {QStringLiteral("title"), qs(s.item.title)},
                               {QStringLiteral("pluginName"), qs(plugin.manifest.name)}};
    }
    return out;
}

void PluginRuntime::restart(const QString &pluginId)
{
    if (const auto it = m_plugins.find(pluginId.toStdString()); it != m_plugins.end()) {
        it->second->held = false;
        if (hasCode(it->second->manifest))
            it->second->process->start();
    }
}

void PluginRuntime::settingChanged(const QString &pluginId, const QString &setting, const QString &json)
{
    const auto it = m_plugins.find(pluginId.toStdString());
    const auto value = api::Json::parse(json.toStdString());
    if (it == m_plugins.end() || !value)
        return;
    m_ui->settingsChanged(it->first);
    const api::Json data = api::Json::object({{"id", setting.toStdString()}, {"value", *value}});
    it->second->process->deliver("settings:changed", data.dump(), "settings:changed#" + setting.toStdString());
}

void PluginRuntime::forceStop(const QString &pluginId)
{
    if (const auto it = m_plugins.find(pluginId.toStdString()); it != m_plugins.end())
        it->second->process->forceStop();
}

void PluginRuntime::stopWorking(const QString &pluginId)
{
    const auto it = m_plugins.find(pluginId.toStdString());
    if (it == m_plugins.end())
        return;
    it->second->held = it->second->manifest.permissions.background.has_value();
    it->second->process->forceStop();
}

bool PluginRuntime::sandboxed() const
{
    return m_wiring.sandbox && backends::sandbox::systemCoverage(backends::sandbox::Network::None).complete();
}

backends::PluginProcess *PluginRuntime::process(const QString &pluginId) const
{
    const auto it = m_plugins.find(pluginId.toStdString());
    return it == m_plugins.end() ? nullptr : it->second->process.get();
}

bool PluginRuntime::busy(const QString &pluginId) const
{
    const PluginProcess *running = process(pluginId);
    return running && running->busy();
}

QSet<QString> PluginRuntime::runningIds() const
{
    QSet<QString> ids;
    for (const auto &[id, plugin] : m_plugins)
        ids.insert(QString::fromStdString(id));
    return ids;
}

QVariantList PluginRuntime::presencePlugins() const
{
    QVariantList found;
    for (const auto &[id, plugin] : m_plugins)
        if (plugin->manifest.permissions.presence)
            found << QVariantMap{{QStringLiteral("id"), QString::fromStdString(id)},
                                 {QStringLiteral("name"), QString::fromStdString(plugin->manifest.name)}};
    return found;
}

}
