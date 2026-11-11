#include "hikari/application/edit_session.h"
#include "hikari/application/generated_runs.h"
#include "hikari/application/karaoke_syllables.h"
#include "hikari/application/node_effects.h"
#include "hikari/application/node_graph.h"
#include "hikari/application/node_graph_editing.h"
#include "hikari/application/node_graph_library.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/ass_save.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <random>
#include <fstream>
#include <map>
#include <set>
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

std::string_view presetGraph(std::string_view templateFile)
{
    for (const NodeGraphPreset &p : nodeGraphPresets())
        if (p.templateFile == templateFile)
            return p.graph;
    ADD_FAILURE() << "no preset graph for " << templateFile;
    return {};
}

const std::string_view kClassicFill = presetGraph("01-classic-fill.ass");
const std::string_view kBounce = presetGraph("05-bounce.ass");
const std::string_view kSparkles = presetGraph("07-sparkles.ass");
const std::string_view kFallingLetters = presetGraph("09-falling-letters.ass");
const std::string_view kPalette = presetGraph("10-palette.ass");
const std::string_view kStaggeredFade = presetGraph("02-staggered-fade.ass");
const std::string_view kColourSweep = presetGraph("03-colour-sweep.ass");
const std::string_view kScalePop = presetGraph("04-scale-pop.ass");
const std::string_view kGlow = presetGraph("06-glow.ass");
const std::string_view kEcho = presetGraph("08-echo.ass");

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
    {"02-staggered-fade.out.ass", "stagger", kStaggeredFade, 1},
    {"03-colour-sweep.out.ass", "sweep", kColourSweep, 2},
    {"04-scale-pop.out.ass", "pop", kScalePop, 1},
    {"06-glow.out.ass", "glow", kGlow, 2},
    {"08-echo.out.ass", "echo", kEcho, 2},
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

TEST(NodeEffects, ATraceRecordsWhatEachNodeGaveOnTheFirstLine)
{
    const core::Document doc = presetDocument(kPresets[4]);
    NodeValueTrace trace;
    NodeEffectOptions options;
    options.trace = &trace;
    const NodeEffectRun traced = runGraph(doc, u8"palette", options);
    EXPECT_EQ(linesOf(traced), linesOf(runGraph(doc, u8"palette"))) << "a trace changes nothing";
    ASSERT_TRUE(trace.values.contains("palette"));
    EXPECT_TRUE(std::holds_alternative<NodeColour>(trace.values["palette"].at("value")));
    EXPECT_TRUE(std::holds_alternative<NodeTags>(trace.values["k"].at("tags")));
    EXPECT_FALSE(trace.values.contains("emit")) << "an Emit has no outputs";
}

TEST(NodeEffects, GroupingNodesChangesNoLine)
{
    const std::vector<std::string> chosen{"place", "palette", "fill", "fade"};
    const auto grouped = groupedNodes(graphOf(kPalette), chosen, "colour");
    ASSERT_TRUE(grouped);
    EXPECT_TRUE(validateGraph(*grouped).empty());
    const core::Document flat = presetDocument(kPresets[4]);
    const core::Document inGroup = fixtureWith(commentLine("graph palette", graphText(*grouped)) + commentLine("", "") +
                                               commentLine("", ""));
    EXPECT_EQ(linesOf(runGraph(inGroup, u8"palette")), linesOf(runGraph(flat, u8"palette")));
}

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

