#pragma once

// The composed application services (composition root). main() and the UI
// workflow tests build the same graph: native file ports, the write
// coordinator, Documents, the workspace and the shell/editor presenters.

#include "hikari/app/automation_hotkeys_controller.h"
#include "hikari/app/update_checker.h"
#include "hikari/app/style_manager_controller.h"
#include "hikari/app/automation_shell.h"
#include "hikari/application/document_files.h"
#include "hikari/application/grid_commands.h"
#include "hikari/application/grid_selection.h"
#include "hikari/application/options_dialog.h"
#include "hikari/application/recent_files.h"
#include "hikari/application/recovery_store.h"
#include "hikari/application/workspace.h"
#include "hikari/backends/ffms_indexed_source.h"
#include "hikari/backends/libass_renderer.h"
#include "hikari/backends/qt_general_player.h"
#include "hikari/backends/platform_files.h"
#include "line_editor_controller.h"
#include "log_controller.h"
#include "colour_picker_controller.h"
#include "workspace_layout.h"
#include "shift_times_controller.h"
#include "tag_buttons_controller.h"
#include "grid_filter_controller.h"
#include "settings_store.h"
#include "shell_controller.h"
#include "video_controller.h"

#include <QDate>
#include <QDateTime>
#include <QLockFile>
#include <QTimer>
#include <QObject>
#include <QUrl>
#include <QVariantMap>

#include <map>
#include <memory>
#include <set>

namespace hikari::app {

class Application : public QObject {
    Q_OBJECT
signals:
    void closeFinished(bool done, const QString &problem);
    // O1: a changed program font destroyed the Select dialog (DestroyDialogs).
    void selectLinesDestroyed();
    void quitApprovedChanged();
    void saveWithVideoNameChanged();
    void askForBadResolutionChanged();
    // Y4: the open video's size differs from the editing target's PlayRes
    // (legacy SetSubsResolution / SetVideoResolution): {subsWidth,
    // subsHeight, videoWidth, videoHeight}.
    void resolutionMismatch(const QVariantMap &sizes);
    void recentChanged();
    // G56: a command was refused because it would break the group described
    // by `description` (0 when the break makes a new malformed group).
    void groupBreakRefused(qulonglong description, const QString &title);

public:
    struct Options {
        // The media helper program; empty: next to the application, else the build tree's.
        QString mediaHelper;
        // The Lua helper and the Automation directory (holding automation/{Autoload,Include});
        // empty: next to the application, else the build tree's.
        QString luaHelper;
        QString automationDir;
        // Load the Autoload scripts at start (the application does; tests choose).
        bool autoload = false;
        // INI file holding the settings registry; empty: in memory only (tests).
        QString settingsFile;
        // P3: where recovery bundles live; empty: no autosave (tests).
        QString recoveryDir;
        // V1: whether video playback may open an audio device.
        bool playbackAudio = true;
        // P8: the release list the update check reads (tests: a file).
        QUrl updateFeed = UpdateChecker::defaultFeed();
        // Y2: where the style catalogs live; empty: "Catalog" beside the settings
        // file, or a temporary directory without one (tests).
        QString catalogDir;
    };
    explicit Application(QObject *parent = nullptr);
    explicit Application(Options options, QObject *parent = nullptr);
    ~Application() override;

    // Opens a file as a new Document; the first becomes the editing target.
    Q_INVOKABLE bool openFile(const QString &path);
    // Opens a file as the protected comparison reference.
    Q_INVOKABLE bool openReference(const QString &path);
    // Closes the editing target at once, without review (callers review first).
    Q_INVOKABLE bool closeEditingTarget();

