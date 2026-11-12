#include "operations.h"

#include "hikari/application/node_graph_editing.h"
#include "hikari/core/ass_save.h"

#include <algorithm>

namespace hikari::api::detail {

namespace {

using application::DocumentGraph;
using application::GraphEnd;
using application::GraphNode;
using application::GraphWire;
using application::NodeGraph;

std::u8string wide(const std::string &s)
{
    return std::u8string(s.begin(), s.end());
}

std::optional<DocumentGraph> graphAtLine(const core::Document &document, std::int64_t line)
{
    for (DocumentGraph &g : application::documentGraphs(document))
        if (std::int64_t(g.line.value) == line)
            return std::move(g);
    return std::nullopt;
}

std::unexpected<Error> noGraph(std::int64_t line)
{
    return fail(errors::kNotFound, "No graph Line has id " + std::to_string(line) + ".", Json::object({{"line", Json(line)}}));
}

std::expected<NodeGraph, Error> graphParam(const Json &params)
{
    auto graph = application::graphFromJson(*params.find("graph"));
    if (!graph)
        return fail(errors::kInvalidParams, "The graph cannot be read: " + graph.error().message,
                    Json::object({{"problems", problemsJson({graph.error()})}}));
    return *graph;
}

Json graphResult(const NodeGraph &graph)
{
    const std::vector<application::GraphProblem> problems = application::validateGraph(graph);
    return Json::object({{"graph", application::graphJson(graph)},
                         {"valid", Json(problems.empty())},
                         {"problems", problemsJson(problems)}});
}

Result graphsList(const Context &context, const Json &params)
{
    auto t = target(context, params);
    if (!t)
        return std::unexpected(t.error());
    Json graphs = Json::array();
    for (const DocumentGraph &g : application::documentGraphs(t->session->document()))
        graphs.push(Json::object({{"line", Json(g.line.value)},
                                  {"name", Json(narrow(g.name))},
                                  {"style", Json(narrow(g.style))},
                                  {"valid", Json(g.graph && application::validateGraph(*g.graph).empty())}}));
    return Json::object({{"document", Json(t->id.value)}, {"revision", Json(t->session->revision())}, {"graphs", graphs}});
}

Result graphGet(const Context &context, const Json &params)
{
    auto t = target(context, params);
    if (!t)
        return std::unexpected(t.error());
    const std::int64_t line = params.find("line")->asInt();
    const auto g = graphAtLine(t->session->document(), line);
    if (!g)
        return noGraph(line);
    Json out = g->graph ? graphResult(*g->graph)
                        : Json::object({{"graph", Json()}, {"valid", Json(false)}, {"problems", problemsJson({g->graph.error()})}});
    out["line"] = Json(line);
    out["name"] = Json(narrow(g->name));
    out["style"] = Json(narrow(g->style));
    out["revision"] = Json(t->session->revision());
    return out;
}

std::optional<GraphEnd> endOf(const std::string &text)
{
    const auto dot = text.find('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 == text.size())
        return std::nullopt;
    return GraphEnd{text.substr(0, dot), text.substr(dot + 1)};
}

GraphNode *nodeNamed(NodeGraph &graph, std::string_view id)
{
    const auto it = std::ranges::find(graph.nodes, id, &GraphNode::id);
    return it == graph.nodes.end() ? nullptr : &*it;
}

std::string wireRefusalText(const application::RefusedWire &refused)
{
    switch (refused.reason) {
    case application::WireRefusal::UnknownNode: return "a wire end names no node";
    case application::WireRefusal::NotAnOutput: return "`from` is not an output of its node";
    case application::WireRefusal::NotAnInput: return "`to` is not an input of its node";
    case application::WireRefusal::SameNode: return "a wire cannot join a node to itself";
    case application::WireRefusal::Loop: return "the wire would make a loop";
    case application::WireRefusal::Types: break;
    }
    return "a " + std::string(application::portTypeName(refused.from)) + " output cannot feed a " +
           std::string(application::portTypeName(refused.to)) + " input";
}

struct StepFailure {
    std::string message;
};

std::expected<void, StepFailure> addStep(NodeGraph &graph, const Json &step, Json &added)
{
    const Json *type = step.find("type");
    if (!type)
        return std::unexpected(StepFailure{"add needs a type"});
    if (!application::nodeSpec(type->asString()))
        return std::unexpected(StepFailure{"no node type \"" + type->asString() + "\"; nodes.catalogue lists them"});
    GraphNode node;
    node.type = type->asString();
    if (const Json *id = step.find("node")) {
        if (!application::isNodeId(id->asString()))
            return std::unexpected(StepFailure{"a node id holds letters, digits, '_' and '-' only"});
        if (nodeNamed(graph, id->asString()))
            return std::unexpected(StepFailure{"the graph already has a node \"" + id->asString() + "\""});
        node.id = id->asString();
    } else {
        node.id = application::freeNodeId(graph, node.type);
    }
    if (const Json *fields = step.find("fields"))
        node.fields = *fields;
    added.push(Json(node.id));
    graph.nodes.push_back(std::move(node));
    return {};
}

std::expected<void, StepFailure> connectStep(NodeGraph &graph, const Json &step)
{
    const Json *from = step.find("from");
    const Json *to = step.find("to");
    if (!from || !to)
        return std::unexpected(StepFailure{"connect needs from and to, as \"node.port\""});
    const auto a = endOf(from->asString());
    const auto b = endOf(to->asString());
    if (!a || !b)
        return std::unexpected(StepFailure{"a wire end is \"node.port\""});
    const GraphWire wire{*a, *b};
    if (const auto refused = application::wireRefusal(graph, wire))
        return std::unexpected(StepFailure{wireRefusalText(*refused)});
    graph = application::withWire(std::move(graph), wire);
    return {};
}

std::expected<void, StepFailure> disconnectStep(NodeGraph &graph, const Json &step)
{
    const Json *to = step.find("to");
    const auto b = to ? endOf(to->asString()) : std::nullopt;
    if (!b)
        return std::unexpected(StepFailure{"disconnect needs to, as \"node.port\""});
    const auto removed = std::erase_if(graph.wires, [&](const GraphWire &w) { return w.to.node == b->node && w.to.port == b->port; });
    if (removed == 0)
        return std::unexpected(StepFailure{"no wire reaches " + to->asString()});
    return {};
}

std::expected<void, StepFailure> removeStep(NodeGraph &graph, const Json &step)
{
    const Json *id = step.find("node");
    if (!id)
        return std::unexpected(StepFailure{"remove needs a node"});
    if (!nodeNamed(graph, id->asString()))
        return std::unexpected(StepFailure{"no node \"" + id->asString() + "\""});
    graph = application::withoutNode(std::move(graph), id->asString());
    return {};
}

std::expected<void, StepFailure> setStep(NodeGraph &graph, const Json &step)
{
    const Json *id = step.find("node");
    const Json *field = step.find("field");
    if (!id || !field || !step.find("value"))
        return std::unexpected(StepFailure{"set needs a node, a field and a value"});
    GraphNode *node = nodeNamed(graph, id->asString());
    if (!node)
        return std::unexpected(StepFailure{"no node \"" + id->asString() + "\""});
    const application::NodeSpec *spec = application::nodeSpec(node->type);
    if (!spec || !application::fieldSpec(*spec, field->asString()))
        return std::unexpected(StepFailure{node->type + " has no field \"" + field->asString() + "\""});
    const Json &value = *step.find("value");
    if (!node->fields.isObject())
        node->fields = Json::object();
    if (value.isNull())
        std::erase_if(node->fields.asObject(), [&](const Json::Member &m) { return m.key == field->asString(); });
    else
        node->fields[field->asString()] = value;
    return {};
}

Result graphEdit(const Context &, const Json &params)
{
    auto graph = graphParam(params);
    if (!graph)
        return std::unexpected(graph.error());
    Json added = Json::array();
    const Json::Array &steps = params.find("steps")->asArray();
    for (std::size_t i = 0; i < steps.size(); ++i) {
        const Json &step = steps[i];
        const std::string &op = step.find("op")->asString();
        const auto done = op == "add"          ? addStep(*graph, step, added)
                          : op == "connect"    ? connectStep(*graph, step)
                          : op == "disconnect" ? disconnectStep(*graph, step)
                          : op == "remove"     ? removeStep(*graph, step)
                                               : setStep(*graph, step);
        if (!done)
            return fail(errors::kInvalidParams, "Step " + std::to_string(i) + " (" + op + "): " + done.error().message + ".",
                        Json::object({{"step", Json(std::int64_t(i))}}));
    }
    Json out = graphResult(*graph);
    out["added"] = std::move(added);
    return out;
}

Json effectProblemsJson(const std::vector<application::NodeEffectProblem> &problems)
{
    Json out = Json::array();
    for (const application::NodeEffectProblem &p : problems) {
        Json entry = Json::object();
        if (!p.node.empty())
            entry["node"] = Json(p.node);
        if (p.line)
            entry["line"] = Json(p.line->value);
        entry["message"] = Json(p.message);
        out.push(std::move(entry));
    }
    return out;
}

std::unexpected<Error> noEffects()
{
    return fail(errors::kUnavailable, "Node effects are not available.");
}

void restrictSources(application::NodeEffectOptions &options, const Json &params)
{
    if (const Json *sources = params.find("sources")) {
        std::set<core::LineId> lines;
        for (const Json &id : sources->asArray())
            lines.insert(core::LineId{std::uint64_t(id.asInt())});
        options.sources = std::move(lines);
    }
}

Result preview(const Context &context, const Json &params)
{
    if (!context.host.effects)
        return noEffects();
    auto t = target(context, params);
    if (!t)
        return std::unexpected(t.error());
    const core::Document &document = t->session->document();
    std::u8string name = u8"preview";
    std::u8string style;
    std::optional<NodeGraph> graph;
    if (const Json *line = params.find("line")) {
        auto g = graphAtLine(document, line->asInt());
        if (!g)
            return noGraph(line->asInt());
        name = g->name;
        style = g->style;
        if (g->graph)
            graph = std::move(*g->graph);
        else if (!params.find("graph"))
            return fail(errors::kRefused, "The graph Line cannot be read: " + g->graph.error().message,
                        Json::object({{"reason", Json("unreadable")}}));
    }
    if (params.find("graph")) {
        auto given = graphParam(params);
        if (!given)
            return std::unexpected(given.error());
        graph = std::move(*given);
    }
    if (!graph)
        return fail(errors::kInvalidParams, "Give a graph Line, a graph, or both.");
    if (const Json *s = params.find("style"))
        style = wide(s->asString());
    if (const Json *n = params.find("name"))
        name = wide(n->asString());
    if (style.empty() && !graph->allStyles)
        return fail(errors::kInvalidParams, "Give the Style whose karaoke Lines the graph runs on.");
    application::NodeEffectOptions options = context.host.effects->options();
    restrictSources(options, params);
    const application::NodeEffectRun run = application::runNodeEffect(document, name, style, *graph, options);
    const Json *limit = params.find("limit");
    const std::size_t shown = std::min(run.lines.size(), std::size_t(limit ? limit->asInt() : 20));
    Json lines = Json::array();
    for (std::size_t i = 0; i < shown; ++i)
        lines.push(Json(narrow(core::legacy::assLineText(run.lines[i]))));
    return Json::object({{"ran", Json(run.ran && run.ok())},
                         {"complete", Json(run.complete)},
                         {"sources", Json(std::int64_t(run.sources.size()))},
                         {"generated", Json(std::int64_t(run.lines.size()))},
                         {"lines", std::move(lines)},
                         {"problems", effectProblemsJson(run.problems)}});
}

struct Step {
    const Context &context;
    Target t;
    std::string name;
    std::optional<core::Document> before;
    application::ContentId previous;

    Step(const Context &c, Target target, std::string stepName)
        : context(c), t(target), name(std::move(stepName)), previous(t.session->contentId())
    {
        if (context.caller.reviewed && context.host.review)
            before = t.session->document();
    }

    void committed()
    {
        if (before)
            context.host.review->record(t.id, context.caller.id, context.caller.label, name, *before, previous, *t.session);
        context.host.documents->committed(t.id);
    }
};

std::expected<Target, Error> writableTarget(const Context &context, const Json &params)
{
    auto t = target(context, params);
    if (!t)
        return std::unexpected(t.error());
    if (!writable(context, *t))
        return unwritable(context, *t);
    return *t;
}

Result save(const Context &context, const Json &params)
{
    auto t = writableTarget(context, params);
    if (!t)
        return std::unexpected(t.error());
    auto graph = graphParam(params);
    if (!graph)
        return std::unexpected(graph.error());
    *graph = application::laidOutGraph(std::move(*graph));
    application::EditSession &session = *t->session;
    if (const Json *line = params.find("line")) {
        const auto g = graphAtLine(session.document(), line->asInt());
        if (!g)
            return noGraph(line->asInt());
        Step step(context, *t, "Edit graph");
        if (auto edited = application::editGraph(session, g->line, *graph); !edited)
            return refusal(edited.error(), session);
        step.committed();
        Json out = graphResult(*graph);
        out["line"] = Json(g->line.value);
        out["revision"] = Json(session.revision());
        return out;
    }
    const Json *name = params.find("name");
    const Json *style = params.find("style");
    if (!name || !style)
        return fail(errors::kInvalidParams, "A new graph needs a name and a Style; give line to replace a graph.");
    const std::u8string graphName = wide(name->asString());
    if (!application::isGraphName(graphName))
        return fail(errors::kInvalidParams, "\"" + name->asString() + "\" cannot name a graph.");
    for (const DocumentGraph &g : application::documentGraphs(session.document()))
        if (g.name == graphName)
            return fail(errors::kRefused, "The Document already has a graph \"" + name->asString() + "\".",
                        Json::object({{"reason", Json("name_taken")}, {"line", Json(g.line.value)}}));
    const std::string label = context.caller.label.empty() ? "New graph" : context.caller.label + ": New graph";
    Step step(context, *t, "New graph");
    const auto added = application::addGraph(session, graphName, wide(style->asString()), *graph, label);
    if (!added)
        return refusal(added.error(), session);
    step.committed();
    Json out = graphResult(*graph);
    out["line"] = Json(added->value);
    out["revision"] = Json(session.revision());
    return out;
}

Result apply(const Context &context, const Json &params)
{
    if (!context.host.effects)
        return noEffects();
    auto t = writableTarget(context, params);
    if (!t)
        return std::unexpected(t.error());
    application::EditSession &session = *t->session;
    const std::int64_t line = params.find("line")->asInt();
    const auto g = graphAtLine(session.document(), line);
    if (!g)
        return noGraph(line);
    application::NodeEffectOptions options = context.host.effects->options();
    std::optional<application::NodeEffectRun> owners;
    if (params.find("sources")) {
        auto all = application::runDocumentGraph(session.document(), g->line, options);
        if (all)
            owners = std::move(*all);
        restrictSources(options, params);
    }
    auto run = application::runDocumentGraph(session.document(), g->line, options);
    if (!run)
        return fail(errors::kRefused, "The graph Line cannot be read: " + run.error().message,
                    Json::object({{"reason", Json("unreadable")}}));
    if (!run->ran || !run->ok())
        return fail(errors::kRefused, "The graph doesn't run, so nothing was applied.",
                    Json::object({{"reason", Json("problems")}, {"problems", effectProblemsJson(run->problems)}}));
    Step step(context, *t, "Apply node effect");
    if (auto applied = application::applyNodeEffect(session, *run, owners ? &*owners : nullptr); !applied) {
        if (applied.error() == application::CommandRefusal::Invalid)
            return Json::object({{"revision", Json(session.revision())}, {"generated", Json(std::int64_t(run->lines.size()))},
                                 {"sources", Json(std::int64_t(run->applied.size()))}, {"changed", Json(false)}});
        return refusal(applied.error(), session);
    }
    step.committed();
    return Json::object({{"revision", Json(session.revision())},
                         {"generated", Json(std::int64_t(run->lines.size()))},
                         {"sources", Json(std::int64_t(run->applied.size()))},
                         {"changed", Json(true)}});
}

constexpr std::string_view kDocument = R"("document": {"$ref": "#/$defs/DocumentId", "description": "Defaults to the active Document."})";

constexpr std::string_view kProblems = R"({"type": "array", "items": {"type": "object", "properties": {
    "node": {"type": "string"}, "port": {"type": "string"}, "message": {"type": "string"}},
    "required": ["message"], "additionalProperties": false}})";

