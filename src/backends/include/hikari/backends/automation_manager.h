#pragma once

// The automation manager (L1; docs/qt/automation.md, ADR 0006): one Lua
// helper process and state per loaded script, loads and reloads that run the
// script's top level visibly, macro identity through AutomationRegistry, and
// one active macro application-wide. A helper crash marks only its script
// unavailable; others keep their state.

#include "hikari/application/automation.h"
#include "hikari/application/automation_registry.h"
#include "hikari/backends/lua_script_host.h"

#include <QDateTime>
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
    // L4: the macro runs against `snapshot`; its result is host(path)->lastResult().
    bool run(const std::string &path, int ordinal, const application::MacroSnapshot &snapshot,
             bool validateFirst = false);
    // S4 (legacy LuaScript::CheckLastModified): whether the script file's
    // modification time differs from the one recorded when it last loaded;
    // false when the file cannot be read.
    bool modifiedSinceLoad(const std::string &path) const;
    void cancel() override;
    bool forceStop(const std::string &path) override;
    // Application quit: no new runs, running macros are cancelled, and helpers
    // still running after `deadlineMs` are terminated. Returns those paths.
    std::vector<std::string> shutdown(int deadlineMs = 5000);
    void setGracePeriod(int ms);
    // Handlers every script's helper gets, now and when loaded later (S1).
    void setDialogHandler(LuaScriptHost::DialogHandler handler);
    void setServiceHandler(LuaScriptHost::ServiceHandler handler);
    void setObserver(std::function<void()> changed) override { m_observer = std::move(changed); }

    const application::AutomationRegistry &registry() const { return m_registry; }
    LuaScriptHost *host(const std::string &path) const;
    bool busy() const;

signals:
    void changed();
    void runFinished(const QString &path, hikari::backends::LuaScriptHost::RunOutcome outcome, const QString &message);
    // The running macro's progress sink (legacy LuaProgressSink).
    void logged(const QString &path, const QString &text);
    void progressChanged(const QString &path, double percent);
    void taskChanged(const QString &path, const QString &task);
    void titleChanged(const QString &path, const QString &title);
    void dialogWithdrawn(const QString &path);
    void servicesWithdrawn(const QString &path);

private:
    struct Entry {
        std::unique_ptr<LuaScriptHost> host;
        std::uint64_t generation = 0;
        std::string sha256; // of the script file when this generation loaded
        QDateTime modified; // the file's modification time when this generation started loading
    };
    void attach(const std::string &path, Entry &entry);
    void rebuildRegistry(); // in load order, from every script with macros
    void notify();

    QString m_helperPath, m_sharedInclude;
    std::vector<std::string> m_order; // load order
    std::map<std::string, Entry> m_entries;
    application::AutomationRegistry m_registry;
    std::function<void()> m_observer;
    LuaScriptHost::DialogHandler m_dialogHandler;
    LuaScriptHost::ServiceHandler m_serviceHandler;
    int m_graceMs = 3000;
    bool m_shuttingDown = false;
};

} // namespace hikari::backends