namespace {

std::vector<std::string> runOn(std::string_view graphJson, NodeEffectOptions options = {})
{
    const core::Document doc = fixtureWith(commentLine("graph g", graphText(graphOf(graphJson))));
    return linesOf(runGraph(doc, u8"g", options));
}

std::vector<double> numbersAfter(const std::string &line, std::string_view tag)
{
    std::vector<double> out;
    const auto at = line.find(std::string(tag) + "(");
    if (at == std::string::npos)
        return out;
    std::istringstream in(line.substr(at + tag.size() + 1));
    for (double v; in >> v;) {
        out.push_back(v);
        if (in.peek() != ',')
            break;
        in.ignore();
    }
    return out;
}

std::string between(const std::string &line, std::string_view from, char to)
{
    const auto at = line.find(from);
    if (at == std::string::npos)
        return {};
    const auto start = at + from.size();
    return line.substr(start, line.find_first_of(std::string(1, to) + "\\}", start) - start);
}

constexpr std::string_view kStrips = R"({"format": 1, "nodes": [
    {"id": "line", "type": "units.for_each", "fields": {"unit": "line"}},
    {"id": "strip", "type": "units.clip_gradient", "fields": {"strips": 4, "colours": ["#FF0000", "#0000FF"], "padding": 2}},
    {"id": "c", "type": "tags.tag", "fields": {"tag": "1c"}},
    {"id": "emit", "type": "output.emit"}],
  "wires": [
    {"from": "line.units", "to": "strip.units"}, {"from": "strip.clip", "to": "c.before"},
    {"from": "strip.colour", "to": "c.colour"}, {"from": "strip.units", "to": "emit.units"},
    {"from": "c.tags", "to": "emit.tags"}]})";

}

TEST(NodeEffectsClipGradient, SplitsEachUnitIntoStripsThatShareTheirEdges)
{
    const auto lines = runOn(kStrips);
    ASSERT_EQ(lines.size(), 3u * 4u);
    for (std::size_t first = 0; first < lines.size(); first += 4) {
        std::vector<std::vector<double>> clips;
        for (std::size_t k = 0; k < 4; ++k) {
            clips.push_back(numbersAfter(lines[first + k], "\\clip"));
            ASSERT_EQ(clips.back().size(), 4u) << lines[first + k];
            for (const double v : clips.back())
                EXPECT_EQ(v, std::floor(v)) << lines[first + k];
            EXPECT_LT(clips.back()[0], clips.back()[2]);
            EXPECT_EQ(clips.back()[1], clips.front()[1]);
            EXPECT_EQ(clips.back()[3], clips.front()[3]);
            if (k > 0)
                EXPECT_EQ(clips[k - 1][2], clips[k][0]);
        }
        EXPECT_EQ(between(lines[first], "\\1c", '\\'), "&H0000FF&");
        EXPECT_EQ(between(lines[first + 1], "\\1c", '\\'), "&H5500AA&");
        EXPECT_EQ(between(lines[first + 3], "\\1c", '\\'), "&HFF0000&");
    }
}

TEST(NodeEffectsClipGradient, StacksStripsDownAndPadsTheBox)
{
    std::string down(kStrips);
    down.replace(down.find("\"strips\": 4"), 11, "\"strips\": 2, \"direction\": \"down\"");
    std::string unpadded(kStrips);
    unpadded.replace(unpadded.find(", \"padding\": 2"), 14, "");
    const auto stacked = runOn(down), plain = runOn(unpadded), padded = runOn(kStrips);
    ASSERT_EQ(stacked.size(), 3u * 2u);
    const auto a = numbersAfter(stacked[0], "\\clip"), b = numbersAfter(stacked[1], "\\clip");
    EXPECT_EQ(a[3], b[1]);
    EXPECT_EQ(a[0], b[0]);
    EXPECT_EQ(a[2], b[2]);
    const auto p = numbersAfter(padded[0], "\\clip"), q = numbersAfter(plain[0], "\\clip");
    EXPECT_LE(p[0], q[0] - 2);
    EXPECT_LE(p[1], q[1] - 2);
}

