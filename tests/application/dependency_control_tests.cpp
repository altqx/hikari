#include "hikari/application/dependency_control.h"

#include <gtest/gtest.h>

using hikari::application::dependencyControlNeeds;

namespace {

std::vector<std::string> modules(std::string_view script)
{
    std::vector<std::string> out;
    for (const auto &need : dependencyControlNeeds(script))
        out.push_back(need.module + (need.version.empty() ? "" : " " + need.version));
    return out;
}

}

TEST(DependencyControlNeeds, ListsEachModuleWithTheVersionItAsksFor)
{
    const char *script = R"lua(
script_name = "Wobble"
local DepCtrl = require("l0.DependencyControl")
local dep = DepCtrl{
    name = "Wobble", version = "1.0.0", namespace = "ai.Wobble",
    feed = "https://example.org/feed.json",
    {
        {"l0.ASSFoundation", version = "0.5.0", url = "https://github.com/TypesettingTools/ASSFoundation"},
        {"a-mo.LineCollection", version = "1.3.0"},
        "arch.Math",
        -- "commented.Out",
    }
}
local ASS, LineCollection, amath = dep:requireModules()
)lua";
    EXPECT_EQ(modules(script), (std::vector<std::string>{"l0.ASSFoundation 0.5.0", "a-mo.LineCollection 1.3.0", "arch.Math"}));
}

TEST(DependencyControlNeeds, AScriptWithoutARecordNeedsNothing)
{
    EXPECT_TRUE(modules("script_name = 'x'\nlocal util = require 'aegisub.util'\n").empty());
    EXPECT_TRUE(modules("local DepCtrl = require 'l0.DependencyControl'\n").empty());
}

TEST(DependencyControlNeeds, ReadsTheRequireWithoutParentheses)
{
    EXPECT_EQ(modules("DC = require 'l0.DependencyControl'\nlocal d = DC({ {'l0.Functional'} })\n"),
              (std::vector<std::string>{"l0.Functional"}));
}
