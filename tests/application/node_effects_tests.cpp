#include "hikari/application/edit_session.h"
#include "hikari/application/generated_runs.h"
#include "hikari/application/karaoke_syllables.h"
#include "hikari/application/node_effects.h"
#include "hikari/application/node_graph.h"
#include "hikari/application/node_graph_library.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/ass_save.h"

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <random>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace hikari;
using namespace hikari::application;
using json::Json;

namespace {

// MUST KILL: a third copy of karaoke_templater_tests' textExtents fake.
class FakeMeasure : public TextMeasurePort {
public:
    std::optional<TextExtents> measure(const std::vector<std::string> &style, const std::string &text) override
    {
        ++calls;
        const auto number = [&](std::size_t i) { return i < style.size() ? std::stod(style[i]) : 0.0; };
        const double size = number(2), scaleX = number(11) / 100, scaleY = number(12) / 100, spacing = number(13);
        double ems = 0;
        int chars = 0;
        for (std::size_t at = 0; at < text.size();) {
            const auto c = static_cast<unsigned char>(text[at]);
            const std::size_t n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
            char32_t cp = n == 1 ? c : c & (0x3F >> (n - 1));
            for (std::size_t k = 1; k < n && at + k < text.size(); ++k)
                cp = (cp << 6) | (static_cast<unsigned char>(text[at + k]) & 0x3F);
            ems += cp >= 0x2E80 ? 1.0 : 0.55;
            ++chars;
            at += n;
        }
        return TextExtents{ems * size * scaleX + spacing * chars, size * scaleY, size * 0.21, 0};
    }
    int calls = 0;
};

std::string readFile(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

core::Document load(std::string_view text)
{
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return core::loadAss(bytes).document;
}

std::string text(const std::vector<std::byte> &bytes)
{
    return std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size());
}

std::string narrow(const std::u8string &s)
{
    return std::string(s.begin(), s.end());
}

NodeGraph graphOf(std::string_view json)
{
    auto graph = parseGraph(json);
    EXPECT_TRUE(graph) << (graph ? "" : graph.error().node + " " + graph.error().message);
    return graph ? *graph : NodeGraph{};
}

std::string commentLine(std::string_view effect, std::string_view body)
{
    return "Comment: 0,0:00:00.00,0:00:00.00,Romaji,,0,0,0," + std::string(effect) + "," + std::string(body) + "\n";
}

core::Document fixtureWith(const std::string &lines)
{
    std::string fixture = readFile(HIKARI_KARAOKE_PRESET_FIXTURES "/fixture.ass");
    fixture.insert(fixture.find("Dialogue: "), lines);
    return load(fixture);
}

std::vector<std::string> goldenLines(const std::string &file, std::string_view name)
{
    std::vector<std::string> out;
    std::istringstream in(readFile(HIKARI_KARAOKE_PRESET_FIXTURES "/" + file));
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const auto fx = line.find(",fx,");
        if (line.starts_with("Dialogue: ") && fx != std::string::npos)
            out.push_back(line.replace(fx, 4, ",fx " + std::string(name) + ","));
    }
    return out;
}

std::vector<std::string> linesOf(const NodeEffectRun &run)
{
    std::vector<std::string> out;
    for (const core::LineRecord &line : run.lines)
        out.push_back(narrow(core::legacy::assLineText(line)));
    return out;
}

core::LineId graphLineOf(const core::Document &doc, std::u8string_view name)
{
    for (const DocumentGraph &g : documentGraphs(doc))
        if (g.name == name)
            return g.line;
    ADD_FAILURE() << "no graph " << narrow(std::u8string(name));
    return {};
}

NodeEffectRun runGraph(const core::Document &doc, std::u8string_view name, NodeEffectOptions options = {})
{
    static FakeMeasure measure;
    if (!options.measure)
        options.measure = &measure;
    auto run = runDocumentGraph(doc, graphLineOf(doc, name), options);
    EXPECT_TRUE(run) << (run ? "" : run.error().message);
    if (!run)
        return {};
    for (const NodeEffectProblem &p : run->problems)
        ADD_FAILURE() << "problem at " << p.node << ": " << p.message;
    return std::move(*run);
}

constexpr std::string_view kClassicFill = R"({"format": 1, "seed": 0, "nodes": [
    {"id": "line", "type": "units.for_each", "fields": {"unit": "line", "skip_blank": false}},
    {"id": "syl", "type": "units.for_each", "fields": {"unit": "syl", "skip_blank": false}},
    {"id": "info", "type": "units.info"},
    {"id": "kf", "type": "tags.tag", "fields": {"tag": "kf"}},
    {"id": "join", "type": "text.join"},
    {"id": "time", "type": "time.retime", "fields": {"mode": "line", "start_offset": -300, "end_offset": 300}},
    {"id": "fade", "type": "tags.fade", "fields": {"fade_in": 300, "fade_out": 300}},
    {"id": "k", "type": "tags.tag", "fields": {"tag": "k", "value": 30}},
    {"id": "emit", "type": "output.emit"}],
  "wires": [
    {"from": "info.kdur", "to": "kf.value"}, {"from": "syl.units", "to": "join.units"},
    {"from": "kf.tags", "to": "join.tags"}, {"from": "fade.tags", "to": "k.before"},
    {"from": "line.units", "to": "emit.units"}, {"from": "time.start", "to": "emit.start"},
    {"from": "time.end", "to": "emit.end"}, {"from": "k.tags", "to": "emit.tags"},
    {"from": "join.text", "to": "emit.text"}]})";