namespace {

std::string arrayGraph(std::string_view fields, bool withPath = false)
{
    return std::string(R"({"format": 1, "nodes": [
    {"id": "line", "type": "units.for_each", "fields": {"unit": "line"}},
    {"id": "copy", "type": "units.array", "fields": )") +
           std::string(fields) + R"(},
    {"id": "path", "type": "drawings.shape", "fields": {"shape": "m 0 0 l 100 0 100 50"}},
    {"id": "pos", "type": "tags.tag", "fields": {"tag": "pos"}},
    {"id": "frz", "type": "tags.tag", "fields": {"tag": "frz"}},
    {"id": "emit", "type": "output.emit"}],
  "wires": [
    {"from": "line.units", "to": "copy.units"}, )" +
           std::string(withPath ? R"({"from": "path.drawing", "to": "copy.path"}, )" : "") +
           R"({"from": "copy.offset", "to": "pos.point"}, {"from": "pos.tags", "to": "frz.before"},
    {"from": "copy.angle", "to": "frz.value"}, {"from": "copy.units", "to": "emit.units"},
    {"from": "frz.tags", "to": "emit.tags"}]})";
}

std::vector<std::string> placesOfFirstLine(const std::vector<std::string> &lines, std::size_t count)
{
    std::vector<std::string> out;
    for (std::size_t k = 0; k < count && k < lines.size(); ++k)
        out.push_back(between(lines[k], "\\pos(", ')') + " " + between(lines[k], "\\frz", '\\'));
    return out;
}

}

TEST(NodeEffectsArray, CopiesOnAGridACircleAndAlongAPath)
{
    const auto grid = runOn(arrayGraph(R"({"shape": "grid", "columns": 3, "rows": 2, "spacing": [10, 20]})"));
    ASSERT_EQ(grid.size(), 3u * 6u);
    EXPECT_EQ(placesOfFirstLine(grid, 6), (std::vector<std::string>{"-10,-10 0", "0,-10 0", "10,-10 0", "-10,10 0",
                                                                     "0,10 0", "10,10 0"}));
    const auto circle = runOn(arrayGraph(R"({"shape": "circle", "count": 4, "radius": 10, "start_angle": 0})"));
    ASSERT_EQ(circle.size(), 3u * 4u);
    EXPECT_EQ(placesOfFirstLine(circle, 4),
              (std::vector<std::string>{"10,0 0", "0,-10 90", "-10,0 180", "0,10 270"}));
    const auto path = runOn(arrayGraph(R"({"shape": "path", "count": 4})", true));
    ASSERT_EQ(path.size(), 3u * 4u);
    EXPECT_EQ(placesOfFirstLine(path, 4),
              (std::vector<std::string>{"0,0 0", "50,0 0", "100,0 0", "100,50 -90"}));
}

TEST(NodeEffectsArray, APathNeedsADrawingOfLineAndCurveCommands)
{
    const auto problems = validateGraph(graphOf(arrayGraph(R"({"shape": "path"})")));
    EXPECT_EQ(problems, (std::vector<GraphProblem>{{"copy", "path", "needs a drawing wired in for a path"}}));
    std::string spline = arrayGraph(R"({"shape": "path"})", true);
    spline.replace(spline.find("m 0 0 l 100 0 100 50"), 20, "m 0 0 s 10 0 10 10 0 10");
    static FakeMeasure measure;
    NodeEffectOptions options;
    options.measure = &measure;
    const core::Document doc = fixtureWith(commentLine("graph g", graphText(graphOf(spline))));
    const auto run = runDocumentGraph(doc, graphLineOf(doc, u8"g"), options);
    ASSERT_TRUE(run);
    EXPECT_TRUE(run->lines.empty());
    ASSERT_FALSE(run->problems.empty());
    EXPECT_EQ(run->problems.front().message, "uses a path command other than m, n, l and b");
}

