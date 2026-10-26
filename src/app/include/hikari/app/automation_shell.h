#pragma once

// Automation composed into the shell (S1; docs/qt/automation.md). The
// automation manager runs one macro at a time on the editing target: the
// target is snapshotted (a pending draft committed first), read-only while the
// macro runs, and the macro's staged result applies as one undo step (L4).
// Script dialogs and pickers use the fixed QML windows; host services answer
// from the open video, the Line editor, the clipboard and the font metrics
// (L3). Progress, the log and Cancel / Force stop are exposed for the
// progress card (AD3), and each run's log is kept in `runs`.

#include "hikari/application/audio_box.h"
#include "hikari/application/automation.h"
#include "hikari/application/automation_services.h"
#include "hikari/application/dialog_memory.h"
#include "hikari/application/document_scripts.h"
#include "hikari/application/document_files.h"
#include "hikari/application/macro_validation.h"
#include "hikari/application/macro_transaction.h"
#include "hikari/application/template_preview.h"
#include "hikari/application/workspace.h"
#include "hikari/backends/automation_manager.h"

#include "automation_dialog_controller.h"
#include "automation_manager_controller.h"
#include "automation_runs.h"
#include "automation_services_qt.h"
#include "karaoke_report.h"
#include "line_editor_controller.h"
#include "script_dialog_memory.h"
#include "script_resolution_prompt.h"
#include "settings_store.h"
#include "shell_controller.h"
#include "video_controller.h"

#include <QObject>
#include <QString>
#include <QTimer>

#include <functional>
#include <set>
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
    // How the last run ended (AM1, the panel's run status): "applied",
    // "refused", "failed", "invalid" (validation answered no or raised),
    // "cancelled", "declined" (the script resolution question declined),
    // "lost", "stopped"; empty while one runs or before any.
    Q_PROPERTY(QString lastOutcome READ lastOutcome NOTIFY progressChanged)
    Q_PROPERTY(bool forceStopOffered READ forceStopOffered NOTIFY progressChanged)
    Q_PROPERTY(bool canRerun READ canRerun NOTIFY progressChanged)
    // AD2-repeat-last-run: Repeat last run is offered, and what it repeats
    // (its tooltip).
    Q_PROPERTY(bool canRepeat READ canRepeat NOTIFY progressChanged)
    Q_PROPERTY(QString repeatText READ repeatText NOTIFY progressChanged)
    // KE1-template-problems: the run ended well but its log names template
    // problems, so the progress window stays for it to be read.
    Q_PROPERTY(bool keepLog READ keepLog NOTIFY progressChanged)
    // AD3 (#312): what the progress card shows. "notice" as a macro starts;
    // "running" once the macro itself has run about a second (time in its
    // dialogs and pickers not counted) or written to its log; when it ends,
    // "applied" (it logged), "problems" (KE1), "cancelled" or "failed" until
    // dismissCard() or the next run; "" otherwise (a run that applied and
    // logged nothing, or whose script-resolution question was declined).
    Q_PROPERTY(QString cardState READ cardState NOTIFY progressChanged)
    // How the ended run went, in a sentence (its error's text when it failed).
    Q_PROPERTY(QString cardText READ cardText NOTIFY progressChanged)
    // The macro running (its script's path and ordinal; "" and -1 when none),
    // which the Automation panel marks busy.
    Q_PROPERTY(QString runPath READ runPath NOTIFY progressChanged)
    Q_PROPERTY(int runOrdinal READ runOrdinal NOTIFY progressChanged)
    // The run in `runs` the card shows (0: none yet).
    Q_PROPERTY(int runId READ runId NOTIFY progressChanged)
    Q_PROPERTY(hikari::ui::AutomationRuns *runs READ runs CONSTANT)
    // Cancel was asked for and the macro has not stopped yet.
    Q_PROPERTY(bool cancelling READ cancelling NOTIFY progressChanged)
    // The script has set a progress (else the bar is busy).
    Q_PROPERTY(bool progressKnown READ progressKnown NOTIFY progressChanged)
    // The last run's karaoke report: template Lines with problems (KE1).
    Q_PROPERTY(int problems READ problems NOTIFY progressChanged)