constexpr std::string_view kBounce = R"({"format": 1, "nodes": [
    {"id": "syl", "type": "units.for_each"},
    {"id": "info", "type": "units.info"},
    {"id": "an", "type": "tags.tag", "fields": {"tag": "an", "value": 5}},
    {"id": "rest", "type": "values.point"},
    {"id": "up", "type": "math.calculate", "fields": {"op": "subtract", "b": 16}},
    {"id": "high", "type": "values.point"},
    {"id": "colour", "type": "values.colour", "fields": {"value": "#FFE870"}},

    {"id": "t1", "type": "time.retime", "fields": {"mode": "start2syl", "start_offset": -300}},
    {"id": "pos1", "type": "tags.tag", "fields": {"tag": "pos"}},
    {"id": "fade1", "type": "tags.fade", "fields": {"fade_in": 300}},
    {"id": "emit1", "type": "output.emit", "fields": {"order": 0}},

    {"id": "t2", "type": "time.retime", "fields": {"mode": "sylpct", "end_offset": 50}},
    {"id": "move2", "type": "tags.move"},
    {"id": "c2", "type": "tags.tag", "fields": {"tag": "1c"}},
    {"id": "emit2", "type": "output.emit", "fields": {"order": 1}},

    {"id": "t3", "type": "time.retime", "fields": {"mode": "sylpct", "start_offset": 50, "end_offset": 100}},
    {"id": "move3", "type": "tags.move"},
    {"id": "c3", "type": "tags.tag", "fields": {"tag": "1c"}},
    {"id": "emit3", "type": "output.emit", "fields": {"order": 2}},

    {"id": "t4", "type": "time.retime", "fields": {"mode": "syl2end", "end_offset": 300}},
    {"id": "pos4", "type": "tags.tag", "fields": {"tag": "pos"}},
    {"id": "c4", "type": "tags.tag", "fields": {"tag": "1c"}},
    {"id": "fade4", "type": "tags.fade", "fields": {"fade_out": 300}},
    {"id": "emit4", "type": "output.emit", "fields": {"order": 3}}],
  "wires": [
    {"from": "info.center", "to": "rest.x"}, {"from": "info.middle", "to": "rest.y"},
    {"from": "info.middle", "to": "up.a"}, {"from": "info.center", "to": "high.x"}, {"from": "up.result", "to": "high.y"},

    {"from": "an.tags", "to": "pos1.before"}, {"from": "rest.point", "to": "pos1.point"},
    {"from": "pos1.tags", "to": "fade1.before"}, {"from": "syl.units", "to": "emit1.units"},
    {"from": "t1.start", "to": "emit1.start"}, {"from": "t1.end", "to": "emit1.end"}, {"from": "fade1.tags", "to": "emit1.tags"},

    {"from": "an.tags", "to": "move2.before"}, {"from": "rest.point", "to": "move2.from"},
    {"from": "high.point", "to": "move2.to"}, {"from": "move2.tags", "to": "c2.before"},
    {"from": "colour.value", "to": "c2.colour"}, {"from": "syl.units", "to": "emit2.units"},
    {"from": "t2.start", "to": "emit2.start"}, {"from": "t2.end", "to": "emit2.end"}, {"from": "c2.tags", "to": "emit2.tags"},

    {"from": "an.tags", "to": "move3.before"}, {"from": "high.point", "to": "move3.from"},
    {"from": "rest.point", "to": "move3.to"}, {"from": "move3.tags", "to": "c3.before"},
    {"from": "colour.value", "to": "c3.colour"}, {"from": "syl.units", "to": "emit3.units"},
    {"from": "t3.start", "to": "emit3.start"}, {"from": "t3.end", "to": "emit3.end"}, {"from": "c3.tags", "to": "emit3.tags"},

    {"from": "an.tags", "to": "pos4.before"}, {"from": "rest.point", "to": "pos4.point"},
    {"from": "pos4.tags", "to": "c4.before"}, {"from": "colour.value", "to": "c4.colour"},
    {"from": "c4.tags", "to": "fade4.before"}, {"from": "syl.units", "to": "emit4.units"},
    {"from": "t4.start", "to": "emit4.start"}, {"from": "t4.end", "to": "emit4.end"}, {"from": "fade4.tags", "to": "emit4.tags"}]})";

