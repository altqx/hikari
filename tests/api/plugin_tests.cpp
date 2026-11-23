#include "fake_host.h"
#include "script_client.h"

#include "hikari/api/formats.h"
#include "hikari/core/ass_save.h"

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

TEST_F(Plugins, FormatsListGivesTheBuiltInFormatsThenThePluginsAndDocumentsNameTheirFormat)
{
    plugins.pluginFormats = {FormatInfo{"plugin:org.example.stl/ebu-stl", "EBU STL", {"stl"}, true, true, "org.example.stl"}};
    const Result result = call("formats.list", "{}");
    ASSERT_TRUE(result) << result.error().message;
    const auto &formats = result->find("formats")->asArray();
    ASSERT_EQ(formats.size(), builtInFormats().size() + 1);
    EXPECT_EQ(formats.front(), *Json::parse(R"({"id": "ass", "title": "Advanced SubStation Alpha",
        "extensions": ["ass", "ssa"], "import": true, "export": true})"));
    EXPECT_EQ(formats.back(), *Json::parse(R"({"id": "plugin:org.example.stl/ebu-stl", "title": "EBU STL",
        "extensions": ["stl"], "import": true, "export": true, "plugin": "org.example.stl"})"));

    host.pluginFormats[host.workspace().tabs().at(1).value] = "plugin:org.example.stl/ebu-stl";
    const Result listed = call("documents.list", "{}");
    ASSERT_TRUE(listed) << listed.error().message;
    const auto &documents = listed->find("documents")->asArray();
    EXPECT_EQ(documents.at(0).find("format")->asString(), "ass");
    EXPECT_EQ(documents.at(1).find("format")->asString(), "plugin:org.example.stl/ebu-stl");
}

namespace {

std::string assOf(const core::Document &document)
{
    const auto bytes = core::encodeAss(document);
    return std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size());
}

}

TEST(FormatImport, LinesStylesAndScriptInfoBecomeAnAssDocument)
{
    const auto document = documentFromImport(*Json::parse(R"({
        "lines": [{"start": 1000, "end": 2500, "style": "Sign", "text": "Hello"}, {"start": 3000, "end": 4000, "text": "Bye", "comment": true}],
        "styles": [{"name": "Sign", "fontname": "Verdana", "fontsize": "30"}],
        "info": {"Title": "Imported", "PlayResX": "1920"}})"));
    ASSERT_TRUE(document) << document.error();
    const std::string ass = assOf(*document);
    EXPECT_NE(ass.find("Title: Imported"), std::string::npos) << ass;
    EXPECT_NE(ass.find("PlayResX: 1920"), std::string::npos) << ass;
    EXPECT_NE(ass.find("Style: Sign,Verdana,30,"), std::string::npos) << ass;
    EXPECT_EQ(ass.find("Style: Default,"), std::string::npos) << ass;
    EXPECT_NE(ass.find("Dialogue: 0,0:00:01.00,0:00:02.50,Sign,,0,0,0,,Hello"), std::string::npos) << ass;
    EXPECT_NE(ass.find("Comment: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,Bye"), std::string::npos) << ass;
    EXPECT_EQ(document->lines().size(), 2u);

    const auto empty = documentFromImport(Json::object());
    ASSERT_TRUE(empty);
    EXPECT_TRUE(empty->lines().empty());
    EXPECT_NE(assOf(*empty).find("Style: Default,"), std::string::npos);
}

TEST(FormatImport, WhatDoesNotFitIsNamedWhereTheImporterPutIt)
{
    const auto at = [](std::string_view text) {
        const auto document = documentFromImport(*Json::parse(text));
        EXPECT_FALSE(document) << text;
        return document ? std::string() : document.error();
    };
    EXPECT_TRUE(at(R"({"lines": [{"text": "a"}, {"start": "soon"}]})").starts_with("/lines/1/start: ")) << at(R"({"lines": [{"text": "a"}, {"start": "soon"}]})");
    EXPECT_TRUE(at(R"({"lines": [{"id": 4, "text": "a"}]})").starts_with("/lines/0")) << at(R"({"lines": [{"id": 4, "text": "a"}]})");
    EXPECT_EQ(at(R"({"styles": [{"fontname": "Arial"}]})"), "/styles/0: a Style needs a name");
    EXPECT_TRUE(at(R"({"info": {"Title": 3}})").starts_with("/info/Title: "));
    EXPECT_EQ(at(R"({"lines": {}})"), "the importer must return {lines = {…}, styles = {…}, info = {…}}");
}
