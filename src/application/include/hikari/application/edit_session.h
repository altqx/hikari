#pragma once

// One Document's editing state (A1; docs/qt/proposals/edit-transactions.md):
// committed content and its history, the single pending Line draft, the
// selection, and saved identity. Accepted policy: commit on leave, commands
// commit an overlapping draft first, Save commits then saves, Undo restores
// content and selection, one user action is one step, 500 steps (C07, C43).

#include "hikari/core/document.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace hikari::application {

struct Selection {
    std::optional<core::LineId> active;
    std::set<core::LineId> selected;
    // Range selection (G1; legacy lastRow / extendRow, as stable identities):
    // the anchor a Shift range starts from, and the moving end of a keyboard
    // range when the active Line stays put.
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
    StaleRevision,  // prepared against an older revision (C07: nothing changes)
    UnknownLine,    // a declared target Line no longer exists
    Protected,      // the session is a protected reference
    Invalid,        // the command's own validation failed
    InvalidDraft,   // the overlapping draft can't be committed (E63-invalid-commit)
    ReadOnly,       // a macro owns the Document until it ends (A33-transaction)
    GroupBreak,     // the result would break a Line group (G56-contiguity); see lastGroupBreak()
};

// E63-invalid-commit: by default a draft whose End is before its Start, or
// with a margin outside 0..9999, is not committed; the draft and its reason
// stay. The legacy preference commits it: margins clamp to 0..9999 (NumCtrl),
// and leaving the Line sets an End before Start to the Start (EditBox::SetLine).
enum class InvalidCommitPolicy { Block, Legacy };
enum class DraftProblem { EndBeforeStart, MarginOutOfRange };

// Field changes of the pending draft; unset fields keep the committed value.
struct DraftChange {
    std::optional<std::u8string> text;
    std::optional<std::u8string> translation; // TLMode translated role
    std::optional<core::DocumentTime> start, end;
    std::optional<std::int64_t> marginLeft, marginRight, marginVertical;
    // E4: the Line editor's other fields (legacy EditBox::Send's cells
    // COMMENT, LAYER, STYLE, ACTOR, EFFECT) and MicroDVD's authored frames.
    std::optional<bool> comment;
    std::optional<std::int64_t> layer;
    std::optional<std::u8string> style, actor, effect;
    std::optional<std::optional<std::int64_t>> startFrame, endFrame;
};

// A command declares the Lines it touches and mutates a working copy. Returning
// false rejects it; the working copy is then discarded (C07-atomic-rejection).
struct Command {
    std::string name;
    std::uint64_t expectedRevision = 0;
    std::set<core::LineId> touches;
    std::function<bool(core::Document &)> apply;
    // G56-contiguity: a result that orphans or moves an existing group member
    // is rejected. Macros opt out: G56 approves no change to their contract.
    bool keepGroups = true;
};

class EditSession {
public:
    static constexpr std::size_t kHistoryCapacity = 500; // legacy capacity, retained

    explicit EditSession(core::Document document, bool protectedReference = false);

    const core::Document &document() const { return m_states[m_cursor].document; }
    const Selection &selection() const { return m_selection; }
    std::uint64_t revision() const { return m_revision; }
    ContentId contentId() const { return m_states[m_cursor].content; }

    // While a macro runs on this Document it is read-only: drafts, commands,
    // Undo and Redo are refused. Selection still moves.
    void setReadOnly(bool readOnly) { m_readOnly = readOnly; }
    bool isReadOnly() const { return m_readOnly; }
    // A protected reference refuses every command (CommandRefusal::Protected).
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
    std::optional<DraftProblem> draftProblem() const;
    bool commitDraft(); // one history step; false when there is no draft or it is blocked
    // The same step under another name (F3: legacy EditBox::Send with an
    // edition type, such as "Correcting spelling errors in the text field").
    bool commitDraftAs(std::string name);
    // E4: legacy EditBox::Send through SubsGrid::ChangeLine. With several
    // Lines selected, every field the draft holds (Send's modified cells)
    // goes to each selected Line in one step; otherwise this is
    // commitDraftAs(name). E63-invalid-commit blocks as commitDraft does;
    // `leaving` is the commit on leave's legacy End correction for the edited
    // Line (EditBox::SetLine on the next Line).
    bool commitDraftToSelected(std::string name = "Edit Line", bool leaving = false);
    void discardDraft();

    std::expected<void, CommandRefusal> run(const Command &command);

    bool canUndo() const;
    bool canRedo() const;
    bool undo(); // a pending draft is committed first, so Redo can bring it back
    bool redo();
    std::size_t historySize() const { return m_states.size(); }

    // History (G10; legacy HistoryDialog and GLOBAL_UNDO_TO_LAST_SAVE). Step 0
    // is the opened Document; each later step names its command and the
    // active Line it was recorded with.
    struct HistoryStep {
        std::string name;
        std::optional<core::LineId> active;
        std::size_t activeRow = 0; // 1-based row of that Line in the step's Document, 0 for none
    };
    std::vector<HistoryStep> history() const;
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
    void markSaved(ContentId content);
    // No step is saved any more (legacy RemoveLastIterSave: the file was removed).
    void markUnsaved() { m_saved.reset(); }
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
    };

    void pushState(core::Document document, std::string name);
    bool commit(bool leaving, std::string name = "Edit Line");

    std::deque<State> m_states;
    std::size_t m_cursor = 0;
    Selection m_selection;
    std::optional<Draft> m_draft;
    std::optional<ContentId> m_saved;
    std::uint64_t m_revision = 0;
    std::uint64_t m_nextContent = 1;
    bool m_protected = false;
    bool m_readOnly = false;
    std::vector<core::LineId> m_lastGroupBreak;
    std::uint64_t m_groupBreakCount = 0;
    InvalidCommitPolicy m_policy = InvalidCommitPolicy::Block;
};

} // namespace hikari::application