    // Close review (P1; accepted L58-write-close). `then` is "close" (the
    // editing target), "new" (replace it with an Untitled Document, legacy
    // "Remove subtitles from the editor") or "quit" (every Document). Returns
    // one row per Document with unsaved work: {id, title, untitled}. With
    // none, call finishClose() to carry `then` out at once.
    Q_INVOKABLE QVariantList reviewClose(const QString &then);
    // One choice per row: {id, save: bool, path: Save As destination for an
    // Untitled Document}. Save commits the draft and writes; Discard drops the
    // draft. Nothing is closed until every save is acknowledged Written, and a
    // Document edited after its choice stays open. closeFinished reports the result.
    Q_INVOKABLE void resolveClose(const QVariantList &choices);
    Q_INVOKABLE void finishClose();
    Q_INVOKABLE void cancelClose();
    // Opening subtitles into the editing target (P2; legacy OpenFile): the
    // file is staged first, so a failed open changes nothing
    // (L58-staged-replacement); then the target's unsaved work is reviewed as
    // for Close. Returns {ok, problem, rows}: rows as reviewClose; with ok and
    // no rows, call finishClose() to publish the new content.
    Q_INVOKABLE QVariantMap reviewOpen(const QString &path);
    Q_INVOKABLE QVariantMap reviewOpenUrl(const QUrl &url) { return reviewOpen(url.toLocalFile()); }
    // Dropped files by the legacy rules (OpenFile for one, OpenFiles for
    // several, sorted): scripts load, the video opens; returns the subtitles
    // to open with reviewOpen, or "".
    Q_INVOKABLE QString openDropped(const QList<QUrl> &urls);
    // GLOBAL_RECENT_SUBS: {path, label} rows, missing local files pruned first.
    Q_INVOKABLE QVariantList recentSubtitles();
    std::vector<std::string> recentEntries() const { return m_recent.entries(); }
    // The legacy check when the window activates (TabPanel::ReloadSubsIfModified):
    // "modified" when the editing target's file is newer than when it was
    // last read or written (ask, then reloadTarget()); a removed file makes
    // the Document unsaved once and reports "removed".
    Q_INVOKABLE QString externalChange();
    // Reloads the editing target from its file with fresh history; a failed
    // or stale reload keeps the Document as it is (L58-staged-replacement).
    Q_INVOKABLE bool reloadTarget();

    // Saves the editing target to a new destination (GLOBAL_SAVE_SUBS_AS).
    Q_INVOKABLE bool saveAs(const QString &path);
    Q_INVOKABLE bool saveAsUrl(const QUrl &url) { return saveAs(url.toLocalFile()); }
    Q_INVOKABLE QString localPath(const QUrl &url) const { return url.toLocalFile(); }
    // The editing target has no file yet (its first save needs Save As).
    Q_INVOKABLE bool targetUntitled() const;
    // P7: legacy HikariSubFrame::Save. saveRoute is "dialog" when Save asks
    // for a file (none yet, or "Save subtitles using the video name" with a
    // name other than the video's), "readonly" when the file is read-only
    // (a warning, then the dialog), or "" to save in place.
    Q_INVOKABLE QString saveRoute() const;
    // Where the "Save subtitle file" dialog starts: {folder, file, filter, extension}.
    Q_INVOKABLE QVariantMap saveDialogValues() const;
    // The dialog's file: the format's extension is added unless the path
    // already ends with it. "readonly" asks again; "" when the save started.
    Q_INVOKABLE QString saveChosen(const QUrl &file);
    // GLOBAL_SAVE_ALL_SUBS: every modified Document that has a file. True
    // when the editing target is modified without one (it needs the dialog).
    Q_INVOKABLE bool saveAll();
    // GLOBAL_SAVE_TRANSLATION: translator mode off (one step), then the dialog.
    Q_INVOKABLE bool turnOffTranslationMode();
    // GLOBAL_SAVE_WITH_VIDEO_NAME (legacy SUBS_AUTONAMING, subtitles.saveWithVideoName).
    Q_PROPERTY(bool saveWithVideoName READ saveWithVideoName WRITE setSaveWithVideoName NOTIFY saveWithVideoNameChanged)
    bool saveWithVideoName() const { return m_settings->boolean("subtitles.saveWithVideoName"); }
    void setSaveWithVideoName(bool on);
    // GLOBAL_ANSI ("Report an issue"): the issue tracker in the browser.
    Q_INVOKABLE void reportIssue();
    // Quit was reviewed and may proceed (the window then closes for good).
    Q_PROPERTY(bool quitApproved READ quitApproved NOTIFY quitApprovedChanged)
    bool quitApproved() const { return m_quitApproved; }

    // Grid selection gestures (G1). The active Line moves through the Line
    // editor (a pending draft commits by policy); a refusal leaves everything as it was.
    Q_INVOKABLE void selectLine(qulonglong id);
    Q_INVOKABLE void extendSelection(int rows);
    Q_INVOKABLE void clickLine(qulonglong id, int modifiers);
    Q_INVOKABLE void dragSelection(qulonglong id);
    Q_INVOKABLE void selectAllLines();
    // The Grid's active Line was hidden: only the active Line moves.
    Q_INVOKABLE void moveActiveLine(qulonglong id);