constexpr std::string_view kSparkles = R"({"format": 1, "nodes": [
    {"id": "syl", "type": "units.for_each"},
    {"id": "info", "type": "units.info"},
    {"id": "here", "type": "values.point"},
    {"id": "an", "type": "tags.tag", "fields": {"tag": "an", "value": 5}},
    {"id": "spark", "type": "values.colour", "fields": {"value": "#FFF8C0"}},

    {"id": "lineTime", "type": "time.retime", "fields": {"mode": "line", "start_offset": -300, "end_offset": 300}},
    {"id": "pos", "type": "tags.tag", "fields": {"tag": "pos"}},
    {"id": "fade", "type": "tags.fade", "fields": {"fade_in": 300, "fade_out": 300}},
    {"id": "lit", "type": "tags.tag", "fields": {"tag": "1c"}},
    {"id": "t1", "type": "math.calculate", "fields": {"op": "add", "b": 300}},
    {"id": "t2", "type": "math.calculate", "fields": {"op": "add", "b": 300}},
    {"id": "light", "type": "tags.transform"},
    {"id": "text", "type": "output.emit", "fields": {"order": 0}},

    {"id": "eight", "type": "units.repeat", "fields": {"count": 8}},
    {"id": "sparkTime", "type": "time.retime", "fields": {"mode": "syl", "end_offset": 600}},
    {"id": "i3", "type": "math.calculate", "fields": {"op": "multiply", "b": 3}},
    {"id": "j2", "type": "math.calculate", "fields": {"op": "multiply", "b": 2}},
    {"id": "sum", "type": "math.calculate", "fields": {"op": "add"}},
    {"id": "mod", "type": "math.calculate", "fields": {"op": "modulo", "b": 5}},
    {"id": "reach", "type": "math.calculate", "fields": {"op": "add", "b": 6}},
    {"id": "dirX", "type": "values.choose", "fields": {"values": [42, 30, 0, -30, -42, -30, 0, 30]}},
    {"id": "dirY", "type": "values.choose", "fields": {"values": [0, -30, -42, -30, 0, 30, 42, 30]}},
    {"id": "mulX", "type": "math.calculate", "fields": {"op": "multiply"}},
    {"id": "mulY", "type": "math.calculate", "fields": {"op": "multiply"}},
    {"id": "divX", "type": "math.calculate", "fields": {"op": "divide", "b": 10}},
    {"id": "divY", "type": "math.calculate", "fields": {"op": "divide", "b": 10}},
    {"id": "floorX", "type": "math.calculate", "fields": {"op": "floor"}},
    {"id": "floorY", "type": "math.calculate", "fields": {"op": "floor"}},
    {"id": "x", "type": "math.calculate", "fields": {"op": "add"}},
    {"id": "y", "type": "math.calculate", "fields": {"op": "add"}},
    {"id": "there", "type": "values.point"},
    {"id": "move", "type": "tags.move"},
    {"id": "bord", "type": "tags.tag", "fields": {"tag": "bord", "value": 0}},
    {"id": "shad", "type": "tags.tag", "fields": {"tag": "shad", "value": 0}},
    {"id": "blur", "type": "tags.tag", "fields": {"tag": "blur", "value": 0.6}},
    {"id": "sparkColour", "type": "tags.tag", "fields": {"tag": "1c"}},
    {"id": "sparkFade", "type": "tags.fade", "fields": {"fade_out": 450}},
    {"id": "shape", "type": "drawings.shape", "fields": {"shape": "m 0 -5 l 5 0 0 5 -5 0"}},
    {"id": "sparks", "type": "output.emit", "fields": {"layer": 1, "order": 1}}],
  "wires": [
    {"from": "info.center", "to": "here.x"}, {"from": "info.middle", "to": "here.y"},
    {"from": "an.tags", "to": "pos.before"}, {"from": "here.point", "to": "pos.point"},
    {"from": "pos.tags", "to": "fade.before"}, {"from": "spark.value", "to": "lit.colour"},
    {"from": "info.start", "to": "t1.a"}, {"from": "info.end", "to": "t2.a"},
    {"from": "fade.tags", "to": "light.before"}, {"from": "t1.result", "to": "light.t1"},
    {"from": "t2.result", "to": "light.t2"}, {"from": "lit.tags", "to": "light.tags"},
    {"from": "syl.units", "to": "text.units"}, {"from": "lineTime.start", "to": "text.start"},
    {"from": "lineTime.end", "to": "text.end"}, {"from": "light.tags", "to": "text.tags"},

    {"from": "syl.units", "to": "eight.units"},
    {"from": "info.i", "to": "i3.a"}, {"from": "eight.j", "to": "j2.a"},
    {"from": "i3.result", "to": "sum.a"}, {"from": "j2.result", "to": "sum.b"},
    {"from": "sum.result", "to": "mod.a"}, {"from": "mod.result", "to": "reach.a"},
    {"from": "eight.j", "to": "dirX.index"}, {"from": "eight.j", "to": "dirY.index"},
    {"from": "dirX.value", "to": "mulX.a"}, {"from": "reach.result", "to": "mulX.b"},
    {"from": "dirY.value", "to": "mulY.a"}, {"from": "reach.result", "to": "mulY.b"},
    {"from": "mulX.result", "to": "divX.a"}, {"from": "mulY.result", "to": "divY.a"},
    {"from": "divX.result", "to": "floorX.a"}, {"from": "divY.result", "to": "floorY.a"},
    {"from": "info.center", "to": "x.a"}, {"from": "floorX.result", "to": "x.b"},
    {"from": "info.middle", "to": "y.a"}, {"from": "floorY.result", "to": "y.b"},
    {"from": "x.result", "to": "there.x"}, {"from": "y.result", "to": "there.y"},
    {"from": "an.tags", "to": "move.before"}, {"from": "here.point", "to": "move.from"},
    {"from": "there.point", "to": "move.to"}, {"from": "move.tags", "to": "bord.before"},
    {"from": "bord.tags", "to": "shad.before"}, {"from": "shad.tags", "to": "blur.before"},
    {"from": "blur.tags", "to": "sparkColour.before"}, {"from": "spark.value", "to": "sparkColour.colour"},
    {"from": "sparkColour.tags", "to": "sparkFade.before"},
    {"from": "eight.units", "to": "sparks.units"}, {"from": "sparkTime.start", "to": "sparks.start"},
    {"from": "sparkTime.end", "to": "sparks.end"}, {"from": "sparkFade.tags", "to": "sparks.tags"},
    {"from": "shape.drawing", "to": "sparks.drawing"}]})";

