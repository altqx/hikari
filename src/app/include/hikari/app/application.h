#pragma once

// The composed application services (composition root). main() and the UI
// workflow tests build the same graph: native file ports, the write
// coordinator, Documents, the workspace and the shell/editor presenters.

#include "hikari/application/document_files.h"
#include "hikari/application/workspace.h"
#include "hikari/backends/platform_files.h"
#include "line_editor_controller.h"
#include "shell_controller.h"

#include <QObject>
#include <QVariantMap>

#include <memory>

namespace hikari::app {

class Application : public QObject {
    Q_OBJECT
public:
    explicit Application(QObject *parent = nullptr);
    ~Application() override;

    // Opens a file as a new Document; the first becomes the editing target.
    Q_INVOKABLE bool openFile(const QString &path);
    // Opens a file as the protected comparison reference.
    Q_INVOKABLE bool openReference(const QString &path);
    // Closes the editing target (close review belongs to a later card).
    Q_INVOKABLE bool closeEditingTarget();

    ui::ShellController &shell() { return *m_shell; }
    ui::LineEditorController &editor() { return *m_editor; }
    application::DocumentFiles &files() { return *m_files; }
    application::Workspace &workspace() { return m_workspace; }
    // Properties for Main.qml.
    QVariantMap qmlProperties();
    // Tests: waits for running writes and delivers their results.
    void waitForWrites();

private:
    std::optional<application::DocumentId> open(const QString &path, bool asReference = false);
    void refreshViews();

    std::unique_ptr<application::FileReadPort> m_reader;
    std::unique_ptr<backends::PlatformFilePort> m_port;
    std::unique_ptr<application::WriteCoordinator> m_writes;
    std::unique_ptr<application::DocumentFiles> m_files;
    application::Workspace m_workspace;
    std::unique_ptr<ui::ShellController> m_shell;
    std::unique_ptr<ui::LineEditorController> m_editor;
    std::optional<application::DocumentId> m_editorDocument;
};

} // namespace hikari::app