    // Grid structural commands (G3): one undo step each, on the editing target.
    // `timing`: "" plain, "video" (the shown frame's time, 4 s) or "frame"
    // (copies of the selection timed to the shown frame).
    Q_INVOKABLE bool insertLine(bool before, const QString &timing = {});
    Q_INVOKABLE bool duplicateLines();
    Q_INVOKABLE bool deleteLines();
    // G5: "join", "previous", "next", "first", "last".
    Q_INVOKABLE bool joinLines(const QString &kind);
    Q_INVOKABLE bool swapLines();
    Q_INVOKABLE bool makeContinuous(bool withPrevious);
    // G11: GRID_SET_NEW_FPS from the FPS window's texts (digits and '.'), and
    // GRID_SET_FPS_FROM_VIDEO with the shown video frame's time.
    Q_INVOKABLE bool setNewFps(const QString &oldFps, const QString &newFps);
    Q_INVOKABLE bool setFpsFromVideo();
    // G8: GRID_FILTER with the Filtering preferences, GRID_HIDE_SELECTED,
    // Turn off filtering, and a +/- mark (the block after a Document row).
    Q_INVOKABLE bool filterLines();
    Q_INVOKABLE bool hideSelectedLines();
    Q_INVOKABLE bool turnOffFiltering();
    Q_INVOKABLE bool toggleHiddenBlock(int documentRow);
    // The editing target's Style names, for "Hide lines with styles".
    Q_INVOKABLE QStringList styleNames() const;
    // P3: autosave recovery. Bundles left by a session that ended without
    // closing cleanly: {key, title, original, written, generations:[{generation, written}]}.
    Q_INVOKABLE QVariantList recoveryBundles() const;
    // Opens a generation as a new unsaved copy with its draft still pending
    // (L58-recovery-copy); the bundle stays until it is dismissed.
    Q_INVOKABLE bool recoverBundle(const QString &key, qulonglong generation);
    Q_INVOKABLE void dismissBundle(const QString &key);
    // P4 (GLOBAL_DELETE_TEMPORARY_FILES): removes the recovery work of ended
    // sessions whose newest autosave is older than `date` (all when invalid);
    // a running session's work is never touched. Returns how many were removed.
    Q_INVOKABLE int removeAutosavesOlderThan(const QDate &date);
    // Writes the editing target's recovery now (the autosave timer's work; tests).
    bool autosaveNow();
    // G4: "Split lines": "videoTime" (GRID_SPLIT_BY_VIDEO_TIME) or "frames"
    // (GRID_SPLIT_BY_FRAME), against the open video's timebase.
    Q_INVOKABLE bool splitLines(const QString &kind);
    // E3: GRID_PASTE_TRANSLATION from a file (ASS Documents with a file), and
    // the "Dialogue shifting window" moves (TLDialog, legacy MoveTextTL modes 0-5).
    Q_INVOKABLE bool canPasteTranslation() const;
    Q_INVOKABLE QUrl targetFolder() const;
    Q_INVOKABLE bool pasteTranslationFile(const QUrl &file);
    Q_INVOKABLE bool canShiftTranslation() const;
    // Y3: GLOBAL_OPEN_ASS_PROPERTIES. The values the dialog opens with (ASS
    // Documents only; empty otherwise), plus the video size for "From video"
    // and the linked-resolutions option; OK applies the edited fields.
    // F5: GLOBAL_SHIFT_TIMES with the panel's settings and the video's
    // timebase and shown frame; an empty result means shifted, otherwise the
    // legacy message. Frames need an exact (indexed) timebase.
    Q_INVOKABLE QString shiftTimes();
    Q_INVOKABLE bool exactTimebase() const;
    // F6: GLOBAL_OPEN_KEYFRAMES. Empty result: loaded (or kept until a video
    // opens, as legacy does); otherwise the legacy message.
    Q_INVOKABLE QString openKeyframes(const QUrl &file);
    // Y5: GLOBAL_CONVERT_TO_ASS/SRT/MDVD/MPL2/TMP with the CONVERT_* options
    // {fps, fpsFromVideo, style, newEndTimes, timePerCharacter, prefix,
    // resolutionWidth, resolutionHeight}, kept in the settings registry (convert.*).
    Q_INVOKABLE QVariantMap conversionOptions() const;
    Q_INVOKABLE void setConversionOptions(const QVariantMap &options);
    // The formats the editing target can be converted to ("ass", "srt",
    // "mdvd", "mpl2", "tmp"); none in translation mode.
    Q_INVOKABLE QStringList conversionTargets() const;
    // Y1: the active Line's Style, which the Style manager selects when it opens.
    Q_INVOKABLE QString activeLineStyle() const;
    // C02-loss-preview: converts a copy and lists what it removes or
    // synthesizes ({ok, problem, losses}); acceptConversion applies that
    // plan as one "Subtitles conversion" step unless the Document or the
    // options changed since.
    Q_INVOKABLE QVariantMap previewConversion(const QString &target);
    Q_INVOKABLE bool acceptConversion();
    // Y4: GLOBAL_OPEN_SUBS_RESAMPLE ("Change resolution"): {subsWidth,
    // subsHeight, videoWidth, videoHeight}, the video's being the subtitles'
    // without a video; empty for a Document that is not ASS.
    Q_INVOKABLE QVariantMap resampleValues() const;
    // OK: nothing when the sizes match, else "Changing subtitles resolution".
    Q_INVOKABLE bool resample(int subsWidth, int subsHeight, int width, int height, bool stretch);
    // The SubsMismatchResolutionDialog's Change: 0 only the resolution,
    // 1 resample (no stretch), 2 resample (stretch).
    Q_INVOKABLE bool matchVideoResolution(int option);
    // "Disable warning" (legacy DONT_ASK_FOR_BAD_RESOLUTION, video.dontAskForBadResolution).
    Q_PROPERTY(bool askForBadResolution READ askForBadResolution WRITE setAskForBadResolution NOTIFY askForBadResolutionChanged)
    bool askForBadResolution() const { return !m_settings->boolean("video.dontAskForBadResolution"); }
    void setAskForBadResolution(bool on);
    // F2: GLOBAL_OPEN_SELECT_LINES. The dialog's settings {find, with,
    // matchCase, regex, field, dialogues, comments, mode, action} and its
    // recent searches (legacy SELECT_LINES_OPTIONS and _RECENT_SELECTIONS,
    // kept in the settings registry); selectLines runs on the editing target or on
    // every open Document and returns the legacy message.
    Q_INVOKABLE QVariantMap selectLinesSettings() const;
    // The dialog opens (legacy creates SelectLines once and keeps it until
    // the app closes or DestroyDialogs): selectLinesSettings, and from now
    // on the dialog's options and recent searches survive "Set default".
    Q_INVOKABLE QVariantMap openSelectLines();
    // The dialog closes: its options are written (not the recent searches,
    // which legacy writes only when a selection runs).
    Q_INVOKABLE void saveSelectLinesSettings(const QVariantMap &settings);
    Q_INVOKABLE QString selectLines(const QVariantMap &settings, bool allTabs);
    // The "+" button: the chosen styles as the legacy anchored pattern.
    Q_INVOKABLE QString selectStylesPattern(const QStringList &styles) const;
    Q_INVOKABLE QVariantMap scriptProperties();
    Q_INVOKABLE bool applyScriptProperties(const QVariantMap &values, const QVariantMap &edits, bool linkResolutions);
    Q_INVOKABLE bool shiftTranslation(int mode);
    // G9: Line groups (legacy trees). The id is the description Line's.
    Q_INVOKABLE bool makeGroups();
    Q_INVOKABLE bool toggleGroup(qulonglong description);
    Q_INVOKABLE bool renameGroup(qulonglong description, const QString &text);
    Q_INVOKABLE bool removeGroup(qulonglong description);
    Q_INVOKABLE bool selectGroup(qulonglong description);
    Q_INVOKABLE bool addLinesToGroup(qulonglong description);
    Q_INVOKABLE bool copyGroup(qulonglong description);
    Q_INVOKABLE QString groupTitle(qulonglong description) const;
    // G6: Edit > Sort all lines / Sort selected lines by "start", "end",
    // "style", "actor", "effect" or "layer"; text keys use the locale's collation.
    Q_INVOKABLE bool sortLines(const QString &key, bool selectedOnly);
    // G2: the Grid clipboard (GRID_COPY Ctrl+C, GRID_CUT Ctrl+X, GRID_PASTE
    // Ctrl+V, GRID_COPY_COLUMNS, GRID_PASTE_COLUMNS) through the system clipboard.
    Q_INVOKABLE bool copyLines();
    Q_INVOKABLE bool cutLines();
    Q_INVOKABLE bool pasteLines();
    // The column choices for the editing target's format: {label, bit,
    // checked}, checked as last chosen (COPY_COLLUMS_SELECTIONS /
    // PASTE_COLUMNS_SELECTION); copy and paste offer different lists.
    Q_INVOKABLE QVariantList columnChoices(bool paste);
    // Copies or pastes the chosen columns (bits OR'ed) and remembers the choice.
    Q_INVOKABLE bool copyColumns(int columns);
    Q_INVOKABLE bool pasteColumns(int columns);
    // Legacy GRID_CHANGE_ACTIVE_ON_SELECTION (grid.changeActiveOnSelection, default true).
    void setChangeActiveOnSelection(bool on) { m_settings->set("grid.changeActiveOnSelection", on); }

