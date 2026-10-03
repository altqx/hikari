#pragma once

// The composed application services (composition root). main() and the UI
// workflow tests build the same graph: native file ports, the write
// coordinator, Documents, the workspace and the shell/editor presenters.

#include "hikari/app/automation_shell.h"
#include "hikari/application/document_files.h"
#include "hikari/application/grid_commands.h"
#include "hikari/application/grid_selection.h"
#include "hikari/application/recent_files.h"
#include "hikari/application/workspace.h"
#include "hikari/backends/ffms_indexed_source.h"
#include "hikari/backends/libass_renderer.h"
#include "hikari/backends/platform_files.h"
#include "line_editor_controller.h"
#include "log_controller.h"
#include "shell_controller.h"
#include "video_controller.h"

#include <QDateTime>
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
    void quitApprovedChanged();
    void recentChanged();

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
        // INI file holding the recent lists; empty: they are not kept (tests).
        QString settingsFile;
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
    // Legacy GRID_CHANGE_ACTIVE_ON_SELECTION (default true) until the settings registry.
    void setChangeActiveOnSelection(bool on) { m_changeActiveOnSelection = on; }

    ui::ShellController &shell() { return *m_shell; }
    ui::LineEditorController &editor() { return *m_editor; }
    ui::VideoController &video() { return *m_video; }
    AutomationShell &automation() { return *m_automation; }
    ui::LogController &log() { return *m_log; }
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
    std::unique_ptr<AutomationShell> m_automation;
    std::unique_ptr<ui::LogController> m_log;
    bool m_videoFailureLogged = false;
    std::optional<application::DocumentId> m_videoDocument;
    std::optional<std::uint64_t> m_videoRevision; // the revision whose content the overlay shows
    std::optional<core::LineId> m_videoLine;
    bool m_changeActiveOnSelection = true;
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