constexpr std::string_view kFallingLetters = R"({"format": 1, "nodes": [
    {"id": "char", "type": "units.for_each", "fields": {"unit": "char"}},
    {"id": "info", "type": "units.info"},
    {"id": "time", "type": "time.retime", "fields": {"mode": "line", "start_offset": -800, "end_offset": 300}},
    {"id": "above", "type": "math.calculate", "fields": {"op": "subtract", "b": 40}},
    {"id": "from", "type": "values.point"},
    {"id": "to", "type": "values.point"},
    {"id": "drop", "type": "math.calculate", "fields": {"op": "subtract"}},
    {"id": "landed", "type": "math.calculate", "fields": {"op": "add", "b": 250}},
    {"id": "an", "type": "tags.tag", "fields": {"tag": "an", "value": 5}},
    {"id": "move", "type": "tags.move", "fields": {"timed": true}},
    {"id": "hidden", "type": "tags.tag", "fields": {"tag": "alpha", "alpha": 255}},
    {"id": "shown", "type": "tags.tag", "fields": {"tag": "alpha", "alpha": 0}},
    {"id": "appear", "type": "tags.transform"},
    {"id": "sung", "type": "tags.tag", "fields": {"tag": "1c", "colour": "#A0E090"}},
    {"id": "s1", "type": "math.calculate", "fields": {"op": "add", "b": 800}},
    {"id": "s2", "type": "math.calculate", "fields": {"op": "add", "b": 800}},
    {"id": "light", "type": "tags.transform"},
    {"id": "fade", "type": "tags.fade", "fields": {"fade_out": 300}},
    {"id": "emit", "type": "output.emit"}],
  "wires": [
    {"from": "info.middle", "to": "above.a"}, {"from": "info.center", "to": "from.x"},
    {"from": "above.result", "to": "from.y"}, {"from": "info.center", "to": "to.x"}, {"from": "info.middle", "to": "to.y"},
    {"from": "info.left", "to": "drop.a"}, {"from": "info.line_left", "to": "drop.b"},
    {"from": "drop.result", "to": "landed.a"},
    {"from": "an.tags", "to": "move.before"}, {"from": "from.point", "to": "move.from"}, {"from": "to.point", "to": "move.to"},
    {"from": "drop.result", "to": "move.t1"}, {"from": "landed.result", "to": "move.t2"},
    {"from": "move.tags", "to": "hidden.before"}, {"from": "hidden.tags", "to": "appear.before"},
    {"from": "shown.tags", "to": "appear.tags"}, {"from": "drop.result", "to": "appear.t1"},
    {"from": "landed.result", "to": "appear.t2"}, {"from": "info.start", "to": "s1.a"}, {"from": "info.end", "to": "s2.a"},
    {"from": "appear.tags", "to": "light.before"}, {"from": "sung.tags", "to": "light.tags"},
    {"from": "s1.result", "to": "light.t1"}, {"from": "s2.result", "to": "light.t2"},
    {"from": "light.tags", "to": "fade.before"},
    {"from": "char.units", "to": "emit.units"}, {"from": "time.start", "to": "emit.start"},
    {"from": "time.end", "to": "emit.end"}, {"from": "fade.tags", "to": "emit.tags"}]})";

constexpr std::string_view kPalette = R"({"format": 1, "nodes": [
    {"id": "line", "type": "units.for_each", "fields": {"unit": "line", "skip_blank": false}},
    {"id": "syl", "type": "units.for_each", "fields": {"unit": "syl", "skip_blank": false}},
    {"id": "info", "type": "units.info"},
    {"id": "kf", "type": "tags.tag", "fields": {"tag": "kf"}},
    {"id": "join", "type": "text.join"},
    {"id": "time", "type": "time.retime", "fields": {"mode": "line", "start_offset": -300, "end_offset": 300}},
    {"id": "fade", "type": "tags.fade", "fields": {"fade_in": 300, "fade_out": 300}},
    {"id": "place", "type": "math.calculate", "fields": {"op": "add", "b": 1}},
    {"id": "palette", "type": "values.choose",
     "fields": {"type": "colour", "values": ["#FFB43C", "#508CFF", "#64DC78", "#FF78C8"]}},
    {"id": "fill", "type": "tags.tag", "fields": {"tag": "1c"}},
    {"id": "k", "type": "tags.tag", "fields": {"tag": "k", "value": 30}},
    {"id": "emit", "type": "output.emit"}],
  "wires": [
    {"from": "info.kdur", "to": "kf.value"}, {"from": "syl.units", "to": "join.units"},
    {"from": "kf.tags", "to": "join.tags"},
    {"from": "info.line_index", "to": "place.a"}, {"from": "place.result", "to": "palette.index"},
    {"from": "fade.tags", "to": "fill.before"}, {"from": "palette.value", "to": "fill.colour"},
    {"from": "fill.tags", "to": "k.before"},
    {"from": "line.units", "to": "emit.units"}, {"from": "time.start", "to": "emit.start"},
    {"from": "time.end", "to": "emit.end"}, {"from": "k.tags", "to": "emit.tags"},
    {"from": "join.text", "to": "emit.text"}]})";

struct Preset {
    const char *golden;
    const char *name;
    std::string_view graph;
    int templateAndCodeLines;
};

const Preset kPresets[] = {
    {"01-classic-fill.out.ass", "fill", kClassicFill, 2},
    {"05-bounce.out.ass", "bounce", kBounce, 4},
    {"07-sparkles.out.ass", "sparkles", kSparkles, 2},
    {"09-falling-letters.out.ass", "letters", kFallingLetters, 1},
    {"10-palette.out.ass", "palette", kPalette, 3},
};

core::Document presetDocument(const Preset &p)
{
    std::string lines = commentLine("graph " + std::string(p.name), graphText(graphOf(p.graph)));
    for (int i = 1; i < p.templateAndCodeLines; ++i)
        lines += commentLine("", "");
    return fixtureWith(lines);
}

std::u8string wide(std::string_view s)
{
    return std::u8string(s.begin(), s.end());
}

}

class NodeEffectPreset : public testing::TestWithParam<Preset> {};

TEST_P(NodeEffectPreset, GivesTheTemplatersLinesByteForByte)
{
    const Preset &p = GetParam();
    const core::Document doc = presetDocument(p);
    const NodeEffectRun run = runGraph(doc, wide(p.name));
    EXPECT_EQ(linesOf(run), goldenLines(p.golden, p.name));
    EXPECT_EQ(run.sources.size(), 3u) << "the Other Line is not the graph's Style";
}

