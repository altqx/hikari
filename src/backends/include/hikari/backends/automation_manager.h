#pragma once

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
    // The result lands in host(path)->lastResult().
    bool run(const std::string &path, int ordinal, const application::MacroSnapshot &snapshot,
             bool validateFirst = false);
    // Runs only the macro's validation; busy until runFinished reports Ok (valid) or NotValid.
    bool validate(const std::string &path, int ordinal, const application::MacroSnapshot &snapshot);
    // Whether the file's mtime differs from the one at last load; false when it cannot be read.
    bool modifiedSinceLoad(const std::string &path) const;
    void cancel() override;
    bool forceStop(const std::string &path) override;
    // Refuses new runs, cancels running macros, terminates helpers still running after
    // `deadlineMs` and returns their paths.
    std::vector<std::string> shutdown(int deadlineMs = 5000);
    void setGracePeriod(int ms);
    // A change reloads every loaded script that is not running a macro.
    void setTraceLevel(int level);
    void setDialogHandler(LuaScriptHost::DialogHandler handler);
    void setServiceHandler(LuaScriptHost::ServiceHandler handler);
    void setObserver(std::function<void()> changed) override { m_observer = std::move(changed); }

    const application::AutomationRegistry &registry() const { return m_registry; }
    LuaScriptHost *host(const std::string &path) const;
    bool busy() const;

signals:
    void changed();
    void runFinished(const QString &path, hikari::backends::LuaScriptHost::RunOutcome outcome, const QString &message);
    void loadFailed(const QString &path);
    void logged(const QString &path, const QString &text, int level);
    void progressChanged(const QString &path, double percent);
    void taskChanged(const QString &path, const QString &task);
    void macroStarted(const QString &path);
    void titleChanged(const QString &path, const QString &title);
    void dialogWithdrawn(const QString &path);
    void servicesWithdrawn(const QString &path);

private:
    struct Entry {
        std::unique_ptr<LuaScriptHost> host;
        std::uint64_t generation = 0;
        std::string sha256;
        QDateTime modified;
    };
    void attach(const std::string &path, Entry &entry);
    void rebuildRegistry();
    void notify();

    QString m_helperPath, m_sharedInclude;
    std::vector<std::string> m_order;
    std::map<std::string, Entry> m_entries;
    application::AutomationRegistry m_registry;
    std::function<void()> m_observer;
    LuaScriptHost::DialogHandler m_dialogHandler;
    LuaScriptHost::ServiceHandler m_serviceHandler;
    int m_graceMs = 3000;
    int m_traceLevel = 3;
    bool m_shuttingDown = false;
};

}
