#include "hikari/backends/automation_manager.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>

#include <algorithm>

namespace hikari::backends {

using application::ScriptStatus;

namespace {

std::string fileSha256(const std::string &path)
{
    QFile f(QString::fromStdString(path));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256).toHex().toStdString();
}

QDateTime modifiedTime(const std::string &path)
{
    const QFileInfo info(QString::fromStdString(path));
    return info.isFile() ? info.lastModified() : QDateTime();
}

ScriptStatus::State stateOf(LuaScriptHost::State s)
{
    switch (s) {
    case LuaScriptHost::State::Ready: return ScriptStatus::State::Ready;
    case LuaScriptHost::State::Running: return ScriptStatus::State::Running;
    case LuaScriptHost::State::LoadFailed: return ScriptStatus::State::LoadFailed;
    case LuaScriptHost::State::Unavailable: return ScriptStatus::State::Unavailable;
    default: return ScriptStatus::State::Loading;
    }
}

} // namespace

AutomationManager::AutomationManager(QString helperPath, QString sharedInclude, QObject *parent)
    : QObject(parent), m_helperPath(std::move(helperPath)), m_sharedInclude(std::move(sharedInclude))
{
}

AutomationManager::~AutomationManager() = default;

void AutomationManager::notify()
{
    emit changed();
    if (m_observer)
        m_observer();
}

std::vector<ScriptStatus> AutomationManager::scripts() const
{
    std::vector<ScriptStatus> out;
    for (const auto &path : m_order) {
        const Entry &e = m_entries.at(path);
        ScriptStatus s;
        s.path = path;
        s.state = stateOf(e.host->state());
        s.info = e.host->info();
        s.error = e.host->lastError().toStdString();
        s.generation = e.generation;
        s.forceStopOffered = e.host->forceStopAvailable();
        out.push_back(std::move(s));
    }
    return out;
}

LuaScriptHost *AutomationManager::host(const std::string &path) const
{
    const auto it = m_entries.find(path);
    return it == m_entries.end() ? nullptr : it->second.host.get();
}

bool AutomationManager::busy() const
{
    return std::any_of(m_entries.begin(), m_entries.end(),
                       [](const auto &e) { return e.second.host->state() == LuaScriptHost::State::Running; });
}

void AutomationManager::rebuildRegistry()
{
    application::AutomationRegistry registry;
    for (const auto &path : m_order) {
        const Entry &e = m_entries.at(path);
        const auto state = e.host->state();
        if (state == LuaScriptHost::State::Loading || state == LuaScriptHost::State::LoadFailed ||
            state == LuaScriptHost::State::Idle)
            continue; // nothing registered to bind to
        application::AutomationRegistry::Script script;
        script.path = path;
        script.sha256 = e.sha256;
        for (const auto &m : e.host->info().macros)
            script.macros.push_back(m.name);
        registry.setScript(std::move(script));
    }
    m_registry = std::move(registry);
}

void AutomationManager::attach(const std::string &path, Entry &entry)
{
    LuaScriptHost *host = entry.host.get();
    host->setDialogHandler(m_dialogHandler);
    host->setServiceHandler(m_serviceHandler);
    const QString qpath = QString::fromStdString(path);
    connect(host, &LuaScriptHost::logged, this, [this, qpath](const QString &t) { emit logged(qpath, t); });
    connect(host, &LuaScriptHost::progressChanged, this, [this, qpath](double p) { emit progressChanged(qpath, p); });
    connect(host, &LuaScriptHost::taskChanged, this, [this, qpath](const QString &t) { emit taskChanged(qpath, t); });
    connect(host, &LuaScriptHost::titleChanged, this, [this, qpath](const QString &t) { emit titleChanged(qpath, t); });
    connect(host, &LuaScriptHost::dialogWithdrawn, this, [this, qpath] { emit dialogWithdrawn(qpath); });
    connect(host, &LuaScriptHost::servicesWithdrawn, this, [this, qpath] { emit servicesWithdrawn(qpath); });
    connect(host, &LuaScriptHost::loaded, this, [this, path] {
        m_entries.at(path).sha256 = fileSha256(path);
        rebuildRegistry();
        notify();
    });
    connect(host, &LuaScriptHost::loadFailed, this, [this] {
        rebuildRegistry();
        notify();
    });
    connect(host, &LuaScriptHost::unavailable, this, [this] { notify(); });
    connect(host, &LuaScriptHost::forceStopOffered, this, [this] { notify(); });
    connect(host, &LuaScriptHost::finished, this,
            [this, path](LuaScriptHost::RunOutcome outcome, const QString &message) {
                emit runFinished(QString::fromStdString(path), outcome, message);
                notify();
            });
}

