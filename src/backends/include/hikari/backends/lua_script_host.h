#pragma once

// The application side of one loaded Lua script (N8; docs/qt/automation.md):
// one helper process and its persistent Lua state. Load registers the
// script's macros; Run executes one macro while the event loop keeps going.
// A dialog the script shows arrives through the dialog handler and is
// answered once; an answer after the run ended is dropped. If the helper
// dies, the script becomes Unavailable with its state lost, and the
// interrupted macro is never rerun: only an explicit restart (which runs the
// script's top-level code again) brings it back.

#include "hikari/application/automation.h"
#include "hikari/backends/helper_host.h"

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
    // ForceStopped: the user chose Force stop after the grace period.
    // NotValid: the macro's validation function answered false (or raised an
    // error, the message) and the macro did not run (S4).
    enum class RunOutcome { Ok, Failed, Cancelled, HelperLost, ForceStopped, NotValid };

    using DialogReply = std::function<void(application::DialogResult)>;
    using DialogHandler = std::function<void(const application::DialogRequest &, DialogReply)>;

    LuaScriptHost(QString helperPath, QString scriptPath, QString sharedInclude, int traceLevel = 3,
                  QObject *parent = nullptr);
    ~LuaScriptHost() override;

    void setDialogHandler(DialogHandler handler) { m_dialogHandler = std::move(handler); }
    // Host services (L3): each request carries this script's path and the run
    // that asked (0 for the top level while loading), and is answered at most
    // once; an answer after the run or load ended is dropped. Without a
    // handler every service answers Unavailable.
    using ServiceReply = std::function<void(application::HostServiceReply)>;
    using ServiceHandler = std::function<void(const application::HostServiceRequest &, ServiceReply)>;
    void setServiceHandler(ServiceHandler handler) { m_serviceHandler = std::move(handler); }
    std::size_t pendingServices() const { return m_pendingServices.size(); }

    void load();
    // A new helper generation that loads the script again.
    void restart();
    // False unless Ready (one macro at a time per script). The snapshot is the
    // subtitles object the macro sees; without one it sees an empty Document.
    bool run(int macroIndex);
    // With `validateFirst` the macro's validation function runs first, on
    // the same subtitles object (legacy LuaCommand::Validate then Run).
    bool run(int macroIndex, const application::MacroSnapshot &snapshot, bool validateFirst = false);
    // What the last successful run staged and returned (set before finished()).
    const std::optional<application::MacroResult> &lastResult() const { return m_lastResult; }
    // Latches the run as cancelled: it ends Cancelled even if the script then
    // returns normally. If it has not stopped after the grace period, Force
    // stop is offered (forceStopOffered); nothing is killed automatically.
    void cancel();
    bool forceStopAvailable() const { return m_forceStopOffered; }
    // Kills this script's helper (only after it was offered). The run ends
    // ForceStopped; the script needs an explicit restart and nothing reruns.
    bool forceStop();
    // Ends the helper now, for application quit (after its deadline).
    void terminate();
    void setGracePeriod(int ms) { m_graceMs = ms; }

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
    void logged(const QString &text);
    void progressChanged(double percent);
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
    std::uint64_t m_runRequest = 0;  // the request of the running macro
    std::uint64_t m_loadRequest = 0; // the Load request while the top level runs
    bool m_dialogOpen = false;
    bool m_cancelRequested = false;
    bool m_forceStopOffered = false;
    int m_graceMs = 3000;
    QTimer m_grace;
    double m_loadMs = 0;
    std::int64_t m_loadStartedNs = 0;
    DialogHandler m_dialogHandler;
    ServiceHandler m_serviceHandler;
    std::set<std::uint64_t> m_pendingServices; // call ids of the running macro
    std::optional<application::MacroResult> m_lastResult;
};

} // namespace hikari::backends
