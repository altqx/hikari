#pragma once

#include "hikari/app/automation_hotkeys_controller.h"
#include "hikari/app/settings_import_controller.h"
#include "hikari/app/settings_import_store.h"
#include "hikari/app/hotkeys_controller.h"
#include "hikari/app/analytics.h"
#include "hikari/app/crash_reports.h"
#include "hikari/app/update_checker.h"
#include "hikari/app/whats_new.h"
#include "hikari/app/style_manager_controller.h"
#include "hikari/app/kanji_timer_controller.h"
#include "hikari/app/font_collector_controller.h"
#include "hikari/app/font_catalogs_controller.h"
#include "hikari/app/matroska_controller.h"
#include "hikari/app/localisation.h"
#include "hikari/app/automation_shell.h"
#include "hikari/app/user_folder.h"
#include "hikari/core/ass_attachments.h"
#include "hikari/core/grammar.h"
#include "hikari/application/associated_files.h"
#include "hikari/application/audio_devices.h"
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
#include "hikari/application/video_timing.h"
#include "hikari/application/translation_mode.h"
#include "hikari/application/workspace.h"
#include "hikari/backends/audio_box_player.h"
#include "hikari/backends/portaudio_output.h"
#include "hikari/backends/ffms_indexed_source.h"
#include "hikari/backends/harper_client.h"
#include "hikari/backends/legacy_spelling.h"
#include "hikari/backends/libass_renderer.h"
#include "hikari/backends/subtitle_renderer_selection.h"
#include "hikari/backends/qt_general_player.h"
#ifdef _WIN32
#include "hikari/backends/directshow_player.h"
#endif
#include "hikari/backends/platform_files.h"
#include "line_editor_controller.h"
#include "template_editor_controller.h"
#include "log_controller.h"
#include "colour_picker_controller.h"
#include "workspace_layout.h"
#include "shift_times_controller.h"
#include "tag_buttons_controller.h"
#include "grid_filter_controller.h"
#include "visual_tools_controller.h"
#include "video_view_controller.h"
#include "status_bar_controller.h"
#include "video_fullscreen_controller.h"
#include "settings_store.h"
#include "shell_controller.h"
#include "video_controller.h"
#include "audio_controller.h"

#include <QChar>
#include <QDate>
#include <QFont>
#include <QDateTime>
#include <QLockFile>
#include <QTemporaryDir>
#include <QTimer>
#include <QElapsedTimer>
#include <QObject>
#include <QUrl>
#include <QFileInfo>
#include <QVariantMap>

#include <functional>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <tuple>
#include <unordered_map>

namespace hikari::app {

class Application : public QObject {
    Q_OBJECT
signals:
    void closeFinished(bool done, const QString &problem);
    // A changed program font closed the Select dialog.
    void selectLinesDestroyed();
    void quitApprovedChanged();
    // Also when a save failed or another review took the quit review's place.
    void quitCancelled();
    void saveWithVideoNameChanged();
    void askForBadResolutionChanged();
    // The video's size differs from the target's PlayRes:
    // {subsWidth, subsHeight, videoWidth, videoHeight}.
    void resolutionMismatch(const QVariantMap &sizes);
    void recentChanged();
    // description: the group the command would break (0: it would make a new
    // malformed group).
    void groupBreakRefused(qulonglong description, const QString &title);
    void spellingChanged();
    // The dictionary changed; a running check starts over with it.
    void spellCheckerRestarted();
    // Also when the Line editor's text has new findings.
    void grammarChanged();
    void grammarWalkChanged();
    void playbackPlayerChanged();
    void audioOutputChanged();
    // kind: 0 message, 1 "Reached end", 2 missing styles, 3 no style found, 4 replace in
    // files; kinds 1-4 wait for answerFindQuestion(id, ...).
    void findQuestion(int id, int kind, const QVariantMap &question);
    void findResultsChanged();
    // Also after its questions; settings: the tab as it is now, with the recent lists.
    void findFinished(const QVariantMap &settings);
    void findBusyChanged();
    // A file result opens into the Untitled target, which has changes: rows as
    // reviewOpen's.
    void findOpenReview(const QVariantList &rows);
    // Both fire when a changed program font closes the Search tool / Multireplacer.
    void findReplaceDestroyed();
    void misspellReplacerDestroyed();
    // The interface language changed while Options is open: the dictionaries' new names.
    void settingsListsChanged(const QStringList &dictionaries);

public:
    struct Options {
        // Empty: next to the application, else the build tree's.
        QString mediaHelper;
        // automationDir holds automation/{Autoload,Include}. Empty: next to the
        // application, else the build tree's.
        QString luaHelper;
        QString automationDir;
        // Empty: automationDir.
        QString userAutomationDir;
        bool autoload = false;
        // The first start marks Getting started due.
        bool gettingStarted = false;
        // Empty: in memory only.
        QString settingsFile;
        // Most preferred first; on the first start a leading Polish one picks Polish for
        // the interface and the spell checker.
        QStringList systemUiLanguages;
        // Empty: no autosave.
        QString recoveryDir;
        // Whether video playback may open an audio device.
        bool playbackAudio = true;
        // Unset: PortAudio, or with playbackAudio off an output without a device.
        std::function<std::unique_ptr<application::AudioOutputPort>(const backends::PortAudioOutput::Options &)>
            makeAudioOutput;
        // Whether audio.outputHostApi chooses the host API.
        bool outputHostApiSetting = backends::PortAudioOutput::onWindows;
        // Unset: QMediaDevices, or with playbackAudio off none.
        std::function<std::unique_ptr<application::AudioDevicesPort>()> makeAudioDevices;
        UpdateFeed updateFeed = UpdateFeed::github();
        // Empty: HIKARI_VERSION.
        QString appVersion;
        // The default installs nothing.
        UpdateEnvironment updateEnvironment;
        // Empty: "Catalog" beside the settings file, or a temporary directory without one.
        QString catalogDir;
        // Unset: no spell checker (only bracket errors are marked; SPELLCHECKER_ON is left
        // as it is).
        application::SpellingBackendLoader spellingBackend = backends::hunspellSpellingLoader();
        // Holds UserDic.udic and added dictionaries; empty: beside the settings file (none,
        // and no spell checker, without one).
        QString dictionaryDir;
        // Searched after dictionaryDir.
        QString bundledDictionaryDir;
        // Empty: next to the application, else the build tree's.
        QString harperLs;
        // harper-ls's own files; empty: "Harper" beside the settings file, or a temporary
        // directory without one.
        QString grammarDir;
        // MeCab's UniDic; empty: Dictionary/unidic-lite beside the executable, else the
        // build tree's.
        QString readingDictionaryDir;
        // Empty: "AudioCache" beside the settings file, or a temporary directory without one.
        QString audioCacheDir;
        // FFMS2 index files; empty: "Indices" beside the settings file, or none without one.
        QString indexDir;
        // Empty: beside the settings file, or a temporary directory without one.
        QString fontCatalogDir;
        // Unset: backends::desktop::selectInFolder.
        std::function<void(const QString &path)> revealInFolder;
        // HikariSub legacy's autosave folder, read only; empty: "Subs" in the first legacy
        // root that has one, or none without a settings file.
        QString legacyAutosaveDir;
        // Empty: none (Open user folder does nothing).
        QString userFolder;
        UserFolder::MoveReport userFolderMove;
        UserFolder::SeedReport userFolderSeed;
        // Empty: beside the settings file.
        QString presetsDir;
        // The default has no key: nothing is queued or sent.
        Analytics::Config analytics;
        // Empty paths keep nothing on disk.
        CrashReports::Config crashReports;
    };
    explicit Application(QObject *parent = nullptr);
    explicit Application(Options options, QObject *parent = nullptr);
    ~Application() override;

    // The first becomes the editing target; an untouched empty Untitled tab is replaced.
    Q_INVOKABLE bool openFile(const QString &path);
    Q_INVOKABLE bool openReference(const QString &path);
    // No review: callers review first.
    Q_INVOKABLE bool closeEditingTarget();

