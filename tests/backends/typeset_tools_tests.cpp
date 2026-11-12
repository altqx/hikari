#include "fake_host.h"
#include "script_client.h"

#include "hikari/application/document_overlay.h"
#include "hikari/backends/libass_renderer.h"
#include "hikari/core/ass_save.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace hikari;
using namespace hikari::api;
using namespace hikari::api::testing;

namespace {

constexpr int kW = 640, kH = 360;

const std::string kSigns =
    "[Script Info]\nScriptType: v4.00+\nPlayResX: 640\nPlayResY: 360\nScaledBorderAndShadow: yes\n\n"
    "[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, "
    "Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, "
    "MarginL, MarginR, MarginV, Encoding\n"
    "Style: Sign,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,0,0,7,0,0,0,1\n\n"
    "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
    "Dialogue: 0,0:00:01.00,0:00:02.00,Sign,,0,0,0,,{\\pos(20,20)\\p1}m 0 0 l 100 0 100 40 0 40{\\p0}\n"
    "Dialogue: 0,0:00:01.00,0:00:02.00,Sign,,0,0,0,,{\\pos(20,80)\\p1}m 0 0 l 100 0 100 40 0 40{\\p0}\n"
    "Dialogue: 0,0:00:01.00,0:00:02.00,Sign,,0,0,0,,{\\pos(20,140)\\p1}m 0 0 l 100 0 100 40 0 40{\\p0}\n";

class LibassRender final : public RenderHost {
public:
    std::optional<application::OverlayFrame> render(application::DocumentId, const core::Document &document,
                                                    std::int64_t frame, int width, int height) override
    {
        auto drawn = overlay.render(document, {}, core::DocumentTime(frame * 40'000), width, height);
        return drawn ? std::optional(std::move(*drawn)) : std::nullopt;
    }

    backends::LibassRenderer renderer;
    application::DocumentOverlay overlay{renderer, [](const core::Document &document) {
                                             return core::encodeAss(document, core::AssSaveOptions{.renderer = true});
                                         }};
};

struct Sign {
    std::uint64_t line;
    std::array<std::array<double, 2>, 4> corners;
};

std::string quadJson(const Sign &sign, double dx = 0)
{
    std::string out = "[";
    for (std::size_t i = 0; i < sign.corners.size(); ++i)
        out += (i ? ", " : "") + std::string(R"({"x": )") + std::to_string(sign.corners[i][0] + dx) + R"(, "y": )" +
               std::to_string(sign.corners[i][1]) + "}";
    return out + "]";
}

std::array<std::array<double, 2>, 4> turned(double cx, double cy, double w, double h, double degrees)
{
    const double a = degrees * std::acos(-1.0) / 180, c = std::cos(a), s = std::sin(a);
    const double xs[] = {-w / 2, w / 2, w / 2, -w / 2}, ys[] = {-h / 2, -h / 2, h / 2, h / 2};
    std::array<std::array<double, 2>, 4> out;
    for (int i = 0; i < 4; ++i)
        out[i] = {cx + xs[i] * c - ys[i] * s, cy + xs[i] * s + ys[i] * c};
    return out;
}

// A shop board seen straight on, a tilted banner and a wall poster seen from the side.
const Sign kFlat{1, {{{100, 50}, {260, 50}, {260, 110}, {100, 110}}}};
const Sign kTurned{2, turned(420, 90, 140, 50, -12)};
const Sign kPoster{3, {{{300, 200}, {520, 180}, {520, 320}, {300, 290}}}};

class TypesetTools : public ::testing::Test {
protected:
    TypesetTools()
    {
        host.media.width = kW;
        host.media.height = kH;
        host.sessions.at(1) = std::make_unique<application::EditSession>(loadAss(kSigns));
    }

    Result call(std::string_view operation, const std::string &params)
    {
        auto parsed = Json::parse(params);
        EXPECT_TRUE(parsed) << params;
        return client.call(aiCaller(), operation, *parsed);
    }

    std::string solve(const Sign &sign)
    {
        const Result solved = call("typeset.perspective",
                                   R"({"line": )" + std::to_string(sign.line) + R"(, "quad": )" + quadJson(sign) + "}");
        EXPECT_TRUE(solved) << (solved ? "" : solved.error().message);
        return solved ? solved->find("text")->asString() : std::string();
    }

    Result check(const Sign &sign, const std::string &text, double dx = 0)
    {
        Json params = Json::object({{"line", Json(std::int64_t(sign.line))},
                                    {"text", Json(text)},
                                    {"target", Json::object({{"quad", *Json::parse(quadJson(sign, dx))}})}});
        return client.call(aiCaller(), "typeset.check", params);
    }

    std::string lineText(std::uint64_t id) const
    {
        for (const core::LineRecord *line : host.target()->document().lines())
            if (line->id.value == id)
                return std::string(line->text.begin(), line->text.end());
        return {};
    }

    LibassRender render;
    FakeHost host{&render};
    Api api{host.host()};
    ScriptClient client{api};
};

TEST_F(TypesetTools, FixtureSignsAreTypesetWithinTolerance)
{
    for (const Sign &sign : {kFlat, kTurned, kPoster}) {
        const std::string text = solve(sign);
        const Result checked = check(sign, text);
        ASSERT_TRUE(checked) << checked.error().message;
        EXPECT_TRUE(checked->find("pass")->asBool()) << "Line " << sign.line << ": " << text << " "
                                                     << checked->dump();
        EXPECT_LE(checked->find("error")->asNumber(), 2) << "Line " << sign.line;
    }
}

TEST_F(TypesetTools, TheRenderCheckRejectsABadPlacement)
{
    const std::string text = solve(kPoster);
    const Result off = check(kPoster, text, 20);
    ASSERT_TRUE(off);
    EXPECT_FALSE(off->find("pass")->asBool());
    EXPECT_GE(off->find("error")->asNumber(), 18);

    const std::string before = lineText(kPoster.line);
    const std::string revision = std::to_string(host.target()->revision());
    Json lines = Json::array({Json::object({{"line", Json(std::int64_t(kPoster.line))},
                                            {"text", Json(text)},
                                            {"target", Json::object({{"quad", *Json::parse(quadJson(kPoster, 20))}})}})});
    Json params = Json::object({{"revision", *Json::parse(revision)}, {"name", Json("Typeset the poster")}, {"lines", lines}});
    const Result refused = client.call(aiCaller(), "typeset.apply", params);
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().data.find("reason")->asString(), "check_failed");
    EXPECT_EQ(lineText(kPoster.line), before);

    lines.asArray()[0]["target"] = Json::object({{"quad", *Json::parse(quadJson(kPoster))}});
    params["lines"] = lines;
    const Result applied = client.call(aiCaller(), "typeset.apply", params);
    ASSERT_TRUE(applied) << applied.error().message;
    EXPECT_EQ(lineText(kPoster.line), text);
}

TEST_F(TypesetTools, AShiftedSignLandsOnTheShiftedTarget)
{
    const std::string text = solve(kFlat);
    const std::string revision = std::to_string(host.target()->revision());
    ASSERT_TRUE(call("document.apply", R"({"revision": )" + revision + R"(, "name": "Typeset the board", "edits": [
        {"op": "set_line", "line": 1, "fields": {"text": )" + Json(text).dump() + "}}]}"));
    const Result shifted = call("typeset.shift", R"({"lines": [1], "dx": 30, "dy": 0})");
    ASSERT_TRUE(shifted) << shifted.error().message;
    ASSERT_EQ(shifted->find("lines")->asArray().size(), 1u);
    const Result checked = check(kFlat, shifted->find("lines")->asArray()[0].find("text")->asString(), 30);
    ASSERT_TRUE(checked);
    EXPECT_TRUE(checked->find("pass")->asBool()) << checked->dump();
}

}
