#include "hikari/application/node_graph_editing.h"

namespace hikari::application {

namespace {

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

constexpr NodeGraphPreset kPresets[] = {
    {"01-classic-fill.ass", kClassicFill},
    {"05-bounce.ass", kBounce},
    {"07-sparkles.ass", kSparkles},
    {"09-falling-letters.ass", kFallingLetters},
    {"10-palette.ass", kPalette},
};

}

std::span<const NodeGraphPreset> nodeGraphPresets()
{
    return kPresets;
}

}
