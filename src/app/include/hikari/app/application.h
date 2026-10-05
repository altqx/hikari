#pragma once

// The composed application services (composition root). main() and the UI
// workflow tests build the same graph: native file ports, the write
// coordinator, Documents, the workspace and the shell/editor presenters.

#include "hikari/app/automation_hotkeys_controller.h"
#include "hikari/app/hotkeys_controller.h"
#include "hikari/app/update_checker.h"
#include "hikari/app/style_manager_controller.h"
#include "hikari/app/font_collector_controller.h"
#include "hikari/app/automation_shell.h"
#include "hikari/application/document_files.h"
#include "hikari/application/find_replace.h"
#include "hikari/application/grid_commands.h"
#include "hikari/application/grid_selection.h"
#include "hikari/application/misspell_replacer.h"
#include "hikari/application/options_dialog.h"
#include "hikari/application/recent_files.h"
#include "hikari/application/video_sources.h"
#include "hikari/application/recovery_store.h"
#include "hikari/application/session_file.h"
#include "hikari/application/spell_checker.h"
#include "hikari/application/subtitle_comparison.h"
#include "hikari/application/workspace.h"
#include "hikari/backends/audio_box_player.h"
#include "hikari/backends/portaudio_output.h"
#include "hikari/backends/ffms_indexed_source.h"
#include "hikari/backends/legacy_spelling.h"
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
#include "visual_tools_controller.h"
#include "settings_store.h"
#include "shell_controller.h"
#include "video_controller.h"
#include "audio_controller.h"