INSTANTIATE_TEST_SUITE_P(KaraokePresets, NodeEffectPreset, testing::ValuesIn(kPresets),
                         [](const testing::TestParamInfo<Preset> &info) { return std::string(info.param.name); });

TEST(NodeEffects, TheSameGraphGivesTheSameLinesOnEveryRunAndThreadCount)
{
    const core::Document doc = presetDocument(kPresets[2]);
    NodeEffectOptions one;
    one.threads = 1;
    NodeEffectOptions many;
    many.threads = 8;
    const auto a = linesOf(runGraph(doc, u8"sparkles", one));
    EXPECT_EQ(a, linesOf(runGraph(doc, u8"sparkles", many)));
    EXPECT_EQ(a, linesOf(runGraph(doc, u8"sparkles", one)));
}

namespace {

constexpr std::string_view kRandom = R"({"format": 1, "seed": 7, "nodes": [
    {"id": "syl", "type": "units.for_each"},
    {"id": "three", "type": "units.repeat", "fields": {"count": 3}},
    {"id": "rise", "type": "math.random", "fields": {"min": -20, "max": 20, "step": 1}},
    {"id": "blur", "type": "math.random", "fields": {"min": 0, "max": 2, "scope": "unit", "seed": 1}},
    {"id": "fsp", "type": "tags.tag", "fields": {"tag": "fsp"}},
    {"id": "be", "type": "tags.tag", "fields": {"tag": "blur"}},
    {"id": "emit", "type": "output.emit"}],
  "wires": [
    {"from": "syl.units", "to": "three.units"}, {"from": "rise.value", "to": "fsp.value"},
    {"from": "fsp.tags", "to": "be.before"}, {"from": "blur.value", "to": "be.value"},
    {"from": "three.units", "to": "emit.units"}, {"from": "be.tags", "to": "emit.tags"}]})";

std::vector<std::string> randomRun(std::int64_t seed, unsigned threads)
{
    NodeGraph graph = graphOf(kRandom);
    graph.seed = seed;
    const core::Document doc = fixtureWith(commentLine("graph sparkle", graphText(graph)));
    NodeEffectOptions options;
    options.threads = threads;
    return linesOf(runGraph(doc, u8"sparkle", options));
}

}

TEST(NodeEffects, CharUnitsAreWholeGraphemeClusters)
{
    std::string fixture = readFile(HIKARI_KARAOKE_PRESET_FIXTURES "/fixture.ass");
    fixture.erase(fixture.find("Dialogue: "));
    fixture += commentLine("graph letters", graphText(graphOf(kFallingLetters)));
    fixture += "Dialogue: 0,0:00:01.00,0:00:04.00,Romaji,,0,0,0,,{\\k40}กิน{\\k60}น้ำ{\\k30}e\u0301\U0001F600ab\n";
    std::vector<std::string> chars;
    for (const std::string &line : linesOf(runGraph(load(fixture), u8"letters")))
        chars.push_back(line.substr(line.rfind('}') + 1));
    EXPECT_EQ(chars, (std::vector<std::string>{"กิ", "น", "น้ำ", "e\u0301", "\U0001F600", "a", "b"}));
}

TEST(NodeEffects, RandomNumbersComeOnlyFromTheSeed)
{
    const auto a = randomRun(7, 1);
    ASSERT_EQ(a.size(), 3u * 17u);
    EXPECT_EQ(a, randomRun(7, 4));
    EXPECT_NE(a, randomRun(8, 1));
    const auto blurOf = [](const std::string &line) { return line.substr(line.find("\\blur")); };
    EXPECT_EQ(blurOf(a[0]), blurOf(a[1]));
    EXPECT_EQ(blurOf(a[1]), blurOf(a[2]));
    bool differs = false;
    for (std::size_t k = 0; k + 1 < a.size(); k += 3)
        differs = differs || a[k].substr(0, a[k].find("\\blur")) != a[k + 1].substr(0, a[k + 1].find("\\blur"));
    EXPECT_TRUE(differs);
}

TEST(NodeEffects, RegeneratingOneGraphLeavesAnothersLinesAlone)
{
    core::Document doc = fixtureWith(commentLine("graph fill", graphText(graphOf(kClassicFill))) +
                                     commentLine("graph bounce", graphText(graphOf(kBounce))));
    ASSERT_TRUE(applyNodeEffect(doc, runGraph(doc, u8"fill")));
    ASSERT_TRUE(applyNodeEffect(doc, runGraph(doc, u8"bounce")));
    const auto ofGraph = [&](std::u8string_view name) {
        std::vector<std::string> out;
        for (const core::LineRecord *l : doc.lines())
            if (isGeneratedBy(*l, name))
                out.push_back(narrow(core::legacy::assLineText(*l)));
        return out;
    };
    const auto bounce = ofGraph(u8"bounce");
    const auto fill = ofGraph(u8"fill");
    ASSERT_EQ(bounce.size(), 4u * 17u);
    ASSERT_EQ(fill.size(), 3u);

    NodeGraph changed = graphOf(kClassicFill);
    changed.nodes[6].fields["fade_in"] = Json(500);
    doc.editLine(graphLineOf(doc, u8"fill"), [&](core::LineRecord &l) { l.text = wide(graphText(changed)); });
    ASSERT_TRUE(applyNodeEffect(doc, runGraph(doc, u8"fill")));
    EXPECT_EQ(ofGraph(u8"bounce"), bounce);
    const auto refilled = ofGraph(u8"fill");
    ASSERT_EQ(refilled.size(), 3u);
    EXPECT_NE(refilled, fill);
    EXPECT_NE(refilled[0].find("\\fad(500,300)"), std::string::npos);
}