    // then: "close", "new" or "quit". One row per Document with unsaved work: {id,
    // title, untitled, folder (native; "" when untitled)}; none: call finishClose().
    Q_INVOKABLE QVariantList reviewClose(const QString &then);
    // The `then` of the review in progress; "" without one.
    Q_INVOKABLE QString closeKind() const { return m_closeThen; }
    // choices: {id, save, path (Save As destination for an Untitled one)}. A Document
    // edited after its choice stays open; closeFinished reports the result.
    Q_INVOKABLE void resolveClose(const QVariantList &choices);
    Q_INVOKABLE void finishClose();
    Q_INVOKABLE void cancelClose();
    // A failed open changes nothing. Returns {ok, problem, rows}: rows as reviewClose's;
    // ok without rows: call finishClose().
    Q_INVOKABLE QVariantMap reviewOpen(const QString &path);
    Q_INVOKABLE QVariantMap reviewOpenUrl(const QUrl &url) { return reviewOpen(url.toLocalFile()); }
    // One file: {kind: "subtitles" (reviewOpen it), "video" (openVideoFile it),
    // "session", or "" when handled}. Several: {kind: "files", rows}, rows as
    // reviewClose's when the target has unsaved work (then finishClose() or cancelClose()).
    Q_INVOKABLE QVariantMap openDropped(const QList<QUrl> &urls);
    // Opens in new tabs without review. Returns {kind: "video", path} when a video waits
    // for openSingleVideo, else as openDropped. Call only while forwardedOpenWaits() is false.
    QVariantMap openForwarded(const QStringList &files);
    // True while a review or a startup session question is open.
    bool forwardedOpenWaits() const;
    bool quitting() const { return m_quitApproved || m_closeThen == QLatin1String("quit"); }
    int quitReviews() const { return m_quitReviews; }
    // Returns {subtitles}: same-named subtitles beside the video to offer ("" when none,
    // or the tab has them); then openVideo(path), or on "Yes" reviewOpenWithVideo.
    Q_INVOKABLE QVariantMap openVideoFile(const QString &path);
    Q_INVOKABLE QVariantMap openVideoFileUrl(const QUrl &url) { return openVideoFile(url.toLocalFile()); }
    Q_INVOKABLE void openVideo(const QString &path);
    Q_INVOKABLE QVariantMap reviewOpenWithVideo(const QString &subtitles, const QString &video,
                                                bool fullscreen = false);
    // With Ctrl alone held, shows the file in its folder instead and returns true.
    Q_INVOKABLE bool revealRecent(const QString &path);
    Q_INVOKABLE void showInFolder(const QString &path);
    Q_INVOKABLE void copyText(const QString &text);
    // answer: 0 load associated, 1 load from directory, 2 no; applyToAll answers the
    // rest of the same opening.
    void answerAssociation(int answer, bool applyToAll);
    // {path, label} rows; missing local files are left out.
    Q_INVOKABLE QVariantList recentSubtitles();
    // Most recent first: {path, name, folder, opened (invalid for an entry imported
    // from HikariSub legacy), missing}.
    Q_INVOKABLE QVariantList homeRecent();
    Q_INVOKABLE void removeRecent(const QString &path);
    Q_INVOKABLE void removeMissingRecent();
    std::vector<std::string> recentEntries() const { return m_recent.entries(); }
    // "modified" when the target's file is newer than when last read or written;
    // "removed" once when it is gone (the Document becomes unsaved).
    Q_INVOKABLE QString externalChange();
    // A failed or stale reload keeps the Document as it is.
    Q_INVOKABLE bool reloadTarget();
    // id 0: the editing target. Its next save writes over the change on disk.
    Q_INVOKABLE bool acceptFileOnDisk(qulonglong id);
    // Opens the file on disk as the read-only reference and compares the two; false
    // when it could not be read.
    Q_INVOKABLE bool compareWithFileOnDisk();

    Q_INVOKABLE bool saveAs(const QString &path);
    Q_INVOKABLE bool saveAsUrl(const QUrl &url) { return saveAs(url.toLocalFile()); }
    Q_INVOKABLE QString localPath(const QUrl &url) const { return url.toLocalFile(); }
    Q_INVOKABLE QUrl fileUrl(const QString &path) const { return QUrl::fromLocalFile(path); }
    Q_INVOKABLE QString folderOf(const QUrl &file) const
    {
        if (!file.isLocalFile())
            return {};
        const QString folder = QFileInfo(file.toLocalFile()).absolutePath();
        return QFileInfo(folder).isDir() ? QUrl::fromLocalFile(folder).toString() : QString();
    }
    Q_INVOKABLE bool targetUntitled() const;
    // "dialog" when Save must ask for a file, "readonly" for a read-only file, "" to
    // save in place.
    Q_INVOKABLE QString saveRoute() const;
    // {folder, file, filter, extension}.
    Q_INVOKABLE QVariantMap saveDialogValues() const;
    // Adds the format's extension when missing. "readonly" asks again; "" when the
    // save started.
    Q_INVOKABLE QString saveChosen(const QUrl &file);
    // Saves in-place tabs now; returns the others, in tab order, as {id, route, title}.
    Q_INVOKABLE QVariantList saveAll();
    // As saveRoute, saveDialogValues, saveChosen and Save for Document id (0: the
    // editing target).
    Q_INVOKABLE QString saveRouteFor(qulonglong id) const;
    Q_INVOKABLE QVariantMap saveDialogValuesFor(qulonglong id) const;
    Q_INVOKABLE QString saveChosenFor(qulonglong id, const QUrl &file);
    Q_INVOKABLE bool saveDocument(qulonglong id);
    Q_INVOKABLE bool turnOffTranslationMode();
    // translationHint: why the mode is unavailable (a PlainText Document); else empty.
    Q_PROPERTY(bool translatorModeAvailable READ translatorModeAvailable NOTIFY tabsChanged)
    Q_PROPERTY(QString translationHint READ translationHint NOTIFY tabsChanged)
    Q_PROPERTY(bool directShowPlayback READ directShowPlayback NOTIFY playbackPlayerChanged)
    bool translatorModeAvailable() const;
    QString translationHint() const;
    Q_INVOKABLE bool turnOnTranslationMode();
    Q_INVOKABLE void setMoveTags(bool on);
    // The target can take Translation mode, has it off and was not declined this session.
    Q_INVOKABLE bool translationQuestionDue() const;
    Q_INVOKABLE void declineTranslationMode();
    Q_PROPERTY(bool saveWithVideoName READ saveWithVideoName WRITE setSaveWithVideoName NOTIFY saveWithVideoNameChanged)
    bool saveWithVideoName() const { return m_settings->boolean("subtitles.saveWithVideoName"); }
    void setSaveWithVideoName(bool on);
    Q_INVOKABLE void openWebsite();
    // kind: "panel" or "window" (guide_links.h); anything else opens the guide's home.
    Q_INVOKABLE void openUserGuide(const QString &kind, const QString &id);
    // Native separators; empty without one.
    Q_PROPERTY(QString userFolder READ userFolder CONSTANT)
    QString userFolder() const;
    Q_INVOKABLE void openUserFolder();
    Q_INVOKABLE void openWhatIsSent();
    Q_PROPERTY(bool quitApproved READ quitApproved NOTIFY quitApprovedChanged)
    // A find and replace question waits for its answer.
    Q_PROPERTY(bool findBusy READ findBusy NOTIFY findBusyChanged)
    bool quitApproved() const { return m_quitApproved; }

    // A refused move of the active Line (its draft) leaves everything as it was.
    Q_INVOKABLE void selectLine(qulonglong id);
    Q_INVOKABLE void extendSelection(int rows);
    Q_INVOKABLE void clickLine(qulonglong id, int modifiers, bool endColumn = false, bool doubleClick = false);
    Q_INVOKABLE void dragSelection(qulonglong id);
    // first: the first Line of a run of generated Lines.
    Q_INVOKABLE void toggleFold(qulonglong first);
    Q_INVOKABLE void selectAllLines();
    // Only the active Line moves (the Grid's active Line was hidden).
    Q_INVOKABLE void moveActiveLine(qulonglong id);

