#pragma once

// Automation composed into the shell (S1; docs/qt/automation.md). The
// automation manager runs one macro at a time on the editing target: the
// target is snapshotted (a pending draft committed first), read-only while the
// macro runs, and the macro's staged result applies as one undo step (L4).
// Script dialogs and pickers use the fixed QML windows; host services answer
// from the open video, the Line editor, the clipboard and the font metrics
// (L3). Progress, the log and Cancel / Force stop are exposed for the
// progress window.

#include "hikari/application/audio_box.h"
#include "hikari/application/automation.h"
#include "hikari/application/automation_services.h"
#include "hikari/application/document_files.h"
#include "hikari/application/macro_transaction.h"
#include "hikari/application/workspace.h"
#include "hikari/backends/automation_manager.h"

#include "automation_dialog_controller.h"
#include "automation_manager_controller.h"
#include "automation_services_qt.h"
#include "line_editor_controller.h"
#include "shell_controller.h"
#include "video_controller.h"

#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <optional>

namespace hikari::app {

class AutomationShell : public QObject, public application::AutomationServicePort {
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY progressChanged)
    Q_PROPERTY(QString runTitle READ runTitle NOTIFY progressChanged)
    Q_PROPERTY(QString task READ task NOTIFY progressChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString log READ log NOTIFY progressChanged)
    Q_PROPERTY(QString lastMessage READ lastMessage NOTIFY progressChanged)
    Q_PROPERTY(bool forceStopOffered READ forceStopOffered NOTIFY progressChanged)
    Q_PROPERTY(bool canRerun READ canRerun NOTIFY progressChanged)
public:
    struct Paths {
        QString helper;        // the Lua helper program
        QString automationDir; // <application>/Automation, holding automation/{Autoload,Include}
    };
    AutomationShell(Paths paths, application::DocumentFiles &files, application::Workspace &workspace,
                    ui::LineEditorController &editor, ui::VideoController &video, ui::ShellController &shell,
                    application::IndexedSourcePort &source, QObject *parent = nullptr);
    ~AutomationShell() override;

    // Called after a macro changed the Document (the views show it again).
    void setDocumentChanged(std::function<void()> changed) { m_documentChanged = std::move(changed); }
    // The audio box whose audio aegisub.get_frequency_peaks reads (null: none).
    void setAudioBox(std::function<const application::AudioBox *()> box);
    // O5: aegisub.gettext's lookup (application::HostService::Gettext).
    void setTranslation(std::function<std::string(const std::string &)> translate)
    {
        m_router.setTranslation(std::move(translate));
    }

    // AutomationServicePort: run() goes through the editing target's transaction.
    std::vector<application::ScriptStatus> scripts() const override { return m_manager.scripts(); }
    void load(const std::string &path) override { m_manager.load(path); }
    bool reload(const std::string &path) override { return m_manager.reload(path); }
    void unload(const std::string &path) override { m_manager.unload(path); }
    bool run(const std::string &path, int ordinal) override;
    void cancel() override { m_manager.cancel(); }
    bool forceStop(const std::string &path) override { return m_manager.forceStop(path); }
    void setObserver(std::function<void()> changed) override { m_manager.setObserver(std::move(changed)); }

    // Legacy autoload: every .lua and .moon in Automation/automation/Autoload.
    Q_INVOKABLE void autoload();
    Q_INVOKABLE void reloadAutoload();
    Q_INVOKABLE void loadScript(const QUrl &file);
    Q_INVOKABLE bool rerunLast();
    Q_INVOKABLE void cancelRun() { m_manager.cancel(); }
    Q_INVOKABLE bool forceStopRun();

    bool running() const { return m_run.has_value(); }
    QString runTitle() const { return m_title; }
    QString task() const { return m_task; }
    double progress() const { return m_progress; }
    QString log() const { return m_log; }
    QString lastMessage() const { return m_lastMessage; }
    bool forceStopOffered() const;
    bool canRerun() const { return m_last.has_value() && !m_run; }

    ui::AutomationManagerController *managerController() { return m_managerController.get(); }
    ui::AutomationDialogController *dialogs() { return &m_dialogs; }
    ui::AutomationFilePickerController *picker() { return &m_picker; }
    backends::AutomationManager &manager() { return m_manager; }
    QString includeDir() const;
    QString autoloadDir() const;

signals:
    void progressChanged();
    // A macro ended: its outcome and, when it changed nothing, why.
    void runCompleted(bool ok, const QString &message);

private:
    struct Run {
        application::DocumentId target;
        application::MacroSnapshot snapshot;
        std::string path, macro;
    };
    void finished(const QString &path, backends::LuaScriptHost::RunOutcome outcome, const QString &message);
    application::EditSession *session(application::DocumentId id) { return m_files.session(id); }

    Paths m_paths;
    application::DocumentFiles &m_files;
    application::Workspace &m_workspace;
    ui::LineEditorController &m_editor;
    ui::VideoController &m_video;
    ui::ShellController &m_shell;
    backends::AutomationManager m_manager;
    application::AutomationServiceRouter m_router;
    ui::AutomationDialogController m_dialogs;
    ui::AutomationFilePickerController m_picker;
    ui::QtClipboardPort m_clipboard;
    ui::QtTextMeasurePort m_measure;
    std::unique_ptr<application::MacroMediaPort> m_media;
    std::unique_ptr<application::EditorPort> m_editorPort;
    std::unique_ptr<ui::AutomationManagerController> m_managerController;
    std::function<void()> m_documentChanged;
    std::optional<Run> m_run;
    std::optional<std::pair<std::string, int>> m_last;
    QString m_title, m_task, m_log, m_lastMessage;
    double m_progress = 0;
};

} // namespace hikari::app
