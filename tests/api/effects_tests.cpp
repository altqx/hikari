#include "fake_host.h"
#include "script_client.h"

#include "hikari/application/node_graph_editing.h"
#include "hikari/core/ass_save.h"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>

using namespace hikari;
using namespace hikari::api;
using namespace hikari::api::testing;

namespace {

const std::filesystem::path kScripts = HIKARI_API_SCRIPTS_DIR;

std::string narrow(const std::u8string &s)
{
    return std::string(s.begin(), s.end());
}

std::string karaokeFixture()
{
    std::ifstream in(HIKARI_KARAOKE_PRESET_FIXTURES "/fixture.ass", std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

application::NodeGraph classicFill()
{
    for (const application::NodeGraphPreset &p : application::nodeGraphPresets())
        if (p.templateFile == "01-classic-fill.ass")
            return *application::parseGraph(p.graph);
    ADD_FAILURE() << "no classic fill preset";
    return {};
}

class Effects : public ::testing::Test {
protected:
    Effects() { host.sessions.at(1) = std::make_unique<application::EditSession>(loadAss(karaokeFixture())); }

    FakeHost host;
    Api api{host.host()};
    ScriptClient client{api};
};

TEST_F(Effects, AScriptedAgentBuildsTheClassicFillPresetThatValidatesPreviewsAndApplies)
{
    EXPECT_GT(client.run(kScripts / "effects.jsonl"), 0);

    const application::NodeGraph preset = classicFill();
    EXPECT_EQ(client.saved.at("fill"), application::graphJson(preset));

    application::NodeEffectOptions options = host.fakeEffects.options();
    const application::NodeEffectRun expected =
        application::runNodeEffect(loadAss(karaokeFixture()), u8"classic", u8"Romaji", preset, options);
    ASSERT_TRUE(expected.ran && expected.ok());
    ASSERT_FALSE(expected.lines.empty());
    EXPECT_EQ(client.saved.at("previewed"), Json(std::int64_t(expected.lines.size())));

    std::vector<std::string> written;
    for (const core::LineRecord *line : host.target()->document().lines())
        if (line->effect == u8"fx classic")
            written.push_back(narrow(core::legacy::assLineText(*line)));
    std::vector<std::string> wanted;
    for (const core::LineRecord &line : expected.lines)
        wanted.push_back(narrow(core::legacy::assLineText(line)));
    EXPECT_EQ(written, wanted);
}

TEST_F(Effects, APreviewGivesTheGeneratedLinesAndChangesNothing)
{
    const std::uint64_t revision = host.target()->revision();
    const Result result = client.call(aiCaller(), "effects.preview",
                                      Json::object({{"graph", application::graphJson(classicFill())},
                                                    {"style", Json("Romaji")},
                                                    {"name", Json("classic")},
                                                    {"limit", Json(1)}}));
    ASSERT_TRUE(result) << result.error().message;
    ASSERT_EQ(result->find("lines")->asArray().size(), 1u);
    EXPECT_TRUE(result->find("lines")->asArray()[0].asString().starts_with("Dialogue: "));
    EXPECT_NE(result->find("lines")->asArray()[0].asString().find(",fx classic,"), std::string::npos);
    EXPECT_EQ(host.target()->revision(), revision);
}

TEST_F(Effects, ALuaNodeNeverRunsThroughTheApi)
{
    application::NodeGraph graph = classicFill();
    graph.nodes.push_back({"code", "lua.code", Json::object({{"code", Json("return 1")}}), std::nullopt});
    const Result result = client.call(
        aiCaller(), "effects.preview",
        Json::object({{"graph", application::graphJson(graph)}, {"style", Json("Romaji")}, {"name", Json("classic")}}));
    ASSERT_TRUE(result) << result.error().message;
    EXPECT_EQ(*result->find("ran"), Json(false));
    EXPECT_EQ(*result->find("generated"), Json(0));
}

TEST_F(Effects, WithoutTheEffectsPortPreviewAndApplyAreUnavailable)
{
    host.effects = nullptr;
    Api bare{host.host()};
    ScriptClient calls{bare};
    const Result result = calls.call(aiCaller(), "effects.preview",
                                       Json::object({{"graph", application::graphJson(classicFill())}, {"style", Json("Romaji")}}));
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, errors::kUnavailable);
}

}
