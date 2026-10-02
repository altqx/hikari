#include "hikari/backends/automation_manager.h"

#include <QCryptographicHash>
#include <QFile>

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
    it->second.host->restart(); // a new helper: the script's top level runs again
    rebuildRegistry();
    notify();
    return true;
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
    if (busy())
        return false; // one active macro application-wide
    LuaScriptHost *h = host(path);
    if (!h || !h->run(ordinal))
        return false;
    notify();
    return true;
}

void AutomationManager::cancel()
{
    for (auto &[path, e] : m_entries)
        if (e.host->state() == LuaScriptHost::State::Running)
            e.host->cancel();
}

} // namespace hikari::backends