namespace {

std::string particleGraph(std::int64_t seed, std::string_view area)
{
    return R"({"format": 1, "seed": )" + std::to_string(seed) + R"(, "nodes": [
    {"id": "syl", "type": "units.for_each"},
    {"id": "spark", "type": "units.particles", "fields": {"count": 5, "width": 40, "height": 20, "area": ")" +
           std::string(area) + R"("}},
    {"id": "pos", "type": "tags.tag", "fields": {"tag": "pos"}},
    {"id": "fade", "type": "tags.tag", "fields": {"tag": "alpha"}},
    {"id": "scale", "type": "math.calculate", "fields": {"op": "multiply", "b": 255}},
    {"id": "emit", "type": "output.emit"}],
  "wires": [
    {"from": "syl.units", "to": "spark.units"}, {"from": "spark.offset", "to": "pos.point"},
    {"from": "pos.tags", "to": "fade.before"}, {"from": "spark.random", "to": "scale.a"},
    {"from": "scale.result", "to": "fade.alpha"}, {"from": "spark.units", "to": "emit.units"},
    {"from": "fade.tags", "to": "emit.tags"}]})";
}

}

TEST(NodeEffectsParticles, SpreadWithinTheirAreaFromTheSeedAlone)
{
    NodeEffectOptions one, many;
    one.threads = 1;
    many.threads = 4;
    const auto a = runOn(particleGraph(3, "rectangle"), one);
    ASSERT_EQ(a.size(), 17u * 5u);
    EXPECT_EQ(a, runOn(particleGraph(3, "rectangle"), many));
    EXPECT_NE(a, runOn(particleGraph(4, "rectangle"), one));
    std::set<std::string> places;
    for (const std::string &line : a) {
        const auto p = numbersAfter(line, "\\pos");
        ASSERT_EQ(p.size(), 2u) << line;
        EXPECT_LE(std::fabs(p[0]), 20) << line;
        EXPECT_LE(std::fabs(p[1]), 10) << line;
        places.insert(between(line, "\\pos(", ')'));
    }
    EXPECT_GT(places.size(), a.size() / 2);
    for (const std::string &line : runOn(particleGraph(3, "ellipse"), one)) {
        const auto p = numbersAfter(line, "\\pos");
        ASSERT_EQ(p.size(), 2u) << line;
        EXPECT_LE((p[0] / 20) * (p[0] / 20) + (p[1] / 10) * (p[1] / 10), 1.0 + 1e-6) << line;
    }
}

TEST(NodeEffectsParticles, TheirValuesAreReadOnlyInsideTheirUnits)
{
    std::string outside = particleGraph(0, "rectangle");
    const std::string_view inside = R"({"from": "spark.units", "to": "emit.units"})";
    outside.replace(outside.find(inside), inside.size(), R"({"from": "syl.units", "to": "emit.units"})");
    EXPECT_EQ(validateGraph(graphOf(outside)),
              (std::vector<GraphProblem>{{"pos", "point", "reads spark.offset outside its units"},
                                         {"scale", "a", "reads spark.random outside its units"}}));
}

TEST(NodeEffectExamples, EachShowsItsNodeAndRunsTheSameEveryTime)
{
    const std::map<std::string_view, std::string_view> nodeOf = {{"gradient-strips", "units.clip_gradient"},
                                                                 {"ring-of-copies", "units.array"},
                                                                 {"particle-burst", "units.particles"},
                                                                 {"highlight-flashes", "units.highlights"},
                                                                 {"paint-and-cloud", "units.inline_fx"}};
    std::set<std::string_view> seen;
    for (const NodeGraphPreset &p : nodeGraphPresets()) {
        if (p.example.empty())
            continue;
        seen.insert(p.example);
        const NodeGraph graph = graphOf(p.graph);
        EXPECT_TRUE(validateGraph(graph).empty()) << p.example;
        ASSERT_TRUE(nodeOf.contains(p.example)) << p.example;
        EXPECT_TRUE(std::ranges::any_of(graph.nodes, [&](const GraphNode &n) { return n.type == nodeOf.at(p.example); }))
            << p.example;
        NodeEffectOptions one, many;
        one.threads = 1;
        many.threads = 8;
        const auto lines = runOn(p.graph, one);
        EXPECT_FALSE(lines.empty()) << p.example;
        EXPECT_EQ(lines, runOn(p.graph, many)) << p.example;
        EXPECT_EQ(lines, runOn(p.graph, one)) << p.example;
    }
    EXPECT_EQ(seen.size(), nodeOf.size());
}

