#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

struct ModuleNeed {
    std::string module; // a DependencyControl namespace, e.g. "l0.ASSFoundation"
    std::string version; // the least version asked for; empty: any
};

// The modules a script's DependencyControl record asks for, in order; empty without one.
std::vector<ModuleNeed> dependencyControlNeeds(std::string_view script);

}
