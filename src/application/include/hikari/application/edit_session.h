#pragma once

#include "hikari/core/document.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <unordered_set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hikari::application {

struct Selection {
    std::optional<core::LineId> active;
    std::set<core::LineId> selected;
    // `anchor`: where a Shift range starts; `extent`: the moving end of a
    // keyboard range while the active Line stays put.
    std::optional<core::LineId> anchor;
    std::optional<core::LineId> extent;
    bool operator==(const Selection &) const = default;
};

// Identity of one committed content state. Distinct commits never share one,
// even when their bytes happen to match; Undo returns to an earlier identity.
struct ContentId {
    std::uint64_t value = 0;
    auto operator<=>(const ContentId &) const = default;
};

enum class CommandRefusal {
    StaleRevision,  // prepared against an older revision; nothing changes
    UnknownLine,    // a declared target Line no longer exists
    Protected,
    Invalid,        // the command's own validation failed
    InvalidDraft,   // the overlapping draft can't be committed
    ReadOnly,       // a macro owns the Document until it ends
    GroupBreak,     // the result would break a Line group; see lastGroupBreak()
};

// Block: a draft whose End is before its Start, or with a margin outside
// 0..9999, is not committed; the draft and its reason stay. `Legacy` commits it:
// margins clamp to 0..9999, and leaving the Line sets such an End to the Start.
enum class InvalidCommitPolicy { Block, Legacy };
enum class DraftProblem { EndBeforeStart, MarginOutOfRange };

// Field changes of the pending draft; unset fields keep the committed value.
struct DraftChange {
    std::optional<std::u8string> text;
    std::optional<std::u8string> translation; // TLMode translated role
    std::optional<core::DocumentTime> start, end;
    std::optional<std::int64_t> marginLeft, marginRight, marginVertical;
    std::optional<bool> comment;
    std::optional<std::int64_t> layer;
    std::optional<std::u8string> style, actor, effect;
    std::optional<std::optional<std::int64_t>> startFrame, endFrame; // MicroDVD's authored frames
    // The translation only followed the text or Style cells and was not typed;
    // several selected Lines then keep their own (commitDraftToSelected).
    bool translationCarried = false;
    // Undo and Redo in the field put back a step's translation as it was,
    // carried or typed: translationCarried then replaces the draft's.
    bool restoresProvenance = false;
};

// A command declares the Lines it touches and mutates a working copy. Returning
// false rejects it; the working copy is then discarded.
struct Command {
    std::string name;
    std::uint64_t expectedRevision = 0;
    std::set<core::LineId> touches;
    std::function<bool(core::Document &)> apply;
    // A result that orphans or moves an existing group member is rejected.
    bool keepGroups = true;
};

class EditSession {
public:
    static constexpr std::size_t kHistoryCapacity = 500;

    explicit EditSession(core::Document document, bool protectedReference = false);

    const core::Document &document() const { return m_states[m_cursor].document; }
    const Selection &selection() const { return m_selection; }
    std::uint64_t revision() const { return m_revision; }
    // Unique among the process's sessions: what scopes a cache keyed by
    // LineId, which is unique only within one Document.
    std::uint64_t serial() const { return m_serial; }
    ContentId contentId() const { return m_states[m_cursor].content; }

    // While a macro runs on this Document it is read-only: drafts, commands,
    // Undo and Redo are refused. Selection still moves.
    void setReadOnly(bool readOnly) { m_readOnly = readOnly; }
    bool isReadOnly() const { return m_readOnly; }
    // A protected reference refuses every command (CommandRefusal::Protected),
    // drafts, Undo and Redo.
    void setProtected(bool protectedReference) { m_protected = protectedReference; }
    bool isProtected() const { return m_protected; }

    // Selection changes are not history steps.
    void setSelection(Selection selection);
    // Moving the active Line commits a pending draft on another Line first.
    // False, and nothing moves, when that draft can't be committed.
    bool navigateTo(core::LineId line);

    void setInvalidCommitPolicy(InvalidCommitPolicy policy) { m_policy = policy; }
    InvalidCommitPolicy invalidCommitPolicy() const { return m_policy; }

