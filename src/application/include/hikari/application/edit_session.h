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

namespace hikari::application {

struct Selection {
    std::optional<core::LineId> active;
    std::set<core::LineId> selected;
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
};

// A command declares the Lines it touches and mutates a working copy. Returning
// false rejects it; the working copy is then discarded (C07-atomic-rejection).
struct Command {
    std::string name;
    std::uint64_t expectedRevision = 0;
    std::set<core::LineId> touches;
    std::function<bool(core::Document &)> apply;
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
    std::optional<DraftProblem> draftProblem() const;
    bool commitDraft(); // one history step; false when there is no draft or it is blocked
    void discardDraft();

    std::expected<void, CommandRefusal> run(const Command &command);

    bool canUndo() const;
    bool canRedo() const;
    bool undo(); // a pending draft is committed first, so Redo can bring it back
    bool redo();
    std::size_t historySize() const { return m_states.size(); }

    // Save: commits the draft, then gives the exact snapshot to write and the
    // identity that becomes saved once the write is reported as Written.
    struct SaveSnapshot {
        core::Document document;
        ContentId content;
        std::uint64_t revision = 0;
    };
    std::expected<SaveSnapshot, DraftProblem> prepareSave();
    void markSaved(ContentId content);
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
    bool commit(bool leaving);

    std::deque<State> m_states;
    std::size_t m_cursor = 0;
    Selection m_selection;
    std::optional<Draft> m_draft;
    std::optional<ContentId> m_saved;
    std::uint64_t m_revision = 0;
    std::uint64_t m_nextContent = 1;
    bool m_protected = false;
    bool m_readOnly = false;
    InvalidCommitPolicy m_policy = InvalidCommitPolicy::Block;
};

} // namespace hikari::application
