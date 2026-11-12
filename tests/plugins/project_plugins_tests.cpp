#include "hikari/plugins/project_plugins.h"

#include <gtest/gtest.h>

using namespace hikari::plugins;
using hikari::api::Json;

namespace {

InstalledPlugin installed(bool enabled)
{
    InstalledPlugin plugin;
    plugin.enabled = enabled;
    return plugin;
}

}

TEST(ProjectPlugins, TheProjectOverridesTheUsersSwitchPerId)
{
    const ProjectPlugins project{{"on"}, {"off"}, {}};
    EXPECT_TRUE(pluginOn("on", installed(false), &project));
    EXPECT_FALSE(pluginOn("off", installed(true), &project));
    EXPECT_TRUE(pluginOn("other", installed(true), &project));
    EXPECT_FALSE(pluginOn("other", installed(false), &project));
    EXPECT_FALSE(pluginOn("on", installed(false), nullptr));
}

TEST(ProjectPlugins, ARevokedPluginStaysOffWhateverTheProjectSays)
{
    InstalledPlugin revoked = installed(false);
    revoked.revoked = Revoked{"bad", ""};
    const ProjectPlugins project{{"x"}, {}, {}};
    EXPECT_FALSE(pluginOn("x", revoked, &project));
}

TEST(ProjectPlugins, APluginRunsWhenAnyWindowHasItOn)
{
    const ProjectPlugins on{{"x"}, {}, {}};
    const ProjectPlugins off{{}, {"x"}, {}};
    EXPECT_TRUE(pluginRunsAnywhere("x", installed(false), {&off, &on}));
    EXPECT_FALSE(pluginRunsAnywhere("x", installed(true), {&off}));
    EXPECT_TRUE(pluginRunsAnywhere("x", installed(true), {&off, nullptr}));
    EXPECT_TRUE(pluginRunsAnywhere("x", installed(true), {}));
    EXPECT_FALSE(pluginRunsAnywhere("x", installed(false), {}));
}

TEST(ProjectPlugins, ProjectSettingsOverrideKeyByKey)
{
    const Json stored = Json::object({{"size", Json(12)}, {"name", Json("mine")}});
    ProjectPlugins project;
    project.settings.emplace("x", Json::object({{"size", Json(20)}, {"extra", Json(true)}}));
    const Json merged = projectSettings("x", stored, &project);
    EXPECT_EQ(*merged.find("size"), Json(20));
    EXPECT_EQ(*merged.find("name"), Json("mine"));
    EXPECT_EQ(*merged.find("extra"), Json(true));
    EXPECT_EQ(projectSettings("y", stored, &project), stored);
    EXPECT_EQ(projectSettings("x", stored, nullptr), stored);
}

TEST(ProjectPlugins, EnabledIdsThatArentInstalledAreMissing)
{
    Registry registry;
    registry.emplace("have", installed(true));
    const ProjectPlugins project{{"have", "gone", "also-gone", "gone"}, {"off-missing"}, {}};
    EXPECT_EQ(missingPlugins(project, registry), (std::vector<std::string>{"gone", "also-gone"}));
}
