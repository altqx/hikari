#pragma once

#include "hikari/application/automation.h"
#include "hikari/application/edit_session.h"

#include <expected>

namespace hikari::application {

// Commits a pending draft on the target first; refused when it cannot.
std::expected<MacroSnapshot, CommandRefusal> snapshotForMacro(EditSession &session);
// Revision 0.
MacroSnapshot macroSnapshotOf(const core::Document &doc, const Selection &selection);

// In TLMode the script's text is the translation.
MacroDialogueLine macroLineOf(const core::LineRecord &line);
core::LineRecord lineOfMacro(const MacroDialogueLine &line);

enum class MacroApplyError {
    Refused,
};

struct MacroApplyFailure {
    MacroApplyError error = MacroApplyError::Refused;
    std::optional<CommandRefusal> refusal;
};

// One history step named "Automation: <name>", or refused without changes
// (stale revision, Line gone, MacroResult::valid false). Unchanged entries
// keep their bytes; a result equal to the snapshot adds no step.
std::expected<void, MacroApplyFailure> applyMacroResult(EditSession &session, const MacroSnapshot &snapshot,
                                                        const MacroResult &result, const std::string &name);

}
