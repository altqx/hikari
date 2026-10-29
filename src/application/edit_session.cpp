#include "hikari/application/edit_session.h"

#include "hikari/core/line_groups.h"

#include <algorithm>
#include <atomic>

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
    if (change.translation) {
        into.translationCarried = change.restoresProvenance
                                      ? change.translationCarried
                                      : change.translationCarried && (!into.translation || into.translationCarried);
        into.translation = change.translation;
    }
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
    if (change.comment)
        into.comment = change.comment;
    if (change.layer)
        into.layer = change.layer;
    if (change.style)
        into.style = change.style;
    if (change.actor)
        into.actor = change.actor;
    if (change.effect)
        into.effect = change.effect;
    if (change.startFrame)
        into.startFrame = change.startFrame;
    if (change.endFrame)
        into.endFrame = change.endFrame;
}

// Fields `change` gives a new value are no longer kept to the draft's Line.
void unmark(DraftChange &own, const DraftChange &change, const DraftChange &before)
{
    if (change.text && change.text != before.text)
        own.text.reset();
    if (change.translation && change.translation != before.translation)
        own.translation.reset();
    if (change.start && change.start != before.start)
        own.start.reset();
    if (change.end && change.end != before.end)
        own.end.reset();
    if (change.marginLeft && change.marginLeft != before.marginLeft)
        own.marginLeft.reset();
    if (change.marginRight && change.marginRight != before.marginRight)
        own.marginRight.reset();
    if (change.marginVertical && change.marginVertical != before.marginVertical)
        own.marginVertical.reset();
    if (change.comment && change.comment != before.comment)
        own.comment.reset();
    if (change.layer && change.layer != before.layer)
        own.layer.reset();
    if (change.style && change.style != before.style)
        own.style.reset();
    if (change.actor && change.actor != before.actor)
        own.actor.reset();
    if (change.effect && change.effect != before.effect)
        own.effect.reset();
    if (change.startFrame && change.startFrame != before.startFrame)
        own.startFrame.reset();
    if (change.endFrame && change.endFrame != before.endFrame)
        own.endFrame.reset();
}

void apply(core::LineRecord &line, const DraftChange &change)
{
    if (change.text)
        line.text = *change.text;
    if (change.translation)
        line.translation = *change.translation;
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
    if (change.comment)
        line.comment = *change.comment;
    if (change.layer)
        line.layer.value = *change.layer;
    if (change.style)
        line.style = *change.style;
    if (change.actor)
        line.actor = *change.actor;
    if (change.effect)
        line.effect = *change.effect;
    if (change.startFrame)
        line.startFrame = *change.startFrame;
    if (change.endFrame)
        line.endFrame = *change.endFrame;
}

bool sameFields(const core::LineRecord &a, const core::LineRecord &b)
{
    return a.text == b.text && a.translation == b.translation && a.start.value == b.start.value && a.end.value == b.end.value &&
           a.marginLeft.value == b.marginLeft.value && a.marginRight.value == b.marginRight.value &&
           a.marginVertical.value == b.marginVertical.value && a.comment == b.comment &&
           a.layer.value == b.layer.value && a.style == b.style && a.actor == b.actor && a.effect == b.effect &&
           a.startFrame == b.startFrame && a.endFrame == b.endFrame;
}

constexpr std::int64_t kMarginMax = 9999;

std::optional<DraftProblem> problemOf(const core::LineRecord &line)
{
    for (auto m : {line.marginLeft.value, line.marginRight.value, line.marginVertical.value})
        if (m < 0 || m > kMarginMax)
            return DraftProblem::MarginOutOfRange;
    if (line.end.value < line.start.value)
        return DraftProblem::EndBeforeStart;
    return std::nullopt;
}

}

EditSession::EditSession(core::Document document, bool protectedReference) : m_protected(protectedReference)
{
    static std::atomic<std::uint64_t> nextSerial{1};
    m_serial = nextSerial++;
    m_states.push_back(State{std::move(document), ContentId{m_nextContent++}, {}, "Open"});
    m_saved = m_states.front().content;
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
    if (m_protected || m_readOnly || !hasLine(document(), line))
        return false;
    if (m_draft && m_draft->line != line) {
        if (draftProblem() && m_policy == InvalidCommitPolicy::Block)
            return false;
        commit(true);
    }
    if (!m_draft)
        m_draft = Draft{line, {}};
    const DraftChange before = m_draft->change;
    merge(m_draft->change, change);
    unmark(m_draft->own, change, before);
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
    while (m_states.size() > m_cursor + 1)
        m_states.pop_back();
    m_states.push_back(State{std::move(document), ContentId{m_nextContent++}, m_selection, std::move(name)});
    if (m_states.size() > kHistoryCapacity)
        m_states.pop_front(); // the saved identity survives its entry
    m_cursor = m_states.size() - 1;
    ++m_revision;
}

