#pragma once

// One script in its own helper process. A lost helper makes it Unavailable and its
// interrupted macro is never rerun; only restart(), which reruns the top level, brings
// it back. A dialog answer after the run ended is dropped.

#include "hikari/application/automation.h"
#include "hikari/backends/helper_host.h"
#include "hikari/backends/lua_protocol.h"

#include <QObject>
#include <QString>
#include <QTimer>

#include <functional>
#include <memory>
#include <optional>
#include <set>

namespace hikari::backends {

class LuaScriptHost : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Loading, Ready, Running, LoadFailed, Unavailable };
    // ForceStopped: the user chose Force stop. NotValid: the validation function answered
    // false or raised (the message), and the macro did not run.
    enum class RunOutcome { Ok, Failed, Cancelled, HelperLost, ForceStopped, NotValid };

    using DialogReply = std::function<void(application::DialogResult)>;
    using DialogHandler = std::function<void(const application::DialogRequest &, DialogReply)>;

    LuaScriptHost(QString helperPath, QString scriptPath, QString sharedInclude, int traceLevel = 3,
                  QObject *parent = nullptr);
    ~LuaScriptHost() override;

    void setDialogHandler(DialogHandler handler) { m_dialogHandler = std::move(handler); }
    // Each request carries the script's path and the asking run (0: the top level while
    // loading); answered at most once, late answers dropped. No handler: Unavailable.
    using ServiceReply = std::function<void(application::HostServiceReply)>;
    using ServiceHandler = std::function<void(const application::HostServiceRequest &, ServiceReply)>;
    void setServiceHandler(ServiceHandler handler) { m_serviceHandler = std::move(handler); }
    std::size_t pendingServices() const { return m_pendingServices.size(); }

    void load();
    // Starts a new helper that loads the script again.
    void restart();
    // False unless Ready. Without a snapshot the macro sees an empty Document.
    bool run(int macroIndex);
    // `validateFirst` runs the validation function first, on the same subtitles object.
    bool run(int macroIndex, const application::MacroSnapshot &snapshot, bool validateFirst = false);
    // Runs only the validation function: finished() gives Ok when it answers true
    // (lastResult() valid, with no lists), else NotValid; nothing is staged. False unless Ready.
    bool validate(int macroIndex, const application::MacroSnapshot &snapshot);
    // What the last successful run staged and returned (set before finished()).
    const std::optional<application::MacroResult> &lastResult() const { return m_lastResult; }
    // The run ends Cancelled even if the script returns normally. Past the grace period
    // forceStopOffered() fires; nothing is killed automatically.
    void cancel();
    bool forceStopAvailable() const { return m_forceStopOffered; }
    // Only after forceStopOffered(); the run ends ForceStopped and the script needs restart().
    bool forceStop();
    void terminate();
    void setGracePeriod(int ms) { m_graceMs = ms; }
    // Read when the script loads (aegisub.debug.out drops messages above it).
    void setTraceLevel(int level) { m_traceLevel = level; }

    State state() const { return m_state; }
    const application::ScriptInfo &info() const { return m_info; }
    QString lastError() const { return m_lastError; }
    QByteArray diagnostics() const;
    std::uint64_t session() const;
    qint64 processId() const;
    // Milliseconds from starting the helper to the script being loaded.
    double loadMs() const { return m_loadMs; }

signals:
    void loaded();
    void loadFailed(const QString &message);
    // aegisub.debug.out / aegisub.log text and its level (0 fatal … 5 trace; -1 none).
    void logged(const QString &text, int level);
    void progressChanged(double percent);
    // The macro itself begins, after its validation function.
    void macroStarted();
    void taskChanged(const QString &task);
    void titleChanged(const QString &title);
    void finished(hikari::backends::LuaScriptHost::RunOutcome outcome, const QString &message);
    // The run ended while its dialog was open: close the dialog.
    void dialogWithdrawn();
    // The run ended while host services were unanswered (a picker may be open).
    void servicesWithdrawn();
    void forceStopOffered();
    void unavailable(const QString &reason);

private:
    void startHelper();
    bool send(lua::Command command, int macroIndex, const application::MacroSnapshot &snapshot);
    void sendLoad();
    void onEvent(std::uint64_t request, std::expected<helper::Event, helper::HostError> event);
    void endRun(RunOutcome outcome, const QString &message);
    void handleHostService(std::uint64_t request, const helper::Event &event);

    QString m_helperPath, m_scriptPath, m_sharedInclude;
    int m_traceLevel;
    std::unique_ptr<helper::HelperHost> m_host;
    State m_state = State::Idle;
    application::ScriptInfo m_info;
    QString m_lastError;
    std::uint64_t m_nextRun = 1;
    std::uint64_t m_runRequest = 0;
    std::uint64_t m_loadRequest = 0;
    bool m_dialogOpen = false;
    bool m_cancelRequested = false;
    bool m_forceStopOffered = false;
    int m_graceMs = 3000;
    QTimer m_grace;
    double m_loadMs = 0;
    std::int64_t m_loadStartedNs = 0;
    DialogHandler m_dialogHandler;
    ServiceHandler m_serviceHandler;
    std::set<std::uint64_t> m_pendingServices;
    std::optional<application::MacroResult> m_lastResult;
};

} // namespace hikari::backends
