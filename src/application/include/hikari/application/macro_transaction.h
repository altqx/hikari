#pragma once

// A macro's staged edits against one Document (L4, A33-transaction; the
// accepted policy in docs/qt/automation.md). The macro starts from a snapshot
// with the Document's revision, explicit target, selection and active Line;
// its result is validated against that revision and applied atomically as
// one undo step through the EditSession, or rejected without changing
// anything (stale revision, protected reference, a Line gone meanwhile).

#include "hikari/application/automation.h"
#include "hikari/application/edit_session.h"

#include <expected>

namespace hikari::application {

// A pending draft on the target is committed first (T43-reconcile); false
// when it cannot be. The snapshot then reflects the committed Document.
std::expected<MacroSnapshot, CommandRefusal> snapshotForMacro(EditSession &session);

enum class MacroApplyError {
    Refused,           // the EditSession refused the command (see refusal)
    UnsupportedChange, // style or Script Info edits are not applied yet; nothing changed
};

struct MacroApplyFailure {
    MacroApplyError error = MacroApplyError::Refused;
    std::optional<CommandRefusal> refusal;
};

// Applies `result` as one history step named "Automation: <name>". A result
// identical to the snapshot changes nothing and adds no step. The returned
// selection (script indices, legacy rules) becomes the session's selection.
std::expected<void, MacroApplyFailure> applyMacroResult(EditSession &session, const MacroSnapshot &snapshot,
                                                        const MacroResult &result, const std::string &name);

} // namespace hikari::application
