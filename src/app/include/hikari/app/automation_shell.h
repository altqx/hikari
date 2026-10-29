#pragma once

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
    // "applied", "refused", "failed", "invalid", "cancelled", "declined", "lost" or "stopped";
    // empty while one runs or before any.
    Q_PROPERTY(QString lastOutcome READ lastOutcome NOTIFY progressChanged)
    Q_PROPERTY(bool forceStopOffered READ forceStopOffered NOTIFY progressChanged)
    Q_PROPERTY(bool canRerun READ canRerun NOTIFY progressChanged)
    Q_PROPERTY(bool canRepeat READ canRepeat NOTIFY progressChanged)
    Q_PROPERTY(QString repeatText READ repeatText NOTIFY progressChanged)
    // A run that applied but logged template problems.
    Q_PROPERTY(bool keepLog READ keepLog NOTIFY progressChanged)
    // "notice", "running", then "applied", "problems", "cancelled" or "failed" until dismissCard()
    // or the next run; "" otherwise.
    Q_PROPERTY(QString cardState READ cardState NOTIFY progressChanged)
    Q_PROPERTY(QString cardText READ cardText NOTIFY progressChanged)
    // "" and -1 when no macro runs.
    Q_PROPERTY(QString runPath READ runPath NOTIFY progressChanged)
    Q_PROPERTY(int runOrdinal READ runOrdinal NOTIFY progressChanged)
    // The run in `runs` the card shows; 0: none yet.
    Q_PROPERTY(int runId READ runId NOTIFY progressChanged)
    Q_PROPERTY(hikari::ui::AutomationRuns *runs READ runs CONSTANT)
    Q_PROPERTY(bool cancelling READ cancelling NOTIFY progressChanged)
    Q_PROPERTY(bool progressKnown READ progressKnown NOTIFY progressChanged)
    // Template Lines with problems in the last run's karaoke report.
    Q_PROPERTY(int problems READ problems NOTIFY progressChanged)