public:
    struct Paths {
        QString helper;        // the Lua helper program
        QString automationDir; // <application>/Automation, holding automation/{Autoload,Include}
        // An installed copy's own Automation folder, where scripts write and
        // the user's autoload and include scripts are (installers spec 1.7);
        // empty: automationDir, as a portable copy has it.
        QString userAutomationDir;
    };
    AutomationShell(Paths paths, application::DocumentFiles &files, application::Workspace &workspace,
                    ui::LineEditorController &editor, ui::VideoController &video, ui::ShellController &shell,
                    application::IndexedSourcePort &source, QObject *parent = nullptr);
    ~AutomationShell() override;

    // Called after a macro changed the Document (the views show it again).
    void setDocumentChanged(std::function<void()> changed) { m_documentChanged = std::move(changed); }
    // Called before a macro reads the target: a panel's draft is committed
    // first, as the Line editor's is (KE2's Template editor).
    void setBeforeRun(std::function<void()> before) { m_beforeRun = std::move(before); }
    // KE2: what a preview reads from the host (video, keyframes, audio
    // selection, frame shown, file), as one value that differs when it does.
    std::string hostState() const;
    // The audio box whose audio aegisub.get_frequency_peaks reads, and whose
    // presence aegisub.get_audio_selection checks (null: none).
    void setAudioBox(std::function<const application::AudioBox *()> box);
    // The Log (legacy HikariLog, a notice; silent: HikariLogSilent) and the
    // settings (AUTOMATION_SCRIPT_EDITOR).
    // A line for the Log: a notice (an error toast), a quiet line, or a
    // quiet error (a message box says it).
    enum class LogLine { Notice, Quiet, QuietError };
    void setLog(std::function<void(const QString &, LogLine)> log) { m_logLine = std::move(log); }
    void setSettings(ui::SettingsStore *settings)
    {
        m_settings = settings;
        m_dialogMemory.setSettings(settings);
    }

    // How a run was asked for: legacy validates every run first
    // (LuaCommand::RunScript, OnRunScript, GLOBAL_AUTOMATION_LOAD_LAST_SCRIPT)
    // and says so when validation fails, except from the menu.
    enum class RunOrigin { Menu, Hotkey, LastScript };
    // O5: aegisub.gettext's lookup (application::HostService::Gettext).
    void setTranslation(std::function<std::string(const std::string &)> translate)
    {
        m_router.setTranslation(std::move(translate));
    }

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
    // AM1: what addFromDocument would load now: the paths the editing
    // target's Script Info names that are not listed, until that value has
    // been read. The panel offers them; loading a script runs its code, so it
    // waits for the user. Nothing here touches the file system: a named path
    // (a UNC one on Windows, too) is first opened on Load.
    Q_INVOKABLE QStringList documentScriptsToLoad();
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
    // AD2-repeat-last-run (GLOBAL_AUTOMATION_REPEAT_LAST_RUN): the last
    // recorded run's macro again, on the Lines selected now. At each dialog
    // the script shows, the same dialog as at that point of the recorded run
    // (application::sameDialog) gets its recorded answer without being
    // shown; another is shown filled with the last answers and says why,
    // and the run goes on as a normal one. A run is recorded when it ends
    // (applied, refused or failed) with no dialog answered by its cancel
    // button or Esc; the record lasts for the session.
    Q_INVOKABLE bool repeatLast();
    // Settings > Clear last answers: the record goes with the answers.
    void forgetRepeat();
    Q_INVOKABLE void cancelRun();
    // The card's ×: the ended run's card goes (its log stays in `runs`).
    Q_INVOKABLE void dismissCard();
    Q_INVOKABLE bool forceStopRun();

    bool running() const { return m_run.has_value(); }
    QString runTitle() const { return m_title; }
    QString task() const { return m_task; }
    double progress() const { return m_progress; }
    QString log() const { return m_log; }
    QString lastMessage() const { return m_lastMessage; }
    QString lastOutcome() const { return m_lastOutcome; }
    bool forceStopOffered() const;
    bool canRerun() const { return m_last.has_value() && !m_run; }
    bool canRepeat() const { return m_repeat.has_value() && !m_run; }
    QString repeatText() const;
    bool keepLog() const { return m_keepLog; }
    QString cardState() const { return m_cardState; }
    QString cardText() const { return m_cardText; }
    QString runPath() const { return m_run ? QString::fromStdString(m_run->path) : QString(); }
    int runOrdinal() const { return m_run ? m_run->ordinal : -1; }
    int runId() const { return m_runId; }
    ui::AutomationRuns *runs() { return &m_runs; }
    bool cancelling() const { return m_cancelling; }
    bool progressKnown() const { return m_progressKnown; }
    int problems() const { return m_problems; }
    // Tests: how long the macro runs before the notice becomes the card.
    void setCardDelay(int ms) { m_cardDelay = ms; }

    ui::AutomationManagerController *managerController() { return m_managerController.get(); }
    // KE2: the karaoke templater for the Template editor. Its previews run
    // in a helper of their own on the loaded kara-templater.lua; its Apply
    // is this shell's run of the stock macro.
    application::KaraokeTemplaterPort &templater();
    ui::AutomationDialogController *dialogs() { return &m_dialogs; }
    ui::AutomationFilePickerController *picker() { return &m_picker; }
    // Y4-playres-ask: the question a macro reading a missing script resolution asks.
    ui::ScriptResolutionPromptController *resolutionPrompt() { return &m_resolutionPrompt; }
    backends::AutomationManager &manager() { return m_manager; }
    QString includeDir() const;
    QString autoloadDir() const;
    // The autoload scripts, a user's in place of a shipped one of its name.
    QStringList autoloadScripts() const;

