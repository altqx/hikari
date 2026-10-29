#include "hikari/application/macro_validation.h"

#include <gtest/gtest.h>

#include <filesystem>

using namespace hikari::application;

namespace {

std::string at(const char *name)
{
    const auto p = (std::filesystem::temp_directory_path() / "hikari-s4-deps" / name).lexically_normal().u8string();
    return std::string(p.begin(), p.end());
}

ScriptStatus script(const char *name, std::vector<bool> validated, std::vector<std::string> loaded = {},
                    ScriptStatus::State state = ScriptStatus::State::Ready)
{
    ScriptStatus s;
    s.path = at(name);
    s.state = state;
    for (std::size_t i = 0; i < validated.size(); ++i)
        s.info.macros.push_back({std::string(name) + " " + std::to_string(i), "", validated[i], false});
    s.info.loadedFiles = std::move(loaded);
    return s;
}

}

TEST(MacroValidation, OnlyTheScriptsItLoadedAndTheirValidatedMacros)
{
    const std::vector<ScriptStatus> scripts{
        script("unrelated.lua", {true, true}),
        script("dep.lua", {true, false, true}),
        script("main.lua", {true, true}, {at("dep.lua"), at("module.lua")}),
    };
    EXPECT_EQ(dependencyValidations(scripts, at("main.lua")),
              (std::vector<MacroRef>{{at("dep.lua"), 0}, {at("dep.lua"), 2}}));
    EXPECT_TRUE(dependencyValidations(scripts, at("dep.lua")).empty());
    EXPECT_TRUE(dependencyValidations(scripts, at("unrelated.lua")).empty());
    EXPECT_TRUE(dependencyValidations(scripts, at("missing.lua")).empty());
}

TEST(MacroValidation, FirstLoadedFirstEachOnceNeverItself)
{
    const auto spelled = at("b.lua");
    const std::string roundabout = at("sub") + "/../b.lua";
    const std::vector<ScriptStatus> scripts{
        script("a.lua", {true}),
        script("b.lua", {true}),
        script("main.lua", {true}, {at("main.lua"), roundabout, at("a.lua"), spelled}),
    };
    EXPECT_EQ(dependencyValidations(scripts, at("main.lua")),
              (std::vector<MacroRef>{{at("b.lua"), 0}, {at("a.lua"), 0}}));
}

TEST(MacroValidation, ScriptsThatAreNotReadyAreLeftOut)
{
    using State = ScriptStatus::State;
    for (const auto state : {State::Loading, State::LoadFailed, State::Unavailable}) {
        const std::vector<ScriptStatus> scripts{
            script("dep.lua", {true}, {}, state),
            script("main.lua", {true}, {at("dep.lua")}),
        };
        EXPECT_TRUE(dependencyValidations(scripts, at("main.lua")).empty());
    }
}