    // One undo step each. timing: "" plain, "video" (at the video frame's time, 4 s
    // long) or "frame" (copies of the selection timed to the video frame).
    Q_INVOKABLE bool insertLine(bool before, const QString &timing = {});
    Q_INVOKABLE bool duplicateLines();
    Q_INVOKABLE bool deleteLines();
    Q_INVOKABLE bool deleteText();
    Q_INVOKABLE void toggleHideTags();
    // kind: "join", "previous", "next", "first", "last".
    Q_INVOKABLE bool joinLines(const QString &kind);
    Q_INVOKABLE bool swapLines();
    Q_INVOKABLE bool makeContinuous(bool withPrevious);
    Q_INVOKABLE bool setNewFps(const QString &oldFps, const QString &newFps);
    Q_INVOKABLE bool setFpsFromVideo();
    Q_INVOKABLE bool filterLines();
    Q_INVOKABLE bool hideSelectedLines();
    Q_INVOKABLE bool turnOffFiltering();
    // documentRow: the Document row the +/- mark's hidden block follows.
    Q_INVOKABLE bool toggleHiddenBlock(int documentRow);
    Q_INVOKABLE QStringList styleNames() const;
    // Lines using each style, by name; the target's styles are there with 0.
    Q_INVOKABLE QVariantMap styleLineCounts() const;
    // The target's shown Lines using style become the selection; false when none.
    Q_INVOKABLE bool selectLinesWithStyle(const QString &style);
    // {key, title, original, written, generations: [{generation, written}]}.
    Q_INVOKABLE QVariantList recoveryBundles() const;
    // Opens a new unsaved copy with its draft pending; the bundle stays until dismissed.
    Q_INVOKABLE bool recoverBundle(const QString &key, qulonglong generation);
    Q_INVOKABLE void dismissBundle(const QString &key);
    // All when date is invalid; a running session's work is never touched. Returns the
    // count removed.
    Q_INVOKABLE int removeAutosavesOlderThan(const QDate &date);
    bool autosaveNow();
    bool autosavePending(application::DocumentId document) const;
    // kind: "videoTime" or "frames".
    Q_INVOKABLE bool splitLines(const QString &kind);
    Q_INVOKABLE bool canPasteTranslation() const;
    Q_INVOKABLE QUrl targetFolder() const;
    Q_INVOKABLE bool pasteTranslationFile(const QUrl &file);
    Q_INVOKABLE bool canShiftTranslation() const;
    // Empty when shifted, else the message; frames need an exact (indexed) timebase.
    Q_INVOKABLE QString shiftTimes();
    // 0: none. Ids are never reused.
    Q_INVOKABLE qulonglong editingTargetId() const;
    Q_INVOKABLE bool exactTimebase() const;
    // Empty: loaded (or kept until a video opens); else the message.
    Q_INVOKABLE QString openKeyframes(const QUrl &file);
    // {fps, fpsFromVideo, style, newEndTimes, timePerCharacter, prefix, resolutionWidth,
    // resolutionHeight}.
    Q_INVOKABLE QVariantMap conversionOptions() const;
    Q_INVOKABLE void setConversionOptions(const QVariantMap &options);
    // "ass", "srt", "mdvd", "mpl2", "tmp"; none in translation mode.
    Q_INVOKABLE QStringList conversionTargets() const;
    Q_INVOKABLE QStringList conversionStyles(const QString &catalog) const;
    Q_INVOKABLE QString activeLineStyle() const;
    // {ok, problem, fpsInvalid, losses, kinds ("warning" removed, else "info")};
    // acceptConversion applies it unless the Document or the options changed since.
    Q_INVOKABLE QVariantMap previewConversion(const QString &target);
    Q_INVOKABLE bool acceptConversion();
    // {subsWidth, subsHeight, videoWidth, videoHeight, hasVideo} (without a video the
    // video size is the subtitles'); empty for a Document that is not ASS.
    Q_INVOKABLE QVariantMap resampleValues() const;
    Q_INVOKABLE bool resample(int subsWidth, int subsHeight, int width, int height, bool stretch);
    // option: 0 only the resolution, 1 resample, 2 resample with stretch.
    Q_INVOKABLE bool matchVideoResolution(int option);
    Q_PROPERTY(bool includesCsri READ includesCsri CONSTANT)
    Q_PROPERTY(bool includesVsfilter READ includesVsfilter CONSTANT)
    static bool includesCsri();
    static bool includesVsfilter();
    Q_PROPERTY(bool askForBadResolution READ askForBadResolution WRITE setAskForBadResolution NOTIFY askForBadResolutionChanged)
    bool askForBadResolution() const { return !m_settings->boolean("video.dontAskForBadResolution"); }
    void setAskForBadResolution(bool on);
    // {find, with, matchCase, regex, field, dialogues, comments, mode, action} and the
    // recent searches.
    Q_INVOKABLE QVariantMap selectLinesSettings() const;
    // From now on the dialog's options and recent searches survive "Set default".
    Q_INVOKABLE QVariantMap openSelectLines();
    // Writes the options, not the recent searches (only a selection run writes those).
    Q_INVOKABLE void saveSelectLinesSettings(const QVariantMap &settings);
    // Returns the message to show.
    Q_INVOKABLE QString selectLines(const QVariantMap &settings, bool allTabs);
    // The chosen styles as an anchored pattern.
    Q_INVOKABLE QString selectStylesPattern(const QStringList &styles) const;
    // Rows {description, find, replace, options, checked}; read from Rules.txt beside the
    // INI file (the shipped rules without one), written back at exit unless empty.
    Q_INVOKABLE QVariantList misspellRules();
    // Refused while the results window is shown.
    Q_INVOKABLE bool addMisspellRule(const QVariantMap &rule);
    Q_INVOKABLE bool editMisspellRule(int index, const QVariantMap &rule);
    Q_INVOKABLE bool removeMisspellRule(int index);
    Q_INVOKABLE void checkMisspellRule(int index, bool checked);
    Q_INVOKABLE void setMisspellResultsShown(bool shown) { m_misspellResultsShown = shown; }
    // scope: {lines (0-3), styles}. A {header: true, text: path} row before each
    // Document's finds {header: false, find (its index), line, text, position, length}.
    Q_INVOKABLE QVariantList findMisspells(const QVariantMap &scope, bool allTabs);
    // One step per Document.
    Q_INVOKABLE void replaceMisspells(const QVariantMap &scope, bool allTabs);
    Q_INVOKABLE void replaceMisspellFinds(const QVariantList &finds);
    Q_INVOKABLE void showMisspellFind(int find);
    Q_PROPERTY(bool spellingOn READ spellingOn WRITE setSpellingOn NOTIFY spellingChanged)
    Q_PROPERTY(QString dictionaryLanguage READ dictionaryLanguage NOTIFY spellingChanged)
    Q_PROPERTY(bool suggestionsOnDoubleClick READ suggestionsOnDoubleClick WRITE setSuggestionsOnDoubleClick NOTIFY spellingChanged)
    bool spellingOn() const;
    void setSpellingOn(bool on);
    QString dictionaryLanguage() const;
    // Restarts the spell checker and clears its marks, also for the current language.
    void setDictionaryLanguage(const QString &symbol);
    bool suggestionsOnDoubleClick() const;
    void setSuggestionsOnDoubleClick(bool on);
    // 1 (Translated) in translation mode, else 0.
    Q_INVOKABLE int spellingRole() const;
    // Flat [start, end) pairs of the field's text; none while spelling is off.
    Q_INVOKABLE QVariantList editorSpellingMarks(int role);
    // {word, start, end, suggestions}; empty when none.
    Q_INVOKABLE QVariantMap editorMisspellAt(int role, int position);
    Q_INVOKABLE bool replaceEditorMisspell(int role, int position, const QString &replacement);
    Q_INVOKABLE bool addEditorWord(const QString &word);
    // options: {ignoreComments, ignoreUpperCase}. Each returns {found, available, line,
    // before, after, word, suggestions, replacement, changed, problem, unchanged,
    // restarted}: found false: no errors; available false: no dictionary; line: the
    // Grid's number; unchanged: the action did nothing; restarted: the Line changed since
    // the word was shown, so the walk started again from it.
    Q_INVOKABLE QVariantMap openSpellChecker(const QVariantMap &options);
    Q_INVOKABLE QVariantMap spellCheckerActivated(const QVariantMap &options);
    Q_INVOKABLE QVariantMap spellCheckerReplace(const QString &replacement, const QVariantMap &options);
    Q_INVOKABLE QVariantMap spellCheckerReplaceAll(const QString &misspell, const QString &replacement,
                                                   const QVariantMap &options);
    Q_INVOKABLE QVariantMap spellCheckerIgnore(const QVariantMap &options);
    Q_INVOKABLE QVariantMap spellCheckerIgnoreAll(const QString &word, const QVariantMap &options);
    Q_INVOKABLE QVariantMap spellCheckerAddWord(const QString &word, const QVariantMap &options);
    Q_INVOKABLE void closeSpellChecker();
    Q_INVOKABLE QStringList addedDictionaryWords() const;
    Q_INVOKABLE bool removeDictionaryWords(const QStringList &words);
    Q_PROPERTY(bool grammarOn READ grammarOn NOTIFY grammarChanged)
    // The pinned harper-ls release.
    Q_PROPERTY(QString grammarVersion READ grammarVersion CONSTANT)
    bool grammarOn() const;
    QString grammarVersion() const;
    // Flat [start, end) pairs while the field's text is the one checked (none for a
    // comment, or while grammar check is off).
    Q_INVOKABLE QVariantList editorGrammarMarks(int role);
    // {rule, group (its index), groupName, message, suggestions, word (a spelling
    // finding), text, start, end}.
    Q_INVOKABLE QVariantMap editorGrammarAt(int role, int position);
    // Tags inside the finding's text are kept in order.
    Q_INVOKABLE bool replaceEditorGrammar(int role, int position, const QString &replacement);
    // The switched-off group's index (undo: setGrammarGroup(index, true)); -1 for a
    // rule Harper doesn't list.
    Q_INVOKABLE int ignoreGrammarRule(const QString &rule);
    Q_INVOKABLE void setGrammarGroup(int group, bool on);
    // {id, name, description, rules}.
    Q_INVOKABLE QVariantList grammarGroups() const;
    Q_INVOKABLE bool addGrammarWord(const QString &word);
    // Each returns {on, walking, done, checked, total, count, index (1-based among
    // count), found, line, groupName, message, before, text, after, suggestions,
    // replacement, word, problem}; found false with done: no findings.
    // grammarWalkIgnoreRule's also has ignoredGroup. Only dialogue Lines are checked.
    Q_INVOKABLE QVariantMap startGrammarWalk();
    Q_INVOKABLE QVariantMap grammarWalkState();
    Q_INVOKABLE QVariantMap cancelGrammarWalk();
    Q_INVOKABLE QVariantMap grammarWalkReplace(const QString &replacement);
    Q_INVOKABLE QVariantMap grammarWalkIgnore();
    Q_INVOKABLE QVariantMap grammarWalkIgnoreRule();
    Q_INVOKABLE QVariantMap grammarWalkAddWord();
    Q_INVOKABLE void closeGrammarWalk();
    // {rows, unlisted}: a row for each of the first core::kListedProblems ({document,
    // line (ids), number (the Grid's 1-based row), check, detail}), unlisted the rest.
    Q_INVOKABLE QVariantMap checkKaraoke() const;
    // False when the target changed, lacks the Line, or the editor refuses to leave it.
    Q_INVOKABLE bool showKaraokeLine(qulonglong document, qulonglong line);
    // A tab: {tab (0 Find, 1 Find and replace, 2 Find in subtitles), find, replace,
    // styles, filters, folder, field, lines, matchCase, regex, startOfText, endOfText,
    // includeComments, skipTags, skipText, subfolders, hiddenFolders} plus the recent
    // lists {finds, replacements, filterList, paths}. openFindReplace reads the recent
    // lists; switchFindReplaceTab saves `settings` and returns tab `tab`.
    Q_INVOKABLE QVariantMap openFindReplace(int tab);
    Q_INVOKABLE QVariantMap findReplaceSettings(int tab) const;
    Q_INVOKABLE QVariantMap switchFindReplaceTab(const QVariantMap &settings, int tab);
    Q_INVOKABLE void saveFindReplaceSettings(const QVariantMap &settings);
    // action: "find", "findAllCurrent", "findAllTabs", "replace", "replaceAll",
    // "replaceAllTabs", "findInFiles" or "replaceInFiles"; findFinished ends it.
    // Ignored while findBusy.
    Q_INVOKABLE void runFindReplace(const QString &action, const QVariantMap &settings);
    Q_INVOKABLE void findNext();
    // answer: 0 Ok, 1 Yes, 2 No, 3 Cancel.
    Q_INVOKABLE void answerFindQuestion(int id, int answer);
    bool findBusy() const;
    // The text selected in the Line editor, or find as it is.
    Q_INVOKABLE QString findReplaceActivated(const QString &find);
    Q_INVOKABLE void resetFindReplace();
    // Rows {header, text, line, before, match, after, checked, visible}.
    Q_INVOKABLE QVariantList findResults() const;
    Q_INVOKABLE bool findResultsShown() const;
    Q_INVOKABLE bool canReplaceFindResults() const;
    Q_INVOKABLE void checkFindResults(bool check);
    Q_INVOKABLE void toggleFindResult(int row);
    Q_INVOKABLE void toggleFindGroup(int row);
    Q_INVOKABLE void showFindResult(int row);
    Q_INVOKABLE void replaceFindResults(const QString &replacement);
    void setFindQuestionHandler(std::function<int(int kind, const QString &text)> handler);
    // Default: ReplaceBackup beside the settings file, none without one.
    void setReplaceBackupFolder(const QString &folder) { m_replaceBackup = folder; }
    // 0: this machine's processor count.
    void setFindProcessorCount(int count) { m_findProcessors = count; }
    Q_INVOKABLE QVariantMap scriptProperties();
    Q_INVOKABLE bool applyScriptProperties(const QVariantMap &values, const QVariantMap &edits, bool linkResolutions);
    Q_INVOKABLE bool shiftTranslation(int mode);
    Q_INVOKABLE bool makeGroups();
    // description: the id of the group's description Line.
    Q_INVOKABLE bool toggleGroup(qulonglong description);
    Q_INVOKABLE bool renameGroup(qulonglong description, const QString &text);
    Q_INVOKABLE bool removeGroup(qulonglong description);
    Q_INVOKABLE bool selectGroup(qulonglong description);
    Q_INVOKABLE bool addLinesToGroup(qulonglong description);
    Q_INVOKABLE bool copyGroup(qulonglong description);
    Q_INVOKABLE QString groupTitle(qulonglong description) const;
    // key: "start", "end", "style", "actor", "effect" or "layer"; text keys use
    // the locale's collation.
    Q_INVOKABLE bool sortLines(const QString &key, bool selectedOnly);
    Q_INVOKABLE bool copyLines();
    Q_INVOKABLE bool cutLines();
    Q_INVOKABLE bool pasteLines();
    // {label, bit, checked (as last chosen)}; copy and paste offer different lists.
    Q_INVOKABLE QVariantList columnChoices(bool paste);
    // shownOnly: only those the filter shows.
    Q_INVOKABLE int selectedLineCount(bool shownOnly = false) const;
    // columns: bits OR'ed; the choice is remembered.
    Q_INVOKABLE bool copyColumns(int columns);
    Q_INVOKABLE bool pasteColumns(int columns);
    void setChangeActiveOnSelection(bool on) { m_settings->set("grid.changeActiveOnSelection", on); }

