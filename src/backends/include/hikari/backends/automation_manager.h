#pragma once

// The automation manager (L1; docs/qt/automation.md, ADR 0006): one Lua
// helper process and state per loaded script, loads and reloads that run the
// script's top level visibly, macro identity through AutomationRegistry, and
// one active macro application-wide. A helper crash marks only its script
// unavailable; others keep their state.

#include "hikari/application/automation.h"
#include "hikari/application/automation_registry.h"
#include "hikari/backends/lua_script_host.h"

#include <QObject>

#include <map>
#include <memory>

namespace hikari::backends {

class AutomationManager : public QObject, public application::AutomationServicePort {
    Q_OBJECT
public:
    AutomationManager(QString helperPath, QString sharedInclude, QObject *parent = nullptr);
    ~AutomationManager() override;

    std::vector<application::ScriptStatus> scripts() const override;
    void load(const std::string &path) override;
    bool reload(const std::string &path) override;
    void unload(const std::string &path) override;
    bool run(const std::string &path, int ordinal) override;
    void cancel() override;
    void setObserver(std::function<void()> changed) override { m_observer = std::move(changed); }

    const application::AutomationRegistry &registry() const { return m_registry; }
    LuaScriptHost *host(const std::string &path) const;
    bool busy() const;

signals:
    void changed();
    void runFinished(const QString &path, hikari::backends::LuaScriptHost::RunOutcome outcome, const QString &message);

private:
    struct Entry {
        std::unique_ptr<LuaScriptHost> host;
        std::uint64_t generation = 0;
        std::string sha256; // of the script file when this generation loaded
    };
    void attach(const std::string &path, Entry &entry);
    void rebuildRegistry(); // in load order, from every script with macros
    void notify();

    QString m_helperPath, m_sharedInclude;
    std::vector<std::string> m_order; // load order
    std::map<std::string, Entry> m_entries;
    application::AutomationRegistry m_registry;
    std::function<void()> m_observer;
};

} // namespace hikari::backends
