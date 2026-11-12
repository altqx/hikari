#include "hikari/application/node_catalogue.h"

#include <array>
#include <deque>
#include <map>
#include <mutex>
#include <utility>

namespace hikari::application {

using json::Json;

namespace {

constexpr std::array<std::pair<PortType, std::string_view>, 11> kPortTypeNames{{
    {PortType::Number, "Number"},
    {PortType::Text, "Text"},
    {PortType::Colour, "Colour"},
    {PortType::Alpha, "Alpha"},
    {PortType::Time, "Time"},
    {PortType::Point, "Point"},
    {PortType::Tags, "Tags"},
    {PortType::Drawing, "Drawing"},
    {PortType::Bool, "Bool"},
    {PortType::Units, "Units"},
    {PortType::Any, "Any"},
}};

bool numeric(PortType type)
{
    return type == PortType::Number || type == PortType::Time || type == PortType::Alpha;
}

PortSpec port(std::string name, PortType type)
{
    return {std::move(name), type};
}

FieldSpec field(std::string name, FieldKind kind, Json fallback, std::string summary)
{
    return {std::move(name), kind, std::move(fallback), std::move(summary), {}, std::nullopt, std::nullopt};
}

FieldSpec choice(std::string name, std::vector<std::string> choices, std::string summary)
{
    Json fallback(choices.front());
    return {std::move(name), FieldKind::Choice, std::move(fallback), std::move(summary), std::move(choices),
            std::nullopt, std::nullopt};
}

FieldSpec atLeast(FieldSpec spec, double minimum)
{
    spec.minimum = minimum;
    return spec;
}

const std::vector<std::string> kRetimeModes{"line",     "syl",       "presyl",  "postsyl", "preline",
                                            "postline", "start2syl", "syl2end", "sylpct",  "set"};

const std::vector<std::string> kTagNames{
    "an",   "pos",  "org",  "clip", "iclip", "1c",   "2c",   "3c",  "4c",   "1a",   "2a",  "3a",
    "4a",   "alpha", "bord", "xbord", "ybord", "shad", "xshad", "yshad", "be", "blur", "fs",   "fscx", "fscy",
    "fsp",  "frx",  "fry",  "frz",  "fax",  "fay",   "b",    "i",    "u",   "s",    "q",    "p",   "k",
    "kf",   "ko",   "K",    "fn",   "r",    "raw"};

std::vector<NodeSpec> makeCatalogue()
{
    using enum PortType;
    const auto domainPorts = [](std::vector<PortSpec> more) {
        std::vector<PortSpec> out{port("units", Units)};
        out.insert(out.end(), more.begin(), more.end());
        return out;
    };
    std::vector<NodeSpec> c;

    c.push_back({"units.for_each", "units",
                 "Each unit of every karaoke Line the graph applies to: the Line itself, its syllables, their "
                 "characters or the furigana. Blank units (no duration or only spaces) can be skipped.",
                 {},
                 {port("units", Units), port("index", Number), port("count", Number)},
                 {choice("unit", {"syl", "line", "char", "furi"}, "What one unit is."),
                  field("skip_blank", FieldKind::Bool, Json(true), "Leave out units with no duration or only spaces.")}});
    c.push_back({"units.repeat", "units", "Each unit `count` times; j runs from 1 to count.",
                 {port("units", Units), port("count", Number)},
                 {port("units", Units), port("j", Number), port("count", Number)},
                 {atLeast(field("count", FieldKind::Integer, Json(1), "How many times."), 0)}});
    c.push_back({"units.frames", "units",
                 "Each unit once per step of time from `start` to `end` (absolute ms); without them, the "
                 "unit's own span.",
                 {port("units", Units), port("start", Time), port("end", Time), port("step", Time)},
                 domainPorts({port("start", Time), port("end", Time), port("index", Number), port("count", Number),
                              port("progress", Number)}),
                 {field("start", FieldKind::Time, Json(), "The first step's start; null: the unit's start."),
                  field("end", FieldKind::Time, Json(), "The last step's end; null: the unit's end."),
                  atLeast(field("step", FieldKind::Time, Json(40), "One step, in ms."), 1)}});
    c.push_back({"units.clip_gradient", "units",
                 "Each unit once per strip of its box, side by side across or stacked down: the strip's \\clip "
                 "rectangle (whole pixels, the edges shared) and its colour along the gradient.",
                 {port("units", Units), port("strips", Number), port("padding", Number)},
                 domainPorts({port("index", Number), port("count", Number), port("progress", Number),
                              port("clip", Tags), port("colour", Colour), port("from", Point), port("to", Point)}),
                 {atLeast(field("strips", FieldKind::Integer, Json(8), "How many strips."), 1),
                  choice("direction", {"across", "down"}, "Strips side by side from left to right, or stacked from "
                                                          "top to bottom."),
                  field("colours", FieldKind::List, Json::array({Json("#FFFFFF"), Json("#000000")}),
                        "The gradient's stops, as #RRGGBB, at least one."),
                  field("padding", FieldKind::Number, Json(0), "Added to the box on every side, in pixels.")}});
    c.push_back({"units.array", "units",
                 "Each unit once per copy: on a grid centred on it, on a circle around it, or spread evenly along "
                 "a path drawn from its position. angle is in degrees, as \\frz takes it.",
                 {port("units", Units), port("path", Drawing), port("count", Number), port("radius", Number)},
                 domainPorts({port("index", Number), port("count", Number), port("offset", Point),
                              port("position", Point), port("angle", Number)}),
                 {choice("shape", {"grid", "circle", "path"}, "Where the copies go."),
                  atLeast(field("columns", FieldKind::Integer, Json(3), "Grid: copies in a row."), 1),
                  atLeast(field("rows", FieldKind::Integer, Json(1), "Grid: rows."), 1),
                  field("spacing", FieldKind::Point, Json::array({Json(40), Json(40)}),
                        "Grid: from one copy to the next, as [across, down]."),
                  atLeast(field("count", FieldKind::Integer, Json(8), "Circle and path: how many copies."), 1),
                  field("radius", FieldKind::Number, Json(50), "Circle: its radius."),
                  field("start_angle", FieldKind::Number, Json(90), "Circle: the first copy's angle, in degrees "
                                                                    "counter-clockwise from the right.")}});
    c.push_back({"units.particles", "units",
                 "Each unit once per particle, spread at random over a rectangle or an ellipse centred on it. The "
                 "same graph seed, node seed and Line give the same particles on every run and platform.",
                 {port("units", Units), port("count", Number), port("width", Number), port("height", Number)},
                 domainPorts({port("index", Number), port("count", Number), port("offset", Point),
                              port("position", Point), port("random", Number)}),
                 {atLeast(field("count", FieldKind::Integer, Json(20), "How many particles."), 0),
                  choice("area", {"rectangle", "ellipse"}, "The area they spread over."),
                  atLeast(field("width", FieldKind::Number, Json(100), "The area's width."), 0),
                  atLeast(field("height", FieldKind::Number, Json(100), "The area's height."), 0),
                  field("seed", FieldKind::Integer, Json(0), "Added to the graph's seed.")}});
    c.push_back({"units.highlights", "units",
                 "Each syllable or character once per highlight, as the templater's multi gives it: the "
                 "highlight's start, end and duration. A furigana or Line unit has one, its own.",
                 {port("units", Units)},
                 domainPorts({port("index", Number), port("count", Number)}),
                 {field("skip_blank", FieldKind::Bool, Json(true), "Leave out highlights with no duration.")}});
    c.push_back({"units.inline_fx", "units",
                 "Only the units whose inline-fx ({\\-name}, kept until the next one) is `fx`, as the templater's "
                 "fx modifier keeps them. Characters have their syllable's; a Line unit has none.",
                 {port("units", Units)},
                 {port("units", Units)},
                 {field("fx", FieldKind::Text, Json(""), "The inline-fx name; empty: units without one.")}});
    c.push_back({"units.info", "units",
                 "What the karaoke templater's $variables give for the current unit and its Line, rounded as the "
                 "templater rounds them. Times of a syllable are from its Line's start. line_alignment is the "
                 "Line's first \\an, or its Style's alignment.",
                 {},
                 {port("text", Text),          port("i", Number),           port("n", Number),
                  port("start", Time),         port("end", Time),           port("duration", Time),
                  port("kdur", Number),        port("mid", Time),           port("left", Number),
                  port("center", Number),      port("right", Number),       port("width", Number),
                  port("top", Number),         port("middle", Number),      port("bottom", Number),
                  port("height", Number),      port("x", Number),           port("y", Number),
                  port("position", Point),     port("layer", Number),       port("line_index", Number),
                  port("line_start", Time),    port("line_end", Time),      port("line_duration", Time),
                  port("line_left", Number),   port("line_center", Number), port("line_right", Number),
                  port("line_width", Number),  port("line_top", Number),    port("line_middle", Number),
                  port("line_bottom", Number), port("line_height", Number), port("style_fontsize", Number),
                  port("style_scale_x", Number), port("style_scale_y", Number), port("style_spacing", Number),
                  port("style_angle", Number), port("style_outline", Number), port("style_shadow", Number),
                  port("style_alignment", Number), port("style_primary", Colour), port("style_secondary", Colour),
                  port("style_outline_colour", Colour), port("style_back", Colour), port("line_alignment", Number)},
                 {}});

    c.push_back({"values.number", "values", "A number.", {}, {port("value", Number)},
                 {field("value", FieldKind::Number, Json(0), "The number.")}});
    c.push_back({"values.text", "values", "A text.", {}, {port("value", Text)},
                 {field("value", FieldKind::Text, Json(""), "The text.")}});
    c.push_back({"values.colour", "values", "A colour.", {}, {port("value", Colour)},
                 {field("value", FieldKind::Colour, Json("#FFFFFF"), "The colour, as #RRGGBB.")}});
    c.push_back({"values.point", "values", "A point from its coordinates.", {port("x", Number), port("y", Number)},
                 {port("point", Point)},
                 {field("x", FieldKind::Number, Json(0), "Across."), field("y", FieldKind::Number, Json(0), "Down.")}});
    NodeSpec chooseSpec{"values.choose", "values",
                        "One of a list of values by its place (from 1); places past the end wrap around.",
                        {port("index", Number)},
                        {port("value", Any)},
                        {choice("type", {"number", "text", "colour", "alpha", "time"}, "The values' type."),
                         field("values", FieldKind::List, Json::array(), "The values, at least one."),
                         field("index", FieldKind::Number, Json(1), "The place.")}};
    chooseSpec.dynamicPorts = true;
    c.push_back(std::move(chooseSpec));

    c.push_back({"math.calculate", "math",
                 "Arithmetic in doubles, as Lua does it. modulo is a - floor(a/b)*b; round is floor(a + 0.5); "
                 "sine and cosine take a in degrees and round to millionths. One-input operations use a.",
                 {port("a", Number), port("b", Number)},
                 {port("result", Number)},
                 {choice("op", {"add", "subtract", "multiply", "divide", "modulo", "power", "minimum", "maximum",
                                "floor", "ceil", "round", "absolute", "negate", "sine", "cosine"},
                         "The operation."),
                  field("a", FieldKind::Number, Json(0), "The first operand."),
                  field("b", FieldKind::Number, Json(0), "The second operand.")}});
    c.push_back({"math.compare", "math", "Whether a stands in that relation to b.",
                 {port("a", Number), port("b", Number)},
                 {port("result", Bool)},
                 {choice("op", {"less", "less_or_equal", "equal", "not_equal", "greater_or_equal", "greater"},
                         "The relation."),
                  field("a", FieldKind::Number, Json(0), "The first operand."),
                  field("b", FieldKind::Number, Json(0), "The second operand.")}});
    c.push_back({"math.progress", "math",
                 "How far index is through count, from 0 at the first to 1 at the last, eased.",
                 {port("index", Number), port("count", Number)},
                 {port("progress", Number)},
                 {choice("ease", {"linear", "in", "out", "in_out"}, "The easing (quadratic)."),
                  field("index", FieldKind::Number, Json(1), "The place, from 1."),
                  field("count", FieldKind::Number, Json(1), "How many places.")}});
    c.push_back({"math.random", "math",
                 "A seeded random number from min to max, in multiples of step from min when step is above 0. The "
                 "same graph seed, node and scope give the same number on every run and platform.",
                 {port("min", Number), port("max", Number), port("step", Number)},
                 {port("value", Number)},
                 {field("min", FieldKind::Number, Json(0), "The smallest value."),
                  field("max", FieldKind::Number, Json(1), "The largest value."),
                  atLeast(field("step", FieldKind::Number, Json(0), "0: any value."), 0),
                  choice("scope", {"element", "unit", "line"},
                         "One number per output Line, per unit (the same across repeats and frames) or per Line."),
                  field("seed", FieldKind::Integer, Json(0), "Added to the graph's seed.")}});

    c.push_back({"time.retime", "time",
                 "A Line's start and end as the templater's retime(mode, start_offset, end_offset) sets them.",
                 {port("start_offset", Time), port("end_offset", Time)},
                 {port("start", Time), port("end", Time)},
                 {{"mode", FieldKind::Choice, Json("line"), "The templater's retime mode.", kRetimeModes, std::nullopt,
                   std::nullopt},
                  field("start_offset", FieldKind::Time, Json(0), "Added to the start (sylpct: a percentage)."),
                  field("end_offset", FieldKind::Time, Json(0), "Added to the end (sylpct: a percentage).")}});
    c.push_back({"time.stagger", "time",
                 "step times the place: index - 1 from the start, count - index from the end, or the distance from "
                 "the middle.",
                 {port("index", Number), port("count", Number), port("step", Time)},
                 {port("offset", Time)},
                 {choice("from", {"start", "end", "centre"}, "Where the first unit is."),
                  field("index", FieldKind::Number, Json(1), "The place, from 1."),
                  field("count", FieldKind::Number, Json(1), "How many places."),
                  field("step", FieldKind::Time, Json(0), "The time between places.")}});

    c.push_back({"tags.tag", "tags",
                 "One override tag after `before`. pos and org take point, clip and iclip take point and point2 "
                 "(or drawing), colours take colour, alpha tags take alpha, fn and r take text, raw writes text "
                 "as it is, and the others take value.",
                 {port("before", Tags), port("value", Number), port("point", Point), port("point2", Point),
                  port("colour", Colour), port("alpha", Alpha), port("drawing", Drawing), port("text", Text)},
                 {port("tags", Tags)},
                 {{"tag", FieldKind::Choice, Json("an"), "The tag, without its backslash.", kTagNames, std::nullopt,
                   std::nullopt},
                  field("value", FieldKind::Number, Json(0), "Its number."),
                  field("point", FieldKind::Point, Json::array({Json(0), Json(0)}), "Its point, as [x, y]."),
                  field("point2", FieldKind::Point, Json::array({Json(0), Json(0)}), "Its second point."),
                  field("colour", FieldKind::Colour, Json("#FFFFFF"), "Its colour, as #RRGGBB."),
                  field("alpha", FieldKind::Alpha, Json(0), "Its alpha, 0 (opaque) to 255."),
                  field("text", FieldKind::Text, Json(""), "Its text.")}});
    c.push_back({"tags.move", "tags", "\\move after `before`, with its times when timed.",
                 {port("before", Tags), port("from", Point), port("to", Point), port("t1", Time), port("t2", Time)},
                 {port("tags", Tags)},
                 {field("from", FieldKind::Point, Json::array({Json(0), Json(0)}), "Where it starts, as [x, y]."),
                  field("to", FieldKind::Point, Json::array({Json(0), Json(0)}), "Where it ends."),
                  field("timed", FieldKind::Bool, Json(false), "Write t1 and t2."),
                  field("t1", FieldKind::Time, Json(0), "When it starts, from the Line's start."),
                  field("t2", FieldKind::Time, Json(0), "When it ends.")}});
    c.push_back({"tags.fade", "tags", "\\fad after `before`.",
                 {port("before", Tags), port("fade_in", Time), port("fade_out", Time)},
                 {port("tags", Tags)},
                 {field("fade_in", FieldKind::Time, Json(0), "The fade in, in ms."),
                  field("fade_out", FieldKind::Time, Json(0), "The fade out, in ms.")}});
    c.push_back({"tags.transform", "tags",
                 "\\t over `tags` after `before`; t1 and t2 when timed, accel when it is not 1.",
                 {port("before", Tags), port("tags", Tags), port("t1", Time), port("t2", Time), port("accel", Number)},
                 {port("tags", Tags)},
                 {field("timed", FieldKind::Bool, Json(true), "Write t1 and t2."),
                  field("t1", FieldKind::Time, Json(0), "When it starts, from the Line's start."),
                  field("t2", FieldKind::Time, Json(0), "When it ends."),
                  atLeast(field("accel", FieldKind::Number, Json(1), "The acceleration."), 0)}});

    c.push_back({"colours.gradient", "colours", "The colour at progress (0 to 1) along evenly spaced stops.",
                 {port("progress", Number)},
                 {port("colour", Colour)},
                 {field("colours", FieldKind::List, Json::array({Json("#FFFFFF"), Json("#000000")}),
                        "The stops, as #RRGGBB, at least one."),
                  field("progress", FieldKind::Number, Json(0), "How far along.")}});
    c.push_back({"audio.peaks", "audio",
                 "The audio's level over the unit's span, or from start to end (absolute ms), read as the Hikari API's audio.peaks reads it. level is the loudest sample from 0 to 1; low and high are the lowest and highest from -1 to 1. Without audio it gives 0 and the graph shows a problem.",
                 {port("start", Time), port("end", Time)},
                 {port("level", Number), port("low", Number), port("high", Number)},
                 {field("start", FieldKind::Time, Json(), "The span's start; null: the unit's start."),
                  field("end", FieldKind::Time, Json(), "The span's end; null: the unit's end.")}});
    c.push_back({"drawings.shape", "drawings", "A vector drawing.", {}, {port("drawing", Drawing)},
                 {field("shape", FieldKind::Text, Json("m 0 0 l 10 0 10 10 0 10"), "The drawing commands."),
                  atLeast(field("scale", FieldKind::Integer, Json(1), "The \\p scale."), 1)}});
    c.push_back({"drawings.text", "drawings",
                 "The text (unwired, the unit's own) drawn in its Line's Style font, at \\p1 from the text's top "
                 "left, as Yutils' text_to_shape draws it. width and height are the text's.",
                 {port("text", Text)},
                 {port("drawing", Drawing), port("width", Number), port("height", Number)},
                 {}});
    c.push_back({"drawings.combine", "drawings",
                 "a and b combined, each filled as libass fills it: unite, subtract (b taken away from a), intersect, "
                 "or exclude (what only one of them covers). Each one's \\p scale is kept; the result is at \\p1.",
                 {port("a", Drawing), port("b", Drawing)},
                 {port("drawing", Drawing)},
                 {choice("op", {"unite", "subtract", "intersect", "exclude"}, "How to combine them.")}});

    c.push_back({"text.join", "text",
                 "The units of the current Line joined: each one's tags as an override block, then its text "
                 "(unwired, the unit's own).",
                 {port("units", Units), port("tags", Tags), port("text", Text)},
                 {port("text", Text)},
                 {choice("unit_text", {"stripped", "raw", "none"},
                         "The unit's text while no text is wired: without tags, with them, or none.")}});
    c.push_back({"output.emit", "output",
                 "One Line per unit: the source Line with these times, Layer, tags and text (or drawing), Effect "
                 "\"fx <graph name>\". Unwired times are the source Line's. Lines come in the templater's order: "
                 "Line units first, then by syllable, then by `order`.",
                 {port("units", Units), port("when", Bool), port("start", Time), port("end", Time),
                  port("layer", Number), port("tags", Tags), port("text", Text), port("drawing", Drawing)},
                 {},
                 {field("when", FieldKind::Bool, Json(true), "Write the Line."),
                  field("layer", FieldKind::Integer, Json(0), "Its layer."),
                  field("order", FieldKind::Integer, Json(0), "Its place among the emits for one unit; ties keep graph order."),
                  choice("unit_text", {"stripped", "raw", "none"},
                         "The unit's text while no text or drawing is wired: without tags, with them, or none.")}});

    NodeSpec group{"groups.group", "groups",
                   "A graph inside a node. Its inputs are its Group input's ports, its outputs its Group output's.",
                   {},
                   {},
                   {field("graph", FieldKind::Graph, Json::object({{"nodes", Json::array()}, {"wires", Json::array()}}),
                          "The inner nodes and wires.")}};
    group.dynamicPorts = true;
    c.push_back(std::move(group));
    NodeSpec groupInput{"groups.input", "groups", "Inside a group: its inputs, as outputs.", {}, {},
                        {field("ports", FieldKind::Ports, Json::array(), "The ports, each a name and a port type.")}};
    groupInput.dynamicPorts = true;
    c.push_back(std::move(groupInput));
    NodeSpec groupOutput{"groups.output", "groups", "Inside a group: its outputs, as inputs.", {}, {},
                         {field("ports", FieldKind::Ports, Json::array(), "The ports, each a name and a port type.")}};
    groupOutput.dynamicPorts = true;
    c.push_back(std::move(groupOutput));

    NodeSpec lua{"lua.code", "lua",
                 "Lua code run in the automation helper once per Line for all its units, only once the user trusts "
                 "the Document's Lua. Its inputs are globals and it sets its outputs as globals; it gets no "
                 "random numbers.",
                 {},
                 {},
                 {field("code", FieldKind::Text, Json(""), "The code."),
                  field("inputs", FieldKind::Ports, Json::array(), "Its inputs, each a name and a port type."),
                  field("outputs", FieldKind::Ports, Json::array(), "Its outputs.")}};
    lua.dynamicPorts = true;
    c.push_back(std::move(lua));
    return c;
}

Json fieldSchema(const FieldSpec &f)
{
    Json s = Json::object();
    switch (f.kind) {
    case FieldKind::Number:
    case FieldKind::Time:
        s["type"] = f.fallback.isNull() ? Json::array({Json("number"), Json("null")}) : Json("number");
        break;
    case FieldKind::Integer:
        s["type"] = Json("integer");
        break;
    case FieldKind::Alpha:
        s["type"] = Json("integer");
        s["minimum"] = Json(0);
        s["maximum"] = Json(255);
        break;
    case FieldKind::Text:
        s["type"] = Json("string");
        break;
    case FieldKind::Bool:
        s["type"] = Json("boolean");
        break;
    case FieldKind::Colour:
        s["type"] = Json("string");
        s["pattern"] = Json("^#[0-9A-Fa-f]{6}$");
        break;
    case FieldKind::Point:
        s["type"] = Json("array");
        s["items"] = Json::object({{"type", Json("number")}});
        s["minItems"] = Json(2);
        s["maxItems"] = Json(2);
        break;
    case FieldKind::Choice: {
        Json values = Json::array();
        for (const std::string &c : f.choices)
            values.push(Json(c));
        s["enum"] = std::move(values);
        break;
    }
    case FieldKind::List:
        s["type"] = Json("array");
        break;
    case FieldKind::Ports:
        s["type"] = Json("array");
        s["items"] = Json::object(
            {{"type", Json("object")},
             {"properties", Json::object({{"name", Json::object({{"type", Json("string")}})},
                                          {"type", Json::object({{"type", Json("string")}})}})},
             {"required", Json::array({Json("name"), Json("type")})},
             {"additionalProperties", Json(false)}});
        break;
    case FieldKind::Graph:
        s["type"] = Json("object");
        break;
    }
    if (f.minimum && f.kind != FieldKind::Alpha)
        s["minimum"] = Json(*f.minimum);
    if (f.maximum && f.kind != FieldKind::Alpha)
        s["maximum"] = Json(*f.maximum);
    s["description"] = Json(f.summary);
    if (!f.fallback.isNull())
        s["default"] = f.fallback;
    return s;
}

}

std::string_view portTypeName(PortType type)
{
    for (const auto &[t, name] : kPortTypeNames)
        if (t == type)
            return name;
    return {};
}

std::optional<PortType> portTypeNamed(std::string_view name)
{
    for (const auto &[t, n] : kPortTypeNames)
        if (n == name)
            return t;
    return std::nullopt;
}

bool portTypesConnect(PortType from, PortType to)
{
    return from == to || (numeric(from) && numeric(to));
}

const std::vector<NodeSpec> &nodeCatalogue()
{
    static const std::vector<NodeSpec> catalogue = makeCatalogue();
    return catalogue;
}

namespace {

struct PluginNodes {
    std::mutex mutex;
    // MUST KILL: replaced plugin nodes are never freed, so NodeSpec pointers a run holds stay valid.
    std::deque<PluginNode> kept;
    std::map<std::string, const PluginNode *, std::less<>> byType;
    std::map<std::string, std::string, std::less<>> unavailable;
};

PluginNodes &pluginRegistry()
{
    static PluginNodes registry;
    return registry;
}

constexpr std::string_view kPluginPrefix = "plugin:";

}

std::string pluginNodeType(std::string_view plugin, std::string_view node)
{
    return std::string(kPluginPrefix) + std::string(plugin) + "/" + std::string(node);
}

std::optional<std::string> pluginOfNodeType(std::string_view type)
{
    if (!type.starts_with(kPluginPrefix))
        return std::nullopt;
    type.remove_prefix(kPluginPrefix.size());
    const auto slash = type.find('/');
    if (slash == std::string_view::npos || slash == 0)
        return std::nullopt;
    return std::string(type.substr(0, slash));
}

void setPluginNodes(std::vector<PluginNode> nodes, std::map<std::string, std::string> unavailable)
{
    PluginNodes &r = pluginRegistry();
    const std::lock_guard lock(r.mutex);
    r.byType.clear();
    for (PluginNode &node : nodes) {
        const std::string type = node.spec.id;
        r.byType[type] = &r.kept.emplace_back(std::move(node));
    }
    r.unavailable.clear();
    for (auto &[id, name] : unavailable)
        r.unavailable.emplace(id, std::move(name));
}

std::vector<PluginNode> pluginNodes()
{
    PluginNodes &r = pluginRegistry();
    const std::lock_guard lock(r.mutex);
    std::vector<PluginNode> out;
    for (const auto &[type, node] : r.byType)
        out.push_back(*node);
    return out;
}

const PluginNode *pluginNode(std::string_view type)
{
    PluginNodes &r = pluginRegistry();
    const std::lock_guard lock(r.mutex);
    const auto it = r.byType.find(type);
    return it == r.byType.end() ? nullptr : it->second;
}

std::optional<std::string> pluginNodeProblem(std::string_view type)
{
    const std::optional<std::string> plugin = pluginOfNodeType(type);
    if (!plugin || pluginNode(type))
        return std::nullopt;
    PluginNodes &r = pluginRegistry();
    const std::lock_guard lock(r.mutex);
    if (const auto it = r.unavailable.find(*plugin); it != r.unavailable.end())
        return "comes from the plugin \"" + it->second + "\", which is turned off or not trusted";
    for (const auto &[t, node] : r.byType)
        if (node->plugin == *plugin)
            return "is not a node of the plugin \"" + node->pluginName + "\"";
    return "comes from the plugin \"" + *plugin + "\", which is not installed";
}

const NodeSpec *nodeSpec(std::string_view id)
{
    for (const NodeSpec &spec : nodeCatalogue())
        if (spec.id == id)
            return &spec;
    if (const PluginNode *node = pluginNode(id))
        return &node->spec;
    return nullptr;
}

const FieldSpec *fieldSpec(const NodeSpec &spec, std::string_view name)
{
    for (const FieldSpec &f : spec.fields)
        if (f.name == name)
            return &f;
    return nullptr;
}

Json nodeFieldsSchema(const NodeSpec &spec)
{
    Json properties = Json::object();
    for (const FieldSpec &f : spec.fields)
        properties[f.name] = fieldSchema(f);
    return Json::object(
        {{"type", Json("object")}, {"properties", std::move(properties)}, {"additionalProperties", Json(false)}});
}

}
