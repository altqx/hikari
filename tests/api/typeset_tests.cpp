#include "fake_host.h"
#include "script_client.h"

#include <gtest/gtest.h>

using namespace hikari;
using namespace hikari::api;
using namespace hikari::api::testing;

namespace {

constexpr std::string_view kSigns =
    "[Script Info]\n"
    "ScriptType: v4.00+\n"
    "PlayResX: 1920\n"
    "PlayResY: 1080\n"
    "\n"
    "[V4+ Styles]\n"
    "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
    "Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, "
    "MarginR, MarginV, Encoding\n"
    "Style: Default,Arial,40,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1\n"
    "\n"
    "[Events]\n"
    "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\pos(100,200)}Shop\n"
    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\pos(300,400)\\clip(0,0,50,50)}Cafe\n"
    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\an7\\pos(10,20)\\bord0\\shad0\\p1}m 0 0 l 100 0 100 40 0 40{\\p0}\n";

class Typeset : public ::testing::Test {
protected:
    Typeset() { host.sessions.at(1) = std::make_unique<application::EditSession>(loadAss(kSigns)); }

    Result call(std::string_view operation, std::string_view params)
    {
        auto parsed = Json::parse(params);
        EXPECT_TRUE(parsed) << params;
        return client.call(aiCaller(), operation, *parsed);
    }

    std::string text(std::string_view operation, std::string_view params)
    {
        const Result result = call(operation, params);
        if (!result)
            return "error " + result.error().code + ": " + result.error().message;
        return result->find("text")->asString();
    }

    std::map<std::uint64_t, std::string> lines(std::string_view operation, std::string_view params)
    {
        std::map<std::uint64_t, std::string> out;
        const Result result = call(operation, params);
        EXPECT_TRUE(result) << (result ? "" : result.error().message);
        if (result)
            for (const Json &line : result->find("lines")->asArray())
                out[std::uint64_t(line.find("line")->asInt())] = line.find("text")->asString();
        return out;
    }

    std::string lineText(std::uint64_t id) const
    {
        for (const core::LineRecord *line : host.target()->document().lines())
            if (line->id.value == id)
                return std::string(line->text.begin(), line->text.end());
        return {};
    }

    FakeHost host;
    Api api{host.host()};
    ScriptClient client{api};
};

TEST_F(Typeset, ARectangleClipReplacesTheFirstClip)
{
    EXPECT_EQ(text("typeset.clip", R"({"line": 2, "rect": {"x1": 120, "y1": 80, "x2": 20, "y2": 10.6}})"),
              "{\\pos(300,400)\\clip(20,11,120,80)}Cafe");
    EXPECT_EQ(text("typeset.clip", R"({"line": 1, "rect": {"x1": 0, "y1": 0, "x2": 10, "y2": 10}, "inverse": true})"),
              "{\\iclip(0,0,10,10)\\pos(100,200)}Shop");
}

TEST_F(Typeset, AVectorClipCanBeInverse)
{
    EXPECT_EQ(text("typeset.clip", R"({"line": 1, "vector": "m 0 0 l 50 0 50 50", "inverse": true})"),
              "{\\iclip(m 0 0 l 50 0 50 50)\\pos(100,200)}Shop");
}

TEST_F(Typeset, ADrawingReplacesTheLinesDrawingAndKeepsItsPlace)
{
    EXPECT_EQ(text("typeset.drawing", R"({"line": 3, "shape": "m 0 0 l 20 0 20 20 0 20"})"),
              "{\\an7\\pos(10,20)\\bord0\\shad0\\p1}m 0 0 l 20 0 20 20 0 20{\\p0}");
}

TEST_F(Typeset, TheShifterMovesPositionsAndRectangleClipsInWholeUnits)
{
    const auto moved = lines("typeset.shift", R"({"lines": [1, 2], "dx": 12.5, "dy": -3})");
    ASSERT_EQ(moved.size(), 2u);
    EXPECT_EQ(moved.at(1), "{\\pos(112.5,197)}Shop");
    EXPECT_EQ(moved.at(2), "{\\pos(312.5,397)\\clip(12,-3,62,47)}Cafe");
    EXPECT_EQ(lineText(1), "{\\pos(100,200)}Shop");
}

TEST_F(Typeset, TheShifterMovesOnlyTheKindsAsked)
{
    const auto moved = lines("typeset.shift", R"({"lines": [2], "dx": 10, "dy": 0, "kinds": ["clip"]})");
    EXPECT_EQ(moved.at(2), "{\\pos(300,400)\\clip(10,0,60,50)}Cafe");
}

TEST_F(Typeset, HydraSetsATagOnEveryLine)
{
    const auto set = lines("typeset.hydra", R"({"lines": [1, 2], "tag": "fax", "change": "set", "values": [0.25]})");
    ASSERT_EQ(set.size(), 2u);
    EXPECT_EQ(set.at(1), "{\\fax0.25\\pos(100,200)}Shop");
    EXPECT_EQ(set.at(2), "{\\fax0.25\\pos(300,400)\\clip(0,0,50,50)}Cafe");
    EXPECT_EQ(lineText(1), "{\\pos(100,200)}Shop");
}

TEST_F(Typeset, HydraMultipliesByLineNumber)
{
    const auto set = lines("typeset.hydra",
                           R"({"lines": [1, 2], "tag": "blur", "change": "multiply", "values": [1], "perLine": [2]})");
    EXPECT_EQ(set.at(1), "{\\blur1\\pos(100,200)}Shop");
    EXPECT_EQ(set.at(2), "{\\blur3\\pos(300,400)\\clip(0,0,50,50)}Cafe");
}

