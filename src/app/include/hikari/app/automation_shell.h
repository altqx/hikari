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
#include "hikari/application/document_scripts.h"
#include "hikari/application/document_files.h"
#include "hikari/application/macro_transaction.h"
#include "hikari/application/workspace.h"
#include "hikari/backends/automation_manager.h"

#include "automation_dialog_controller.h"
#include "automation_manager_controller.h"
#include "automation_services_qt.h"
#include "line_editor_controller.h"
#include "settings_store.h"
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
    // The audio box whose audio aegisub.get_frequency_peaks reads, and whose
    // presence aegisub.get_audio_selection checks (null: none).
    void setAudioBox(std::function<const application::AudioBox *()> box);
    // The log window (legacy HikariLog) and the settings (AUTOMATION_SCRIPT_EDITOR).
    void setLog(std::function<void(const QString &)> log) { m_logLine = std::move(log); }
    void setSettings(ui::SettingsStore *settings) { m_settings = settings; }

    // How a run was asked for: legacy validates every run first
    // (LuaCommand::RunScript, OnRunScript, GLOBAL_AUTOMATION_LOAD_LAST_SCRIPT)
    // and says so when validation fails, except from the menu.
    enum class RunOrigin { Menu, Hotkey, LastScript };

    // AutomationServicePort: run() goes through the editing target's transaction.
    std::vector<application::ScriptStatus> scripts() const override { return m_manager.scripts(); }
    void load(const std::string &path) override { m_manager.load(path); }
    bool reload(const std::string &path) override { return m_manager.reload(path); }
    void unload(const std::string &path) override;
    bool run(const std::string &path, int ordinal) override { return run(path, ordinal, RunOrigin::Menu); }
    bool run(const std::string &path, int ordinal, RunOrigin origin);
    void cancel() override { m_manager.cancel(); }
    bool forceStop(const std::string &path) override { return m_manager.forceStop(path); }
    void setObserver(std::function<void()> changed) override { m_manager.setObserver(std::move(changed)); }

    // Legacy autoload: every .lua and .moon in Automation/automation/Autoload.
    Q_INVOKABLE void autoload();
    Q_INVOKABLE void reloadAutoload();
    // Legacy Automation::Add for Load script and an opened .lua/.moon: the
    // script joins the Document scripts and the editing target's Script Info
    // names it ("Changing the subtitle header"); nothing happens for one
    // listed already.
    Q_INVOKABLE void loadScript(const QUrl &file);
    // S4, legacy Automation::AddFromSubs: the scripts the editing target's
    // Script Info names ("Automation Scripts") load.
    Q_INVOKABLE void addFromDocument();
    // The Automation menu opens (legacy BuildMenu): with a Document open, its
    // scripts load and every script whose file changed reloads.
    Q_INVOKABLE void menuOpened();
    // GLOBAL_AUTOMATION_LOAD_LAST_SCRIPT ("Run the last loaded script"): the
    // first macro of the last Document script, validated first. Asked while
    // another macro runs, it starts when that one ends.
    Q_INVOKABLE void runLastLoadedScript();
    // Legacy Automation::OnEdit: the script in AUTOMATION_SCRIPT_EDITOR, or
    // chooseScriptEditor first (none set, or Shift held).
    Q_INVOKABLE void editScript(const QString &script);
    Q_INVOKABLE void editWith(const QUrl &editor, const QString &script);
    const application::DocumentScripts &documentScripts() const { return m_documentScripts; }
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
    // A legacy message box (HikariMessageBox: title, text, OK).
    void notice(const QString &title, const QString &text);
    // Legacy "Select a script editor" (a program to open `script` with).
    void chooseScriptEditor(const QString &script);

private:
    struct Run {
        application::DocumentId target;
        application::MacroSnapshot snapshot;
        std::string path, macro;
        RunOrigin origin = RunOrigin::Menu;
    };
    void finished(const QString &path, backends::LuaScriptHost::RunOutcome outcome, const QString &message);
    application::EditSession *session(application::DocumentId id) { return m_files.session(id); }
    void loadDocumentScript(const std::string &path);
    void continueLastScript();
    void startEditor(const QString &editor, const QString &script);

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
    application::DocumentScripts m_documentScripts;
    std::optional<std::string> m_pendingLast; // waiting for this script to load
    std::function<void(const QString &)> m_logLine;
    ui::SettingsStore *m_settings = nullptr;
    QString m_title, m_task, m_log, m_lastMessage;
    double m_progress = 0;
};

} // namespace hikari::app