bool EditSession::commitDraft()
{
    return commit(false);
}

bool EditSession::commitDraftAs(std::string name)
{
    return commit(false, std::move(name));
}

bool EditSession::commit(bool leaving, std::string name)
{
    if (!m_draft)
        return false;
    auto record = draftRecord();
    if (!record) {
        m_draft.reset();
        return false;
    }
    if (problemOf(*record)) {
        if (m_policy == InvalidCommitPolicy::Block)
            return false;
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
        return false;
    core::Document next = document();
    next.editLine(id, [&](core::LineRecord &line) {
        line.text = record->text;
        line.translation = record->translation;
        line.start.value = record->start.value;
        line.end.value = record->end.value;
        line.marginLeft.value = record->marginLeft.value;
        line.marginRight.value = record->marginRight.value;
        line.marginVertical.value = record->marginVertical.value;
        line.comment = record->comment;
        line.layer.value = record->layer.value;
        line.style = record->style;
        line.actor = record->actor;
        line.effect = record->effect;
        line.startFrame = record->startFrame;
        line.endFrame = record->endFrame;
    });
    pushState(std::move(next), std::move(name));
    return true;
}

bool EditSession::commitDraftToSelected(std::string name, bool leaving, const Companion &companion)
{
    if (!m_draft)
        return false;
    if (m_selection.selected.size() < 2)
        return commit(leaving, std::move(name));
    auto record = draftRecord();
    if (!record) {
        m_draft.reset();
        return false;
    }
    if (problemOf(*record)) {
        if (m_policy == InvalidCommitPolicy::Block)
            return false;
        record->marginLeft.value = std::clamp<std::int64_t>(record->marginLeft.value, 0, kMarginMax);
        record->marginRight.value = std::clamp<std::int64_t>(record->marginRight.value, 0, kMarginMax);
        record->marginVertical.value = std::clamp<std::int64_t>(record->marginVertical.value, 0, kMarginMax);
    }
    // Every field the draft holds, even one back at its committed value.
    const DraftChange cells = m_draft->change;
    const DraftChange own = m_draft->own;
    const core::LineId edited = m_draft->line;
    m_draft.reset();
    core::Document next = document();
    bool changed = false;
    for (const auto *line : document().lines()) {
        if (!m_selection.selected.contains(line->id))
            continue;
        core::LineRecord updated = *line;
        const bool other = line->id != edited;
        if (cells.text && !(other && own.text))
            updated.text = record->text;
        if (cells.translation && !(other && (own.translation || cells.translationCarried)))
            updated.translation = record->translation;
        if (cells.start && !(other && own.start))
            updated.start.value = record->start.value;
        if (cells.end && !(other && own.end))
            updated.end.value = record->end.value;
        if (cells.marginLeft && !(other && own.marginLeft))
            updated.marginLeft.value = record->marginLeft.value;
        if (cells.marginRight && !(other && own.marginRight))
            updated.marginRight.value = record->marginRight.value;
        if (cells.marginVertical && !(other && own.marginVertical))
            updated.marginVertical.value = record->marginVertical.value;
        if (cells.comment && !(other && own.comment))
            updated.comment = record->comment;
        if (cells.layer && !(other && own.layer))
            updated.layer.value = record->layer.value;
        if (cells.style && !(other && own.style))
            updated.style = record->style;
        if (cells.actor && !(other && own.actor))
            updated.actor = record->actor;
        if (cells.effect && !(other && own.effect))
            updated.effect = record->effect;
        if (cells.startFrame && !(other && own.startFrame))
            updated.startFrame = record->startFrame;
        if (cells.endFrame && !(other && own.endFrame))
            updated.endFrame = record->endFrame;
        if (line->id != edited && companion &&
            (cells.translationCarried || (!cells.translation && (cells.text || cells.style))))
            companion(*line, updated);
        if (leaving && line->id == edited && m_policy == InvalidCommitPolicy::Legacy &&
            updated.end.value < updated.start.value)
            updated.end.value = updated.start.value;
        if (sameFields(*line, updated))
            continue;
        changed = true;
        next.editLine(line->id, [&](core::LineRecord &l) {
            l.text = updated.text;
            l.translation = updated.translation;
            l.start.value = updated.start.value;
            l.end.value = updated.end.value;
            l.marginLeft.value = updated.marginLeft.value;
            l.marginRight.value = updated.marginRight.value;
            l.marginVertical.value = updated.marginVertical.value;
            l.comment = updated.comment;
            l.layer.value = updated.layer.value;
            l.style = updated.style;
            l.actor = updated.actor;
            l.effect = updated.effect;
            l.startFrame = updated.startFrame;
            l.endFrame = updated.endFrame;
        });
    }
    if (!changed)
        return false;
    pushState(std::move(next), std::move(name));
    return true;
}

void EditSession::discardDraft()
{
    m_draft.reset();
}

void EditSession::keepDraftFieldsToItsLine(const DraftChange &fields)
{
    if (m_draft)
        merge(m_draft->own, fields);
}

std::expected<void, CommandRefusal> EditSession::run(const Command &command)
{
    if (m_protected)
        return std::unexpected(CommandRefusal::Protected);
    if (m_readOnly)
        return std::unexpected(CommandRefusal::ReadOnly);
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
        return std::unexpected(CommandRefusal::Invalid);
    if (command.keepGroups) {
        m_lastGroupBreak = core::groupBreaks(document(), working);
        if (!m_lastGroupBreak.empty()) {
            ++m_groupBreakCount;
            return std::unexpected(CommandRefusal::GroupBreak);
        }
    }
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
    if (m_protected || m_readOnly)
        return false;
    if (m_draft && draftProblem() && m_policy == InvalidCommitPolicy::Block)
        return false;
    commitDraft();
    if (m_cursor == 0)
        return false;
    m_selection = m_states[m_cursor].selection;
    --m_cursor;
    ++m_revision;
    return true;
}

bool EditSession::redo()
{
    if (m_protected || m_readOnly || m_draft || !canRedo())
        return false;
    ++m_cursor;
    m_selection = m_states[m_cursor].selection;
    ++m_revision;
    return true;
}

std::vector<EditSession::HistoryStep> EditSession::history() const
{
    std::vector<HistoryStep> out;
    for (std::size_t i = 0; i < m_states.size(); ++i) {
        HistoryStep step{m_states[i].name, m_states[i].selection.active, 0};
        if (step.active) {
            const auto lines = m_states[i].document.lines();
            for (std::size_t row = 0; row < lines.size(); ++row)
                if (lines[row]->id == *step.active)
                    step.activeRow = row + 1;
        }
        out.push_back(std::move(step));
    }
    return out;
}

std::optional<std::size_t> EditSession::savedStep() const
{
    for (std::size_t i = 0; i < m_states.size(); ++i)
        if (m_states[i].content == m_saved)
            return i;
    return std::nullopt;
}

bool EditSession::goTo(std::size_t step)
{
    if (m_protected || m_readOnly)
        return false;
    if (m_draft) {
        if (draftProblem() && m_policy == InvalidCommitPolicy::Block)
            return false;
        commitDraft(); // may drop the redo steps, as any new step does
    }
    if (step >= m_states.size())
        return false;
    if (step == m_cursor)
        return true;
    m_selection = step < m_cursor ? m_states[step + 1].selection : m_states[step].selection;
    m_cursor = step;
    ++m_revision;
    return true;
}

std::expected<EditSession::SaveSnapshot, DraftProblem> EditSession::prepareSave()
{
    if (auto problem = draftProblem(); problem && m_policy == InvalidCommitPolicy::Block)
        return std::unexpected(*problem);
    commitDraft();
    auto &versions = m_preparedVersions[contentId().value];
    versions.clear();
    for (const auto *line : document().lines())
        if (line->changeVersion != 0)
            versions.push_back(line->changeVersion);
    return SaveSnapshot{document(), contentId(), m_revision};
}

void EditSession::markSaved(ContentId content)
{
    m_saved = content;
    if (const auto prepared = m_preparedVersions.find(content.value); prepared != m_preparedVersions.end()) {
        m_savedVersions.insert(prepared->second.begin(), prepared->second.end());
        m_preparedVersions.erase(prepared);
        return;
    }
    for (const auto &state : m_states)
        if (state.content == content)
            for (const auto *line : state.document.lines())
                if (line->changeVersion != 0)
                    m_savedVersions.insert(line->changeVersion);
}

int EditSession::changeState(const core::LineRecord &line) const
{
    if (line.changeVersion == 0)
        return 0;
    return m_savedVersions.contains(line.changeVersion) ? 2 : 1;
}

bool EditSession::amendScriptInfo(std::u8string_view key, std::u8string value)
{
    return m_states[m_cursor].document.setScriptInfo(key, std::move(value));
}

bool EditSession::amendHikariProperty(std::u8string_view key, std::u8string value)
{
    return m_states[m_cursor].document.setHikariProperty(key, std::move(value));
}

bool EditSession::isDirty() const
{
    return m_draft.has_value() || !m_saved || *m_saved != contentId();
}

}
