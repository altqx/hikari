#include "hikari/application/typesetting_graphs.h"

namespace hikari::application {

namespace {

constexpr std::string_view kShake = R"({"format": 2, "target": "lines", "nodes": [
    {"id": "line", "type": "units.for_each", "fields": {"unit": "line", "skip_blank": false}},
    {"id": "step", "type": "values.number", "fields": {"value": 42}},
    {"id": "frames", "type": "units.frames"},
    {"id": "amplitude", "type": "values.number", "fields": {"value": 6}},
    {"id": "least", "type": "math.calculate", "fields": {"op": "negate"}},
    {"id": "dx", "type": "math.random", "fields": {"seed": 1}},
    {"id": "dy", "type": "math.random", "fields": {"seed": 2}},
    {"id": "info", "type": "units.info"},
    {"id": "x", "type": "math.calculate", "fields": {"op": "add"}},
    {"id": "y", "type": "math.calculate", "fields": {"op": "add"}},
    {"id": "at", "type": "values.point"},
    {"id": "pos", "type": "tags.tag", "fields": {"tag": "pos"}},
    {"id": "emit", "type": "output.emit", "fields": {"unit_text": "raw"}}],
  "wires": [
    {"from": "line.units", "to": "frames.units"}, {"from": "step.value", "to": "frames.step"},
    {"from": "amplitude.value", "to": "least.a"},
    {"from": "least.result", "to": "dx.min"}, {"from": "amplitude.value", "to": "dx.max"},
    {"from": "least.result", "to": "dy.min"}, {"from": "amplitude.value", "to": "dy.max"},
    {"from": "info.x", "to": "x.a"}, {"from": "dx.value", "to": "x.b"},
    {"from": "info.y", "to": "y.a"}, {"from": "dy.value", "to": "y.b"},
    {"from": "x.result", "to": "at.x"}, {"from": "y.result", "to": "at.y"},
    {"from": "at.point", "to": "pos.point"},
    {"from": "frames.units", "to": "emit.units"}, {"from": "frames.start", "to": "emit.start"},
    {"from": "frames.end", "to": "emit.end"}, {"from": "info.layer", "to": "emit.layer"},
    {"from": "pos.tags", "to": "emit.tags"}]})";

constexpr std::string_view kTypewriter = R"({"format": 2, "target": "lines", "nodes": [
    {"id": "char", "type": "units.for_each", "fields": {"unit": "char"}},
    {"id": "delay", "type": "values.number", "fields": {"value": 60}},
    {"id": "stagger", "type": "time.stagger"},
    {"id": "info", "type": "units.info"},
    {"id": "start", "type": "math.calculate", "fields": {"op": "add"}},
    {"id": "an", "type": "tags.tag", "fields": {"tag": "an"}},
    {"id": "pos", "type": "tags.tag", "fields": {"tag": "pos"}},
    {"id": "emit", "type": "output.emit"}],
  "wires": [
    {"from": "char.index", "to": "stagger.index"}, {"from": "char.count", "to": "stagger.count"},
    {"from": "delay.value", "to": "stagger.step"},
    {"from": "info.line_start", "to": "start.a"}, {"from": "stagger.offset", "to": "start.b"},
    {"from": "info.line_alignment", "to": "an.value"},
    {"from": "an.tags", "to": "pos.before"}, {"from": "info.position", "to": "pos.point"},
    {"from": "char.units", "to": "emit.units"}, {"from": "start.result", "to": "emit.start"},
    {"from": "info.layer", "to": "emit.layer"}, {"from": "pos.tags", "to": "emit.tags"}]})";

constexpr std::string_view kFadeByLetter = R"({"format": 2, "target": "lines", "nodes": [
    {"id": "char", "type": "units.for_each", "fields": {"unit": "char"}},
    {"id": "delay", "type": "values.number", "fields": {"value": 40}},
    {"id": "fade", "type": "values.number", "fields": {"value": 200}},
    {"id": "stagger", "type": "time.stagger"},
    {"id": "until", "type": "math.calculate", "fields": {"op": "add"}},
    {"id": "info", "type": "units.info"},
    {"id": "an", "type": "tags.tag", "fields": {"tag": "an"}},
    {"id": "pos", "type": "tags.tag", "fields": {"tag": "pos"}},
    {"id": "hidden", "type": "tags.tag", "fields": {"tag": "alpha", "alpha": 255}},
    {"id": "shown", "type": "tags.tag", "fields": {"tag": "alpha", "alpha": 0}},
    {"id": "appear", "type": "tags.transform"},
    {"id": "emit", "type": "output.emit"}],
  "wires": [
    {"from": "char.index", "to": "stagger.index"}, {"from": "char.count", "to": "stagger.count"},
    {"from": "delay.value", "to": "stagger.step"},
    {"from": "stagger.offset", "to": "until.a"}, {"from": "fade.value", "to": "until.b"},
    {"from": "info.line_alignment", "to": "an.value"},
    {"from": "an.tags", "to": "pos.before"}, {"from": "info.position", "to": "pos.point"},
    {"from": "pos.tags", "to": "hidden.before"},
    {"from": "hidden.tags", "to": "appear.before"}, {"from": "shown.tags", "to": "appear.tags"},
    {"from": "stagger.offset", "to": "appear.t1"}, {"from": "until.result", "to": "appear.t2"},
    {"from": "char.units", "to": "emit.units"}, {"from": "info.layer", "to": "emit.layer"},
    {"from": "appear.tags", "to": "emit.tags"}]})";

constexpr TypesettingGraph kGraphs[] = {
    {"shake", kShake},
    {"typewriter", kTypewriter},
    {"fade-by-letter", kFadeByLetter},
};

}

std::span<const TypesettingGraph> typesettingGraphs()
{
    return kGraphs;
}

}
