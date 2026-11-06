#pragma once

#include "hikari/application/automation_services.h"
#include "hikari/application/edit_session.h"
#include "hikari/application/node_graph.h"
#include "hikari/application/resample.h"
#include "hikari/core/document.h"

#include <cstdint>
#include <expected>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

namespace hikari::application {

struct NodeColour {
    int r = 0, g = 0, b = 0;
    friend bool operator==(const NodeColour &, const NodeColour &) = default;
};
struct NodePoint {
    double x = 0, y = 0;
    friend bool operator==(const NodePoint &, const NodePoint &) = default;
};
// Override tags without braces, e.g. "\an5\pos(1,2)".
struct NodeTags {
    std::string text;
    friend bool operator==(const NodeTags &, const NodeTags &) = default;
};
struct NodeDrawing {
    std::string commands;
    int scale = 1;
    friend bool operator==(const NodeDrawing &, const NodeDrawing &) = default;
};
// Number, Time and Alpha are doubles.
using NodeValue = std::variant<double, bool, std::string, NodeColour, NodePoint, NodeTags, NodeDrawing>;

// Runs a Lua node's code in the automation helper, once per Line for all its rows.
class LuaNodePort {
public:
    virtual ~LuaNodePort() = default;
    struct Call {
        std::string node;
        std::string code;
        std::vector<PortSpec> inputs, outputs;
        std::vector<std::vector<NodeValue>> rows; // one value per input, per row
    };
    // One value per output, per row; or what went wrong.
    virtual std::expected<std::vector<std::vector<NodeValue>>, std::string> run(const Call &call) = 0;
};

// Generated Lines of one Emit for one karaoke Line, kept across runs while neither changes.
class NodeEffectCache {
public:
    NodeEffectCache();
    ~NodeEffectCache();
    NodeEffectCache(const NodeEffectCache &) = delete;
    NodeEffectCache &operator=(const NodeEffectCache &) = delete;
    void clear();
    std::size_t size() const;

    struct Store;
    Store &store() { return *m_store; }

private:
    std::unique_ptr<Store> m_store;
};

// What each node's outputs gave for the first unit they were read for, on the first karaoke Line
// run, keyed by node id ("group/inner" inside a group) and then port.
struct NodeValueTrace {
    std::map<std::string, std::map<std::string, NodeValue>> values;
};

struct NodeEffectOptions {
    TextMeasurePort *measure = nullptr;
    // The video's size: karaskel corrects widths when its aspect differs from the script's.
    std::optional<Resolution> video;
    // The user trusts the Document's Lua; without it a graph with a Lua node writes nothing.
    bool trustLua = false;
    LuaNodePort *lua = nullptr;
    // Only these karaoke Lines; all of them when unset.
    std::optional<std::set<core::LineId>> sources;
    NodeEffectCache *cache = nullptr;
    unsigned threads = 0; // 0: one per hardware thread
    // Filled by a run that evaluates; cached Lines are not evaluated, so pass no cache with it.
    NodeValueTrace *trace = nullptr;
};

struct NodeEffectProblem {
    std::string node; // empty: the graph as a whole
    std::optional<core::LineId> line;
    std::string message;
    friend bool operator==(const NodeEffectProblem &, const NodeEffectProblem &) = default;
};

struct EmitReport {
    std::string node;
    std::size_t generated = 0;
    double seconds = 0;
};

struct NodeEffectRun {
    std::u8string name;
    bool ran = false; // false: invalid, or Lua not trusted; applying it changes nothing
    bool complete = true; // every karaoke Line was run, not only some
    // The karaoke Lines run, in Document order, and which of them got Lines.
    std::vector<core::LineId> sources;
    std::set<core::LineId> applied;
    // In the order they go into the Document; generatedFrom names the source.
    std::vector<core::LineRecord> lines;
    // Furigana Styles the Lines use that the Document lacks, as fields, Name first.
    std::vector<std::vector<std::u8string>> styles;
    std::vector<NodeEffectProblem> problems;
    std::vector<EmitReport> emits;
    double seconds = 0;
    bool ok() const { return problems.empty(); }
};

// The karaoke Lines (the templater's: a Dialogue without Effect, or any Line whose Effect says
// karaoke) of `style`, or of every Style with allStyles. A graph that fails validation, or holds
// a Lua node the user does not trust, writes nothing and says why.
NodeEffectRun runNodeEffect(const core::Document &document, std::u8string_view name, std::u8string_view style,
                            const NodeGraph &graph, const NodeEffectOptions &options);

// Runs the Document's graph Line `graphLine`.
std::expected<NodeEffectRun, GraphProblem> runDocumentGraph(const core::Document &document, core::LineId graphLine,
                                                            const NodeEffectOptions &options);

// Replaces the graph's earlier Lines (all of them, or for a partial run those of its sources)
// with the run's, where the first of them stood or else at the end, and turns each source that
// got Lines into a Comment with Effect "karaoke". Lines of other graphs and of the templater stay.
// A file read back has no generatedFrom marks: `owners` (from a complete run) matches them.
bool applyNodeEffect(core::Document &document, const NodeEffectRun &run, const NodeEffectRun *owners = nullptr);

// As one undo step named "Apply node effect".
std::expected<void, CommandRefusal> applyNodeEffect(EditSession &session, const NodeEffectRun &run,
                                                    const NodeEffectRun *owners = nullptr);

}
