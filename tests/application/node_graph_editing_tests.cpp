#include "hikari/application/edit_session.h"
#include "hikari/application/node_graph.h"
#include "hikari/application/node_graph_editing.h"
#include "hikari/core/ass_load.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

using namespace hikari;
using namespace hikari::application;

namespace {

NodeGraph graphOf(std::string_view text)
{
    auto graph = parseGraph(text);
    EXPECT_TRUE(graph) << (graph ? "" : graph.error().node + " " + graph.error().message);
    return graph ? *graph : NodeGraph{};
}

NodeGraph palette()
{
    for (const NodeGraphPreset &p : nodeGraphPresets())
        if (p.templateFile == "10-palette.ass")
            return graphOf(p.graph);
    ADD_FAILURE() << "no palette preset";
    return {};
}

core::Document load(std::string_view text)
{
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return core::loadAss(bytes).document;
}

constexpr std::string_view kFile = R"([Script Info]
ScriptType: v4.00+

[V4+ Styles]
Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding
Style: Romaji,Arial,40,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,0,8,10,10,10,1

[Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
Dialogue: 0,0:00:01.00,0:00:03.00,Romaji,,0,0,0,,{\k50}ka{\k50}ra
Dialogue: 0,0:00:01.00,0:00:03.00,Romaji,,0,0,0,fx old,made
Dialogue: 0,0:00:01.00,0:00:03.00,Romaji,,0,0,0,fx other,kept
)";

std::vector<std::u8string> effects(const core::Document &doc)
{
    std::vector<std::u8string> out;
    for (const core::LineRecord *line : doc.lines())
        out.push_back(line->effect);
    return out;
}

const DocumentGraph *graphNamed(const core::Document &doc, std::u8string_view name)
{
    static std::vector<DocumentGraph> graphs;
    graphs = documentGraphs(doc);
    for (const DocumentGraph &g : graphs)
        if (g.name == name)
            return &g;
    return nullptr;
}

}

TEST(NodeGraphEditing, TheCanvasRefusesWiresTheValidatorWouldRefuse)
{
    const NodeGraph graph = palette();
    const auto types = wireRefusal(graph, {{"palette", "value"}, {"place", "b"}});
    ASSERT_TRUE(types);
    EXPECT_EQ(types->reason, WireRefusal::Types);
    EXPECT_EQ(types->from, PortType::Colour);
    EXPECT_EQ(types->to, PortType::Number);
    EXPECT_EQ(wireRefusal(graph, {{"fill", "tags"}, {"fade", "before"}})->reason, WireRefusal::Loop);
    EXPECT_EQ(wireRefusal(graph, {{"k", "tags"}, {"k", "before"}})->reason, WireRefusal::SameNode);
    EXPECT_EQ(wireRefusal(graph, {{"k", "nothing"}, {"emit", "tags"}})->reason, WireRefusal::NotAnOutput);
    EXPECT_EQ(wireRefusal(graph, {{"k", "tags"}, {"emit", "nothing"}})->reason, WireRefusal::NotAnInput);
    EXPECT_EQ(wireRefusal(graph, {{"gone", "tags"}, {"emit", "tags"}})->reason, WireRefusal::UnknownNode);
}

TEST(NodeGraphEditing, AWireIntoAWiredInputReplacesItsWire)
{
    const NodeGraph graph = palette();
    const GraphWire wire{{"fade", "tags"}, {"emit", "tags"}};
    EXPECT_FALSE(wireRefusal(graph, wire));
    const NodeGraph wired = withWire(graph, wire);
    EXPECT_EQ(wired.wires.size(), graph.wires.size());
    EXPECT_TRUE(std::ranges::any_of(wired.wires, [](const GraphWire &w) {
        return w.from.node == "fade" && w.to.node == "emit" && w.to.port == "tags";
    }));
    EXPECT_FALSE(std::ranges::any_of(wired.wires, [](const GraphWire &w) { return w.from.node == "k" && w.to.node == "emit"; }));
}

TEST(NodeGraphEditing, RemovingOrRenamingANodeTakesItsWiresAlong)
{
    const NodeGraph graph = palette();
    const NodeGraph without = withoutNode(graph, "k");
    EXPECT_EQ(without.nodes.size(), graph.nodes.size() - 1);
    EXPECT_EQ(without.wires.size(), graph.wires.size() - 2);
    const auto renamed = withNodeRenamed(graph, "k", "karaoke_tag");
    ASSERT_TRUE(renamed);
    EXPECT_TRUE(validateGraph(*renamed).empty());
    EXPECT_FALSE(withNodeRenamed(graph, "k", "emit"));
    EXPECT_FALSE(withNodeRenamed(graph, "k", "has space"));
}

