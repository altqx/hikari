#include "hikari/app/plugin_runtime.h"

#include "hikari/app/plugin_nodes.h"

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

QString stopReason(PluginProcess::StopReason reason, int timeLimitMs)
{
    switch (reason) {
    case PluginProcess::StopReason::TimeLimit:
        return QCoreApplication::translate("HikariSub.Plugins", "It ran for more than %n second(s).", nullptr,
                                           std::max(1, timeLimitMs / 1000));
    case PluginProcess::StopReason::Crashed:
        return QCoreApplication::translate("HikariSub.Plugins", "It stopped unexpectedly.");
    case PluginProcess::StopReason::ForceStopped:
        break;
    }
    return QCoreApplication::translate("HikariSub.Plugins", "It was force stopped.");
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
};

PluginRuntime::PluginRuntime(Wiring wiring, QObject *parent) : QObject(parent), m_wiring(std::move(wiring))
{
    m_ui = new PluginUi(
        [this](const std::string &plugin, const std::string &event, const std::string &json, const std::string &key) {
            const auto it = m_plugins.find(plugin);
            return it != m_plugins.end() && it->second->process->deliver(event, json, key);
        },
        this);
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
    m_files = std::make_unique<PluginFiles>(m_wiring.api, m_installer ? &*m_installer : nullptr, *m_ui);
    m_ui->setGrantDrop([this](const std::string &plugin, const QString &file) {
        const auto it = m_plugins.find(plugin);
        return it == m_plugins.end() ? api::Json() : m_files->grantDropped(it->second->files, file);
    });
    wireQmlPanels();
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
    std::vector<PluginUi::Plugin> installed;
    std::vector<application::PluginNode> nodes;
    std::map<std::string, std::string> unavailable;
    if (m_installer) {
        if (const auto registry = m_installer->registry())
            for (const auto &[id, entry] : *registry) {
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
                if (runs)
                    wanted.emplace(id, std::move(*manifest));
            }
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
        process->forceStop();
        it = m_plugins.erase(it);
    }
    for (auto &[id, manifest] : wanted)
        if (!m_plugins.contains(id))
            m_plugins.emplace(id, launch(id, std::move(manifest)));
    m_userOn = std::move(userOn);
    m_onIn = std::move(onIn);
    application::setPluginNodes(std::move(nodes), std::move(unavailable));
    m_ui->setPlugins(std::move(installed));
    relist();
}