    Q_PROPERTY(hikari::ui::SettingsStore *settings READ settingsStore CONSTANT)
    ui::SettingsStore *settingsStore() const { return m_settings.get(); }
    // The subtitles the video overlay was given last (the draft while editing live).
    const std::vector<std::byte> &videoScript() const { return m_videoScript; }
    application::AudioCacheSettings audioSettings() const { return m_audioSettings(); }
    // {values (by setting id), languages, dictionaries, catalogs, styles, warnings}.
    // Opening fixes an FFMS2 seeking value outside 0-3 to 2 and loads the conversion
    // catalog.
    Q_INVOKABLE QVariantMap openSettingsDialog();
    // Writes only controls whose value differs from the stored one. resetAll: every
    // setting but the appearance, hotkeys and recent files goes back to its default first.
    Q_INVOKABLE void applySettings(const QVariantMap &values, bool resetAll = false);
    // Due after the first start; Finish, Skip setup, Close and Esc mark it done.
    Q_INVOKABLE bool gettingStartedDue() const;
    // As openSettingsDialog(), plus legacy: whether a HikariSub legacy installation was
    // found.
    Q_INVOKABLE QVariantMap openGettingStarted();
    // The values as the import's chosen rows would leave them; nothing is written.
    Q_INVOKABLE QVariantMap gettingStartedImported(const QVariantMap &values);
    // index: into the opening's languages; neither call saves anything.
    Q_INVOKABLE void previewLanguage(int index);
    Q_INVOKABLE void endLanguagePreview();
    // import: the chosen rows first (in effect at the next start), then the pages'
    // values over them. False, with nothing written, when the import cannot be staged.
    Q_INVOKABLE bool finishGettingStarted(const QVariantMap &values, bool import);
    Q_INVOKABLE void skipGettingStarted();
    // [start, end) pairs; dictionary: an index into the opening's list; none while on
    // is false.
    Q_INVOKABLE QVariantList sampleSpellingMarks(const QString &text, bool on, int dictionary);
    // renderer: an index into the opening's list. Returns a PNG data URL.
    Q_INVOKABLE QString sampleFrame(int renderer, int width, int height);
    // device: an index into the opening's list.
    Q_INVOKABLE void playTestSound(int device);
    // The controls' state at the defaults; nothing is stored until OK or Apply.
    Q_INVOKABLE QVariantMap resetSettings(const QVariantMap &values);
    // page: an OptionsPage id ("general", "appearance", ...); stored only at OK or Apply.
    Q_INVOKABLE QVariantMap resetSettingsPage(const QVariantMap &values, const QString &page);
    // The conversion catalog's Styles.
    Q_INVOKABLE QStringList settingsStyles() const;
    // Loads the catalog; returns {values, styles}.
    Q_INVOKABLE QVariantMap chooseSettingsCatalog(const QVariantMap &values, int index);
    // Native separators.
    Q_INVOKABLE QString settingsFolderPath(const QUrl &url) const;