    // O1: GLOBAL_SETTINGS, the Options dialog (legacy OptionsDialog at
    // 20d647c4) over the settings registry. The dialog reads each page's
    // values as legacy shows them and writes the changed ones on OK/Apply.
    Q_PROPERTY(hikari::ui::SettingsStore *settings READ settingsStore CONSTANT)
    ui::SettingsStore *settingsStore() const { return m_settings.get(); }
    // Opening the dialog (application::openOptionsDialog): {values: the
    // controls' state by setting id, languages, dictionaries, catalogs,
    // styles: the choices' entries, warnings: the legacy "does not exist"
    // messages}. Opening fixes an FFMS2 seeking value outside 0-3 to 2 at
    // once and loads the conversion catalog, as legacy does.
    Q_INVOKABLE QVariantMap openSettingsDialog();
    // OK/Apply (legacy SetOptions): each bound control whose value differs
    // from the stored one is written; nothing else.
    Q_INVOKABLE void applySettings(const QVariantMap &values);
    // "Set default" (legacy ResetDefault): every setting at once, even if the
    // dialog is then cancelled; returns the controls' state as legacy
    // refreshes them (its defects included).
    Q_INVOKABLE QVariantMap resetSettings(const QVariantMap &values);
    // OnChangeCatalog: loads the catalog and returns {values, styles}.
    Q_INVOKABLE QVariantMap chooseSettingsCatalog(const QVariantMap &values, int index);
    // wxDirDialog::GetPath: the chosen folder with native separators.
    Q_INVOKABLE QString settingsFolderPath(const QUrl &url) const;