void AutomationManager::load(const std::string &path)
{
    if (m_entries.contains(path)) {
        reload(path);
        return;
    }
    Entry entry;
    entry.host = std::make_unique<LuaScriptHost>(m_helperPath, QString::fromStdString(path), m_sharedInclude);
    entry.generation = 1;
    entry.modified = modifiedTime(path);
    entry.host->setGracePeriod(m_graceMs);
    auto &stored = m_entries.emplace(path, std::move(entry)).first->second;
    m_order.push_back(path);
    attach(path, stored);
    stored.host->load();
    notify();
}

bool AutomationManager::reload(const std::string &path)
{
    const auto it = m_entries.find(path);
    if (it == m_entries.end() || it->second.host->state() == LuaScriptHost::State::Running)
        return false;
    ++it->second.generation;
    it->second.modified = modifiedTime(path);
    it->second.host->restart(); // a new helper: the script's top level runs again
    rebuildRegistry();
    notify();
    return true;
}

bool AutomationManager::modifiedSinceLoad(const std::string &path) const
{
    const auto it = m_entries.find(path);
    if (it == m_entries.end())
        return false;
    // Legacy compares the last write time (GetFileTime) and treats a file it
    // cannot open as unchanged (Automation.cpp:751-773).
    const QDateTime now = modifiedTime(path);
    return now.isValid() && now != it->second.modified;
}

void AutomationManager::unload(const std::string &path)
{
    const auto it = m_entries.find(path);
    if (it == m_entries.end())
        return;
    m_entries.erase(it);
    std::erase(m_order, path);
    rebuildRegistry();
    notify();
}

bool AutomationManager::run(const std::string &path, int ordinal)
{
    return run(path, ordinal, application::MacroSnapshot{});
}

bool AutomationManager::run(const std::string &path, int ordinal, const application::MacroSnapshot &snapshot,
                            bool validateFirst)
{
    if (m_shuttingDown || busy())
        return false; // one active macro application-wide
    LuaScriptHost *h = host(path);
    if (!h || !h->run(ordinal, snapshot, validateFirst))
        return false;
    notify();
    return true;
}

bool AutomationManager::forceStop(const std::string &path)
{
    LuaScriptHost *h = host(path);
    if (!h || !h->forceStop())
        return false;
    notify();
    return true;
}

void AutomationManager::setDialogHandler(LuaScriptHost::DialogHandler handler)
{
    m_dialogHandler = std::move(handler);
    for (auto &[path, e] : m_entries)
        e.host->setDialogHandler(m_dialogHandler);
}

void AutomationManager::setServiceHandler(LuaScriptHost::ServiceHandler handler)
{
    m_serviceHandler = std::move(handler);
    for (auto &[path, e] : m_entries)
        e.host->setServiceHandler(m_serviceHandler);
}

void AutomationManager::setGracePeriod(int ms)
{
    m_graceMs = ms;
    for (auto &[path, e] : m_entries)
        e.host->setGracePeriod(ms);
}

std::vector<std::string> AutomationManager::shutdown(int deadlineMs)
{
    m_shuttingDown = true;
    cancel();
    QElapsedTimer waited;
    waited.start();
    while (busy() && waited.elapsed() < deadlineMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    std::vector<std::string> terminated;
    for (const auto &path : m_order) {
        LuaScriptHost *h = m_entries.at(path).host.get();
        if (h->state() == LuaScriptHost::State::Running) {
            h->terminate();
            terminated.push_back(path);
        }
    }
    notify();
    return terminated;
}

void AutomationManager::cancel()
{
    for (auto &[path, e] : m_entries)
        if (e.host->state() == LuaScriptHost::State::Running)
            e.host->cancel();
}

} // namespace hikari::backends