public:
    struct Paths {
        QString helper;
        QString automationDir; // <application>/Automation, holding automation/{Autoload,Include}
        // Where user scripts live and scripts write; empty: automationDir.
        QString userAutomationDir;
    };
    AutomationShell(Paths paths, application::DocumentFiles &files, application::Workspace &workspace,
                    ui::LineEditorController &editor, ui::VideoController &video, ui::ShellController &shell,
                    application::IndexedSourcePort &source, QObject *parent = nullptr);
    ~AutomationShell() override;

    void setDocumentChanged(std::function<void()> changed) { m_documentChanged = std::move(changed); }
    void setBeforeRun(std::function<void()> before) { m_beforeRun = std::move(before); }
    // An opaque value that changes whenever the host state a preview reads changes.
    std::string hostState() const;
    // What aegisub.get_frequency_peaks and aegisub.get_audio_selection read; null: none.
    void setAudioBox(std::function<const application::AudioBox *()> box);
    // QuietError: an error line a message box already reports.
    enum class LogLine { Notice, Quiet, QuietError };
    void setLog(std::function<void(const QString &, LogLine)> log) { m_logLine = std::move(log); }
    void setSettings(ui::SettingsStore *settings)
    {
        m_settings = settings;
        m_dialogMemory.setSettings(settings);
    }

    // Every run validates first; a failed validation is reported unless the origin is Menu.
    enum class RunOrigin { Menu, Hotkey, LastScript };
    // aegisub.gettext's lookup.
    void setTranslation(std::function<std::string(const std::string &)> translate)
    {
        m_router.setTranslation(std::move(translate));
    }

    std::vector<application::ScriptStatus> scripts() const override { return m_manager.scripts(); }
    void load(const std::string &path) override { m_manager.load(path); }
    bool reload(const std::string &path) override { return m_manager.reload(path); }
    void unload(const std::string &path) override;
    bool run(const std::string &path, int ordinal) override { return run(path, ordinal, RunOrigin::Menu); }
    bool run(const std::string &path, int ordinal, RunOrigin origin);
    void cancel() override { m_manager.cancel(); }
    bool forceStop(const std::string &path) override { return m_manager.forceStop(path); }
    void setObserver(std::function<void()> changed) override { m_manager.setObserver(std::move(changed)); }

    // Every .lua and .moon in the autoload folder but those turned off (by file name).
    Q_INVOKABLE void autoload();
    Q_INVOKABLE void reloadAutoload();
    bool autoloadEnabled(const QString &script) const;
    // Loads or unloads the script at once.
    void setAutoloadEnabled(const QString &script, bool enabled);
    // Also names the script in the editing target's Script Info; a no-op for one listed already.
    Q_INVOKABLE void loadScript(const QUrl &file);
    Q_INVOKABLE void addFromDocument();
    // Unlisted paths the editing target's Script Info names, each Script Info value only once.
    // Never touches the file system.
    Q_INVOKABLE QStringList documentScriptsToLoad();
    // Loads the Document's scripts and reloads every script whose file changed.
    Q_INVOKABLE void menuOpened();
    // The first macro of the last Document script; while another macro runs, it waits for it.
    Q_INVOKABLE void runLastLoadedScript();
    // Emits chooseScriptEditor first when no editor is set or Shift is held.
    Q_INVOKABLE void editScript(const QString &script);
    Q_INVOKABLE void editWith(const QUrl &editor, const QString &script);
    const application::DocumentScripts &documentScripts() const { return m_documentScripts; }
    Q_INVOKABLE bool rerunLast();
    // Reruns the last recorded macro on the current selection, answering matching dialogs unseen.
    Q_INVOKABLE bool repeatLast();
    void forgetRepeat();
    Q_INVOKABLE void cancelRun();
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
    // Tests: macro run time before the notice becomes the card.
    void setCardDelay(int ms) { m_cardDelay = ms; }

    ui::AutomationManagerController *managerController() { return m_managerController.get(); }
    application::KaraokeTemplaterPort &templater();
    ui::AutomationDialogController *dialogs() { return &m_dialogs; }
    ui::AutomationFilePickerController *picker() { return &m_picker; }
    ui::ScriptResolutionPromptController *resolutionPrompt() { return &m_resolutionPrompt; }
    backends::AutomationManager &manager() { return m_manager; }
    QString includeDir() const;
    QString autoloadDir() const;
    // A user script replaces a shipped one of the same name.
    QStringList autoloadScripts() const;

signals:
    void progressChanged();
    // `message`: why, when the macro changed nothing.
    void runCompleted(bool ok, const QString &message);
    // severity: "info" (a toast) or "error" (a message box).
    void notice(const QString &severity, const QString &title, const QString &text, const QString &details);
    void chooseScriptEditor(const QString &script);
    // `report`: the karaoke report as JSON.
    void karaokeApplied(quint64 document, const QByteArray &report);

private:
    class Templater;
    struct AnsweredDialog {
        application::DialogRequest request;
        application::DialogResult answer;
    };
    struct Run {
        application::DocumentId target;
        application::MacroSnapshot snapshot;
        std::uint64_t generation = 0;
        std::string path, macro;
        int ordinal = 0;
        RunOrigin origin = RunOrigin::Menu;
        std::vector<application::MacroRef> validations;
        // MUST KILL: step == validations.size() means the macro itself
        std::size_t step = 0;
        std::string active;
        std::optional<application::Resolution> resolution;
        bool declined = false;
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
    std::set<std::string> m_readScriptInfo;
    std::optional<std::string> m_pendingLast;
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
    int m_cardDelay = 1000;
    int m_cardLeft = 1000;
    bool m_macroStarted = false;
    bool m_logged = false;
    bool m_cancelling = false;
    bool m_progressKnown = false;
    int m_problems = 0;
};

}
