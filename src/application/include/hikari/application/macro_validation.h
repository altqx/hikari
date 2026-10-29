#pragma once

#include "hikari/application/automation.h"

#include <string>
#include <vector>

namespace hikari::application {

struct MacroRef {
    std::string path; // as the automation manager lists it
    int ordinal = 0;  // registration index in the script
    bool operator==(const MacroRef &) const = default;
};

// Validators to run first: each macro with one in every other ready script whose file `path` loaded,
// in first-load order. Empty when `path` is not listed.
std::vector<MacroRef> dependencyValidations(const std::vector<ScriptStatus> &scripts, const std::string &path);

}