namespace {

std::vector<std::string> runOnKaraoke(std::string_view graphJson, std::string_view karaoke)
{
    std::string fixture = readFile(HIKARI_KARAOKE_PRESET_FIXTURES "/fixture.ass");
    fixture.erase(fixture.find("Dialogue: "));
    fixture += commentLine("graph g", graphText(graphOf(graphJson)));
    fixture += "Dialogue: 0,0:00:01.00,0:00:04.00,Romaji,,0,0,0,," + std::string(karaoke) + "\n";
    return linesOf(runGraph(load(fixture), u8"g"));
}

std::string highlightsGraph(std::string_view unit, bool skipBlank)
{
    return R"({"format": 1, "nodes": [
        {"id": "units", "type": "units.for_each", "fields": {"unit": ")" + std::string(unit) + R"("}},
        {"id": "hl", "type": "units.highlights", "fields": {"skip_blank": )" + (skipBlank ? "true" : "false") + R"(}},
        {"id": "info", "type": "units.info"},
        {"id": "time", "type": "time.retime", "fields": {"mode": "syl"}},
        {"id": "index", "type": "tags.tag", "fields": {"tag": "fsp"}},
        {"id": "count", "type": "tags.tag", "fields": {"tag": "fs"}},
        {"id": "k", "type": "tags.tag", "fields": {"tag": "k"}},
        {"id": "emit", "type": "output.emit"}],
      "wires": [
        {"from": "units.units", "to": "hl.units"}, {"from": "hl.index", "to": "index.value"},
        {"from": "index.tags", "to": "count.before"}, {"from": "hl.count", "to": "count.value"},
        {"from": "count.tags", "to": "k.before"}, {"from": "info.kdur", "to": "k.value"},
        {"from": "hl.units", "to": "emit.units"}, {"from": "k.tags", "to": "emit.tags"},
        {"from": "time.start", "to": "emit.start"}, {"from": "time.end", "to": "emit.end"}]})";
}

std::string inlineFxGraph(std::string_view unit, std::string_view fx)
{
    return R"({"format": 1, "nodes": [
        {"id": "units", "type": "units.for_each", "fields": {"unit": ")" + std::string(unit) + R"("}},
        {"id": "fx", "type": "units.inline_fx", "fields": {"fx": ")" + std::string(fx) + R"("}},
        {"id": "emit", "type": "output.emit"}],
      "wires": [{"from": "units.units", "to": "fx.units"}, {"from": "fx.units", "to": "emit.units"}]})";
}

std::vector<std::string> textsOf(const std::vector<std::string> &lines)
{
    std::vector<std::string> out;
    for (const std::string &line : lines)
        out.push_back(line.substr(line.find(",fx g,") + 6));
    return out;
}

}

TEST(NodeEffectsHighlights, GiveEachSyllableOncePerHighlightWithItsTimes)
{
    constexpr std::string_view karaoke = R"({\k20}ha{\k30}#{\k0}#{\k10}na)";
    EXPECT_EQ(runOnKaraoke(highlightsGraph("syl", true), karaoke),
              (std::vector<std::string>{
                  R"(Dialogue: 0,0:00:01.00,0:00:01.20,Romaji,,0,0,0,fx g,{\fsp1\fs3\k20}ha)",
                  R"(Dialogue: 0,0:00:01.20,0:00:01.50,Romaji,,0,0,0,fx g,{\fsp2\fs3\k30}ha)",
                  R"(Dialogue: 0,0:00:01.50,0:00:01.60,Romaji,,0,0,0,fx g,{\fsp1\fs1\k10}na)",
              }));
    const auto all = runOnKaraoke(highlightsGraph("syl", false), karaoke);
    ASSERT_EQ(all.size(), 4u);
    EXPECT_EQ(all[2], R"(Dialogue: 0,0:00:01.50,0:00:01.50,Romaji,,0,0,0,fx g,{\fsp3\fs3\k0}ha)");
}