std::unique_ptr<PluginRuntime::Running> PluginRuntime::launch(const std::string &id, plugins::Manifest manifest)
{
    auto plugin = std::make_unique<Running>();
    Running *raw = plugin.get();
    raw->id = id;
    raw->manifest = std::move(manifest);
    raw->caller = api::Caller{"plugin:" + id, raw->manifest.name, raw->manifest.permissions.allDocuments.has_value()};
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
        return request;
    };
    raw->process = std::make_unique<PluginProcess>(m_wiring.helperPath, load, m_wiring.limits);
    PluginProcess *process = raw->process.get();
    if (m_wiring.sandbox && m_installer && raw->manifest.tier == plugins::Tier::Restricted) {
        const fs::path root = m_installer->folders().plugins / fs::path(std::u8string(id.begin(), id.end()));
        process->setSandbox(backends::pluginProfile(m_wiring.helperPath, QString::fromStdU16String(root.u16string()), id));
    }
    process->setServiceHandler([this, raw](const PluginProcess::ServiceRequest &request, PluginProcess::ServiceReply reply) {
        serve(*raw, request, std::move(reply));
    });
    const QString name = qs(raw->manifest.name);
    auto log = [this] { return m_wiring.log; };
    connect(process, &PluginProcess::unitEnded, this, [this, raw] {
        releaseLock(*raw);
        emit idle(qs(raw->id));
    });
    connect(process, &PluginProcess::stopped, this, [this, raw] { emit idle(qs(raw->id)); });
    connect(process, &PluginProcess::loaded, this, [this, raw] { m_ui->running(raw->id); });
    connect(process, &PluginProcess::loadFailed, this, [this, raw, name, log](const QString &message) {
        unsubscribe(*raw);
        releaseLock(*raw);
        endAccess(*raw);
        m_ui->stopped(raw->id, QStringLiteral("failed"), message);
        if (log())
            log()->notice(ui::LogController::Error, name, QCoreApplication::translate("HikariSub.Plugins", "The plugin didn’t start."), message);
    });
    connect(process, &PluginProcess::commandFailed, this, [raw, name, log](const QString &command, const QString &message) {
        QString title = command;
        for (const auto &c : raw->manifest.commands)
            if (qs(c.id) == command)
                title = qs(c.title);
        if (log())
            log()->notice(ui::LogController::Error, name, QCoreApplication::translate("HikariSub.Plugins", "“%1” failed.").arg(title), message);
    });
    connect(process, &PluginProcess::eventFailed, this, [name, log](const QString &event, const QString &message) {
        if (log())
            log()->log(QCoreApplication::translate("HikariSub.Plugins", "Its %1 handler failed: %2").arg(event, message), ui::LogController::Error, name);
    });
    connect(process, &PluginProcess::stopped, this, [this, raw, name, log](PluginProcess::StopReason reason) {
        unsubscribe(*raw);
        releaseLock(*raw);
        endAccess(*raw);
        m_ui->stopped(raw->id, QStringLiteral("stopped"), stopReason(reason, m_wiring.limits.timeLimitMs));
        if (!log())
            return;
        switch (reason) {
        case PluginProcess::StopReason::TimeLimit:
            log()->notice(ui::LogController::Warning, name,
                          QCoreApplication::translate("HikariSub.Plugins",
                                                      "The plugin ran for more than %n second(s), so HikariSub stopped it. "
                                                      "Its next command starts it again.",
                                                      nullptr, std::max(1, m_wiring.limits.timeLimitMs / 1000)));
            break;
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
    process->start();
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
                if (onIn(qs(id), m_wiring.activeWindow ? m_wiring.activeWindow() : 0))
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
}

void PluginRuntime::endAccess(Running &plugin)
{
    m_network->cancel(plugin.id);
    m_files->end(plugin.id, plugin.caller.id);
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

void PluginRuntime::relist()
{
    std::vector<const Running *> order;
    for (const auto &[id, plugin] : m_plugins)
        order.push_back(plugin.get());
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::ranges::stable_sort(order, [&](const Running *a, const Running *b) {
        return collator.compare(qs(a->manifest.name), qs(b->manifest.name)) < 0;
    });
    QVariantList commands;
    for (const Running *plugin : order) {
        const QString name = qs(plugin->manifest.name);
        for (const auto &command : plugin->manifest.commands)
            commands << QVariantMap{{QStringLiteral("id"), qs(std::string(kCommandPrefix) + plugin->id + "." + command.id)},
                                    {QStringLiteral("plugin"), qs(plugin->id)},
                                    {QStringLiteral("pluginName"), name},
                                    {QStringLiteral("title"), qs(command.title)},
                                    {QStringLiteral("titles"), QStringList{name, qs(command.title)}}};
    }
    if (commands == m_commands)
        return;
    m_commands = std::move(commands);
    emit commandsChanged();
}

bool PluginRuntime::run(const QString &commandId)
{
    const std::string id = commandId.toStdString();
    const auto cut = id.rfind('.');
    if (!id.starts_with(kCommandPrefix) || cut == std::string::npos || cut <= kCommandPrefix.size())
        return false;
    const auto it = m_plugins.find(id.substr(kCommandPrefix.size(), cut - kCommandPrefix.size()));
    if (it == m_plugins.end())
        return false;
    const std::string command = id.substr(cut + 1);
    const auto &declared = it->second->manifest.commands;
    if (std::ranges::find(declared, command, &plugins::Contribution::id) == declared.end())
        return false;
    PluginProcess &process = *it->second->process;
    if (process.state() == PluginProcess::State::LoadFailed) {
        if (m_wiring.log)
            m_wiring.log->notice(ui::LogController::Error, qs(it->second->manifest.name), QCoreApplication::translate("HikariSub.Plugins", "The plugin isn’t running."),
                                 process.loadError());
        return false;
    }
    return process.run(command);
}

void PluginRuntime::restart(const QString &pluginId)
{
    if (const auto it = m_plugins.find(pluginId.toStdString()); it != m_plugins.end())
        it->second->process->start();
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

}