    ui::ShellController &shell() { return *m_shell; }
    ui::LineEditorController &editor() { return *m_editor; }
    ui::VideoController &video() { return *m_video; }
    AutomationShell &automation() { return *m_automation; }
    AutomationHotkeysController &automationHotkeys() { return *m_automationHotkeys; }
    UpdateChecker &updates() { return *m_updates; }
    StyleManagerController &styleManager() { return *m_styleManager; }
    ui::LogController &log() { return *m_log; }
    ui::TagButtonsController &tagButtons() { return *m_tagButtons; }
    ui::ColourPickerController &colourPicker() { return *m_colourPicker; }
    // E1/O1: the Line editor's colour picker opens for the editing target
    // (legacy DialogColorPicker::Get(EditBox of that tab)).
    Q_INVOKABLE void colourPickerOpened();
    ui::WorkspaceLayoutController &workspaceLayout() { return *m_workspaceLayout; }
    ui::ShiftTimesController &shiftTimesSettings() { return *m_shiftTimes; }
    ui::GridFilterController &gridFilter() { return *m_gridFilter; }
    application::DocumentFiles &files() { return *m_files; }
    application::Workspace &workspace() { return m_workspace; }
    // Properties for Main.qml.
    QVariantMap qmlProperties();
    // Tests: waits for running writes and delivers their results.
    void waitForWrites();

private:
    std::optional<application::DocumentId> open(const QString &path, bool asReference = false);
    std::optional<application::DocumentId> publish(application::StagedOpen staged, const QString &path, bool asReference);
    void rememberRecent(const std::string &path);
    void recordFileTime(application::DocumentId document);
    application::EditSession *targetSession() const;
    application::LineVisible shownLines() const;
    // The Lines "some actions" walk: the shown ones of `session`, or every
    // Line with "Ignore filtering in some actions" (legacy ignoreFiltered).
    application::LineVisible actionLines(const application::EditSession &session) const;
    void rememberColumns(bool paste, int columns);
    void refreshViews();
    // The Video panel follows the editing target: its association when the
    // target changes, its committed content and its active Line.
    void refreshVideo();
    void writeFinished(const application::WriteResult &result);
    void newDocument();
    application::GridSelection gridSelection() const;
    bool applySelection(application::Selection next);