    Q_INVOKABLE void openAudioFromVideo();
    // {path, label} rows; missing local files are pruned first.
    Q_INVOKABLE QVariantList recentAudio();
    // The video's folder, else the latest recent video's.
    Q_INVOKABLE QUrl audioDialogFolder() const;
    // {path, label} rows; missing local files are pruned first.
    Q_INVOKABLE QVariantList recentVideos();
    Q_INVOKABLE QVariantList recentKeyframes();
    Q_INVOKABLE QString openKeyframesFile(const QString &path);
    Q_INVOKABLE QUrl videoDialogFolder() const;
    Q_INVOKABLE QUrl keyframesDialogFolder() const;
    Q_INVOKABLE QVariantMap dummyVideoDefaults() const;
    // The error to log, else empty.
    Q_INVOKABLE QString openDummyVideo(const QVariantMap &values);
    Q_INVOKABLE bool nextVideoFile(bool next);
    // -1: a rate it refuses.
    Q_INVOKABLE int dummyVideoFrames(const QString &fps, int durationMs) const;
    // "" when it would open none.
    Q_INVOKABLE QString nextVideoFileName(bool next) const;
    // "" when the walk would open none.
    Q_INVOKABLE QString nextVideoFilePath(bool next) const;
    // Walks only while it still leads to expected; else false, nothing done.
    Q_INVOKABLE bool nextVideoFileIfStill(bool next, const QString &expected);
    // Centres the audio box on the video's time (0 without video); mark: the mark too.
    Q_INVOKABLE void setAudioFromVideo(bool mark);
    Q_INVOKABLE bool setTimeFromVideo(bool end);
    Q_INVOKABLE bool selectLineFromVideo();
    // Commits the Line editor's change first; acts only with
    // GRID_SET_VISIBLE_LINE_AFTER_FULL_SCREEN on and a Document with a file.
    Q_INVOKABLE bool selectVisibleLineAfterFullScreen();
    // Pauses, selects the Line at the video's time (a Document with a file) and leaves
    // fullscreen.
    Q_INVOKABLE bool openEditorFromFullScreen();
    Q_INVOKABLE bool selectLinesVisibleOnVideo();
    Q_INVOKABLE bool snapToKeyframe(bool start);
    Q_INVOKABLE bool commitAndAdvance();

    ui::ShellController &shell() { return *m_shell; }
    ui::LineEditorController &editor() { return *m_editor; }
    ui::TemplateEditorController &templateEditor() { return *m_templateEditor; }
    ui::VideoController &video() { return *m_video; }
    backends::QtGeneralPlayer &generalPlayer() { return *m_generalPlayer; }
    // The general player, or on Windows DirectShow when video.playbackPlayer is 1 (never
    // a fallback).
    application::GeneralPlayerPort &activePlayer() { return *m_activePlayer; }
    bool directShowPlayback() const;
#ifdef _WIN32
    backends::DirectShowPlayer *directShowPlayer() { return m_directShow.get(); } // tests
#endif
    // While DirectShow plays: its graph's filters in order, {name, enabled (has property
    // pages)}; empty otherwise.
    Q_INVOKABLE QVariantList playerFilters() const;
    // Modal, owned by item's window, at (x, y) in item.
    Q_INVOKABLE bool showPlayerFilterProperties(const QString &name, QObject *item, qreal x, qreal y);
    ui::AudioController &audio() { return *m_audio; }
    AutomationShell &automation() { return *m_automation; }
    AutomationHotkeysController &automationHotkeys() { return *m_automationHotkeys; }
    SettingsImportController &settingsImport() { return *m_settingsImport; }
    HotkeysController &hotkeys() { return *m_hotkeys; }
    UpdateChecker &updates() { return *m_updates; }
    Analytics &analytics() { return *m_analytics; }
    CrashReports &crashReports() { return *m_crashReports; }
    WhatsNew &whatsNew() { return *m_whatsNew; }
    StyleManagerController &styleManager() { return *m_styleManager; }
    KanjiTimerController &kanjiTimer() { return *m_kanjiTimer; }
    FontCollectorController &fontCollector() { return *m_fontCollector; }
    FontCatalogsController &fontCatalogs() { return *m_fontCatalogs; }
    MatroskaController &matroska() { return *m_matroska; }
    backends::SubtitleRendererSelection &subtitleRenderer() { return m_renderer; }
    static QString subtitleRendererLabel(const std::string &name);
    static QString outputDeviceLabel(const application::OutputDeviceChoice &choice);
    application::OutputRoute audioRoute() const { return m_audioRouting->route(); }
    // {label, checked, id, name} entries, the stored choice checked; choosing one stores
    // it as Settings' OK does.
    Q_INVOKABLE QVariantList audioOutputMenu() const;
    Q_INVOKABLE void chooseAudioOutput(const QString &id, const QString &name);
    Q_PROPERTY(QString audioOutputName READ audioOutputName NOTIFY audioOutputChanged)
    QString audioOutputName() const;
    backends::AudioBoxPlayer &audioPlayer() { return *m_audioPlayer; }
    ui::LogController &log() { return *m_log; }
    Localisation &localisation() { return *m_localisation; }
    ui::TagButtonsController &tagButtons() { return *m_tagButtons; }
    ui::ColourPickerController &colourPicker() { return *m_colourPicker; }
    Q_INVOKABLE void colourPickerOpened();
    ui::WorkspaceLayoutController &workspaceLayout() { return *m_workspaceLayout; }
    ui::ShiftTimesController &shiftTimesSettings() { return *m_shiftTimes; }
    ui::GridFilterController &gridFilter() { return *m_gridFilter; }
    ui::VisualToolsController &visualTools() { return *m_visualTools; }
    ui::VideoViewController &videoView() { return *m_videoView; }
    ui::StatusBarController &statusBar() { return *m_statusBar; }
    ui::VideoFullscreenController &videoFullscreen() { return *m_videoFullscreen; }
    application::DocumentFiles &files() { return *m_files; }
    application::Workspace &workspace() { return m_workspace; }
    QVariantMap qmlProperties();
    // Waits for running writes and delivers their results.
    void waitForWrites();

    // Rows {id, label ("<step>*name" when modified, at most TAB_TEXT_MAX_CHARS
    // characters), title, current, tip}; the protected reference is not a tab.
    Q_PROPERTY(QVariantList tabs READ tabs NOTIFY tabsChanged)
    Q_PROPERTY(int currentTab READ currentTab NOTIFY tabsChanged)
    Q_PROPERTY(bool home READ home NOTIFY tabsChanged)
    // 0 nothing, 1 ask, 2 load at start.
    Q_PROPERTY(int sessionRestore READ sessionRestore WRITE setSessionRestore NOTIFY sessionRestoreChanged)
    // {row, tab, title, kind ("subtitles", "video", "audio", "keyframes"), path}.
    Q_PROPERTY(QVariantList unresolvedRestores READ unresolvedRestores NOTIFY unresolvedRestoresChanged)
    QVariantList tabs() const;
    bool home() const { return m_workspace.tabs().empty() && !m_workspace.reference(); }
    int currentTab() const;
    // A tab row's id; 0 without one.
    Q_INVOKABLE qulonglong editingDocument() const;
    int sessionRestore() const;
    void setSessionRestore(int value);
    QVariantList unresolvedRestores() const;
    // fromTabBar also writes the session.
    Q_INVOKABLE void addPage(bool fromTabBar = false);
    // step: +1 or -1, wrapping; nothing with fewer than two tabs.
    Q_INVOKABLE void changeTab(int step);
    Q_INVOKABLE void selectTab(int index);
    // Rows as reviewClose's; with none, finishClose() closes the tab.
    Q_INVOKABLE QVariantList reviewCloseTab(int index);
    // Empty file: the last session. Returns {ok, problem, rows}, rows as reviewClose's
    // for every open Document; ok without rows: finishClose() replaces the tabs. UNC
    // hosts not all in allowedHosts: {ok: false, network: [hosts]}.
    Q_INVOKABLE QVariantMap reviewSession(const QUrl &file = {}, const QStringList &allowedHosts = {});
    // "readonly", "failed", "exists" (a .kls name's file is there and not replace)
    // or "". Written as .hksession unless the name ends in .kls.
    Q_INVOKABLE QString saveSessionTo(const QUrl &file, bool replace = false);
    Q_INVOKABLE QUrl sessionFolder() const;
    // "load", "ask", "crash" (the last session was not written at close) or "".
    Q_INVOKABLE QString startupSession() const;
    Q_INVOKABLE void startupSessionDecided() { m_startupSessionPending = false; }
    // {names: each Document's file name, or its video's for a tab without subtitles}.
    Q_INVOKABLE QVariantMap lastSessionSummary() const;
    Q_INVOKABLE bool lastSessionCrashed() const;
    // "crash" (the last session did not end with "[Close session]", or autosaved work
    // was left), "ask" (On start: Ask) or "".
    Q_INVOKABLE QString startupOffer() const;
    // About the last session as it was at start: names as lastSessionSummary's, the
    // review as reviewSession's.
    Q_INVOKABLE QStringList startupOfferNames() const;
    Q_INVOKABLE QVariantMap reviewStartupSession();
    Q_INVOKABLE bool startedWithPaths() const { return m_startedWithPaths; }
    void setStartedWithPaths(bool paths) { m_startedWithPaths = paths; }
    // row: an unresolvedRestores row.
    Q_INVOKABLE bool retryRestore(int row);
    Q_INVOKABLE bool relinkRestore(int row, const QUrl &file);
    Q_INVOKABLE void removeRestore(int row);
    // Ignored after tabShown until scrollRestored(), so a model reset's transient 0
    // never overwrites the tab's scroll.
    Q_INVOKABLE void setTargetScroll(int row);
    Q_INVOKABLE void scrollRestored() { m_scrollRestoring = false; }
    // Writes the session with "[Close session]".
    Q_INVOKABLE void endSession();