TEST(NodeEffectsHighlights, GiveCharactersEachHighlightAndFuriganaAndLinesTheirOwn)
{
    EXPECT_EQ(textsOf(runOnKaraoke(highlightsGraph("char", true), R"({\k20}ab{\k30}#)")),
              (std::vector<std::string>{R"({\fsp1\fs2\k20}a)", R"({\fsp2\fs2\k30}a)", R"({\fsp1\fs2\k20}b)",
                                        R"({\fsp2\fs2\k30}b)"}));
    const auto furi = runOnKaraoke(highlightsGraph("furi", true), R"({\k20}空|そ{\k30}#|ら)");
    ASSERT_EQ(furi.size(), 2u);
    EXPECT_NE(furi[0].find(R"(0:00:01.00,0:00:01.20,Romaji-furigana,,0,0,0,fx g,{\fsp1\fs1\k20}そ)"),
              std::string::npos)
        << furi[0];
    EXPECT_NE(furi[1].find(R"(0:00:01.20,0:00:01.50,Romaji-furigana,,0,0,0,fx g,{\fsp1\fs1\k30}ら)"),
              std::string::npos)
        << furi[1];
    const auto line = runOnKaraoke(highlightsGraph("line", true), R"({\k20}ha{\k30}#)");
    ASSERT_EQ(line.size(), 1u);
    EXPECT_NE(line[0].find(R"(0:00:01.00,0:00:04.00,Romaji,,0,0,0,fx g,{\fsp1\fs1\k300}ha)"), std::string::npos)
        << line[0];
}

TEST(NodeEffectsInlineFx, KeepsTheUnitsWhoseInlineFxIsTheName)
{
    constexpr std::string_view karaoke = R"({\k10}a{\k10\-paint}b{\k10}c{\k10\-cloud}d{\k10}e)";
    EXPECT_EQ(textsOf(runOnKaraoke(inlineFxGraph("syl", "paint"), karaoke)), (std::vector<std::string>{"b", "c"}));
    EXPECT_EQ(textsOf(runOnKaraoke(inlineFxGraph("syl", "cloud"), karaoke)), (std::vector<std::string>{"d", "e"}));
    EXPECT_EQ(textsOf(runOnKaraoke(inlineFxGraph("syl", ""), karaoke)), (std::vector<std::string>{"a"}));
    EXPECT_TRUE(runOnKaraoke(inlineFxGraph("syl", "pain"), karaoke).empty());
    EXPECT_EQ(textsOf(runOnKaraoke(inlineFxGraph("char", "paint"), R"({\k10}ab{\k10\-paint}cd)")),
              (std::vector<std::string>{"c", "d"}));
    EXPECT_EQ(textsOf(runOnKaraoke(inlineFxGraph("furi", "paint"), R"({\k10}空|そ{\k10\-paint}見|み)")),
              (std::vector<std::string>{"み"}));
    EXPECT_EQ(runOnKaraoke(inlineFxGraph("line", ""), karaoke).size(), 1u);
    EXPECT_TRUE(runOnKaraoke(inlineFxGraph("line", "paint"), karaoke).empty());
}

std::string_view exampleGraph(std::string_view example)
{
    for (const NodeGraphPreset &p : nodeGraphPresets())
        if (p.example == example)
            return p.graph;
    ADD_FAILURE() << "no example " << example;
    return {};
}

