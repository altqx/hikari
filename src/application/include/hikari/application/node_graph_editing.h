#pragma once

#include "hikari/application/edit_session.h"
#include "hikari/application/node_graph.h"
#include "hikari/core/document.h"

#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace hikari::application {

enum class WireRefusal { UnknownNode, NotAnOutput, NotAnInput, SameNode, Types, Loop };

struct RefusedWire {
    WireRefusal reason;
    PortType from = PortType::Any, to = PortType::Any; // set for WireRefusal::Types
};

// Why the canvas refuses the wire, or nothing when it may be drawn. A wire into an input that
// already has one replaces it, so that is no refusal.
std::optional<RefusedWire> wireRefusal(const NodeGraph &graph, const GraphWire &wire);

// The graph with `wire` in place of whatever fed its input.
NodeGraph withWire(NodeGraph graph, const GraphWire &wire);

// The graph without the node and every wire to or from it.
NodeGraph withoutNode(NodeGraph graph, std::string_view node);

// The graph with node `from` renamed, its wires following; unchanged when `to` is taken or not an id.
std::optional<NodeGraph> withNodeRenamed(NodeGraph graph, std::string_view from, std::string_view to);

// A node id not yet in the graph, from the type's last part: "random", "random2", ...
std::string freeNodeId(const NodeGraph &graph, std::string_view type);

// The graph inside the groups `path` names, outermost first; the graph itself for an empty path.
std::optional<NodeGraph> graphAt(const NodeGraph &root, std::span<const std::string> path);
// `root` with the graph inside the groups `path` names replaced by `inner`.
std::optional<NodeGraph> withGraphAt(NodeGraph root, std::span<const std::string> path, const NodeGraph &inner);

// The graph with the `nodes` moved into a new group `groupId`. Each wire into or out of them
// becomes a port of the group, through its Group input or Group output, so the graph gives what
// it gave before. Nothing when no node is named or `groupId` is taken.
std::optional<NodeGraph> groupedNodes(const NodeGraph &graph, std::span<const std::string> nodes, std::string_view groupId);

// Whether the canvas hides the node's unwired outputs until asked.
bool foldsUnwiredOutputs(const GraphNode &node);

// Places every node without a canvas position: in columns by how far it is from the graph's
// inputs, top to bottom in graph order. Nodes with a position keep it.
NodeGraph laidOutGraph(NodeGraph graph);

// The KE3 karaoke presets that exist as graphs; `templateFile` names the preset's template file.
struct NodeGraphPreset {
    std::string_view templateFile;
    std::string_view graph;
};
std::span<const NodeGraphPreset> nodeGraphPresets();

// A graph name the Document's graphs don't use, from `base`: "base", "base 2", ...
std::u8string freeGraphName(const core::Document &document, std::u8string_view base);

// Each is one undo step on the graph Line, and refuses a name isGraphName refuses or another
// graph has.
std::expected<core::LineId, CommandRefusal> addGraph(EditSession &session, std::u8string_view name,
                                                     std::u8string_view style, const NodeGraph &graph,
                                                     std::string commandName = "New graph");
std::expected<void, CommandRefusal> editGraph(EditSession &session, core::LineId graphLine, const NodeGraph &graph);
std::expected<void, CommandRefusal> setGraphStyle(EditSession &session, core::LineId graphLine, std::u8string_view style);
// The graph's generated Lines take the new name too, so the next Apply still replaces them.
std::expected<void, CommandRefusal> renameGraph(EditSession &session, core::LineId graphLine, std::u8string_view name);
// Removes the graph Line and the Lines it generated.
std::expected<void, CommandRefusal> deleteGraph(EditSession &session, core::LineId graphLine);

}
