#include "shell_controller.h"

namespace hikari::ui {

namespace {

QString qs(const std::u8string &s)
{
    return QString::fromUtf8(reinterpret_cast<const char *>(s.data()), static_cast<qsizetype>(s.size()));
}

} // namespace

ShellController::ShellController(application::Workspace &workspace, QObject *parent)
    : QObject(parent), m_workspace(workspace)
{
}

void ShellController::refresh(const core::Document *target, const core::Document *reference)
{
    m_lines.setDocument(target ? *target : core::Document{});
    m_referenceLines.setDocument(reference ? *reference : core::Document{});
    m_activeText.clear();
    m_activeStyle.clear();
    emit targetsChanged();
    emit activeLineChanged();
}

QString ShellController::titleOf(std::optional<application::DocumentId> id) const
{
    const std::string *title = id ? m_workspace.title(*id) : nullptr;
    return title ? QString::fromStdString(*title) : QString();
}

QString ShellController::editingTitle() const
{
    return titleOf(m_workspace.editingTarget());
}

QString ShellController::referenceTitle() const
{
    return titleOf(m_workspace.reference());
}

void ShellController::activateLine(qulonglong id)
{
    const core::LineId line{id};
    const auto row = m_lines.rowOf(line);
    if (!row)
        return;
    m_lines.setSelection(application::Selection{line, {line}}, line);
    const core::LineRecord *record = m_lines.recordAt(*row);
    m_activeText = qs(record->text);
    m_activeStyle = qs(record->style);
    emit activeLineChanged();
}

} // namespace hikari::ui
