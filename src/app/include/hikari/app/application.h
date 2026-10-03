#pragma once

// The composed application services (composition root). main() and the UI
// workflow tests build the same graph: native file ports, the write
// coordinator, Documents, the workspace and the shell/editor presenters.

#include "hikari/app/automation_shell.h"
#include "hikari/application/document_files.h"
#include "hikari/application/grid_selection.h"
#include "hikari/application/workspace.h"
#include "hikari/backends/ffms_indexed_source.h"
#include "hikari/backends/libass_renderer.h"
#include "hikari/backends/platform_files.h"
#include "line_editor_controller.h"
#include "shell_controller.h"
#include "video_controller.h"

#include <QObject>
#include <QUrl>
#include <QVariantMap>

#include <memory>

namespace hikari::app {

class Application : public QObject {
    Q_OBJECT
signals:
    void closeFinished(bool done, const QString &problem);
    void quitApprovedChanged();

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
    // Legacy GRID_CHANGE_ACTIVE_ON_SELECTION (default true) until the settings registry.
    void setChangeActiveOnSelection(bool on) { m_changeActiveOnSelection = on; }

    ui::ShellController &shell() { return *m_shell; }
    ui::LineEditorController &editor() { return *m_editor; }
    ui::VideoController &video() { return *m_video; }
    AutomationShell &automation() { return *m_automation; }
    application::DocumentFiles &files() { return *m_files; }
    application::Workspace &workspace() { return m_workspace; }
    // Properties for Main.qml.
    QVariantMap qmlProperties();
    // Tests: waits for running writes and delivers their results.
    void waitForWrites();

private:
    std::optional<application::DocumentId> open(const QString &path, bool asReference = false);
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
};

} // namespace hikari::app