TEST(NodeEffects, ApplyCommentsOutTheSourcesAndAReRunChangesNothing)
{
    core::Document doc = presetDocument(kPresets[0]);
    const NodeEffectRun run = runGraph(doc, u8"fill");
    ASSERT_TRUE(applyNodeEffect(doc, run));
    int karaoke = 0;
    for (const core::LineRecord *l : doc.lines())
        if (l->comment && l->effect == u8"karaoke")
            ++karaoke;
    EXPECT_EQ(karaoke, 3);
    const std::string once = text(core::encodeAss(doc));
    ASSERT_TRUE(applyNodeEffect(doc, runGraph(doc, u8"fill")));
    EXPECT_EQ(text(core::encodeAss(doc)), once);

    core::Document reread = load(once);
    std::set<core::LineId> second;
    for (const core::LineRecord *l : reread.lines())
        if (l->comment && l->effect == u8"karaoke" && second.empty())
            second.insert(l->id);
    NodeEffectOptions only;
    only.sources = second;
    const NodeEffectRun owners = runGraph(reread, u8"fill");
    const NodeEffectRun some = runGraph(reread, u8"fill", only);
    ASSERT_EQ(some.lines.size(), 1u);
    ASSERT_TRUE(applyNodeEffect(reread, some, &owners));
    EXPECT_EQ(text(core::encodeAss(reread)), once);
}

TEST(NodeEffects, ApplyIsOneUndoStep)
{
    EditSession session(presetDocument(kPresets[1]));
    const std::string before = text(core::encodeAss(session.document()));
    const NodeEffectRun run = runGraph(session.document(), u8"bounce");
    ASSERT_TRUE(applyNodeEffect(session, run));
    EXPECT_NE(text(core::encodeAss(session.document())), before);
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(text(core::encodeAss(session.document())), before);
}

TEST(NodeEffects, GeneratedLinesFoldInTheGrid)
{
    core::Document doc = presetDocument(kPresets[0]);
    const NodeEffectRun run = runGraph(doc, u8"fill");
    for (const core::LineRecord &l : run.lines)
        EXPECT_TRUE(isFoldableGeneratedLine(l));
}

TEST(NodeEffects, TheCacheGivesTheSameLinesWithoutMeasuringAgain)
{
    const core::Document doc = presetDocument(kPresets[2]);
    NodeEffectCache cache;
    FakeMeasure measure;
    NodeEffectOptions options;
    options.cache = &cache;
    options.measure = &measure;
    const auto first = linesOf(runGraph(doc, u8"sparkles", options));
    EXPECT_EQ(cache.size(), 2u * 3u);
    EXPECT_EQ(linesOf(runGraph(doc, u8"sparkles", options)), first);
    EXPECT_EQ(cache.size(), 2u * 3u);
    EXPECT_EQ(first, goldenLines(kPresets[2].golden, "sparkles"));
}

namespace {

class FakeLua : public LuaNodePort {
public:
    std::expected<std::vector<std::vector<NodeValue>>, Failure> run(const Call &call) override
    {
        calls.push_back(call.rows.size());
        std::vector<std::vector<NodeValue>> out;
        for (const auto &row : call.rows)
            out.push_back({std::get<double>(row[0]) * 2});
        return out;
    }
    std::vector<std::size_t> calls;
};

constexpr std::string_view kLua = R"({"format": 1, "nodes": [
    {"id": "syl", "type": "units.for_each"},
    {"id": "info", "type": "units.info"},
    {"id": "double", "type": "lua.code", "fields": {"code": "out = i * 2",
        "inputs": [{"name": "i", "type": "Number"}], "outputs": [{"name": "out", "type": "Number"}]}},
    {"id": "layer", "type": "output.emit"}],
  "wires": [{"from": "info.i", "to": "double.i"}, {"from": "syl.units", "to": "layer.units"},
            {"from": "double.out", "to": "layer.layer"}]})";

}

TEST(NodeEffects, ALuaNodeRunsOnlyWithTheUsersTrustAndOncePerLine)
{
    const core::Document doc = fixtureWith(commentLine("graph doubled", graphText(graphOf(kLua))));
    FakeMeasure measure;
    FakeLua lua;
    NodeEffectOptions options;
    options.measure = &measure;
    options.lua = &lua;
    auto untrusted = runDocumentGraph(doc, graphLineOf(doc, u8"doubled"), options);
    ASSERT_TRUE(untrusted);
    EXPECT_FALSE(untrusted->ran);
    EXPECT_TRUE(untrusted->lines.empty());
    ASSERT_EQ(untrusted->problems.size(), 1u);
    EXPECT_EQ(untrusted->problems[0].node, "double");
    EXPECT_TRUE(lua.calls.empty());
    core::Document unchanged = doc;
    EXPECT_FALSE(applyNodeEffect(unchanged, *untrusted));

    options.trustLua = true;
    const NodeEffectRun run = runGraph(doc, u8"doubled", options);
    EXPECT_EQ(lua.calls, (std::vector<std::size_t>{7, 5, 5}));
    ASSERT_EQ(run.lines.size(), 17u);
    EXPECT_EQ(run.lines[0].layer.value, 2);
    EXPECT_EQ(run.lines[1].layer.value, 4);
}

