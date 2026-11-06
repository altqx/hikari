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