TEST_F(Typeset, HydraNamesTheTagsItKnows)
{
    const Result result = call("typeset.hydra", R"({"lines": [1], "tag": "nope", "change": "set", "values": [1]})");
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, errors::kInvalidParams);
    EXPECT_NE(result.error().message.find("blur, bord"), std::string::npos);
}

TEST_F(Typeset, PerspectivePutsADrawingOnAQuad)
{
    const Result result = call("typeset.perspective", R"({"line": 3, "quad": [{"x": 200, "y": 100},
        {"x": 400, "y": 100}, {"x": 400, "y": 180}, {"x": 200, "y": 180}]})");
    ASSERT_TRUE(result) << result.error().message;
    const Json &t = *result->find("transform");
    EXPECT_NEAR(t.find("fscx")->asNumber(), 200, 1e-6);
    EXPECT_NEAR(t.find("fscy")->asNumber(), 200, 1e-6);
    EXPECT_NEAR(t.find("frz")->asNumber(), 0, 1e-6);
    EXPECT_NEAR(t.find("pos")->find("x")->asNumber(), 200, 1e-3);
    EXPECT_NEAR(t.find("pos")->find("y")->asNumber(), 100, 1e-3);
    EXPECT_NE(result->find("text")->asString().find("\\fscx200"), std::string::npos) << result->find("text")->asString();
}

TEST_F(Typeset, PerspectiveRefusesAFoldedQuadAndTextItCannotMeasure)
{
    const Result folded = call("typeset.perspective", R"({"line": 3, "quad": [{"x": 0, "y": 0},
        {"x": 100, "y": 100}, {"x": 100, "y": 0}, {"x": 0, "y": 100}]})");
    ASSERT_FALSE(folded);
    EXPECT_EQ(folded.error().code, errors::kInvalidParams);
    const Result text = call("typeset.perspective", R"({"line": 1, "quad": [{"x": 0, "y": 0},
        {"x": 100, "y": 0}, {"x": 100, "y": 40}, {"x": 0, "y": 40}]})");
    ASSERT_FALSE(text);
    EXPECT_EQ(text.error().code, errors::kRefused);
    EXPECT_EQ(text.error().data.find("reason")->asString(), "unmeasured");
}

// FakeRender draws Line n as a 3x2 box at video (4n, 2); the script is 30 times the 64x36 video.
TEST_F(Typeset, TheCheckComparesTheInkWithTheTarget)
{
    const Result hit = call("typeset.check", R"({"line": 1, "target": {"box": {"x": 120, "y": 60, "width": 90, "height": 60}}})");
    ASSERT_TRUE(hit) << hit.error().message;
    EXPECT_TRUE(hit->find("pass")->asBool());
    EXPECT_EQ(hit->find("error")->asNumber(), 0);
    const Result miss = call("typeset.check", R"({"line": 1, "target": {"box": {"x": 600, "y": 60, "width": 90, "height": 60}}})");
    ASSERT_TRUE(miss);
    EXPECT_FALSE(miss->find("pass")->asBool());
    EXPECT_EQ(miss->find("error")->asNumber(), 16);
}

TEST_F(Typeset, ApplyRefusesEveryLineWhenOneMissesItsTarget)
{
    const auto revision = host.target()->revision();
    const Result result = call("typeset.apply", R"({"revision": )" + std::to_string(revision) + R"(, "name": "Typeset the signs",
        "lines": [{"line": 1, "text": "{\\pos(1,1)}Shop", "target": {"box": {"x": 120, "y": 60, "width": 90, "height": 60}}},
                  {"line": 2, "text": "{\\pos(2,2)}Cafe", "target": {"box": {"x": 900, "y": 60, "width": 90, "height": 60}}}]})");
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, errors::kRefused);
    EXPECT_EQ(result.error().data.find("reason")->asString(), "check_failed");
    ASSERT_EQ(result.error().data.find("lines")->asArray().size(), 1u);
    EXPECT_EQ(result.error().data.find("lines")->asArray()[0].find("line")->asInt(), 2);
    EXPECT_EQ(host.target()->revision(), revision);
    EXPECT_EQ(lineText(1), "{\\pos(100,200)}Shop");
}

TEST_F(Typeset, ApplyCommitsOneReviewedStepWhenEveryLinePasses)
{
    const auto revision = host.target()->revision();
    Caller reviewed = aiCaller();
    reviewed.reviewed = true;
    const Result result = client.call(reviewed, "typeset.apply", *Json::parse(std::string(R"({"revision": )") + std::to_string(revision) + R"(, "name": "Typeset the shop sign",
        "lines": [{"line": 1, "text": "{\\pos(1,1)}Shop", "target": {"box": {"x": 120, "y": 60, "width": 90, "height": 60}}}]})"));
    ASSERT_TRUE(result) << result.error().message;
    EXPECT_TRUE(result->find("changed")->asBool());
    EXPECT_TRUE(result->find("checks")->asArray()[0].find("pass")->asBool());
    EXPECT_EQ(lineText(1), "{\\pos(1,1)}Shop");
    EXPECT_EQ(host.review.changes(*host.workspace().editingTarget()).size(), 1u);
}

TEST_F(Typeset, ChecksNeedAVideo)
{
    host.media.videoOpen = false;
    const Result result = call("typeset.check", R"({"line": 1, "target": {"box": {"x": 0, "y": 0, "width": 1, "height": 1}}})");
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, errors::kUnavailable);
}

}