TEST(NodeEffects, ALuaHelperThatStopsEndsTheRunAndItWritesNothing)
{
    class StoppingLua : public LuaNodePort {
    public:
        std::expected<std::vector<std::vector<NodeValue>>, Failure> run(const Call &) override
        {
            ++calls;
            return std::unexpected(Failure{"took longer than 10 seconds and was stopped", true});
        }
        int calls = 0;
    };
    const core::Document doc = fixtureWith(commentLine("graph doubled", graphText(graphOf(kLua))));
    FakeMeasure measure;
    StoppingLua lua;
    NodeEffectOptions options;
    options.measure = &measure;
    options.lua = &lua;
    options.trustLua = true;
    auto run = runDocumentGraph(doc, graphLineOf(doc, u8"doubled"), options);
    ASSERT_TRUE(run);
    EXPECT_EQ(lua.calls, 1);
    EXPECT_FALSE(run->ran);
    EXPECT_TRUE(run->lines.empty());
    ASSERT_EQ(run->problems.size(), 1u);
    EXPECT_EQ(run->problems[0].node, "double");
    EXPECT_EQ(run->problems[0].message, "took longer than 10 seconds and was stopped");
    core::Document unchanged = doc;
    EXPECT_FALSE(applyNodeEffect(unchanged, *run));
}

TEST(NodeEffects, AGroupRunsAsItsNodesWould)
{
    const NodeGraph flat = graphOf(kClassicFill);
    const NodeGraph grouped = graphOf(R"({"format": 1, "nodes": [
    {"id": "line", "type": "units.for_each", "fields": {"unit": "line", "skip_blank": false}},
    {"id": "syl", "type": "units.for_each", "fields": {"unit": "syl", "skip_blank": false}},
    {"id": "info", "type": "units.info"},
    {"id": "kf", "type": "tags.tag", "fields": {"tag": "kf"}},
    {"id": "join", "type": "text.join"},
    {"id": "time", "type": "time.retime", "fields": {"mode": "line", "start_offset": -300, "end_offset": 300}},
    {"id": "in", "type": "values.number", "fields": {"value": 300}},
    {"id": "intro", "type": "groups.group", "fields": {"graph": {"nodes": [
        {"id": "input", "type": "groups.input", "fields": {"ports": [{"name": "fade_in", "type": "Time"}]}},
        {"id": "fade", "type": "tags.fade", "fields": {"fade_out": 300}},
        {"id": "k", "type": "tags.tag", "fields": {"tag": "k", "value": 30}},
        {"id": "output", "type": "groups.output", "fields": {"ports": [{"name": "tags", "type": "Tags"}]}}],
      "wires": [{"from": "input.fade_in", "to": "fade.fade_in"}, {"from": "fade.tags", "to": "k.before"},
                {"from": "k.tags", "to": "output.tags"}]}}},
    {"id": "emit", "type": "output.emit"}],
  "wires": [
    {"from": "info.kdur", "to": "kf.value"}, {"from": "syl.units", "to": "join.units"},
    {"from": "kf.tags", "to": "join.tags"}, {"from": "in.value", "to": "intro.fade_in"},
    {"from": "line.units", "to": "emit.units"}, {"from": "time.start", "to": "emit.start"},
    {"from": "time.end", "to": "emit.end"}, {"from": "intro.tags", "to": "emit.tags"},
    {"from": "join.text", "to": "emit.text"}]})");
    EXPECT_TRUE(validateGraph(grouped).empty());
    const auto ports = nodePorts(grouped.nodes[7]);
    ASSERT_TRUE(ports);
    ASSERT_EQ(ports->inputs.size(), 1u);
    EXPECT_EQ(ports->inputs[0].type, PortType::Time);
    EXPECT_EQ(ports->outputs[0].name, "tags");
    const core::Document a = fixtureWith(commentLine("graph fill", graphText(flat)) + commentLine("", ""));
    const core::Document b = fixtureWith(commentLine("graph fill", graphText(grouped)) + commentLine("", ""));
    EXPECT_EQ(linesOf(runGraph(a, u8"fill")), linesOf(runGraph(b, u8"fill")));
}

namespace {

std::vector<GraphProblem> problemsOf(std::string_view json)
{
    const auto graph = parseGraph(json);
    if (!graph)
        return {graph.error()};
    return validateGraph(*graph);
}

}

TEST(NodeGraphValidation, NamesTheNodeThatIsWrong)
{
    EXPECT_TRUE(problemsOf(kSparkles).empty());
    EXPECT_EQ(problemsOf(R"({"format": 1, "nodes": [{"id": "a", "type": "units.nothing"}]})"),
              (std::vector<GraphProblem>{{"a", "", "has the unknown type \"units.nothing\""}}));
    EXPECT_EQ(problemsOf(R"({"format": 1, "nodes": [{"id": "a", "type": "values.number", "fields": {"value": "1"}},
                                                     {"id": "b", "type": "values.number", "fields": {"size": 1}}]})"),
              (std::vector<GraphProblem>{{"a", "value", "is not a number"},
                                         {"b", "size", "is not a field of values.number"}}));
    EXPECT_EQ(problemsOf(R"({"format": 1, "nodes": [{"id": "a", "type": "values.number"}, {"id": "a", "type": "values.text"}]})")
                  .front(),
              (GraphProblem{"a", "", "has an id another node has"}));
    EXPECT_EQ(problemsOf(R"({"format": 1, "nodes": [{"id": "e", "type": "output.emit"}, {"id": "t", "type": "values.text"}],
                             "wires": [{"from": "t.value", "to": "e.layer"}, {"from": "t.nothing", "to": "e.text"}]})"),
              (std::vector<GraphProblem>{{"e", "layer", "takes Number, not Text"},
                                         {"t", "nothing", "is not an output"},
                                         {"e", "units", "needs units wired in"}}));
    EXPECT_EQ(problemsOf(R"({"format": 1, "nodes": [{"id": "a", "type": "math.calculate"}, {"id": "b", "type": "math.calculate"}],
                             "wires": [{"from": "a.result", "to": "b.a"}, {"from": "b.result", "to": "a.a"}]})")
                  .front()
                  .message,
              "is wired into a loop");
    EXPECT_EQ(problemsOf(R"({"format": 1, "nodes": [
        {"id": "syl", "type": "units.for_each"}, {"id": "twice", "type": "units.repeat", "fields": {"count": 2}},
        {"id": "e", "type": "output.emit"}],
      "wires": [{"from": "syl.units", "to": "twice.units"}, {"from": "syl.units", "to": "e.units"},
                {"from": "twice.j", "to": "e.layer"}]})"),
              (std::vector<GraphProblem>{{"e", "layer", "reads twice.j outside its units"}}));
    EXPECT_EQ(problemsOf(R"({"format": 1, "nodes": [{"id": "n", "type": "values.number"}]})"),
              (std::vector<GraphProblem>{{"", "", "the graph has no Emit node, so it writes no Line"}}));
    EXPECT_EQ(problemsOf(R"({"format": 2, "nodes": []})").front().message,
              "the graph needs a newer HikariSub (graph format 2)");
    EXPECT_EQ(problemsOf(R"({"format": 1, "nodes": [{"id": "g", "type": "groups.group", "fields": {"graph": {
        "nodes": [{"id": "x", "type": "math.calculate", "fields": {"op": "cube"}}]}}}]})"),
              (std::vector<GraphProblem>{{"g/x", "op", "is not one of its choices"}}));
    EXPECT_EQ(problemsOf(R"({"format": 1, "nodes": [{"id": "c", "type": "values.choose",
        "fields": {"type": "colour", "values": ["#FFFFFF", "white"]}}]})")
                  .front(),
              (GraphProblem{"c", "values", "holds a value that is not of its type"}));
}

