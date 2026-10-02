#include "hikari/application/edit_session.h"

#include <algorithm>

namespace hikari::application {

namespace {

bool hasLine(const core::Document &document, core::LineId id)
{
    const auto lines = document.lines();
    return std::ranges::any_of(lines, [&](const core::LineRecord *l) { return l->id == id; });
}

const core::LineRecord *findLine(const core::Document &document, core::LineId id)
{
    for (const auto *line : document.lines())
        if (line->id == id)
            return line;
    return nullptr;
}

void merge(DraftChange &into, const DraftChange &change)
{
    if (change.text)
        into.text = change.text;
    if (change.start)
        into.start = change.start;
    if (change.end)
        into.end = change.end;
    if (change.marginLeft)
        into.marginLeft = change.marginLeft;
    if (change.marginRight)
        into.marginRight = change.marginRight;
    if (change.marginVertical)
        into.marginVertical = change.marginVertical;
}

void apply(core::LineRecord &line, const DraftChange &change)
{
    if (change.text)
        line.text = *change.text;
    if (change.start)
        line.start.value = *change.start;
    if (change.end)
        line.end.value = *change.end;
    if (change.marginLeft)
        line.marginLeft.value = *change.marginLeft;
    if (change.marginRight)
        line.marginRight.value = *change.marginRight;
    if (change.marginVertical)
        line.marginVertical.value = *change.marginVertical;
}

bool sameFields(const core::LineRecord &a, const core::LineRecord &b)
{
    return a.text == b.text && a.start.value == b.start.value && a.end.value == b.end.value &&
           a.marginLeft.value == b.marginLeft.value && a.marginRight.value == b.marginRight.value &&
           a.marginVertical.value == b.marginVertical.value;
}

constexpr std::int64_t kMarginMax = 9999; // the legacy NumCtrl range is 0..9999

std::optional<DraftProblem> problemOf(const core::LineRecord &line)
{
    for (auto m : {line.marginLeft.value, line.marginRight.value, line.marginVertical.value})
        if (m < 0 || m > kMarginMax)
            return DraftProblem::MarginOutOfRange;
    if (line.end.value < line.start.value)
        return DraftProblem::EndBeforeStart;
    return std::nullopt;
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

bool EditSession::navigateTo(core::LineId line)
{
    if (m_draft && m_draft->line != line && draftProblem() && m_policy == InvalidCommitPolicy::Block)
        return false;
    if (m_draft && m_draft->line != line)
        commit(true);
    m_selection.active = line;
    return true;
}

bool EditSession::editDraft(core::LineId line, const DraftChange &change)
{
    if (m_protected || !hasLine(document(), line))
        return false;
    if (m_draft && m_draft->line != line) {
        if (draftProblem() && m_policy == InvalidCommitPolicy::Block)
            return false;
        commit(true); // commit on leave
    }
    if (!m_draft)
        m_draft = Draft{line, {}};
    merge(m_draft->change, change);
    m_selection.active = line;
    return true;
}

bool EditSession::editDraftText(core::LineId line, std::u8string text)
{
    return editDraft(line, DraftChange{.text = std::move(text)});
}

std::optional<core::LineId> EditSession::draftLine() const
{
    return m_draft ? std::optional(m_draft->line) : std::nullopt;
}

std::optional<std::u8string> EditSession::draftText() const
{
    if (!m_draft)
        return std::nullopt;
    if (m_draft->change.text)
        return m_draft->change.text;
    const auto *line = findLine(document(), m_draft->line);
    return line ? std::optional(line->text) : std::nullopt;
}

std::optional<core::LineRecord> EditSession::draftRecord() const
{
    if (!m_draft)
        return std::nullopt;
    const auto *line = findLine(document(), m_draft->line);
    if (!line)
        return std::nullopt;
    core::LineRecord record = *line;
    apply(record, m_draft->change);
    return record;
}

std::optional<DraftProblem> EditSession::draftProblem() const
{
    const auto record = draftRecord();
    return record ? problemOf(*record) : std::nullopt;
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
    return commit(false);
}

bool EditSession::commit(bool leaving)
{
    if (!m_draft)
        return false;
    auto record = draftRecord();
    if (!record) {
        m_draft.reset(); // the Line is gone
        return false;
    }
    if (problemOf(*record)) {
        if (m_policy == InvalidCommitPolicy::Block)
            return false; // the draft and its reason stay
        record->marginLeft.value = std::clamp<std::int64_t>(record->marginLeft.value, 0, kMarginMax);
        record->marginRight.value = std::clamp<std::int64_t>(record->marginRight.value, 0, kMarginMax);
        record->marginVertical.value = std::clamp<std::int64_t>(record->marginVertical.value, 0, kMarginMax);
        if (leaving && record->end.value < record->start.value)
            record->end.value = record->start.value;
    }
    const core::LineId id = m_draft->line;
    m_draft.reset();
    const auto *current = findLine(document(), id);
    if (sameFields(*current, *record))
        return false; // nothing changed
    core::Document next = document();
    next.editLine(id, [&](core::LineRecord &line) {
        line.text = record->text;
        line.start.value = record->start.value;
        line.end.value = record->end.value;
        line.marginLeft.value = record->marginLeft.value;
        line.marginRight.value = record->marginRight.value;
        line.marginVertical.value = record->marginVertical.value;
    });
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
    if (m_draft && command.touches.contains(m_draft->line)) {
        if (draftProblem() && m_policy == InvalidCommitPolicy::Block)
            return std::unexpected(CommandRefusal::InvalidDraft);
        commitDraft();
    }
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
    if (m_draft && draftProblem() && m_policy == InvalidCommitPolicy::Block)
        return false;
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

std::expected<EditSession::SaveSnapshot, DraftProblem> EditSession::prepareSave()
{
    if (auto problem = draftProblem(); problem && m_policy == InvalidCommitPolicy::Block)
        return std::unexpected(*problem); // an invalid draft blocks the save, with its reason
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
