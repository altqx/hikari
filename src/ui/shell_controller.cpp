#include "shell_controller.h"

#include "hikari/core/ass_save.h"

#include <QDateTime>

#include <algorithm>

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
    m_shown.setLineModel(&m_lines);
    m_shown.setPredicate([](const core::LineRecord &line) { return line.visibility != core::LineVisibility::Hidden; });
}

void ShellController::refresh(const core::Document *target, const core::Document *reference)
{
    refresh(target, reference, nullptr, nullptr);
}

void ShellController::refresh(const core::Document *target, const core::Document *reference,
                              const std::vector<application::LineComparison> *targetComparison,
                              const std::vector<application::LineComparison> *referenceComparison)
{
    m_lines.setDocument(target ? *target : core::Document{}, targetComparison);
    const auto format = target ? target->format() : core::SubtitleFormat::Ass;
    m_assFormat = format == core::SubtitleFormat::Ass || format == core::SubtitleFormat::PlainText;
    m_endColumn = format != core::SubtitleFormat::TMPlayer;
    m_referenceLines.setDocument(reference ? *reference : core::Document{}, referenceComparison);
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

void ShellController::selectLine(qulonglong id)
{
    const core::LineId line{id};
    if (m_lines.rowOf(line))
        m_lines.setSelection(application::Selection{line, {line}}, line);
}

void ShellController::setSelection(const application::Selection &selection)
{
    m_lines.setSelection(selection, selection.anchor);
    const auto row = selection.active ? m_lines.rowOf(*selection.active) : std::nullopt;
    const core::LineRecord *record = row ? m_lines.recordAt(*row) : nullptr;
    m_activeText = record ? qs(record->text) : QString();
    m_activeStyle = record ? qs(record->style) : QString();
    const auto shown = displayedLines();
    const auto hidden = std::count_if(selection.selected.begin(), selection.selected.end(), [&](core::LineId id) {
        return std::find(shown.begin(), shown.end(), id) == shown.end();
    });
    const auto count = static_cast<qsizetype>(selection.selected.size());
    m_selectionStatus = count == 0 ? QString()
                        : count == 1 ? tr("1 Line selected")
                                     : tr("%1 Lines selected").arg(count);
    if (hidden > 0)
        m_selectionStatus += tr(" (%1 hidden)").arg(hidden);
    emit activeLineChanged();
}

std::vector<core::LineId> ShellController::displayedLines() const
{
    std::vector<core::LineId> out;
    for (int row = 0; row < m_shown.rowCount(); ++row)
        if (const auto id = m_lines.lineAt(m_shown.mapToSource(m_shown.index(row, 0)).row()))
            out.push_back(*id);
    return out;
}

qint64 ShellController::progressNow() const
{
    return m_progressClock ? m_progressClock() : QDateTime::currentMSecsSinceEpoch();
}

void ShellController::startProgress(const QString &title)
{
    // ProgressSetup: the start time, then ProgressTitle when there is a title.
    m_progressStart = progressNow();
    if (title.isEmpty())
        return;
    m_progressTitle = title;
    // ProgressTitle (HikariSubFrame.cpp:578-588): 0%, with the time since the start.
    const qint64 elapsed = std::max<qint64>(progressNow() - m_progressStart, 0);
    setStatusText(m_progressTitle + QStringLiteral(" 0%. ") +
                  tr("Time elapsed: %1").arg(qs(core::legacy::assTimeText(elapsed))));
}

void ShellController::setProgress(int percent)
{
    // ProgressParcentProgress (HikariSubFrame.cpp:590-609): SubsTime's
    // NewTime keeps a negative time at 0; raw() is the ASS form.
    const qint64 elapsed = std::max<qint64>(progressNow() - m_progressStart, 0);
    setStatusText(m_progressTitle + QLatin1Char(' ') + QString::number(percent) + QStringLiteral("%. ") +
                  tr("Time elapsed: %1").arg(qs(core::legacy::assTimeText(elapsed))));
}

void ShellController::endProgress()
{
    // ProgressEnd (HikariSubFrame.cpp:611-621).
    setStatusText(QString());
}

} // namespace hikari::ui
