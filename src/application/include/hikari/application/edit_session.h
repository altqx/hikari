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

    // Selection changes are not history steps.
    void setSelection(Selection selection);
    // Moving the active Line commits a pending draft on another Line first.
    void navigateTo(core::LineId line);

    // Draft: at most one, on the active Line.
    bool editDraftText(core::LineId line, std::u8string text);
    std::optional<core::LineId> draftLine() const;
    std::optional<std::u8string> draftText() const;
    bool commitDraft(); // one history step; false when there is no draft
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
    SaveSnapshot prepareSave();
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
        std::u8string text;
    };

    void pushState(core::Document document, std::string name);

    std::deque<State> m_states;
    std::size_t m_cursor = 0;
    Selection m_selection;
    std::optional<Draft> m_draft;
    std::optional<ContentId> m_saved;
    std::uint64_t m_revision = 0;
    std::uint64_t m_nextContent = 1;
    bool m_protected = false;
};

} // namespace hikari::application
