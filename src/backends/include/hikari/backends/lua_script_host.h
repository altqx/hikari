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

#include <functional>
#include <memory>
#include <optional>

namespace hikari::backends {

class LuaScriptHost : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Loading, Ready, Running, LoadFailed, Unavailable };
    enum class RunOutcome { Ok, Failed, Cancelled, HelperLost };

    using DialogReply = std::function<void(application::DialogResult)>;
    using DialogHandler = std::function<void(const application::DialogRequest &, DialogReply)>;

    LuaScriptHost(QString helperPath, QString scriptPath, QString sharedInclude, int traceLevel = 3,
                  QObject *parent = nullptr);
    ~LuaScriptHost() override;

    void setDialogHandler(DialogHandler handler) { m_dialogHandler = std::move(handler); }

    void load();
    // A new helper generation that loads the script again.
    void restart();
    // False unless Ready (one macro at a time per script).
    bool run(int macroIndex);
    // Latches the run as cancelled: it ends Cancelled even if the script then
    // returns normally.
    void cancel();

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
    void unavailable(const QString &reason);

private:
    void startHelper();
    void sendLoad();
    void onEvent(std::uint64_t request, std::expected<helper::Event, helper::HostError> event);
    void endRun(RunOutcome outcome, const QString &message);

    QString m_helperPath, m_scriptPath, m_sharedInclude;
    int m_traceLevel;
    std::unique_ptr<helper::HelperHost> m_host;
    State m_state = State::Idle;
    application::ScriptInfo m_info;
    QString m_lastError;
    std::uint64_t m_nextRun = 1;
    std::uint64_t m_runRequest = 0; // the request of the running macro
    bool m_dialogOpen = false;
    bool m_cancelRequested = false;
    double m_loadMs = 0;
    std::int64_t m_loadStartedNs = 0;
    DialogHandler m_dialogHandler;
};

} // namespace hikari::backends
