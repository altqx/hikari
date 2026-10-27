#pragma once

// KE2: the template editor's preview runs. The karaoke templater script is
// loaded in a helper of its own, apart from the automation shell's: a
// preview never takes the one macro slot the Automation menu uses, never
// writes the log, and only reads from the host (isPreviewService). Each run
// is "Apply karaoke template to selected lines" (the script's second macro)
// on a reduced snapshot, read and never applied. The newest run wins: one
// in flight is cancelled, and if it has not stopped after the grace period
// its helper is stopped and loaded again.

#include "hikari/application/template_preview.h"
#include "hikari/backends/lua_script_host.h"

#include <QObject>
#include <QString>

#include <memory>
#include <optional>

namespace hikari::backends {

class KaraokePreviewRunner : public QObject {
    Q_OBJECT
public:
    using Done = application::KaraokeTemplaterPort::PreviewDone;

    KaraokePreviewRunner(QString helperPath, QString sharedInclude, QObject *parent = nullptr);
    ~KaraokePreviewRunner() override;

    // Services outside isPreviewService answer Unavailable without asking.
    void setServiceHandler(LuaScriptHost::ServiceHandler handler);
    void setGracePeriod(int ms);
    // Loads `script` unless it is the one loaded; empty unloads.
    void setScript(const QString &path);
    QString script() const { return m_script; }
    // The script is loaded and has the selected-Lines macro.
    bool ready() const;

    // Runs once the script is loaded; replaces a run asked for earlier.
    void run(const application::MacroSnapshot &snapshot, Done done);
    void cancel();

signals:
    void readyChanged();

private:
    struct Pending {
        application::MacroSnapshot snapshot;
        Done done;
    };
    void startNext();
    void finished(LuaScriptHost::RunOutcome outcome, const QString &message);

    QString m_helper, m_include, m_script;
    int m_graceMs = 3000;
    LuaScriptHost::ServiceHandler m_services;
    std::unique_ptr<LuaScriptHost> m_host;
    std::optional<Pending> m_pending;
    Done m_running; // the run in flight's, null once replaced or cancelled
    bool m_busy = false;
};

} // namespace hikari::backends
