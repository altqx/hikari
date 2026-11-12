#include "fake_host.h"
#include "script_client.h"

#include <gtest/gtest.h>

using namespace hikari;
using namespace hikari::api;
using namespace hikari::api::testing;

namespace {

class Plugins : public ::testing::Test {
protected:
    Result call(std::string_view operation, std::string_view params, Caller caller = aiCaller())
    {
        auto parsed = Json::parse(params);
        EXPECT_TRUE(parsed) << params;
        return client.call(caller, operation, *parsed);
    }

    Caller approved(std::string digest = std::string(FakePlugins::kDigest))
    {
        Caller caller = aiCaller();
        caller.approvedPackage = std::move(digest);
        return caller;
    }

    FakeHost host;
    FakePlugins &plugins = host.fakePlugins;
    Api api{host.host()};
    ScriptClient client{api};
};

const std::string &code(const Result &result)
{
    static const std::string none;
    return result ? none : result.error().code;
}

TEST_F(Plugins, ListGivesEachPluginWithItsTierAndCommands)
{
    const Result result = call("plugins.list", "{}");
    ASSERT_TRUE(result) << result.error().message;
    const Json &row = result->find("plugins")->asArray().at(0);
    EXPECT_EQ(row.find("id")->asString(), "org.example.word-count");
    EXPECT_EQ(row.find("tier")->asString(), "restricted");
    EXPECT_FALSE(row.find("signed")->asBool());
    EXPECT_EQ(row.find("commands")->asArray().at(0).find("command")->asString(), "plugin.org.example.word-count.count");
}

TEST_F(Plugins, ManifestReadsAnInstalledPluginOnly)
{
    EXPECT_TRUE(call("plugins.manifest", R"({"plugin": "org.example.word-count"})"));
    EXPECT_EQ(code(call("plugins.manifest", R"({"plugin": "org.example.missing"})")), errors::kNotFound);
}

TEST_F(Plugins, ScaffoldWritesTheGuideTemplateRestrictedByDefault)
{
    const Result result = call("plugins.scaffold", R"({"plugin": "org.example.word-count", "name": "Word count",
        "commands": [{"id": "count", "title": "Count \"words\""}]})");
    ASSERT_TRUE(result) << result.error().message;
    const Json &manifest = *result->find("manifest");
    EXPECT_EQ(manifest.find("version")->asString(), "0.1.0");
    EXPECT_EQ(manifest.find("api")->asInt(), kApiVersion);
    EXPECT_EQ(manifest.find("main")->asString(), "main.lua");
    EXPECT_EQ(manifest.find("tier"), nullptr);
    ASSERT_EQ(plugins.scaffolded.size(), 2u);
    EXPECT_EQ(plugins.scaffolded[0].path, "hikari-plugin.json");
    EXPECT_NE(plugins.scaffolded[1].text.find(R"(hikari.commands.register("count", function())"), std::string::npos);
    EXPECT_NE(plugins.scaffolded[1].text.find(R"(Count \"words\": )"), std::string::npos);
    EXPECT_TRUE(plugins.installs.empty());
}

TEST_F(Plugins, ScaffoldRefusesADuplicateFileAPathOutsideThePluginAndNoCommands)
{
    EXPECT_EQ(code(call("plugins.scaffold", R"({"plugin": "org.example.a", "name": "A", "commands": []})")),
              errors::kInvalidParams);
    EXPECT_EQ(code(call("plugins.scaffold", R"({"plugin": "org.example.a", "name": "A", "commands": [{"id": "count", "title": "Count"}],
        "files": [{"path": "main.lua", "text": ""}]})")),
              errors::kInvalidParams);
    EXPECT_EQ(code(call("plugins.scaffold", R"({"plugin": "org.example.a", "name": "A", "commands": [{"id": "count", "title": "Count"}],
        "files": [{"path": "../evil.lua", "text": ""}]})")),
              errors::kInvalidParams);
    EXPECT_EQ(code(call("plugins.scaffold", R"({"plugin": "../a", "name": "A", "commands": [{"id": "count", "title": "Count"}]})")), errors::kInvalidParams);
}