    // index -1: not on a tab. {tabs: [{title, current}], save (the tab is modified),
    // folders: [{kind: "subtitles"|"video"|"audio"|"keyframes", path}]}.
    Q_INVOKABLE QVariantMap tabMenu(int index);
    // Tab index trades places with the first visible tab and is shown there.
    Q_INVOKABLE void chooseTab(int index, int firstVisible);
    // The two tabs trade places; endTabDrag writes the session once a drag swapped tabs.
    Q_INVOKABLE bool dragTab(int from, int to);
    Q_INVOKABLE void endTabDrag();
    // 0: none.
    Q_INVOKABLE qulonglong tabDocument(int index) const;
    // Rows as reviewClose's for every tab; with none, finishClose() closes them all.
    Q_INVOKABLE QVariantList reviewCloseAll();
    // HikariSub legacy's autosaves, read only: [{name, versions: [{written, time (ISO),
    // file}]}]; filterLegacyAutosaves returns the indices of the names to show.
    Q_INVOKABLE QVariantList legacyAutosaves();
    Q_INVOKABLE QVariantList filterLegacyAutosaves(const QVariantList &files, const QString &query, bool allWords) const;
    // Opens a new unsaved copy; the file is not changed.
    Q_INVOKABLE bool openLegacyAutosave(const QString &file);

    // index -1: not on a tab. {enabled, canCompare, active, times, visible, selections,
    // selectionsEnabled, styles, chosenStyles, styleItems: [{name, checked}]}. Opening
    // adds the checked styles to the chosen ones again.
    Q_INVOKABLE QVariantMap openComparisonMenu(int index);
    // Flips SUBS_COMPARISON_TYPE's bit; returns the new value.
    Q_INVOKABLE int toggleComparisonBit(int bit);
    // Whether "Compare by selected styles" is now checked.
    Q_INVOKABLE bool toggleComparisonStyle(const QString &name, bool checked);
    Q_INVOKABLE bool compareWithTab(int index);
    // Also for a reference opened as no tab (openReference).
    Q_INVOKABLE bool compareWithReference();
    Q_INVOKABLE void turnOffComparison();
    const application::SubtitleComparison &comparison() const { return m_comparison; }

    // More than one tab and no reference shown.
    Q_INVOKABLE bool canShowPreview() const;
    Q_INVOKABLE bool showPreview();
    // One-way linked matching from the editing target's active Line.
    Q_INVOKABLE void setReferenceLinked(bool linked);
    // delta: -1 the previous, +1 the next linked candidate.
    Q_INVOKABLE bool stepReferenceMatch(int delta);
    // In the no-match state.
    Q_INVOKABLE bool showNearestReferenceLine();
    // The reference's Document stays open as a tab.
    Q_INVOKABLE void closeReference();
    // The reference's selection only, never its content.
    Q_INVOKABLE void selectReferenceLine(qulonglong id);
    Q_INVOKABLE void clickReferenceLine(qulonglong id, int modifiers);
    Q_INVOKABLE void extendReferenceSelection(int rows);
    Q_INVOKABLE void dragReferenceSelection(qulonglong id);
    Q_INVOKABLE void selectAllReferenceLines();
    Q_INVOKABLE bool copyReferenceLines();
    Q_INVOKABLE void refuseReferenceChange();
    // Every occurrence of the editing Line in each other Document: [{text, checked}].
    Q_INVOKABLE QVariantList referenceOccurrences();
    Q_INVOKABLE bool chooseReferenceOccurrence(int index);
    // The reference row paired with the editing Grid's shown row, or -1.
    Q_INVOKABLE int comparedReferenceRow(int row) const;
    QString lastSessionPath() const;
    // Writes last-session.json, or the .hksession path.
    bool saveLastSession(bool closing = false, const QString &path = {});
signals:
    void tabsChanged();
    void sessionRestoreChanged();
    void unresolvedRestoresChanged();
    // scroll: the tab's Grid scroll to show again.
    void tabShown(int scroll);
    void sessionRestored(int unresolved);
    void documentWritten(qulonglong document);
    // Also when cancelled: the write published nothing.
    void documentWriteFailed(qulonglong document);
    // A later start handed files over, about to open.
    void filesForwarded();

private:
    std::optional<application::DocumentId> open(const QString &path, bool asReference = false);
    std::optional<application::DocumentId> publish(application::StagedOpen staged, const QString &path, bool asReference);
    static QString openProblem(const QString &path, const application::StageFailure &failure);
    void rememberRecent(const std::string &path, bool opened = true);
    void storeRecent();
    // MUST KILL: returns whether the Document changed since the video last showed it.
    bool showVideoSubtitles();
    void recordFileTime(application::DocumentId document);
    application::EditSession *targetSession() const;
    application::LineVisible shownLines() const;
    application::LineVisible actionLines(const application::EditSession &session) const;
    void rememberColumns(bool paste, int columns);
    void protectReference();
    void refreshViews();
    void refreshVideo();
    void applySubtitlesAt();
    void writeFinished(const application::WriteResult &result);
    void newDocument();
    void setupTabMenu(const Options &options);
    std::function<void(const QString &)> m_revealInFolder;
    QString m_legacyAutosaveDir;
    bool m_tabsSwapped = false;
    struct PendingOffer {
        application::AssociationOffer offer;
        std::uint64_t batch = 0;
    };
    std::map<std::uint64_t, PendingOffer> m_offers;
    std::uint64_t m_openBatch = 0;
    void offerAssociations(application::DocumentId document);
    void showAssociationOffer();
    void loadAssociations(application::DocumentId document, const application::AssociationLoad &load);
    application::TabMediaPaths tabMediaPaths(application::DocumentId document) const;
    QString tabVideo(application::DocumentId document) const;
    std::optional<application::DocumentId> documentOf(qulonglong id) const;
    QString m_videoAfterOpen;
    bool m_videoAfterOpenFullscreen = false;
    bool m_openFromVideo = false;
    void closeAllTabs();
    struct PendingFiles {
        std::vector<application::StagedOpen> subtitles;
        QStringList subtitlePaths;
        QStringList videos;
        int count = -1;
        bool reuseFirst = false;
    };
    std::optional<PendingFiles> m_pendingFiles;
    void applyFiles(PendingFiles files, bool skipFirst);
    bool openInNewTab(application::StagedOpen staged, const QString &path);
    PendingFiles stageFiles(const QStringList &files);
    bool targetUntouched() const;
    application::GridSelection gridSelection() const;
    bool applySelection(application::Selection next);
    void selectFoldedRunsWhole();