    std::unique_ptr<application::FileReadPort> m_reader;
    std::unique_ptr<backends::PlatformFilePort> m_port;
    std::unique_ptr<application::WriteCoordinator> m_writes;
    std::unique_ptr<application::DocumentFiles> m_files;
    application::Workspace m_workspace;
    std::unique_ptr<ui::ShellController> m_shell;
    std::unique_ptr<ui::LineEditorController> m_editor;
    std::optional<application::DocumentId> m_editorDocument;
    std::unique_ptr<backends::FfmsIndexedSource> m_mediaSource;
    backends::LibassRenderer m_renderer;
    std::unique_ptr<ui::VideoController> m_video;
    std::unique_ptr<backends::QtGeneralPlayer> m_generalPlayer;
    // O1: declared before everything that keeps a reference to it.
    std::unique_ptr<ui::SettingsStore> m_settings;
    void settingChanged(const QString &id);
    // R6-dictionary-location: the settings folder's Dictionary (user
    // dictionaries), then the program folder's (bundled ones).
    QStringList m_dictionaryDirs;
    // The process locale legacy CmpNoCase runs under: a translation language
    // initialized at startup folds every letter, "C" only ASCII letters.
    bool m_foldsEveryLetter = false;
    bool m_resettingSettings = false; // "Set default" is resetting the registry
    bool m_selectLinesOpened = false; // legacy SelectLines exists
    application::OptionsLists m_optionsLists; // what the open Options dialog lists
    std::unique_ptr<AutomationShell> m_automation;
    std::unique_ptr<AutomationHotkeysController> m_automationHotkeys;
    std::unique_ptr<UpdateChecker> m_updates;
    std::unique_ptr<StyleManagerController> m_styleManager;
    std::unique_ptr<ui::LogController> m_log;
    std::unique_ptr<ui::TagButtonsController> m_tagButtons;
    std::unique_ptr<ui::ColourPickerController> m_colourPicker;
    std::unique_ptr<ui::WorkspaceLayoutController> m_workspaceLayout;
    std::unique_ptr<ui::ShiftTimesController> m_shiftTimes;
    int m_selectOptions = 0;
    struct ConversionPlan {
        std::uint64_t document = 0;
        std::uint64_t revision = 0;
        QVariantMap options;
        core::Document converted;
    };
    std::unique_ptr<ConversionPlan> m_conversionPlan;
    std::set<std::uint64_t> m_formatChanged; // converted since opened or saved: Save asks for a file
    QString m_resolutionCheckedVideo; // the video whose size was compared
    void checkResolution();
    QStringList m_selectRecent;
    QString m_pendingKeyframes; // opened before a video (legacy m_KeyframesFileName)
    std::unique_ptr<ui::GridFilterController> m_gridFilter;
    bool runFilter(const std::function<std::expected<void, application::CommandRefusal>(application::EditSession &)> &command);
    bool m_videoFailureLogged = false;
    std::uint64_t m_seenGroupBreaks = 0;
    // P3
    std::unique_ptr<application::RecoveryStore> m_recovery;
    std::unique_ptr<QLockFile> m_sessionLock;
    QString m_recoveryDir;
    QString m_sessionId;
    std::map<std::uint64_t, QTimer *> m_autosaveTimers;
    void scheduleAutosave();
    bool autosave(application::DocumentId document);
    void discardRecovery(application::DocumentId document);
    std::string recoveryKey(application::DocumentId document) const;
    void reportGroupBreak();
    std::optional<application::DocumentId> m_videoDocument;
    std::optional<std::uint64_t> m_videoRevision; // the revision whose content the overlay shows
    std::optional<core::LineId> m_videoLine;
    struct Closing {
        application::DocumentId document;
        bool save = false;
        std::uint64_t revision = 0; // the content the choice covers
        std::optional<application::PermitId> permit;
    };
    QString m_closeThen;
    std::vector<Closing> m_closing;
    bool m_quitApproved = false;
    // P2: the subtitles waiting for the review to replace the editing target.
    std::optional<application::StagedOpen> m_pendingOpen;
    QString m_pendingOpenPath;
    application::RecentFiles m_recent;
    QString m_settingsFile;
    // Modification time of each Document's file when last read or written,
    // and the Documents whose removal was already noticed.
    std::map<std::uint64_t, QDateTime> m_fileTimes;
    std::set<std::uint64_t> m_removedNoticed;
    // The last column choices (legacy default: none).
    int m_copyColumns = 0;
    int m_pasteColumns = 0;
};

} // namespace hikari::app