TEST_F(Plugins, ReviewTakesAScaffoldedPluginOrAGrantedFile)
{
    EXPECT_EQ(code(call("plugins.review", R"({"plugin": "org.example.word-count"})")), errors::kNotFound);
    ASSERT_TRUE(call("plugins.scaffold", R"({"plugin": "org.example.word-count", "name": "Word count", "commands": [{"id": "count", "title": "Count"}]})"));
    const Result scaffolded = call("plugins.review", R"({"plugin": "org.example.word-count"})");
    ASSERT_TRUE(scaffolded) << scaffolded.error().message;
    EXPECT_EQ(scaffolded->find("digest")->asString(), FakePlugins::kDigest);
    EXPECT_EQ(scaffolded->find("files")->asArray().at(0).find("path")->asString(), "main.lua");

    EXPECT_EQ(code(call("plugins.review", R"({"plugin": "org.example.word-count", "file": "f1"})")), errors::kInvalidParams);
    EXPECT_EQ(code(call("plugins.review", R"({"file": "f9"})")), errors::kNotGranted);
    const std::string handle = api.grantFile(aiCaller().id, "/downloads/word-count.hikariplugin", false);
    ASSERT_TRUE(call("plugins.review", R"({"file": ")" + handle + R"("})"));
    EXPECT_EQ(plugins.reviewed.back(), "/downloads/word-count.hikariplugin");
}

TEST_F(Plugins, InstallRefusesWithoutTheUsersApproval)
{
    ASSERT_TRUE(call("plugins.scaffold", R"({"plugin": "org.example.word-count", "name": "Word count", "commands": [{"id": "count", "title": "Count"}]})"));
    EXPECT_EQ(code(call("plugins.install", R"({"plugin": "org.example.word-count"})")), errors::kNotGranted);
    EXPECT_EQ(code(call("plugins.install", R"({"plugin": "org.example.word-count"})", pluginCaller())), errors::kNotGranted);
    EXPECT_TRUE(plugins.installs.empty());
}

TEST_F(Plugins, InstallRefusesAPackageThatChangedSinceTheUserApprovedIt)
{
    ASSERT_TRUE(call("plugins.scaffold", R"({"plugin": "org.example.word-count", "name": "Word count", "commands": [{"id": "count", "title": "Count"}]})"));
    EXPECT_EQ(code(call("plugins.install", R"({"plugin": "org.example.word-count"})", approved("ff"))), errors::kRefused);
    EXPECT_TRUE(plugins.installs.empty());

    const Result result = call("plugins.install", R"({"plugin": "org.example.word-count"})", approved());
    ASSERT_TRUE(result) << result.error().message;
    EXPECT_TRUE(result->find("installed")->asBool());
    EXPECT_EQ(plugins.installs, std::vector<std::string>{"/user/ai/plugins/org.example.word-count.hikariplugin"});
}

TEST_F(Plugins, RunMapsEachOutcome)
{
    EXPECT_TRUE(call("plugins.run", R"({"plugin": "org.example.word-count", "command": "count"})"));
    EXPECT_EQ(plugins.runs.back(), "org.example.word-count/count");
    plugins.nextRun = PluginRun::NotInstalled;
    EXPECT_EQ(code(call("plugins.run", R"({"plugin": "org.example.x", "command": "count"})")), errors::kNotFound);
    plugins.nextRun = PluginRun::UnknownCommand;
    EXPECT_EQ(code(call("plugins.run", R"({"plugin": "org.example.word-count", "command": "x"})")), errors::kNotFound);
    plugins.nextRun = PluginRun::NotRunning;
    const Result off = call("plugins.run", R"({"plugin": "org.example.word-count", "command": "count"})");
    EXPECT_EQ(code(off), errors::kRefused);
}

TEST(PluginsUnavailable, EveryOperationAnswersUnavailableWithoutThePort)
{
    FakeHost host;
    host.plugins = nullptr;
    Api api{host.host()};
    ScriptClient client{api};
    EXPECT_EQ(code(client.call(aiCaller(), "plugins.list", Json::object())), errors::kUnavailable);
}

}