    // Draft: at most one, on the active Line. Editing another Line commits
    // the current draft first; false when that commit is blocked.
    bool editDraft(core::LineId line, const DraftChange &change);
    bool editDraftText(core::LineId line, std::u8string text);
    std::optional<core::LineId> draftLine() const;
    std::optional<std::u8string> draftText() const;
    // The draft's Line with its changes applied, as it would be committed.
    std::optional<core::LineRecord> draftRecord() const;
    // The pending draft's Line and its changes (recovery keeps them pending).
    std::optional<std::pair<core::LineId, DraftChange>> draftChange() const
    {
        return m_draft ? std::optional(std::pair(m_draft->line, m_draft->change)) : std::nullopt;
    }
    // Null without a draft.
    const DraftChange *draftChangeView() const { return m_draft ? &m_draft->change : nullptr; }
    std::optional<DraftProblem> draftProblem() const;
    bool commitDraft(); // one history step; false when there is no draft or it is blocked
    // The same step under another name.
    bool commitDraftAs(std::string name);
    // With several Lines selected, every field the draft holds goes to each
    // selected Line in one step; otherwise this is commitDraftAs(name). Blocks
    // as commitDraft does; `leaving` applies the commit-on-leave End
    // correction. A carried translation (DraftChange::translationCarried) is
    // not copied: each other Line keeps its own, which `companion` may write
    // again from the Line before and after; so too when only the text or Style
    // changed.
    using Companion = std::function<void(const core::LineRecord &before, core::LineRecord &after)>;
    bool commitDraftToSelected(std::string name = "Edit Line", bool leaving = false, const Companion &companion = {});
    void discardDraft();
    // The draft's fields that `fields` holds go to its own Line alone, however
    // many Lines are selected, until it is committed or discarded or a later
    // edit changes them; its other fields still go to every selected Line.
    void keepDraftFieldsToItsLine(const DraftChange &fields);
    // The fields kept to the draft's Line (presence alone); null without a draft.
    const DraftChange *draftOwnFields() const { return m_draft ? &m_draft->own : nullptr; }

    std::expected<void, CommandRefusal> run(const Command &command);

    bool canUndo() const;
    bool canRedo() const;
    bool undo(); // a pending draft is committed first, so Redo can bring it back
    bool redo();
    std::size_t historySize() const { return m_states.size(); }

    // Step 0 is the opened Document; each later step names its command and
    // the active Line it was recorded with.
    struct HistoryStep {
        std::string name;
        std::optional<core::LineId> active;
        std::size_t activeRow = 0; // 1-based row of that Line in the step's Document, 0 for none
    };
    std::vector<HistoryStep> history() const;
    // The Document a kept history step holds (0 <= step < historySize()).
    const core::Document &stepDocument(std::size_t step) const { return m_states[step].document; }
    std::size_t historyCursor() const { return m_cursor; }
    // The step whose content is saved, while it is still in history.
    std::optional<std::size_t> savedStep() const;
    // Jumps to a step as a run of Undo or Redo would (the selection recorded
    // with it comes back); a pending draft is committed first. Redo steps stay.
    bool goTo(std::size_t step);

    // Save: commits the draft, then gives the exact snapshot to write and the
    // identity that becomes saved once the write is reported as Written.
    struct SaveSnapshot {
        core::Document document;
        ContentId content;
        std::uint64_t revision = 0;
    };
    std::expected<SaveSnapshot, DraftProblem> prepareSave();
    // The members the last command refused with GroupBreak would have broken.
    const std::vector<core::LineId> &lastGroupBreak() const { return m_lastGroupBreak; }
    // How many commands GroupBreak has refused, so the shell can offer removal.
    std::uint64_t groupBreakCount() const { return m_groupBreakCount; }
    // Also marks that content's changed Lines saved.
    void markSaved(ContentId content);
    // 0 not changed since loading, 1 changed, 2 changed and saved since. A
    // saved copy stays saved in every history step that holds it.
    int changeState(const core::LineRecord &line) const;
    // No step is saved any more (the file was removed).
    void markUnsaved() { m_saved.reset(); }
    // Sets the current step's Script Info in place, with no history step; the
    // content identity, the save point and the revision stay, so nothing
    // becomes modified and no prepared command goes stale. False when the
    // Document is not ASS.
    bool amendScriptInfo(std::u8string_view key, std::u8string value);
    // The same for the [Hikari] section.
    bool amendHikariProperty(std::u8string_view key, std::u8string value);
    bool isDirty() const; // committed content differs from the save point, or a draft is pending

private:
    struct State {
        core::Document document;
        ContentId content;
        Selection selection;
        std::string name;
    };
    struct Draft {
        core::LineId line;
        DraftChange change;
        DraftChange own; // keepDraftFieldsToItsLine
    };

    void pushState(core::Document document, std::string name);
    bool commit(bool leaving, std::string name = "Edit Line");

    std::deque<State> m_states;
    std::size_t m_cursor = 0;
    Selection m_selection;
    std::optional<Draft> m_draft;
    std::optional<ContentId> m_saved;
    std::unordered_set<std::uint64_t> m_savedVersions;
    // Kept until the write is reported; the step may leave history meanwhile.
    std::map<std::uint64_t, std::vector<std::uint64_t>> m_preparedVersions;
    std::uint64_t m_revision = 0;
    std::uint64_t m_serial = 0;
    std::uint64_t m_nextContent = 1;
    bool m_protected = false;
    bool m_readOnly = false;
    std::vector<core::LineId> m_lastGroupBreak;
    std::uint64_t m_groupBreakCount = 0;
    InvalidCommitPolicy m_policy = InvalidCommitPolicy::Block;
};

} // namespace hikari::application