TEST(NodeGraphFormat, ItsJsonRoundTripsInAGraphLine)
{
    NodeGraph graph = graphOf(kBounce);
    graph.nodes[0].at = std::pair{12.5, -3.0};
    graph.allStyles = true;
    const core::LineRecord line = graphLine(u8"bounce", u8"Romaji", graph);
    EXPECT_EQ(graphNameOf(line), u8"bounce");
    EXPECT_TRUE(line.comment);
    const auto back = parseGraph(narrow(line.text));
    ASSERT_TRUE(back);
    EXPECT_EQ(graphJson(*back), graphJson(graph));
    EXPECT_TRUE(isGraphName(u8"Sparkle 2"));
    for (const char8_t *bad : {u8"", u8" lead", u8"a,b", u8"my karaoke", u8"Karaoke fill", u8"a/b", u8"end."})
        EXPECT_FALSE(isGraphName(bad)) << narrow(bad);
}

TEST(NodeGraphLibrary, KeepsTheUsersGraphsAsFiles)
{
    const auto folder = std::filesystem::temp_directory_path() / ("hikari-ne1-" + std::to_string(std::random_device{}()));
    const NodeGraphLibrary library(folder / std::string(kNodeGraphFolder));
    EXPECT_TRUE(library.names().empty());
    EXPECT_FALSE(library.load(u8"fill"));
    ASSERT_TRUE(library.save(u8"fill", graphOf(kClassicFill)));
    ASSERT_TRUE(library.save(u8"bounce", graphOf(kBounce)));
    EXPECT_FALSE(library.save(u8"a/b", graphOf(kBounce)));
    EXPECT_EQ(library.names(), (std::vector<std::u8string>{u8"bounce", u8"fill"}));
    ASSERT_TRUE(library.save(u8"fill", graphOf(kPalette)));
    const auto fill = library.load(u8"fill");
    ASSERT_TRUE(fill);
    EXPECT_EQ(graphJson(*fill), graphJson(graphOf(kPalette)));
    EXPECT_TRUE(library.remove(u8"bounce"));
    EXPECT_FALSE(library.remove(u8"bounce"));
    EXPECT_EQ(library.names(), (std::vector<std::u8string>{u8"fill"}));
    std::filesystem::remove_all(folder);
}

TEST(NodeCatalogue, EveryNodeHasAValidFieldSchemaAndUniqueId)
{
    const std::set<std::string> kFromTheUnit = {"output.emit.start", "output.emit.end",
                                                "output.emit.text", "text.join.text"};
    std::set<std::string> ids;
    for (const NodeSpec &spec : nodeCatalogue()) {
        EXPECT_TRUE(ids.insert(spec.id).second) << spec.id;
        EXPECT_EQ(spec.id.substr(0, spec.id.find('.')), spec.category) << spec.id;
        const Json schema = nodeFieldsSchema(spec);
        for (const FieldSpec &f : spec.fields)
            EXPECT_NE(schema.find("properties")->find(f.name), nullptr) << spec.id << "." << f.name;
        for (const PortSpec &in : spec.inputs)
            if (in.type != PortType::Units && in.type != PortType::Tags && in.type != PortType::Drawing &&
                !kFromTheUnit.contains(spec.id + "." + in.name))
                EXPECT_NE(fieldSpec(spec, in.name), nullptr) << spec.id << " has no constant for " << in.name;
    }
    EXPECT_GE(ids.size(), 20u);
}

TEST(KaraokeSyllables, SplitsAsTheAutomationHelperDoes)
{
    const auto syls = karaokeSyllables("{\\k40}ki{\\kf35}ra {\\k0}", 1000, 4000);
    ASSERT_EQ(syls.size(), 4u);
    EXPECT_EQ(syls[0].text, "");
    EXPECT_EQ(syls[1].text, "ki");
    EXPECT_EQ(syls[2].tag, "kf");
    EXPECT_EQ(syls[2].stripped, "ra ");
    EXPECT_EQ(syls[2].start, 400);
    EXPECT_EQ(syls[2].end, 750);
    const auto plain = karaokeSyllables("no tags", 1000, 4000);
    ASSERT_EQ(plain.size(), 2u);
    EXPECT_EQ(plain[1].start, 1000);
    EXPECT_EQ(plain[1].duration, 3000);
}
