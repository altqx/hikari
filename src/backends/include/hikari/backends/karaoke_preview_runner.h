#pragma once

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
    // A replaced run still going after `ms` loses its helper, which loads the script again.
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
    Done m_running;
    bool m_busy = false;
};

}