TEST(NodeEffectExport, HighlightsBecomeMultiAndInlineFxBecomesFx)
{
    const auto flashes = exportTemplateLines(graphOf(exampleGraph("highlight-flashes")), u8"g");
    ASSERT_TRUE(flashes.has_value());
    EXPECT_EQ(*flashes, (std::vector<TemplateLine>{
                            {0, u8"template syl noblank multi",
                             u8"{\\an5\\pos($center,$middle)\\1a&HFF&\\t($start,$end,\\bord5\\3a&HFF&)}"}}));
    const auto paint = exportTemplateLines(graphOf(exampleGraph("paint-and-cloud")), u8"g");
    ASSERT_TRUE(paint.has_value()) << paint.error().front().node << " " << paint.error().front().message;
    std::vector<std::u8string> effects;
    for (const TemplateLine &l : *paint)
        effects.push_back(l.effect);
    EXPECT_EQ(effects, (std::vector<std::u8string>{u8"template syl noblank fx paint", u8"template syl noblank fx cloud",
                                                   u8"code syl", u8"template syl noblank fxgroup g_rest"}));
    EXPECT_EQ(paint->at(2).text, u8"fxgroup[\"g_rest\"] = (syl.inline_fx == \"\")");
}

TEST(NodeEffectExport, HighlightsAndInlineFxATemplateCantFollowBlockTheExport)
{
    const auto blockers = [](std::string_view unit, bool skipBlank, std::string_view between) {
        const auto lines = exportTemplateLines(graphOf(R"({"format": 1, "nodes": [
            {"id": "units", "type": "units.for_each", "fields": {"unit": ")" + std::string(unit) + R"("}},
            {"id": "hl", "type": "units.highlights", "fields": {"skip_blank": )" + (skipBlank ? "true" : "false") + R"(}},
            {"id": "x", "type": ")" + std::string(between) + R"("},
            {"id": "emit", "type": "output.emit"}],
          "wires": [{"from": "units.units", "to": "x.units"}, {"from": "x.units", "to": "hl.units"},
                    {"from": "hl.units", "to": "emit.units"}]})"),
                                               u8"g");
        std::vector<std::string> out;
        if (!lines)
            for (const NodeEffectProblem &p : lines.error())
                out.push_back(p.node);
        return out;
    };
    EXPECT_EQ(blockers("syl", true, "units.inline_fx"), std::vector<std::string>{});
    EXPECT_EQ(blockers("line", true, "units.inline_fx"), std::vector<std::string>{"hl"});
    EXPECT_EQ(blockers("syl", false, "units.inline_fx"), std::vector<std::string>{"hl"});
    EXPECT_EQ(blockers("syl", true, "units.repeat"), std::vector<std::string>{"x"});
    EXPECT_EQ(blockers("syl", true, "units.highlights"), std::vector<std::string>{"x"});
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

namespace {

std::vector<std::string> blockersOf(std::string_view json)
{
    std::vector<std::string> out;
    const auto lines = exportTemplateLines(graphOf(json), u8"g");
    EXPECT_FALSE(lines.has_value());
    if (!lines)
        for (const NodeEffectProblem &p : lines.error())
            out.push_back(p.node);
    return out;
}

}

TEST(NodeEffectExport, ClassicFillBecomesOnePreLineAndLineTemplate)
{
    const auto lines = exportTemplateLines(graphOf(kClassicFill), u8"Classic fill");
    ASSERT_TRUE(lines.has_value());
    const std::vector<TemplateLine> expected{
        {0, u8"template pre-line classic_fill_emit", u8"!retime(\"line\", (-300), 300)!{\\fad(300,300)\\k30}"},
        {0, u8"template line classic_fill_emit", u8"{\\kf$kdur}"}};
    EXPECT_EQ(*lines, expected);
}

TEST(NodeEffectExport, SparklesLoopsAndDrawsWithoutText)
{
    const auto lines = exportTemplateLines(graphOf(kSparkles), u8"s");
    ASSERT_TRUE(lines.has_value());
    ASSERT_EQ(lines->size(), 2u);
    EXPECT_EQ((*lines)[1].layer, 1);
    EXPECT_EQ((*lines)[1].effect, u8"template syl noblank notext loop 8");
    EXPECT_TRUE((*lines)[1].text.ends_with(u8"\\p1}m 0 -5 l 5 0 0 5 -5 0"));
}