constexpr std::string_view kSources = R"("sources": {"type": "array", "minItems": 1, "maxItems": 100000,
    "items": {"$ref": "#/$defs/LineId"}, "description": "Only these karaoke Lines; all of them when omitted."})";

std::string fill(std::string_view text)
{
    std::string out(text);
    const auto put = [&](std::string_view key, std::string_view value) {
        for (std::size_t at = out.find(key); at != std::string::npos; at = out.find(key, at + value.size()))
            out.replace(at, key.size(), value);
    };
    put("@document", kDocument);
    put("@problems", kProblems);
    put("@sources", kSources);
    return out;
}

void add(Registry &registry, Access access, std::string name, std::string summary, std::string_view params,
         std::string_view result, SyncHandler handler)
{
    addOperation(registry, access, std::move(name), std::move(summary), fill(params), fill(result), now(std::move(handler)));
}

}

void addEffectsOperations(Registry &registry)
{
    add(registry, Access::Read, "effects.graphs", "The Document's node-effect graphs, one per graph Line.",
        R"({"type": "object", "properties": {@document}, "additionalProperties": false})",
        R"({"type": "object", "properties": {"document": {"$ref": "#/$defs/DocumentId"}, "revision": {"$ref": "#/$defs/Revision"},
            "graphs": {"type": "array", "items": {"type": "object", "properties": {"line": {"$ref": "#/$defs/LineId"},
                "name": {"type": "string"}, "style": {"type": "string", "description": "The Style whose karaoke Lines it runs on."},
                "valid": {"type": "boolean"}}, "required": ["line", "name", "style", "valid"], "additionalProperties": false}}},
            "required": ["document", "revision", "graphs"], "additionalProperties": false})",
        graphsList);
    add(registry, Access::Read, "effects.graph", "One graph Line's graph, with what keeps it from running.",
        R"({"type": "object", "properties": {@document, "line": {"$ref": "#/$defs/LineId"}}, "required": ["line"],
            "additionalProperties": false})",
        R"({"type": "object", "properties": {"line": {"$ref": "#/$defs/LineId"}, "name": {"type": "string"},
            "style": {"type": "string"}, "revision": {"$ref": "#/$defs/Revision"},
            "graph": {"type": ["object", "null"], "description": "Null when the Line's text is no graph."},
            "valid": {"type": "boolean"}, "problems": @problems},
            "required": ["line", "name", "style", "revision", "graph", "valid", "problems"], "additionalProperties": false})",
        graphGet);
    add(registry, Access::Read, "effects.edit",
        "Edits a graph in steps (add, connect, disconnect, remove, set) and gives the new graph with its problems; "
        "nothing in the Document changes.",
        R"({"type": "object", "properties": {
            "graph": {"type": "object", "description": "A graph as effects.graph gives it; {\"format\": 1, \"nodes\": [], \"wires\": []} starts an empty one."},
            "steps": {"type": "array", "minItems": 1, "maxItems": 500, "description": "Applied in order; a refused step changes nothing.",
                "items": {"type": "object", "properties": {
                    "op": {"enum": ["add", "connect", "disconnect", "remove", "set"]},
                    "type": {"type": "string", "description": "add: a node type from nodes.catalogue."},
                    "node": {"type": "string", "description": "add: the new node's id, made from its type when omitted; remove, set: the node."},
                    "fields": {"type": "object", "description": "add: the node's fields."},
                    "from": {"type": "string", "description": "connect: an output, \"node.port\"."},
                    "to": {"type": "string", "description": "connect, disconnect: an input, \"node.port\"; connect replaces a wire already there."},
                    "field": {"type": "string", "description": "set: the field."},
                    "value": {"description": "set: the field's value; null restores its default."}},
                    "required": ["op"], "additionalProperties": false}}},
            "required": ["graph", "steps"], "additionalProperties": false})",
        R"({"type": "object", "properties": {"graph": {"type": "object"},
            "added": {"type": "array", "items": {"type": "string"}, "description": "The ids of the nodes added, in step order."},
            "valid": {"type": "boolean"}, "problems": @problems},
            "required": ["graph", "added", "valid", "problems"], "additionalProperties": false})",
        graphEdit);
    add(registry, Access::Read, "effects.preview",
        "Runs a graph on the Document's karaoke Lines and gives the Lines it would write; nothing is applied. "
        "Lua nodes don't run.",
        R"({"type": "object", "properties": {@document,
            "line": {"$ref": "#/$defs/LineId", "description": "A graph Line: its graph, name and Style."},
            "graph": {"type": "object", "description": "A graph to run in place of the Line's."},
            "style": {"type": "string", "description": "The Style whose karaoke Lines it runs on; needed without line."},
            "name": {"type": "string", "description": "The graph's name, which the Lines' Effect \"fx <name>\" carries."},
            @sources,
            "limit": {"type": "integer", "minimum": 0, "maximum": 500, "description": "How many generated Lines to give; default 20."}},
            "additionalProperties": false})",
        R"({"type": "object", "properties": {
            "ran": {"type": "boolean", "description": "False when a problem stopped the run; applying it would change nothing."},
            "complete": {"type": "boolean"},
            "sources": {"type": "integer", "description": "How many karaoke Lines ran."},
            "generated": {"type": "integer", "description": "How many Lines the run made."},
            "lines": {"type": "array", "items": {"type": "string"}, "description": "The first generated Lines, as ASS Dialogue lines."},
            "problems": {"type": "array", "items": {"type": "object", "properties": {"node": {"type": "string"},
                "line": {"$ref": "#/$defs/LineId"}, "message": {"type": "string"}}, "required": ["message"],
                "additionalProperties": false}}},
            "required": ["ran", "complete", "sources", "generated", "lines", "problems"], "additionalProperties": false})",
        preview);
    add(registry, Access::Write, "effects.save",
        "Writes a graph to the Document as one undo step: into a new graph Line, or in place of a graph Line's.",
        R"({"type": "object", "properties": {@document,
            "graph": {"type": "object"},
            "line": {"$ref": "#/$defs/LineId", "description": "The graph Line to replace; omit it to add a graph."},
            "name": {"type": "string", "minLength": 1, "maxLength": 200, "description": "A new graph's name."},
            "style": {"type": "string", "minLength": 1, "description": "A new graph's Style: whose karaoke Lines it runs on."}},
            "required": ["graph"], "additionalProperties": false})",
        R"({"type": "object", "properties": {"line": {"$ref": "#/$defs/LineId"}, "revision": {"$ref": "#/$defs/Revision"},
            "graph": {"type": "object"}, "valid": {"type": "boolean"}, "problems": @problems},
            "required": ["line", "revision", "graph", "valid", "problems"], "additionalProperties": false})",
        save);
    add(registry, Access::Write, "effects.apply",
        "Runs a graph Line and writes its Lines as one undo step, replacing those it wrote before; the karaoke Lines "
        "it ran on become Comments. Lua nodes don't run, so a graph with one applies from the node-effect editor.",
        R"({"type": "object", "properties": {@document, "line": {"$ref": "#/$defs/LineId", "description": "The graph Line."},
            @sources}, "required": ["line"], "additionalProperties": false})",
        R"({"type": "object", "properties": {"revision": {"$ref": "#/$defs/Revision"},
            "generated": {"type": "integer"}, "sources": {"type": "integer", "description": "How many karaoke Lines got Lines."},
            "changed": {"type": "boolean"}}, "required": ["revision", "generated", "sources", "changed"],
            "additionalProperties": false})",
        apply);
}

}
