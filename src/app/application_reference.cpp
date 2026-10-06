// R2: reference navigation and the subtitles preview (legacy SubsGridPreview,
// SubsGrid::OnShowPreview, ShowPreviewWindow and ShowSecondComparedLine at
// 20d647c4; docs/qt/ux/translation-comparison.md).
//
// Legacy drew another tab's grid inside the editing Grid (SubsGridPreview);
// the rewrite shows that Document in the reference tray as the protected
// reference, and it stays a tab (Workspace::setTabReference). The tray is
// navigated on its own: clicks and keys move the reference's selection, never
// its content. Linked matching makes it follow the editing target's active
// Line one way, as the preview followed its grid (NewSeeking): the
// reference's Lines overlapping the editing Line in time, in runs, the first
// one shown, with the candidates counted and an empty no-match state instead
// of legacy's nearest Line. While the editing target is compared (R1), the
// compared partner follows by its pairing instead (ShowSecondComparedLine).

#include "hikari/app/application.h"

#include "hikari/application/grid_clipboard.h"

#include <QClipboard>
#include <QGuiApplication>

#include <algorithm>

namespace hikari::app {

namespace {

std::optional<std::size_t> rowOf(const core::Document &document, core::LineId id)
{
    const auto lines = document.lines();
    for (std::size_t row = 0; row < lines.size(); ++row)
        if (lines[row]->id == id)
            return row;
    return std::nullopt;
}

} // namespace

std::optional<application::DocumentId> Application::comparedPartner() const
{
    // The editing grid has a Comparison table: it is CG1 or CG2 (only the
    // compared pair has tables, R1-stale-table).
    const auto target = m_workspace.editingTarget();
    if (!target || !m_comparison.table(*target))
        return std::nullopt;
    const auto first = m_comparison.first(), second = m_comparison.second();
    if (first == target)
        return second;
    if (second == target)
        return first;
    return std::nullopt;
}

bool Application::canShowPreview() const
{
    // SubsGrid.cpp:277 and 884-887: Notebook::GetTabs()->Size() > 1 && !preview.
    return m_workspace.editingTarget() && !m_workspace.reference() && m_workspace.tabs().size() > 1;
}

bool Application::showPreview()
{
    if (!canShowPreview())
        return false;
    const auto target = *m_workspace.editingTarget();
    auto *session = m_files->session(target);
    if (!session || !session->selection().active)
        return false;
    std::optional<application::DocumentId> shown;
    if (const auto partner = comparedPartner()) {
        // SubsGrid::OnShowPreview (SubsGrid.cpp:1416-1424): a compared grid
        // shows ShowSecondComparedLine(currentLine, true), which returns
        // before showing anything for a Line without a pair
        // (SubsGridWindow.cpp:2038-2041).
        const auto *table = m_comparison.table(target);
        const auto row = rowOf(session->document(), *session->selection().active);
        if (!row || *row >= table->size() || !(*table)[*row].matchedRow)
            return false;
        shown = partner;
    } else {
        // ShowPreviewWindow(nullptr, ...) and NewSeeking(false): with no grid
        // shown yet, SeekForOccurences takes the first entry, the first other
        // tab's (every tab gives one, SubsGridPreview.cpp:917-924).
        for (const auto id : m_workspace.tabs())
            if (id != target) {
                shown = id;
                break;
            }
    }
    if (!shown || !m_workspace.setTabReference(*shown))
        return false;
    // The preview follows its grid (NewSeeking from every line change).
    m_referenceLinked = true;
    m_linkedFrom.reset();
    refreshViews();
    return true;
}

void Application::setReferenceLinked(bool linked)
{
    m_referenceLinked = linked && m_workspace.reference();
    m_linkedFrom.reset();
    if (m_referenceLinked)
        followEditingLine(true);
    else
        m_linkedMatch.clear();
    publishReferenceNavigation();
}

void Application::followEditingLine(bool force)
{
    const auto reference = m_workspace.reference();
    if (!reference) {
        // The tray closed or its Document went: a later reference starts independent.
        if (m_referenceLinked || m_linkedMatch.active()) {
            m_referenceLinked = false;
            m_linkedMatch.clear();
            m_linkedFrom.reset();
            m_linkedReference.reset();
            publishReferenceNavigation();
        }
        return;
    }
    if (!m_referenceLinked)
        return;
    const auto target = m_workspace.editingTarget();
    auto *session = target ? m_files->session(*target) : nullptr;
    const auto active = session ? session->selection().active : std::nullopt;
    if (!active)
        return;
    const std::pair key{target->value, active->value};
    // Only a move to another Line seeks (legacy NewSeeking runs from the
    // line-changing gestures, never from an edit of the same Line).
    if (!force && m_linkedFrom == key && m_linkedReference == reference->value)
        return;
    m_linkedFrom = key;
    m_linkedReference = reference->value;
    seekLinkedReference();
}

void Application::seekLinkedReference()
{
    const auto target = m_workspace.editingTarget();
    const auto reference = m_workspace.reference();
    auto *session = target ? m_files->session(*target) : nullptr;
    auto *referenceSession = reference ? m_files->session(*reference) : nullptr;
    if (!session || !referenceSession || !session->selection().active)
        return;
    const auto row = rowOf(session->document(), *session->selection().active);
    if (!row)
        return;
    if (const auto partner = comparedPartner()) {
        // `if (Comparison) ShowSecondComparedLine(row); else if (preview)
        // preview->NewSeeking();` (SubsGridWindow.cpp:1638-1639, 1662-1663,
        // 1872-1873; SubsGridBase.cpp:1415-1416): a compared grid moves its
        // partner to the paired Line, and nothing for an unpaired one.
        if (*partner != *reference) {
            m_linkedMatch.clear(); // legacy moved the hidden partner, not the preview
            publishReferenceNavigation();
            return;
        }
        const auto *table = m_comparison.table(*target);
        std::vector<application::Occurrence> paired;
        if (*row < table->size())
            if (const auto matched = (*table)[*row].matchedRow)
                paired.push_back({*matched, 1});
        m_linkedMatch.set(paired);
    } else {
        m_linkedMatch.set(application::seekOccurrences(referenceSession->document(), *session->document().lines()[*row]));
    }
    publishReferenceNavigation();
    // NewSeeking: previewGrid->ChangeActiveLine(lastData.lineRangeStart),
    // then MakeVisible. A no-match leaves the reference where it was.
    if (const auto current = m_linkedMatch.current())
        showReferenceRow(current->row);
}

void Application::showReferenceRow(std::size_t row)
{
    // SubsGrid::ChangeActiveLine (SubsGridWindow.cpp:1968-1980): the Line
    // becomes active and is selected alone.
    const auto reference = m_workspace.reference();
    auto *referenceSession = reference ? m_files->session(*reference) : nullptr;
    if (!referenceSession)
        return;
    const auto lines = referenceSession->document().lines();
    if (row >= lines.size())
        return;
    const auto line = lines[row]->id;
    referenceSession->setSelection(application::Selection{line, {line}, line, {}});
    m_shell->setReferenceSelection(referenceSession->selection());
    m_shell->showReferenceLine(line);
}

void Application::publishReferenceNavigation()
{
    m_shell->setReferenceNavigation(m_referenceLinked, m_linkedMatch);
}

bool Application::stepReferenceMatch(int delta)
{
    if (!m_referenceLinked || !m_linkedMatch.step(delta))
        return false;
    publishReferenceNavigation();
    showReferenceRow(m_linkedMatch.current()->row);
    return true;
}

bool Application::showNearestReferenceLine()
{
    if (!m_referenceLinked || !m_linkedMatch.noMatch() || !m_linkedMatch.nearest())
        return false;
    showReferenceRow(*m_linkedMatch.nearest());
    return true;
}

void Application::closeReference()
{
    // DestroyPreview (SubsGridPreview.cpp:91-107): the preview goes; the
    // previewed tab stays as it was.
    if (!m_workspace.reference())
        return;
    m_workspace.setReference(std::nullopt);
    refreshViews();
}

application::GridSelection Application::referenceGridSelection() const
{
    std::vector<core::LineId> document;
    const auto reference = m_workspace.reference();
    if (auto *session = reference ? m_files->session(*reference) : nullptr)
        for (const auto *line : session->document().lines())
            document.push_back(line->id);
    application::GridSelection rules(std::move(document), m_shell->referenceDisplayedLines());
    rules.setChangeActiveOnSelection(m_settings->boolean("grid.changeActiveOnSelection"));
    return rules;
}

void Application::applyReferenceSelection(application::Selection next)
{
    // A selection change is not a content change (EditSession::setSelection
    // makes no history step), so the protected reference allows it.
    const auto reference = m_workspace.reference();
    auto *session = reference ? m_files->session(*reference) : nullptr;
    if (!session)
        return;
    session->setSelection(std::move(next));
    m_shell->setReferenceSelection(session->selection());
}

void Application::selectReferenceLine(qulonglong id)
{
    const auto reference = m_workspace.reference();
    if (auto *session = reference ? m_files->session(*reference) : nullptr)
        applyReferenceSelection(referenceGridSelection().plain(session->selection(), core::LineId{id}));
}

void Application::clickReferenceLine(qulonglong id, int modifiers)
{
    // SubsGridPreview::OnMouseEvent's selection (SubsGridPreview.cpp:700-790)
    // is the Grid's: a click selects the Line alone, Ctrl toggles it, Shift
    // selects the block.
    const auto reference = m_workspace.reference();
    auto *session = reference ? m_files->session(*reference) : nullptr;
    if (!session)
        return;
    const auto rules = referenceGridSelection();
    const bool ctrl = modifiers & Qt::ControlModifier, shift = modifiers & Qt::ShiftModifier;
    const core::LineId line{id};
    if (shift)
        applyReferenceSelection(rules.shiftClick(session->selection(), line, ctrl));
    else if (ctrl)
        applyReferenceSelection(rules.ctrlClick(session->selection(), line));
    else
        applyReferenceSelection(rules.plain(session->selection(), line));
}

void Application::extendReferenceSelection(int rows)
{
    const auto reference = m_workspace.reference();
    if (auto *session = reference ? m_files->session(*reference) : nullptr)
        applyReferenceSelection(referenceGridSelection().shiftKey(session->selection(), rows));
}

void Application::dragReferenceSelection(qulonglong id)
{
    const auto reference = m_workspace.reference();
    if (auto *session = reference ? m_files->session(*reference) : nullptr)
        applyReferenceSelection(referenceGridSelection().shiftClick(session->selection(), core::LineId{id}, false));
}

void Application::selectAllReferenceLines()
{
    const auto reference = m_workspace.reference();
    if (auto *session = reference ? m_files->session(*reference) : nullptr)
        applyReferenceSelection(referenceGridSelection().selectAll(session->selection()));
}

bool Application::copyReferenceLines()
{
    // SubsGridPreview::OnAccelerator PREVIEW_COPY (SubsGridPreview.cpp:842-850):
    // with a selection, CopyRows(GRID_COPY) of the previewed grid.
    const auto reference = m_workspace.reference();
    auto *session = reference ? m_files->session(*reference) : nullptr;
    if (!session || session->selection().selected.empty())
        return false;
    const auto text = application::copyRows(*session);
    QGuiApplication::clipboard()->setText(
        QString::fromUtf8(reinterpret_cast<const char *>(text.data()), static_cast<qsizetype>(text.size())));
    return true;
}

void Application::refuseReferenceChange()
{
    // Legacy pasted into the previewed grid (PREVIEW_PASTE); the reference is
    // protected, so nothing changes.
    m_shell->setStatusText(tr("The reference is protected (read-only)."));
}

QVariantList Application::referenceOccurrences()
{
    // SubsGridPreview::ContextMenu (SubsGridPreview.cpp:940-961):
    // SeekForOccurences over every tab but the editing one, each entry
    // "SubsName (lineRangeStart lineRangeLen)", the one shown checked.
    m_occurrenceMenu.clear();
    QVariantList rows;
    const auto target = m_workspace.editingTarget();
    auto *session = target ? m_files->session(*target) : nullptr;
    const auto active = session ? session->selection().active : std::nullopt;
    const auto row = active ? rowOf(session->document(), *active) : std::nullopt;
    if (!row)
        return rows;
    const core::LineRecord &editing = *session->document().lines()[*row];
    const auto reference = m_workspace.reference();
    for (const auto id : m_workspace.documents()) {
        if (id == *target)
            continue;
        const auto *other = m_files->session(id);
        if (!other)
            continue;
        const std::string *title = m_workspace.title(id);
        const auto lines = other->document().lines();
        for (const auto &occurrence : application::seekOccurrences(other->document(), editing)) {
            // lastData == previewData[i]: the occurrence the shown grid's active Line starts.
            const bool checked = reference == id && other->selection().active && occurrence.row < lines.size() &&
                                 lines[occurrence.row]->id == *other->selection().active;
            rows << QVariantMap{{QStringLiteral("text"), QStringLiteral("%1 (%2 %3)")
                                                             .arg(title ? QString::fromStdString(*title) : QString())
                                                             .arg(occurrence.row)
                                                             .arg(occurrence.length)},
                                {QStringLiteral("checked"), checked}};
            m_occurrenceMenu.emplace_back(id, occurrence);
        }
    }
    return rows;
}

bool Application::chooseReferenceOccurrence(int index)
{
    // ContextMenu's choice (SubsGridPreview.cpp:950-960): that grid is shown
    // and its Line lineRangeStart becomes active.
    if (index < 0 || index >= static_cast<int>(m_occurrenceMenu.size()))
        return false;
    const auto [id, occurrence] = m_occurrenceMenu[static_cast<std::size_t>(index)];
    if (!m_files->session(id) || m_workspace.editingTarget() == id)
        return false;
    if (m_workspace.reference() != id && !m_workspace.setTabReference(id))
        return false;
    refreshViews(); // a linked tray follows the editing Line in the Document now shown
    if (m_referenceLinked && m_linkedMatch.choose(occurrence.row))
        publishReferenceNavigation();
    showReferenceRow(occurrence.row);
    return true;
}

int Application::comparedReferenceRow(int row) const
{
    // ShowSecondComparedLine(scrollPosition, false, false, true) from the
    // compared grid's wheel and scroll bar (SubsGridWindow.cpp:1462-1465,
    // 1719-1729): the partner's first row becomes the paired one.
    const auto reference = m_workspace.reference();
    const auto partner = comparedPartner();
    const auto target = m_workspace.editingTarget();
    if (!m_referenceLinked || !reference || partner != reference || row < 0)
        return -1;
    const auto shown = m_shell->displayedLines();
    auto *session = m_files->session(*target);
    if (static_cast<std::size_t>(row) >= shown.size() || !session)
        return -1;
    const auto documentRow = rowOf(session->document(), shown[static_cast<std::size_t>(row)]);
    const auto *table = m_comparison.table(*target);
    if (!documentRow || *documentRow >= table->size())
        return -1;
    const auto matched = (*table)[*documentRow].matchedRow;
    return matched ? static_cast<int>(*matched) : -1;
}

} // namespace hikari::app