#include <QDate>
#include <QDateTime>
#include <QLockFile>
#include <QTimer>
#include <QObject>
#include <QUrl>
#include <QVariantMap>

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <tuple>

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
    // F3: the spelling options or the dictionary changed (marks are stale),
    // and legacy's message boxes when the spell checker cannot start.
    void spellingChanged();
    void spellingNotice(const QString &message);
    // F1: find and replace shows a legacy message box (kind: 0 message, 1
    // "Reached end", 2 missing styles, 3 no style found, 4 replace in files);
    // a question (kinds 1-4) waits for answerFindQuestion(id, ...), a message
    // needs no answer. Nothing blocks meanwhile.
    void findQuestion(int id, int kind, const QString &text, const QString &title);
    void findResultsChanged();
    // An operation finished (also after its questions): the tab as it is now
    // (the styles question may have changed its styles) with the recent lists.
    void findFinished(const QVariantMap &settings);
    void findBusyChanged();
    // F1: a file result opens into the Untitled editing target, which has
    // changes: the close review shows these rows (as reviewOpen's), and the
    // result is shown once it finishes or is cancelled.
    void findOpenReview(const QVariantList &rows);
    // O1: HikariSubFrame::DestroyDialogs (a changed program font). The
    // Search tool saves its tab (FR->SaveOptions) and closes with its
    // results; the Multireplacer and its Search results close.
    void findReplaceDestroyed();
    void misspellReplacerDestroyed();

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
        // A4: makes the audio box's output with the options the settings
        // give (tests record them); unset: PortAudio, or with playbackAudio
        // off an output without a device.
        std::function<std::unique_ptr<application::AudioOutputPort>(const backends::PortAudioOutput::Options &)>
            makeAudioOutput;
        // Whether audio.outputHostApi chooses the host API (Windows; tests
        // elsewhere may say so).
        bool outputHostApiSetting = backends::PortAudioOutput::onWindows;
        // P8: the release list the update check reads (tests: a file).
        QUrl updateFeed = UpdateChecker::defaultFeed();
        // Y2: where the style catalogs live; empty: "Catalog" beside the settings
        // file, or a temporary directory without one (tests).
        QString catalogDir;
        // F3 / R2-hunspell: the spelling backend (Hunspell; tests may pass a
        // fake). Without one there is no spell checker: no marks but bracket
        // errors, no notice, and SPELLCHECKER_ON is left as it is.
        application::SpellingBackendLoader spellingBackend = backends::hunspellSpellingLoader();
        // The user's "Dictionary" folder (UserDic.udic and dictionaries the
        // user adds); empty: beside the settings file (none without one, and
        // then no spell checker).
        QString dictionaryDir;
        // Bundled dictionaries, searched after the user's folder (legacy
        // <executable dir>/Dictionary; the composition sets it).
        QString bundledDictionaryDir;
        // A1: legacy's AudioCache folder; empty: "AudioCache" beside the
        // settings file, or a temporary directory without one (tests).
        QString audioCacheDir;
        // A1: legacy's FFMS2 index files (Indices); empty: "Indices" beside
        // the settings file, or none without one (tests).
        QString indexDir;
    };
    explicit Application(QObject *parent = nullptr);
    explicit Application(Options options, QObject *parent = nullptr);
    ~Application() override;

    // Opens a file as a new Document; the first becomes the editing target.
    // P6: an untouched empty Untitled tab is replaced by it (legacy OpenFile).
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
    // F1: a find and replace question waits for its answer.
    Q_PROPERTY(bool findBusy READ findBusy NOTIFY findBusyChanged)
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
    // F4: GLOBAL_MISSPELLS_REPLACER ("Fix minor errors (experimental)", the
    // "Multireplacer" dialog). The rules are read from Rules.txt beside the
    // INI file (the shipped rules without one) when first asked for, and
    // written back when the application ends unless the list is empty
    // (legacy: when the dialog is destroyed with the main window). Rows
    // {description, find, replace, options, checked}.
    Q_INVOKABLE QVariantList misspellRules();
    // Add rule / Edit rule / Delete rule: refused with the legacy message in
    // the log while the results window is shown.
    Q_INVOKABLE bool addMisspellRule(const QVariantMap &rule);
    Q_INVOKABLE bool editMisspellRule(int index, const QVariantMap &rule);
    Q_INVOKABLE bool removeMisspellRule(int index);
    Q_INVOKABLE void checkMisspellRule(int index, bool checked);
    Q_INVOKABLE void setMisspellResultsShown(bool shown) { m_misspellResultsShown = shown; }
    // "Find errors in current tab / in all tabs" with {lines (the "Which
    // lines" choice 0-3), styles}: the results list, a header row {header:
    // true, text: the file's path} before each Document's finds {header:
    // false, find (its index), line, text, position, length}.
    Q_INVOKABLE QVariantList findMisspells(const QVariantMap &scope, bool allTabs);
    // "Replace all errors in current tab / in all tabs": one step per Document.
    Q_INVOKABLE void replaceMisspells(const QVariantMap &scope, bool allTabs);
    // The results window's Replace with the checked finds' indices.
    Q_INVOKABLE void replaceMisspellFinds(const QVariantList &finds);
    // A double click on a find: its Document, Line and text in the editor.
    Q_INVOKABLE void showMisspellFind(int find);
    // F3: spelling. SPELLCHECKER_ON, DICTIONARY_LANGUAGE and
    // EDITBOX_SUGGESTIONS_ON_DOUBLE_CLICK in the settings registry
    // (editor.spellchecker, editor.dictionaryLanguage,
    // editor.suggestionsOnDoubleClick); the Line editor keeps its own switch
    // (TextEditor::SpellCheckerOnOff) that the menu and the Options dialog
    // set. Turning spell checking on does not start a spell checker created
    // while it was off (legacy SpellChecker::Get); choosing a language restarts it.
    Q_PROPERTY(bool spellingOn READ spellingOn WRITE setSpellingOn NOTIFY spellingChanged)
    Q_PROPERTY(QString dictionaryLanguage READ dictionaryLanguage WRITE setDictionaryLanguage NOTIFY spellingChanged)
    Q_PROPERTY(bool suggestionsOnDoubleClick READ suggestionsOnDoubleClick WRITE setSuggestionsOnDoubleClick NOTIFY spellingChanged)
    bool spellingOn() const;
    void setSpellingOn(bool on);
    QString dictionaryLanguage() const;
    void setDictionaryLanguage(const QString &symbol);
    bool suggestionsOnDoubleClick() const;
    void setSuggestionsOnDoubleClick(bool on);
    // The "Dictionary" folder's dictionaries: {symbol, name} (legacy AvailableDics, FindLanguage).
    Q_INVOKABLE QVariantList dictionaries() const;
    Q_INVOKABLE QString dictionaryName(const QString &symbol) const;
    // The Line editor's spell-checked field: 1 (Translated) in translation
    // mode, else 0. Its marks as flat [start, end) pairs of the field's text
    // (none while spelling is off); the misspelling at a field position
    // {word, start, end, suggestions} (empty when none); a suggestion
    // replacing it ("Correcting spelling errors in the text field"); and
    // "Add word to dictionary".
    Q_INVOKABLE int spellingRole() const;
    Q_INVOKABLE QVariantList editorSpellingMarks(int role);
    Q_INVOKABLE QVariantMap editorMisspellAt(int role, int position);
    Q_INVOKABLE bool replaceEditorMisspell(int role, int position, const QString &replacement);
    Q_INVOKABLE bool addEditorWord(const QString &word);
    // GLOBAL_OPEN_SPELLCHECKER: the Spellchecker window. Each call takes the
    // window's {ignoreComments, ignoreUpperCase} and returns its state
    // {found, word, suggestions, replacement, changed, problem, unchanged,
    // restarted}; found false is "No spelling errors were found". unchanged:
    // the action did nothing (legacy returns without touching the window).
    // restarted: the active Line or its text changed since the word was
    // shown (the editor's draft commits when the window acts), so the walk
    // started again from the active Line instead of acting.
    Q_INVOKABLE QVariantMap openSpellChecker(const QVariantMap &options);
    Q_INVOKABLE QVariantMap spellCheckerActivated(const QVariantMap &options);
    Q_INVOKABLE QVariantMap spellCheckerReplace(const QString &replacement, const QVariantMap &options);
    Q_INVOKABLE QVariantMap spellCheckerReplaceAll(const QString &misspell, const QString &replacement,
                                                   const QVariantMap &options);
    Q_INVOKABLE QVariantMap spellCheckerIgnore(const QVariantMap &options);
    Q_INVOKABLE QVariantMap spellCheckerIgnoreAll(const QString &word, const QVariantMap &options);
    Q_INVOKABLE QVariantMap spellCheckerAddWord(const QString &word, const QVariantMap &options);
    Q_INVOKABLE void closeSpellChecker();
    // "Remove from dictionary": the user dictionary's lines, and removing the chosen ones.
    Q_INVOKABLE QStringList addedDictionaryWords() const;
    Q_INVOKABLE bool removeDictionaryWords(const QStringList &words);
    // F1: GLOBAL_SEARCH, GLOBAL_FIND_REPLACE and GLOBAL_FIND_NEXT. A tab of
    // the dialog as {tab (0 Find, 1 Find and replace, 2 Find in subtitles),
    // find, replace, styles, filters, folder, field, lines, matchCase, regex,
    // startOfText, endOfText, includeComments, skipTags, skipText,
    // subfolders, hiddenFolders} plus the recent lists {finds, replacements,
    // filterList, paths}; legacy FIND_REPLACE_OPTIONS, FIND_REPLACE_STYLES
    // and the recent lists live in the settings registry (find.options,
    // find.styles, find.recentFinds, find.recentReplacements,
    // findInSubs.recentFilters, findInSubs.recentPaths). openFindReplace is
    // the tool's first opening (legacy creates FindReplaceDialog: the recent
    // lists are read then and kept until it is destroyed); findReplaceSettings
    // is a tab as it shows; switchFindReplaceTab saves the tab left
    // (SaveValues) and gives the next one (SetValues).
    Q_INVOKABLE QVariantMap openFindReplace(int tab);
    Q_INVOKABLE QVariantMap findReplaceSettings(int tab) const;
    Q_INVOKABLE QVariantMap switchFindReplaceTab(const QVariantMap &settings, int tab);
    Q_INVOKABLE void saveFindReplaceSettings(const QVariantMap &settings);
    // A button: "find", "findAllCurrent", "findAllTabs", "replace",
    // "replaceAll", "replaceAllTabs", "findInFiles" or "replaceInFiles". The
    // legacy messages and questions come as findQuestion; findFinished
    // gives the tab afterwards (the styles may change) with the recent lists.
    // Ignored while a question waits (findBusy).
    Q_INVOKABLE void runFindReplace(const QString &action, const QVariantMap &settings);
    Q_INVOKABLE void findNext();
    // The answer to question `id`: 0 Ok, 1 Yes, 2 No, 3 Cancel.
    Q_INVOKABLE void answerFindQuestion(int id, int answer);
    bool findBusy() const;
    // The Search tool gains the focus (FindReplaceDialog::OnActivate): the text selected
    // in the Line editor, or `find` as it is.
    Q_INVOKABLE QString findReplaceActivated(const QString &find);
    // A Lines radio button (TabWindow::Reset): the next search starts over.
    Q_INVOKABLE void resetFindReplace();
    // The results dialog: rows {header, text, line, before, match, after, checked, visible}.
    Q_INVOKABLE QVariantList findResults() const;
    Q_INVOKABLE bool findResultsShown() const;
    Q_INVOKABLE bool canReplaceFindResults() const;
    Q_INVOKABLE void checkFindResults(bool check);
    Q_INVOKABLE void toggleFindResult(int row);
    Q_INVOKABLE void toggleFindGroup(int row);
    Q_INVOKABLE void showFindResult(int row);
    Q_INVOKABLE void replaceFindResults(const QString &replacement);
    // Tests answer the questions themselves; backups go to this folder
    // (default: ReplaceBackup beside the settings file, none without one).
    void setFindQuestionHandler(std::function<int(int kind, const QString &text)> handler);
    void setReplaceBackupFolder(const QString &folder) { m_replaceBackup = folder; }
    // Legacy numOfProcessors for replacing in files (0: this machine's).
    void setFindProcessorCount(int count) { m_findProcessors = count; }
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
    // A1: the options the audio box's next open reads.
    application::AudioCacheSettings audioSettings() const { return m_audioSettings(); }
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

    // A1: the Audio menu. GLOBAL_AUDIO_FROM_VIDEO opens the video's file
    // again as audio; GLOBAL_RECENT_AUDIO lists {path, label} rows (missing
    // local files pruned first, as legacy AppendRecent); the GLOBAL_OPEN_AUDIO
    // dialog starts in the video's folder, else the latest recent video's (V3).
    Q_INVOKABLE void openAudioFromVideo();
    Q_INVOKABLE QVariantList recentAudio();
    Q_INVOKABLE QUrl audioDialogFolder() const;
    // V3 (application_video.cpp): the Video menu's recent lists
    // (GLOBAL_RECENT_VIDEO, GLOBAL_RECENT_KEYFRAMES: {path, label} rows,
    // missing local files pruned first as legacy AppendRecent), keyframes
    // opened from the dialog or the list (SetRecent(3) either way), the
    // dialogs' folders (legacy OnMenuSelected), the dummy video
    // (GLOBAL_OPEN_DUMMY_VIDEO; the error text to log, else empty) and
    // VIDEO_PREVIOUS_FILE / VIDEO_NEXT_FILE.
    Q_INVOKABLE QVariantList recentVideos();
    Q_INVOKABLE QVariantList recentKeyframes();
    Q_INVOKABLE QString openKeyframesFile(const QString &path);
    Q_INVOKABLE QUrl videoDialogFolder() const;
    Q_INVOKABLE QUrl keyframesDialogFolder() const;
    Q_INVOKABLE QVariantMap dummyVideoDefaults() const;
    Q_INVOKABLE QString openDummyVideo(const QVariantMap &values);
    Q_INVOKABLE bool nextVideoFile(bool next);
    // A3: GLOBAL_SET_AUDIO_FROM_VIDEO and GLOBAL_SET_AUDIO_MARK_FROM_VIDEO (the
    // Video menu, enabled while the audio box exists): the box centred on the
    // video's time (VideoBox::Tell, 0 without video), with the mark there too.
    Q_INVOKABLE void setAudioFromVideo(bool mark);

    ui::ShellController &shell() { return *m_shell; }
    ui::LineEditorController &editor() { return *m_editor; }
    ui::VideoController &video() { return *m_video; }
    backends::QtGeneralPlayer &generalPlayer() { return *m_generalPlayer; } // tests: general playback
    ui::AudioController &audio() { return *m_audio; }
    AutomationShell &automation() { return *m_automation; }
    AutomationHotkeysController &automationHotkeys() { return *m_automationHotkeys; }
    // O2: the shortcut editor and the bindings in effect.
    HotkeysController &hotkeys() { return *m_hotkeys; }
    UpdateChecker &updates() { return *m_updates; }
    StyleManagerController &styleManager() { return *m_styleManager; }
    // Y8: the font collector (GLOBAL_OPEN_FONT_COLLECTOR).
    FontCollectorController &fontCollector() { return *m_fontCollector; }
    ui::LogController &log() { return *m_log; }
    ui::TagButtonsController &tagButtons() { return *m_tagButtons; }
    ui::ColourPickerController &colourPicker() { return *m_colourPicker; }
    // E1/O1: the Line editor's colour picker opens for the editing target
    // (legacy DialogColorPicker::Get(EditBox of that tab)).
    Q_INVOKABLE void colourPickerOpened();
    ui::WorkspaceLayoutController &workspaceLayout() { return *m_workspaceLayout; }
    ui::ShiftTimesController &shiftTimesSettings() { return *m_shiftTimes; }
    ui::GridFilterController &gridFilter() { return *m_gridFilter; }
    // T1: the Video panel's visual tools.
    ui::VisualToolsController &visualTools() { return *m_visualTools; }
    application::DocumentFiles &files() { return *m_files; }
    application::Workspace &workspace() { return m_workspace; }
    // Properties for Main.qml.
    QVariantMap qmlProperties();
    // Tests: waits for running writes and delivers their results.
    void waitForWrites();

    // P6: tabs and session restore (legacy Notebook, HikariSubFrame at
    // 20d647c4). The tabs are the Workspace's Documents but the protected
    // reference, in order; the editing target is the active tab. Each tab
    // keeps its own media (video and its position, its own audio file,
    // keyframes) and Grid scroll, shown again when it becomes active.
    // Rows {id, label ("<step>*name" when modified, at most TAB_TEXT_MAX_CHARS
    // characters), title, current, tip (subtitles and video paths)}.
    Q_PROPERTY(QVariantList tabs READ tabs NOTIFY tabsChanged)
    Q_PROPERTY(int currentTab READ currentTab NOTIFY tabsChanged)
    // LAST_SESSION_CONFIG (session.restore): 0 nothing, 1 ask, 2 load at start.
    Q_PROPERTY(int sessionRestore READ sessionRestore WRITE setSessionRestore NOTIFY sessionRestoreChanged)
    // Session entries a restore could not resolve (L58 "unresolved restores
    // stay visible"): {row, tab, title, kind ("subtitles", "video", "audio",
    // "keyframes"), path}.
    Q_PROPERTY(QVariantList unresolvedRestores READ unresolvedRestores NOTIFY unresolvedRestoresChanged)
    QVariantList tabs() const;
    int currentTab() const;
    int sessionRestore() const;
    void setSessionRestore(int value);
    QVariantList unresolvedRestores() const;
    // GLOBAL_ADD_PAGE (Ctrl+T): a new Untitled tab after the last, shown.
    // The tab bar's "+" (fromTabBar) also writes the session, as legacy's did.
    Q_INVOKABLE void addPage(bool fromTabBar = false);
    // GLOBAL_NEXT_TAB (+1, Ctrl+PgDown) / GLOBAL_PREVIOUS_TAB (-1, Ctrl+PgUp),
    // wrapping; nothing with fewer than two tabs.
    Q_INVOKABLE void changeTab(int step);
    // A click on a tab.
    Q_INVOKABLE void selectTab(int index);
    // A middle click on a tab (legacy DeletePage of that tab): its review
    // rows as reviewClose's; with none, finishClose() closes it.
    Q_INVOKABLE QVariantList reviewCloseTab(int index);
    // GLOBAL_LOAD_LAST_SESSION (empty file) and GLOBAL_LOAD_EXTERNAL_SESSION
    // (a .kls file): the session is read and its subtitles staged
    // (L58-staged-replacement), then every open Document's unsaved work is
    // reviewed as for Quit (P6-session-review). Returns {ok, problem, rows}; with ok and no rows,
    // finishClose() replaces the open tabs with the session's.
    Q_INVOKABLE QVariantMap reviewSession(const QUrl &file = {});
    // GLOBAL_SAVE_EXTERNAL_SESSION: "readonly" (legacy asks again), "failed" or "".
    Q_INVOKABLE QString saveSessionTo(const QUrl &file);
    // Where the session dialogs start (legacy Options.configPath).
    Q_INVOKABLE QUrl sessionFolder() const;
    // At startup (legacy hikarisubApp::OnInit): "load", "ask" or "crash" (the
    // last session did not end with "[Close session]"), or "".
    Q_INVOKABLE QString startupSession() const;
    Q_INVOKABLE bool lastSessionCrashed() const;
    void setStartedWithPaths(bool paths) { m_startedWithPaths = paths; }
    // The unresolved entry `row`: try again, choose another file, or forget it.
    Q_INVOKABLE bool retryRestore(int row);
    Q_INVOKABLE bool relinkRestore(int row, const QUrl &file);
    Q_INVOKABLE void removeRestore(int row);
    // The Grid's first shown row for the active tab (legacy Scroll). Ignored
    // from a tab change (tabShown) until the Grid has restored that tab's
    // scroll and called scrollRestored(), so a model reset's transient 0
    // never overwrites it.
    Q_INVOKABLE void setTargetScroll(int row);
    Q_INVOKABLE void scrollRestored() { m_scrollRestoring = false; }
    // The program closes: the session is written with "[Close session]".
    Q_INVOKABLE void endSession();

    // R1: the tab menu's "Subtitle comparison" (legacy Notebook::ContextMenu,
    // its ID_CHECK_EVENT handler and SubsGrid::SubsComparison at 20d647c4).
    // The menu opened on tab `index` (-1: not on a tab): {enabled,
    // canCompare, active, times, visible, selections, selectionsEnabled,
    // styles, chosenStyles, styleItems: [{name, checked}]}. Opening it adds
    // the checked styles to the chosen ones again, as legacy's did.
    Q_INVOKABLE QVariantMap openComparisonMenu(int index);
    // MENU_COMPARE + 1..4: flips SUBS_COMPARISON_TYPE's bit; the new value.
    Q_INVOKABLE int toggleComparisonBit(int bit);
    // A style of "Compare by selected styles" checked or unchecked; whether
    // "Compare by selected styles" is now shown checked.
    Q_INVOKABLE bool toggleComparisonStyle(const QString &name, bool checked);
    // MENU_COMPARE: the editing target (CG1) with tab `index` (CG2).
    Q_INVOKABLE bool compareWithTab(int index);
    // MENU_COMPARE - 1, "Turn off comparison".
    Q_INVOKABLE void turnOffComparison();
    const application::SubtitleComparison &comparison() const { return m_comparison; }
    QString lastSessionPath() const;
    // Writes LastSession.txt (legacy SaveLastSession), or `path`.
    bool saveLastSession(bool closing = false, const QString &path = {});
