#include "hikari/application/edit_session.h"

#include <algorithm>

namespace hikari::application {

namespace {

bool hasLine(const core::Document &document, core::LineId id)
{
    const auto lines = document.lines();
    return std::ranges::any_of(lines, [&](const core::LineRecord *l) { return l->id == id; });
}

} // namespace

EditSession::EditSession(core::Document document, bool protectedReference) : m_protected(protectedReference)
{
    m_states.push_back(State{std::move(document), ContentId{m_nextContent++}, {}, "Open"});
    m_saved = m_states.front().content; // a freshly loaded Document is saved as opened
}

void EditSession::setSelection(Selection selection)
{
    m_selection = std::move(selection);
}

void EditSession::navigateTo(core::LineId line)
{
    if (m_draft && m_draft->line != line)
        commitDraft();
    m_selection.active = line;
}

bool EditSession::editDraftText(core::LineId line, std::u8string text)
{
    if (m_protected || !hasLine(document(), line))
        return false;
    if (m_draft && m_draft->line != line)
        commitDraft(); // commit on leave
    m_draft = Draft{line, std::move(text)};
    m_selection.active = line;
    return true;
}

std::optional<core::LineId> EditSession::draftLine() const
{
    return m_draft ? std::optional(m_draft->line) : std::nullopt;
}

std::optional<std::u8string> EditSession::draftText() const
{
    return m_draft ? std::optional(m_draft->text) : std::nullopt;
}

void EditSession::pushState(core::Document document, std::string name)
{
    // A new commit after Undo drops the redo branch.
    while (m_states.size() > m_cursor + 1)
        m_states.pop_back();
    m_states.push_back(State{std::move(document), ContentId{m_nextContent++}, m_selection, std::move(name)});
    if (m_states.size() > kHistoryCapacity)
        m_states.pop_front(); // the saved identity is kept even when its entry goes (C43)
    m_cursor = m_states.size() - 1;
    ++m_revision;
}

bool EditSession::commitDraft()
{
    if (!m_draft)
        return false;
    core::Document next = document();
    const Draft draft = *m_draft;
    m_draft.reset();
    const auto lines = next.lines();
    const auto it = std::ranges::find_if(lines, [&](const core::LineRecord *l) { return l->id == draft.line; });
    if (it == lines.end() || (*it)->text == draft.text)
        return false; // nothing to commit: the Line is gone or unchanged
    next.setLineText(draft.line, draft.text);
    pushState(std::move(next), "Edit Line");
    return true;
}

void EditSession::discardDraft()
{
    m_draft.reset();
}

std::expected<void, CommandRefusal> EditSession::run(const Command &command)
{
    if (m_protected)
        return std::unexpected(CommandRefusal::Protected);
    // An overlapping draft is committed first, as its own step; that step stays
    // even if the command is then rejected.
    if (m_draft && command.touches.contains(m_draft->line))
        commitDraft();
    if (command.expectedRevision != m_revision)
        return std::unexpected(CommandRefusal::StaleRevision);
    for (const auto &id : command.touches)
        if (!hasLine(document(), id))
            return std::unexpected(CommandRefusal::UnknownLine);
    core::Document working = document();
    if (!command.apply || !command.apply(working))
        return std::unexpected(CommandRefusal::Invalid); // C07: content, selection, dirty, history untouched
    pushState(std::move(working), command.name);
    return {};
}

bool EditSession::canUndo() const
{
    return m_cursor > 0 || m_draft.has_value();
}

bool EditSession::canRedo() const
{
    return m_cursor + 1 < m_states.size();
}

bool EditSession::undo()
{
    commitDraft();
    if (m_cursor == 0)
        return false;
    // Restore the content before the step and the selection recorded with it.
    m_selection = m_states[m_cursor].selection;
    --m_cursor;
    ++m_revision;
    return true;
}

bool EditSession::redo()
{
    if (m_draft || !canRedo())
        return false;
    ++m_cursor;
    m_selection = m_states[m_cursor].selection;
    ++m_revision;
    return true;
}

EditSession::SaveSnapshot EditSession::prepareSave()
{
    commitDraft();
    return SaveSnapshot{document(), contentId(), m_revision};
}

void EditSession::markSaved(ContentId content)
{
    m_saved = content;
}

bool EditSession::isDirty() const
{
    return m_draft.has_value() || !m_saved || *m_saved != contentId();
}

} // namespace hikari::application