TEST(NodeEffectExport, NodesWithoutATemplateFormBlockTheExportByName)
{
    EXPECT_EQ(blockersOf(R"({"format": 1, "nodes": [
        {"id": "syl", "type": "units.for_each"},
        {"id": "dice", "type": "math.random", "fields": {"max": 10}},
        {"id": "blur", "type": "tags.tag", "fields": {"tag": "blur"}},
        {"id": "emit", "type": "output.emit"}],
      "wires": [{"from": "dice.value", "to": "blur.value"}, {"from": "blur.tags", "to": "emit.tags"},
                {"from": "syl.units", "to": "emit.units"}]})"),
              std::vector<std::string>{"dice"});
    EXPECT_EQ(blockersOf(R"({"format": 1, "nodes": [
        {"id": "syl", "type": "units.for_each"},
        {"id": "info", "type": "units.info"},
        {"id": "long", "type": "math.compare", "fields": {"op": "greater", "b": 300}},
        {"id": "later", "type": "math.calculate", "fields": {"op": "add", "b": 100}},
        {"id": "emit", "type": "output.emit"}],
      "wires": [{"from": "info.duration", "to": "long.a"}, {"from": "long.result", "to": "emit.when"},
                {"from": "info.start", "to": "later.a"}, {"from": "later.result", "to": "emit.start"},
                {"from": "syl.units", "to": "emit.units"}]})"),
              (std::vector<std::string>{"long", "emit"}));
    EXPECT_EQ(blockersOf(R"({"format": 1, "nodes": [
        {"id": "line", "type": "units.for_each", "fields": {"unit": "line", "skip_blank": false}},
        {"id": "time", "type": "time.retime", "fields": {"mode": "syl"}},
        {"id": "emit", "type": "output.emit"}],
      "wires": [{"from": "line.units", "to": "emit.units"}, {"from": "time.start", "to": "emit.start"},
                {"from": "time.end", "to": "emit.end"}]})"),
              std::vector<std::string>{"time"});
}

TEST(NodeEffectExport, StripsCopiesAndParticlesBlockTheExport)
{
    for (const std::string type : {"units.clip_gradient", "units.array", "units.particles"}) {
        SCOPED_TRACE(type);
        const auto lines = exportTemplateLines(graphOf(R"({"format": 1, "nodes": [
            {"id": "syl", "type": "units.for_each"},
            {"id": "many", "type": ")" + type + R"("},
            {"id": "emit", "type": "output.emit"}],
          "wires": [{"from": "syl.units", "to": "many.units"}, {"from": "many.units", "to": "emit.units"}]})"),
                                               u8"g");
        ASSERT_FALSE(lines.has_value());
        ASSERT_EQ(lines.error().size(), 1u);
        EXPECT_EQ(lines.error().front().node, "many");
        EXPECT_NE(lines.error().front().message.find("which"), std::string::npos);
    }
}

TEST(NodeEffectExport, ExportAddsTheTemplateLinesAfterTheGraphInOneStep)
{
    EditSession session(presetDocument(kPresets[1]));
    const std::string before = text(core::encodeAss(session.document()));
    const core::LineId graph = graphLineOf(session.document(), u8"bounce");
    ASSERT_TRUE(exportTemplateLines(session, graph));
    std::vector<const core::LineRecord *> added;
    bool after = false;
    for (const core::LineRecord *l : session.document().lines()) {
        if (after && l->effect.starts_with(u8"template "))
            added.push_back(l);
        after = after || l->id == graph;
    }
    ASSERT_EQ(added.size(), 4u);
    for (const core::LineRecord *l : added) {
        EXPECT_TRUE(l->comment);
        EXPECT_EQ(l->style, u8"Romaji");
        EXPECT_EQ(l->effect, u8"template syl noblank");
    }
    EXPECT_EQ(session.history().back().name, "Export as template lines");
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(text(core::encodeAss(session.document())), before);
}