signals:
    void tabsChanged();
    void sessionRestoreChanged();
    void unresolvedRestoresChanged();
    // A tab became active: its Grid scroll to show again.
    void tabShown(int scroll);
    // A session finished loading with `unresolved` entries left.
    void sessionRestored(int unresolved);

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
    // V3: the Video panel's source: dummy videos, else the media helper
    std::unique_ptr<application::DummyVideoSource> m_videoSource;
    backends::LibassRenderer m_renderer;
    std::unique_ptr<ui::VideoController> m_video;
    std::unique_ptr<backends::QtGeneralPlayer> m_generalPlayer;
    // O1: declared before everything that keeps a reference to it.
    std::unique_ptr<ui::SettingsStore> m_settings;
    void settingChanged(const QString &id);
    // R6-dictionary-location: the settings folder's Dictionary (user
    // dictionaries), then the program folder's (bundled ones).
    QStringList m_dictionaryDirs;
    bool m_resettingSettings = false; // "Set default" is resetting the registry
    bool m_selectLinesOpened = false; // legacy SelectLines exists
    application::OptionsLists m_optionsLists; // what the open Options dialog lists
    std::unique_ptr<AutomationShell> m_automation;
    std::unique_ptr<AutomationHotkeysController> m_automationHotkeys;
    std::unique_ptr<HotkeysController> m_hotkeys; // O2: after the automation hotkeys it shares
    std::unique_ptr<UpdateChecker> m_updates;
    std::unique_ptr<StyleManagerController> m_styleManager;
    std::unique_ptr<FontCollectorController> m_fontCollector; // Y8
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
    // F4: the rules once read, and the last search's finds per Document.
    std::optional<std::vector<application::ReplacerRule>> m_misspellRules;
    std::vector<std::pair<application::DocumentId, application::ReplacerFind>> m_misspellFinds;
    bool m_misspellResultsShown = false;
    std::vector<application::ReplacerRule> &misspellRuleList();
    void saveMisspellRules();
    void destroyDialogs();
    // F1
    class FindHost;
    friend class FindHost;
    std::unique_ptr<FindHost> m_findHost;
    std::unique_ptr<application::FindReplace> m_find;
    bool m_findOpened = false; // legacy FR exists
    void createFindReplace();
    QString m_replaceBackup;
    std::function<int(int, const QString &)> m_findQuestionHandler;
    int m_findQuestionId = 0;
    std::map<int, std::function<void(application::FindAnswer)>> m_findAnswers; // questions shown, by id
    // A file result waiting for the close review of its open (true: opened).
    std::function<void(bool)> m_findOpenDone;
    int m_findProcessors = 0;
    void endFindOpen(bool opened);
    void saveFindRecent(application::FindReplaceSettings::Tab tab);
    void findFinished();
    QString m_pendingKeyframes; // opened before a video (legacy m_KeyframesFileName)
    std::unique_ptr<ui::GridFilterController> m_gridFilter;
    // R1: legacy's comparison statics and each grid's table, the revision
    // each compared Document had when its table was last made, and whether a
    // +/- mark or a group is being opened or closed (legacy FilterPartial).
    application::SubtitleComparison m_comparison;
    std::map<std::uint64_t, std::uint64_t> m_comparedRevisions;
    bool m_partialFilter = false;
    application::ComparedDocument comparedDocument(application::DocumentId id) const;
    void recompare();
    void refreshComparison();
    std::unique_ptr<ui::VisualToolsController> m_visualTools;
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
    // F3: spelling. The checker exists when there is a Dictionary folder;
    // m_spellingStarted is legacy's SpellChecker::SC (created on first use,
    // initialized then only when spelling is on; Destroy clears it).
    application::SpellChecker *spellChecker();
    void restartSpellChecker();
    void spellingRefresh();
    std::optional<std::u16string> editorRaw(int role) const;
    QVariantMap spellCheckState(bool changed, const QString &problem = {});
    // Commits the editor's draft (commit on leave) and, when that or anything
    // else left the window's word stale, starts the walk again: its state.
    std::optional<QVariantMap> restartStaleSpellCheck(application::EditSession &session, const QVariantMap &options);
    void showSpellCheckWord();
    std::unique_ptr<application::SpellChecker> m_spellChecker;
    bool m_spellingStarted = false;
    application::SpellingText m_spellingText;
    bool m_spellingOn = true; // the Line editor's switch (TextEditor::SpellCheckerOnOff)
    QString m_dictionaryDir;
    QString m_bundledDictionaryDir;
    std::unique_ptr<application::SpellCheckWalk> m_spellWalk;
    std::optional<application::DocumentId> m_spellWalkDocument;
    // A1: the audio box, with its own media helper; the Lines and marks it
    // shows follow the editing target and the video.
    std::unique_ptr<backends::FfmsIndexedSource> m_audioSource;
    std::unique_ptr<ui::AudioController> m_audio;
    QList<QMetaObject::Connection> m_audioConnections; // dropped first on destruction
    application::RecentFiles m_recentAudio;
    QString m_audioFollowedVideo; // the video whose audio the box last followed
    // A1: the video's index handed over in a temporary file, until the box
    // has opened from it (then the video's source removes it)
    std::string m_pendingIndexHandoff;
    void releaseReadIndexHandoff();
    std::optional<std::tuple<std::uint64_t, std::uint64_t, std::uint64_t>> m_audioLine; // target, active Line, revision
    // the video's keyframes as the box marks them, worked out once per video
    std::optional<application::LegacyTimebase> m_audioTimebase;
    std::string m_audioTimebasePath;
    std::vector<int> m_audioKeyframes;
    std::function<application::AudioCacheSettings()> m_audioSettings;
    QString m_indexDir; // legacy Indices; empty: no index files
    // A4: the box's player and the editor output it plays through (PortAudio,
    // made at the first play; without playbackAudio an output with no device)
    std::unique_ptr<application::AudioOutputPort> m_audioOutput;
    std::string m_audioOutputHostApi; // the host API m_audioOutput was made for
    std::unique_ptr<backends::AudioBoxPlayer> m_audioPlayer;
    void refreshAudio();
    void followVideoInAudio();
    // V3 (application_video.cpp)
    void trackVideoSources();
    void logVideoFailure();
    void rememberRecentVideo(const QString &path);
    void rememberRecentKeyframes(const QString &path);
    QVariantList recentRows(application::RecentFiles &list, const char *setting);
    // keyframes opened with the audio box and no video, at 23.976 fps
    bool keyframesWithoutVideo(const QString &path, QString &problem);
    application::RecentFiles m_recentVideo;
    application::RecentFiles m_recentKeyframes;
    application::NextFileWalker m_nextFile;
    QString m_recentVideoSeen; // the ready video last added to the list
    void rememberRecentAudio(const QString &path);
    void trimAudioCache();
    // Legacy's index file for `path` and an audio track (-1: none).
    QString indexFile(const QString &path, int track) const;
    // A3: the box's commits reach the editing target here (legacy
    // CommitChanges through the edit box and grid); while one runs the box
    // keeps its selection for the same Line (no SetDialogue).
    bool m_audioCommitting = false;
    void commitAudioTimes(const application::AudioCommitRequest &request);
    // The Options dialog's Themes page colours (A2: the spectrum's).
    void addThemeColours(QVariantMap &values) const;
    void setAudioActive(int key);
    void seekVideoFromAudio(int ms);
    // P6
    struct TabMedia {
        QString video;      // legacy VideoPath (native separators)
        int position = 0;   // ms, kept while another tab is shown
        QString audio;      // legacy AudioPath: an audio file of the tab's own ("" from the video, or none)
        QString keyframes;  // legacy KeyframesPath
        int scroll = 0;     // the Grid's first row
    };
    struct UnresolvedRestore {
        application::DocumentId document;
        QString kind;
        QString path;
        int active = 0;   // subtitles: the session's active row
        int position = 0; // video: the session's position
    };
    struct PendingSession {
        std::vector<application::SessionTab> tabs;
        std::vector<std::optional<application::StagedOpen>> staged;
        // Each staged file as it was read (L58-staged-replacement: a file
        // changed or gone by the time the review allows the load is read again).
        std::vector<std::pair<QDateTime, qint64>> stamps;
    };
    std::map<std::uint64_t, TabMedia> m_tabMedia;
    std::vector<UnresolvedRestore> m_unresolved;
    std::optional<PendingSession> m_pendingSession;
    std::optional<application::DocumentId> m_closingTab; // reviewCloseTab's
    QString m_keepTabAudio;  // a restored video whose audio must not replace the tab's own
    std::optional<std::pair<QString, int>> m_pendingTabSeek; // video path, ms; until the seek has landed
    bool m_tabSeekQueued = false;
    bool m_scrollRestoring = false;
    bool m_startedWithPaths = false;
    int m_tabTextMax = 40; // legacy maxCharPerTab, read once at start
    void leaveTabMedia(std::optional<application::DocumentId> document);
    void enterTabMedia(std::optional<application::DocumentId> previous, std::optional<application::DocumentId> document);
    void closeDocument(application::DocumentId document);
    void replaceTarget(application::DocumentId replacement);
    void applySession();
    void forgetTab(application::DocumentId document);
    void selectRow(application::DocumentId document, int row);
    bool keepTabAudio(const QString &videoPath);
    void trackTabMedia();
    int targetVideoPosition() const;
};

} // namespace hikari::app