    std::unique_ptr<application::FileReadPort> m_reader;
    std::unique_ptr<backends::PlatformFilePort> m_port;
    std::unique_ptr<application::WriteCoordinator> m_writes;
    std::unique_ptr<application::DocumentFiles> m_files;
    application::Workspace m_workspace;
    std::unique_ptr<ui::ShellController> m_shell;
    std::unique_ptr<ui::LineEditorController> m_editor;
    std::optional<application::DocumentId> m_editorDocument;
    std::unique_ptr<backends::FfmsIndexedSource> m_mediaSource;
    std::unique_ptr<application::DummyVideoSource> m_videoSource;
    backends::SubtitleRendererSelection m_renderer;
    std::unique_ptr<ui::VideoController> m_video;
    std::unique_ptr<backends::QtGeneralPlayer> m_generalPlayer;
#ifdef _WIN32
    std::unique_ptr<backends::DirectShowPlayer> m_directShow; // W1, made when chosen
#endif
    application::GeneralPlayerPort *m_activePlayer = nullptr;
    void choosePlayer();
    // MUST KILL: destruction order: members declared after it may keep a reference to it.
    std::unique_ptr<ui::SettingsStore> m_settings;
    std::unique_ptr<SettingsImportStore> m_importStore;
    std::unique_ptr<SettingsImportController> m_settingsImport;
    void settingChanged(const QString &id);
    QStringList m_dictionaryDirs;
    bool m_resettingSettings = false;
    bool m_selectLinesOpened = false;
    application::OptionsLists m_optionsLists;
    std::unique_ptr<AutomationShell> m_automation;
    std::unique_ptr<ui::TemplateEditorController> m_templateEditor;
    bool m_videoShowsTemplate = false;
    std::optional<std::pair<std::int64_t, std::int64_t>> m_templateWindow;
    std::unique_ptr<AutomationHotkeysController> m_automationHotkeys;
    // MUST KILL: must be declared after m_automationHotkeys, which it holds a reference to.
    std::unique_ptr<HotkeysController> m_hotkeys;
    std::unique_ptr<UpdateChecker> m_updates;
    std::unique_ptr<Analytics> m_analytics;
    std::unique_ptr<CrashReports> m_crashReports;
    application::VideoSession::State m_videoUsageState = application::VideoSession::State::Closed;
    application::AudioBox::State m_audioUsageState = application::AudioBox::State::Closed;
    QElapsedTimer m_videoOpenClock;
    QElapsedTimer m_audioOpenClock;
    QString bundledScriptName(const QString &path) const;
    std::unique_ptr<WhatsNew> m_whatsNew;
    std::unique_ptr<StyleManagerController> m_styleManager;
    std::unique_ptr<FontCollectorController> m_fontCollector;
    std::unique_ptr<KanjiTimerController> m_kanjiTimer;
    std::unique_ptr<QTemporaryDir> m_fontCatalogTemp;
    std::unique_ptr<FontCatalogsController> m_fontCatalogs;
    std::unique_ptr<MatroskaController> m_matroska;
    std::map<std::uint64_t, QString> m_importPaths;
    std::set<std::uint64_t> m_lyricsImports;
    bool m_keepVideoOnEnter = false;
    std::optional<application::DocumentId> m_matroskaTarget;
    std::uint64_t m_matroskaRevision = 0;
    QString m_matroskaVideo;
    QString m_matroskaRequested;
    std::optional<core::LineId> m_matroskaDraftLine;
    std::optional<std::u8string> m_matroskaDraft;
    std::uint64_t m_matroskaReloads = 0;
    std::uint64_t m_reloads = 0;
    bool m_matroskaFromHome = false;
    void applyMatroska(application::MatroskaLoaded loaded, bool newTab);
    QString targetVideoPath() const;
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
        float frameMs = 0.f;
        core::Document converted;
    };
    std::unique_ptr<ConversionPlan> m_conversionPlan;
    std::set<std::uint64_t> m_formatChanged;
    QString m_resolutionCheckedVideo;
    void checkResolution();
    QStringList m_selectRecent;
    std::optional<std::vector<application::ReplacerRule>> m_misspellRules;
    std::vector<std::pair<application::DocumentId, application::ReplacerFind>> m_misspellFinds;
    bool m_misspellResultsShown = false;
    std::vector<application::ReplacerRule> &misspellRuleList();
    void saveMisspellRules();
    std::shared_ptr<ui::ListFile> m_rulesFile;
    void destroyDialogs();
    void resetAllSettings();
    class FindHost;
    friend class FindHost;
    std::unique_ptr<FindHost> m_findHost;
    std::unique_ptr<application::FindReplace> m_find;
    bool m_findOpened = false;
    void createFindReplace();
    QString m_replaceBackup;
    std::function<int(int, const QString &)> m_findQuestionHandler;
    int m_findQuestionId = 0;
    std::map<int, std::function<void(application::FindAnswer)>> m_findAnswers;
    std::function<void(bool)> m_findOpenDone;
    int m_findProcessors = 0;
    void endFindOpen(bool opened);
    void saveFindRecent(application::FindReplaceSettings::Tab tab);
    void findFinished();
    QString m_pendingKeyframes;
    std::unique_ptr<ui::GridFilterController> m_gridFilter;
    application::SubtitleComparison m_comparison;
    std::map<std::uint64_t, std::uint64_t> m_comparedRevisions;
    bool m_partialFilter = false;
    application::ComparedDocument comparedDocument(application::DocumentId id) const;
    bool comparePair(application::DocumentId first, application::DocumentId second);
    void recompare();
    void refreshComparison();
    bool m_referenceLinked = false;
    application::LinkedMatch m_linkedMatch;
    std::optional<std::pair<std::uint64_t, std::uint64_t>> m_linkedFrom;
    std::optional<std::uint64_t> m_linkedReference;
    std::vector<std::pair<application::DocumentId, application::Occurrence>> m_occurrenceMenu;
    void followEditingLine(bool force = false);
    void seekLinkedReference();
    void showReferenceRow(std::size_t row);
    void publishReferenceNavigation();
    std::optional<application::DocumentId> comparedPartner() const;
    application::GridSelection referenceGridSelection() const;
    void applyReferenceSelection(application::Selection next);
    std::unique_ptr<ui::VisualToolsController> m_visualTools;
    std::unique_ptr<ui::VideoViewController> m_videoView;
    std::unique_ptr<ui::VideoFullscreenController> m_videoFullscreen;
    struct OriginalColumnsEntry {
        std::uint64_t generation = 0;
        application::OriginalColumns columns;
    };
    std::map<std::uint64_t, OriginalColumnsEntry> m_originalColumns;
    std::set<std::uint64_t> m_translationDeclined;
    application::OriginalColumns &originalColumns(application::DocumentId document);
    bool showOriginal(application::DocumentId document);
    void setUpTranslationControls();
    std::vector<std::byte> rendererScript(const core::Document &document) const;
    // The same leases while the fonts stay: the renderers keep the fonts they added.
    struct AttachedFontsEntry {
        std::uint64_t generation = 0;
        core::AttachedFontsKey key;
        std::vector<application::FontLease> leases;
    };
    std::map<std::uint64_t, AttachedFontsEntry> m_attachedFonts;
    std::vector<application::FontLease> attachedFontLeases(std::optional<application::DocumentId> document);
    std::vector<application::FontLease> subtitleFonts(std::optional<application::DocumentId> document);
    bool setVideoFonts(std::optional<application::DocumentId> document);
    std::vector<application::FontLease> m_videoFonts;
    std::vector<std::pair<std::shared_ptr<const std::vector<std::byte>>, std::vector<std::string>>> m_leasedFontNames;
    std::unique_ptr<ui::StatusBarController> m_statusBar;
    bool runFilter(const std::function<std::expected<void, application::CommandRefusal>(application::EditSession &)> &command);
    bool m_videoFailureLogged = false;
    std::uint64_t m_seenGroupBreaks = 0;
    std::unique_ptr<application::RecoveryStore> m_recovery;
    std::unique_ptr<QLockFile> m_sessionLock;
    QString m_recoveryDir;
    QString m_sessionId;
    std::map<std::uint64_t, QTimer *> m_autosaveTimers;
    void scheduleAutosave(std::optional<application::DocumentId> document = std::nullopt);
    bool autosave(application::DocumentId document);
    void discardRecovery(application::DocumentId document);
    std::string recoveryKey(application::DocumentId document) const;
    void reportGroupBreak();
    std::optional<application::DocumentId> m_videoDocument;
    std::optional<std::uint64_t> m_videoRevision;
    bool m_videoShowsDraft = false;
    std::vector<std::byte> m_videoScript;
    QString m_editorTimebaseVideo;
    std::optional<core::LineId> m_videoLine;
    struct LineChangeOrigin {
        bool gridClick = false;
        bool autoPlay = false;
    };
    std::optional<LineChangeOrigin> m_lineChangeOrigin;
    bool m_holdVideoRefresh = false;
    QTimer *m_editRefresh = nullptr;
    std::optional<core::LineId> m_leftEditedLine;
    void trackVideoFollow();
    void followActiveLine(bool rowChanged, bool edited, std::optional<core::LineId> left);
    void followEditOn(const application::FollowedLine &line);
    void followShownLine(bool rowChanged, LineChangeOrigin origin);
    void followGridPress(std::optional<core::LineId> before, core::LineId line, int modifiers, bool endColumn,
                         bool doubleClick);
    void applyVideoFollow(const application::VideoFollow &follow);
    application::VideoState videoState() const;
    std::optional<application::FollowedLine> followedLine() const;
    std::optional<application::FollowedLine> followedLine(core::LineId line) const;
    struct Closing {
        application::DocumentId document;
        bool save = false;
        std::uint64_t revision = 0;
        std::optional<application::PermitId> permit;
    };
    QString m_closeThen;
    std::vector<Closing> m_closing;
    int m_quitReviews = 0;
    bool m_quitApproved = false;
    std::optional<application::StagedOpen> m_pendingOpen;
    QString m_pendingOpenPath;
    application::RecentFiles m_recent;
    QString m_settingsFile;
    QString m_userFolder;
    void reportUserFolder(const UserFolder::MoveReport &moved, const UserFolder::SeedReport &seeded);
    void reportSettingsFiles();
    void convertRules();
    void convertListFiles();
    std::map<std::uint64_t, QDateTime> m_fileTimes;
    std::set<std::uint64_t> m_removedNoticed;
    int m_copyColumns = 0;
    int m_pasteColumns = 0;
    application::SpellChecker *spellChecker();
    void restartSpellChecker();
    void spellingRefresh();
    std::optional<std::u16string> editorRaw(int role) const;
    QVariantMap spellCheckState(bool changed, const QString &problem = {});
    std::optional<QVariantMap> restartStaleSpellCheck(application::EditSession &session, const QVariantMap &options);
    void showSpellCheckWord();
    std::unique_ptr<application::SpellChecker> m_spellChecker;
    // MUST KILL: set on first use even with spelling off; turning spelling on doesn't reset it.
    bool m_spellingStarted = false;
    application::SpellingText m_spellingText;
    application::SpellingBackendLoader m_spellingLoader;
    std::unique_ptr<application::SpellChecker> m_sampleChecker;
    QString m_sampleCheckerLanguage;
    bool m_sampleCheckerReady = false;
    std::unique_ptr<backends::SubtitleRendererSelection> m_sampleRenderer;
    int m_testSounds = 0;
    // MUST KILL: the Line editor's own switch, apart from the editor.spellchecker setting.
    bool m_spellingOn = true;
    QString m_dictionaryDir;
    QString m_bundledDictionaryDir;
    std::unique_ptr<application::SpellCheckWalk> m_spellWalk;
    std::optional<application::DocumentId> m_spellWalkDocument;
    struct GrammarCheck {
        std::u16string raw;
        core::grammar::CheckedText text;
        QList<backends::GrammarFinding> findings;
        core::SubtitleFormat format = core::SubtitleFormat::Ass;
    };
    struct GrammarWalk {
        struct Finding {
            std::size_t line;
            backends::GrammarFinding finding;
            bool handled = false;
        };
        application::DocumentId document;
        bool translation = false;
        std::vector<core::LineId> lines;
        std::map<std::size_t, GrammarCheck> checks;
        std::unordered_map<std::uint64_t, const core::LineRecord *> rows;
        const core::Document *rowsOf = nullptr;
        std::uint64_t rowsRevision = 0;
        bool pumpQueued = false;
        std::deque<std::size_t> queue;
        std::map<quint64, std::size_t> pending;
        std::map<std::size_t, QList<backends::GrammarFinding>> arrived;
        std::size_t checked = 0;
        std::vector<Finding> findings;
        std::optional<std::size_t> current;
        bool cancelled = false;
        QString problem;
    };
    void setUpGrammar(const Options &options);
    backends::HarperClient *grammarClient();
    QJsonObject grammarSettings() const;
    void applyGrammarSettings();
    void grammarWalkSettingsChanged(const QJsonObject &before, const QJsonObject &after);
    void scheduleGrammar();
    void checkEditorGrammar();
    void grammarChecked(quint64 ticket, const QList<backends::GrammarFinding> &findings);
    void grammarFailed(const QString &message);
    void syncGrammarWords();
    const GrammarCheck *editorGrammarCheck(int role) const;
    core::SubtitleFormat editorFormat() const;
    std::optional<std::pair<int, int>> fieldSpan(int role, const core::grammar::CheckedText &text, int start,
                                                  int end) const;
    const backends::GrammarFinding *editorGrammarFinding(int role, int position) const;
    void pumpGrammarWalk();
    const core::LineRecord *grammarWalkLine(const application::EditSession &session, std::size_t index);
    void nextGrammarFinding();
    void showGrammarFinding();
    bool grammarWalkStale();
    QVariantMap grammarWalkProblem(const QString &problem);
    QString m_harperProgram;
    QString m_grammarDir;
    std::unique_ptr<QTemporaryDir> m_grammarTemp;
    std::unique_ptr<backends::HarperClient> m_harper;
    QTimer *m_grammarDelay = nullptr;
    quint64 m_grammarTicket = 0;
    int m_grammarRole = 0;
    GrammarCheck m_grammarPending;
    std::optional<GrammarCheck> m_grammarEditor;
    int m_grammarEditorRole = 0;
    bool m_grammarFailed = false;
    bool m_grammarApplyPending = false;
    QString m_grammarWords;
    QStringList m_grammarOwnWords;
    std::unique_ptr<GrammarWalk> m_grammarWalk;
    std::unique_ptr<backends::FfmsIndexedSource> m_audioSource;
    std::unique_ptr<ui::AudioController> m_audio;
    QList<QMetaObject::Connection> m_audioConnections;
    application::RecentFiles m_recentAudio;
    QString m_audioFollowedVideo;
    std::string m_pendingIndexHandoff;
    void releaseReadIndexHandoff();
    std::optional<std::tuple<std::uint64_t, std::uint64_t, std::uint64_t>> m_audioLine;
    std::optional<application::LegacyTimebase> m_audioTimebase;
    std::string m_audioTimebasePath;
    std::vector<int> m_audioKeyframes;
    std::function<application::AudioCacheSettings()> m_audioSettings;
    QString m_indexDir;
    std::unique_ptr<application::AudioOutputPort> m_audioOutput;
    std::string m_audioOutputHostApi;
    std::unique_ptr<backends::AudioBoxPlayer> m_audioPlayer;
    std::unique_ptr<application::AudioDevicesPort> m_audioDevices;
    std::unique_ptr<application::AudioOutputRouting> m_audioRouting;
    void routeAudio(const application::OutputRoute &route, bool move);
    void noticeAudioRoute(const application::OutputRoute &before, const application::OutputRoute &after);
    void noticeOnsets();
    application::OutputRoute m_noticedRoute;
    void syncAudioWithVideo();
    static QString dummyVideoAudio(const QString &videoPath);
    static bool isVideoAudio(const QString &audio, const QString &videoPath);
    void showVideoAtAudio(int ms);
    std::optional<int> m_audioWantsFrame;
    void refreshAudio();
    void followVideoInAudio();
    void trackVideoSources();
    void logVideoFailure();
    void rememberRecentVideo(const QString &path);
    void rememberRecentKeyframes(const QString &path);
    QVariantList recentRows(application::RecentFiles &list, const char *setting);
    bool keyframesWithoutVideo(const QString &path, QString &problem);
    application::RecentFiles m_recentVideo;
    application::RecentFiles m_recentKeyframes;
    application::NextFileWalker m_nextFile{[](char32_t c) { return QChar::toLower(c); }};
    std::optional<application::NextFileStep> nextVideoStep(application::NextFileWalker &walker, bool next) const;
    QString m_recentVideoSeen;
    void rememberRecentAudio(const QString &path);
    void trimAudioCache();
    QString indexFile(const QString &path, int track) const;
    bool m_audioCommitting = false;
    bool commitAudioTimes(const application::AudioCommitRequest &request);
    void addAppearance(QVariantMap &values) const;
    void setAudioActive(int key);
    void seekVideoFromAudio(int ms);
    struct TabMedia {
        QString video;
        int position = 0;
        QString audio;
        QString keyframes;
        int scroll = 0;
        bool audioFromVideo = false;
    };
    struct UnresolvedRestore {
        application::DocumentId document;
        QString kind;
        QString path;
        int active = 0;
        int position = 0;
        bool layout = false;
    };
    struct PendingSession {
        std::vector<application::SessionTab> tabs;
        std::optional<std::size_t> activeTab;
        std::vector<std::optional<application::StagedOpen>> staged;
        std::vector<std::pair<QDateTime, qint64>> stamps;
    };
    std::map<std::uint64_t, TabMedia> m_tabMedia;
    std::vector<UnresolvedRestore> m_unresolved;
    std::optional<PendingSession> m_pendingSession;
    std::optional<application::DocumentId> m_closingTab;
    std::optional<application::DocumentId> m_closeTarget;
    QString m_keepTabAudio;
    std::optional<std::pair<QString, int>> m_pendingTabSeek;
    bool m_tabSeekQueued = false;
    bool m_scrollRestoring = false;
    bool m_startedWithPaths = false;
    std::optional<application::Session> m_startSession;
    bool m_lastSessionWritable = true;
    void rememberStartSession();
    void convertLastSession();
    std::optional<application::Session> loadLastSession();
    static QStringList sessionNames(const std::optional<application::Session> &session);
    QVariantMap reviewSessionRead(std::optional<application::Session> session, const QString &problem = {});
    // MUST KILL: mutable so the const startupSession() can mark itself pending.
    mutable bool m_startupSessionPending = false;
    int m_tabTextMax = 40;
    void leaveTabMedia(std::optional<application::DocumentId> document);
    void enterTabMedia(std::optional<application::DocumentId> previous, std::optional<application::DocumentId> document);
    void closeDocument(application::DocumentId document);
    void replaceTarget(application::DocumentId replacement, bool keepMedia = false);
    void applySession();
    void forgetTab(application::DocumentId document);
    void selectRow(application::DocumentId document, int row);
    bool keepTabAudio(const QString &videoPath);
    void trackTabMedia();
    int targetVideoPosition() const;
    std::optional<application::DocumentId> m_layoutDocument;
    void showTabArrangement();
    void registerTabArrangements();
    void replaceTabArrangement(application::DocumentId old, application::DocumentId replacement,
                               ui::WorkspaceLayoutController::TabStart start);
    ui::WorkspaceLayoutController::TabStart tabStart(application::DocumentId document) const;
    void setUpFileArrangements();
    std::optional<QByteArray> fileArrangement(application::DocumentId document, bool report = false);
    void takeFileArrangement(application::DocumentId document);
    void openIntoTabArrangement(application::DocumentId old, application::DocumentId replacement);

    std::unique_ptr<Localisation> m_localisation;
    QString m_untitledTitle;
    QFont m_startFont;
    void startLocalisation();
    void switchLanguage();
    void languageSwitched();
    void applyProgramFont();
};

}
