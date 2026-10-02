#include "hikari/app/application.h"

#include <QCoreApplication>
#include <QFileInfo>

namespace hikari::app {

Application::Application(QObject *parent) : QObject(parent)
{
    m_reader = backends::makeFileReader();
    // Write outcomes arrive on the writer's thread; publish them on this one.
    m_port = backends::makeFilePort([this](application::PermitId permit, application::WriteOutcome outcome) {
        QMetaObject::invokeMethod(this, [this, permit, outcome] { m_writes->complete(permit, outcome); },
                                  Qt::QueuedConnection);
    });
    m_writes = std::make_unique<application::WriteCoordinator>(*m_port, [this](const application::WriteResult &r) {
        m_files->onWriteResult(r);
        m_editor->writeFinished();
    });
    m_files = std::make_unique<application::DocumentFiles>(*m_reader, *m_writes);
    m_shell = std::make_unique<ui::ShellController>(m_workspace);
    m_editor = std::make_unique<ui::LineEditorController>(*m_files);
    m_editor->setCommittedListener([this] { refreshViews(); });
    refreshViews();
}

Application::~Application()
{
    m_port->waitIdle(); // no write may outlive the services it reports to
}

std::optional<application::DocumentId> Application::open(const QString &path, bool asReference)
{
    auto staged = m_files->stageOpen({QFileInfo(path).absoluteFilePath().toStdString()});
    if (!staged)
        return std::nullopt;
    auto id = m_files->activate(std::move(*staged));
    if (!id)
        return std::nullopt;
    m_workspace.add(*id, QFileInfo(path).fileName().toStdString(), asReference);
    return *id;
}

bool Application::openFile(const QString &path)
{
    if (!open(path))
        return false;
    refreshViews();
    return true;
}

bool Application::openReference(const QString &path)
{
    if (!open(path, true))
        return false;
    refreshViews();
    return true;
}

bool Application::closeEditingTarget()
{
    const auto target = m_workspace.editingTarget();
    if (!target)
        return false;
    m_files->close(*target);
    m_workspace.remove(*target);
    refreshViews();
    return true;
}

void Application::refreshViews()
{
    const auto target = m_workspace.editingTarget();
    const auto reference = m_workspace.reference();
    auto *targetSession = target ? m_files->session(*target) : nullptr;
    auto *referenceSession = reference ? m_files->session(*reference) : nullptr;
    m_shell->refresh(targetSession ? &targetSession->document() : nullptr,
                     referenceSession ? &referenceSession->document() : nullptr);
    if (targetSession && targetSession->selection().active)
        m_shell->selectLine(targetSession->selection().active->value);
    // The editor only ever edits the editing target, never the reference.
    if (target != m_editorDocument) {
        m_editorDocument = target;
        m_editor->setDocument(target, target && m_workspace.checkContentCommand(*target).has_value());
    }
}

QVariantMap Application::qmlProperties()
{
    return {{QStringLiteral("shell"), QVariant::fromValue(m_shell.get())},
            {QStringLiteral("editor"), QVariant::fromValue(m_editor.get())},
            {QStringLiteral("app"), QVariant::fromValue(static_cast<QObject *>(this))}};
}

void Application::waitForWrites()
{
    m_port->waitIdle();
    QCoreApplication::processEvents();
}

} // namespace hikari::app