TEST(NodeGraphEditing, NewNodesGetAFreeIdFromTheirType)
{
    NodeGraph graph;
    EXPECT_EQ(freeNodeId(graph, "math.random"), "random");
    graph.nodes.push_back({"random", "math.random"});
    graph.nodes.push_back({"random2", "math.random"});
    EXPECT_EQ(freeNodeId(graph, "math.random"), "random3");
}

TEST(NodeGraphEditing, EveryPresetIsValidAndLaysOutLeftToRight)
{
    ASSERT_EQ(nodeGraphPresets().size(), 18u);
    for (const NodeGraphPreset &p : nodeGraphPresets()) {
        const NodeGraph graph = laidOutGraph(graphOf(p.graph));
        EXPECT_TRUE(validateGraph(graph).empty()) << p.templateFile;
        std::map<std::string, std::pair<double, double>> at;
        for (const GraphNode &n : graph.nodes) {
            ASSERT_TRUE(n.at) << p.templateFile << " " << n.id;
            at[n.id] = *n.at;
        }
        for (const GraphWire &w : graph.wires)
            EXPECT_LT(at[w.from.node].first, at[w.to.node].first) << p.templateFile << " " << w.from.node << " " << w.to.node;
    }
}

TEST(NodeGraphEditing, LayoutKeepsThePositionsTheUserGave)
{
    NodeGraph graph = palette();
    graph.nodes.front().at = std::pair(900.0, 700.0);
    EXPECT_EQ(laidOutGraph(graph).nodes.front().at, std::pair(900.0, 700.0));
}

TEST(NodeGraphEditing, LayoutLeavesNoRoomForFoldedOutputs)
{
    NodeGraph graph;
    graph.nodes.push_back({"info", "units.info"});
    graph.nodes.push_back({"place", "values.number"});
    ASSERT_TRUE(foldsUnwiredOutputs(graph.nodes.front()));
    const NodeGraph laid = laidOutGraph(graph);
    EXPECT_LT(laid.nodes.back().at->second - laid.nodes.front().at->second, 150.0);
}

TEST(NodeGraphEditing, GraphLinesChangeOneUndoStepAtATime)
{
    EditSession session(load(kFile));
    EXPECT_EQ(freeGraphName(session.document(), u8"Colour per line"), u8"Colour per line");
    const auto added = addGraph(session, u8"old", u8"Romaji", palette());
    ASSERT_TRUE(added);
    EXPECT_EQ(session.history().back().name, "New graph");
    EXPECT_EQ(freeGraphName(session.document(), u8"old"), u8"old 2");
    EXPECT_FALSE(addGraph(session, u8"old", u8"Romaji", palette()));
    EXPECT_FALSE(addGraph(session, u8"my karaoke", u8"Romaji", palette()));
    EXPECT_EQ(effects(session.document()).front(), u8"graph old") << "a first graph goes before every Line";

    NodeGraph seeded = palette();
    seeded.seed = 7;
    ASSERT_TRUE(editGraph(session, *added, seeded));
    EXPECT_EQ(session.history().back().name, "Edit graph");
    EXPECT_EQ(graphNamed(session.document(), u8"old")->graph->seed, 7);

    ASSERT_TRUE(setGraphStyle(session, *added, u8"Other"));
    EXPECT_EQ(graphNamed(session.document(), u8"old")->style, u8"Other");

    ASSERT_TRUE(renameGraph(session, *added, u8"new"));
    EXPECT_EQ(session.history().back().name, "Rename graph");
    EXPECT_EQ(effects(session.document()),
              (std::vector<std::u8string>{u8"graph new", u8"", u8"fx new", u8"fx other"}));

    const std::size_t steps = session.historySize();
    ASSERT_TRUE(deleteGraph(session, *added));
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(effects(session.document()), (std::vector<std::u8string>{u8"", u8"fx other"}));
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(effects(session.document()).size(), 4u);
}

TEST(NodeGraphEditing, AGroupsGraphIsEditedInPlace)
{
    const std::vector<std::string> chosen{"place", "palette"};
    const auto grouped = groupedNodes(palette(), chosen, "colour");
    ASSERT_TRUE(grouped);
    EXPECT_FALSE(groupedNodes(palette(), chosen, "emit")) << "the id is taken";
    const std::vector<std::string> path{"colour"};
    auto inner = graphAt(*grouped, path);
    ASSERT_TRUE(inner);
    EXPECT_EQ(inner->nodes.size(), 4u) << "the two nodes, the Group input and the Group output";
    for (GraphNode &n : inner->nodes)
        if (n.id == "place")
            n.fields["b"] = json::Json(2);
    const auto edited = withGraphAt(*grouped, path, *inner);
    ASSERT_TRUE(edited);
    EXPECT_TRUE(validateGraph(*edited).empty());
    EXPECT_EQ(nodeField(graphAt(*edited, path)->nodes.front(), "b").asNumber(), 2);
    const std::vector<std::string> nowhere{"emit"};
    EXPECT_FALSE(graphAt(*grouped, nowhere));
}