signals:
    void progressChanged();
    // A macro ended: its outcome and, when it changed nothing, why.
    void runCompleted(bool ok, const QString &message);
    // A legacy message box (HikariMessageBox) or Log pop-up (HikariLog),
    // NT1: severity "info" (a toast) or "error" (a message box: the command
    // stopped), titled with the command, the technical text in `details`.
    void notice(const QString &severity, const QString &title, const QString &text, const QString &details);
    // Legacy "Select a script editor" (a program to open `script` with).
    void chooseScriptEditor(const QString &script);
    // A karaoke templater run was applied to Document `document` (its
    // report, KE1's JSON).
    void karaokeApplied(quint64 document, const QByteArray &report);

private:
    class Templater;
    // AD2: a dialog of a run and its answer.
    struct AnsweredDialog {
        application::DialogRequest request;
        application::DialogResult answer;
    };
    struct Run {
        application::DocumentId target;
        application::MacroSnapshot snapshot;
        // The target's load the run started on: a reload meanwhile is new
        // content the result was not made from (set by run()).
        std::uint64_t generation = 0;
        std::string path, macro;
        int ordinal = 0;
        RunOrigin origin = RunOrigin::Menu;
        // S4-run-validation: the dependencies' validations made first, the
        // step under way (validations.size(): the macro itself, validated
        // first) and the script whose helper works on it.
        std::vector<application::MacroRef> validations;
        std::size_t step = 0;
        std::string active;
        // Y4-playres-ask: the resolution the user set (or the video gave)
        // while the macro ran, applied as its own step before the macro's;
        // declined: the user cancelled the question, so the run ends quietly.
        std::optional<application::Resolution> resolution;
        bool declined = false;
        // AD2: the dialogs answered so far, and whether one was cancelled;
        // for Repeat last run, the recorded run's dialogs and whether they
        // are still being replayed.
        std::vector<AnsweredDialog> dialogs;
        bool cancelledAtDialog = false;
        std::optional<std::vector<AnsweredDialog>> repeat;
        bool replaying = false;
    };
    struct RepeatRecord {
        std::string path, macro;
        int ordinal = 0;
        std::vector<AnsweredDialog> dialogs;
    };
    void presentDialog(const application::DialogRequest &request, backends::LuaScriptHost::DialogReply reply);
    bool startStep(Run &run);
    void resolveScriptResolution(const application::ScriptResolutionRequest &request,
                                 std::function<void(std::optional<application::Resolution>)> reply);
    void finished(const QString &path, backends::LuaScriptHost::RunOutcome outcome, const QString &message);
    void karaokeReport(const ui::KaraokeReport &report);
    application::EditSession *session(application::DocumentId id) { return m_files.session(id); }
    void loadDocumentScript(const std::string &path);
    void continueLastScript();
    void startEditor(const QString &editor, const QString &script);
    // The card's second runs only while the macro itself runs: not before
    // it starts (validations), nor while its dialog, picker or the script
    // resolution question waits for the user, which starts it again.
    bool waitingForUser() const;
    void updateCardTimer();
    void showCard();
    void finishCard(const QString &ended, const QString &text, const QString &validationError);

    Paths m_paths;
    application::DocumentFiles &m_files;
    application::Workspace &m_workspace;
    ui::LineEditorController &m_editor;
    ui::VideoController &m_video;
    ui::ShellController &m_shell;
    backends::AutomationManager m_manager;
    application::AutomationServiceRouter m_router;
    ui::ScriptDialogMemory m_dialogMemory;
    ui::AutomationDialogController m_dialogs;
    ui::AutomationFilePickerController m_picker;
    ui::ScriptResolutionPromptController m_resolutionPrompt;
    std::unique_ptr<application::ScriptResolutionPort> m_resolutionPort;
    ui::QtClipboardPort m_clipboard;
    ui::QtTextMeasurePort m_measure;
    std::unique_ptr<application::MacroMediaPort> m_media;
    std::unique_ptr<application::EditorPort> m_editorPort;
    std::unique_ptr<ui::AutomationManagerController> m_managerController;
    std::function<void()> m_documentChanged;
    std::function<void()> m_beforeRun;
    std::optional<Run> m_run;
    std::optional<std::pair<std::string, int>> m_last;
    std::optional<RepeatRecord> m_repeat;
    application::DocumentScripts m_documentScripts;
    std::set<std::string> m_readScriptInfo; // the values addFromDocument read
    std::optional<std::string> m_pendingLast; // waiting for this script to load
    std::function<void(const QString &, LogLine)> m_logLine;
    ui::SettingsStore *m_settings = nullptr;
    QString m_title, m_task, m_log, m_lastMessage, m_lastOutcome;
    double m_progress = 0;
    bool m_keepLog = false;
    std::uint64_t m_statusSerial = 0;
    std::unique_ptr<Templater> m_templater;
    ui::AutomationRuns m_runs;
    int m_runId = 0;
    QString m_cardState, m_cardText;
    QTimer m_cardTimer;
    // The macro's own time the card waits for, and what is left of it (time
    // in a dialog, picker or prompt does not count).
    int m_cardDelay = 1000;
    int m_cardLeft = 1000;
    bool m_macroStarted = false; // the run's own macro (not a validation) runs
    bool m_logged = false;       // the run wrote to its log
    bool m_cancelling = false;
    bool m_progressKnown = false;
    int m_problems = 0;
};

} // namespace hikari::app
