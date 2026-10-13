#pragma once

// S4-run-validation (docs/qt/compatibility-decisions.md): which validation
// functions a macro run calls. Legacy "Run the last loaded script" (and a
// script hotkey, through the menu item) rebuilt the Automation menu first,
// which validated every Document-script macro (Automation.cpp:1375, 1434),
// and then validated the one it ran (LuaCommand::Validate). The rewrite
// validates the macro that runs and, before it, the macros of the scripts it
// depends on, and nothing else.
//
// Dependency, as the automation model defines it: a macro can call nothing
// of another macro through the automation API (aegisub.* has no such call),
// and the modules a script requires (directly, or through DependencyControl's
// requireModules, which calls require) have no validation functions of their
// own. What a macro can depend on is another loaded script's code: script S
// depends on script T when S's top level loaded T's file as Lua code (by
// require or include, directly or from a file it loaded), so S's macros run
// T's code. The helper reports those files (ScriptInfo::loadedFiles).

#include "hikari/application/automation.h"

#include <string>
#include <vector>

namespace hikari::application {

struct MacroRef {
    std::string path; // the script, as the automation manager lists it
    int ordinal = 0;  // the macro's registration index in that script
    bool operator==(const MacroRef &) const = default;
};

// The validation functions to call before the macro of `path` validates and
// runs: for each ready script (another path) whose file `path`'s script
// loaded, in the order its files were first loaded and each script once,
// every macro of it with a validation function, in registration order. Files
// are compared as normalized absolute paths. Empty when the script is not
// listed or depends on nothing.
std::vector<MacroRef> dependencyValidations(const std::vector<ScriptStatus> &scripts, const std::string &path);

} // namespace hikari::application
