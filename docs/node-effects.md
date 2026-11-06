# Node effects

A node effect is a graph that turns karaoke Lines into generated Lines. The decision is [ADR 0023](adr/0023-node-effects.md). This page is the contract for the engine, the node catalogue and the graph format (NE1, [#423](https://github.com/altqx/hikari/issues/423)). The canvas and its editor mode (NE2, [#424](https://github.com/altqx/hikari/issues/424)) and the ten [karaoke presets](karaoke-presets.md) as graphs (NE3) build on it.

The engine is `hikari::application` code with no Qt: `node_catalogue.h`, `node_graph.h`, `node_graph_library.h` and `node_effects.h`.

## Graphs in a Document

A graph lives in its Document as a Comment Line:

- its Effect is `graph <name>`;
- its Style is the Style of the karaoke Lines it applies to;
- its text is the graph as compact JSON.

`documentGraphs` finds them, and `graphLine` makes one.

A graph name is not empty and has no space at either end, no dot at the end, and no comma, control character or `/ \ : * ? " < > |`. It also has no `karaoke` or `Karaoke`, because the stock templater would take a Line with such an Effect for a karaoke Line.

The Lines a graph generates carry the Effect `fx <name>`, and the Grid folds them as it folds the templater's (`isFoldableGeneratedLine`, [workspaces](ux/workspaces.md)).

## The graph format

```json
{"format": 1, "seed": 0, "allStyles": false,
 "nodes": [{"id": "syl", "type": "units.for_each", "fields": {"unit": "syl"}, "at": [120, 40]},
           {"id": "emit", "type": "output.emit"}],
 "wires": [{"from": "syl.units", "to": "emit.units"}]}
```

- `format` is the graph format, now 1. HikariSub refuses a graph whose format is newer than its own.
- `seed` seeds every Random node.
- `allStyles` runs the graph on karaoke Lines of every Style, not only the graph Line's.
- A node has:
  - an `id` (letters, digits, `_` and `-`, unique in its graph);
  - a `type` from the catalogue;
  - `fields`, which may be left out;
  - an optional canvas position `at`.
- A field holds a typed constant only. A computed value reaches a node only through a wire.
- An input that is wired ignores the field of the same name.
- A wire runs from `node.output` to `node.input`. An input takes one wire.

Unknown keys are refused, so a later format can add keys safely.

## Port types

Number, Text, Colour, Alpha, Time, Point, Tags, Drawing, Bool and Units.

- Number, Time and Alpha connect to one another.
- Every other type connects only to itself.
- Colours are written `#RRGGBB`. Alpha runs from 0 (opaque) to 255.
- Tags are override tags without braces.

## The nodes

| Category | Nodes |
|---|---|
| units | `for_each` (the Line, its syllables, their characters or the furigana), `repeat` (each unit `count` times, with `j`), `frames` (one element per frame step across a span), `info` (the templater's variables for the current unit, its Line and its Style) |
| values | `number`, `text`, `colour`, `point`, `choose` (the index-th of a list of values, wrapping) |
| math | `calculate`, `compare`, `progress` (with easing), `random` (seeded) |
| time | `retime` (the templater's ten modes), `stagger` |
| tags | `tag` (one override tag), `move`, `fade`, `transform` |
| colours | `gradient` |
| drawings | `shape` |
| text | `join` (one piece per unit, e.g. `{\kf$kdur}` and the syllable) |
| output | `emit` (writes one Line per element) |
| groups | `group`, `input`, `output` |
| lua | `code` |

`nodeCatalogue()` lists every node with its ports and fields, and `nodeFieldsSchema` gives each node's fields as a JSON Schema. A node's `since` is the catalogue version that added it. The catalogue only grows: a later version adds nodes, ports and fields, and keeps the ones it has.

**Units.** A `for_each` starts a chain of units. `repeat` and `frames` take units and give units.

- A unit-bound value is read inside its own chain. Examples are `info` read from a unit, a `repeat`'s `j`, and anything computed from them.
- An Emit writes one Line per element of the units wired into it.

**Groups.** A group holds a graph of its own.

- Its `input` and `output` nodes declare its ports.
- The engine inlines groups before it runs, so a group gives exactly what its nodes would give ungrouped.

## Validation

`validateGraph` returns a list of problems. Each one names its node (`group/inner` inside a group), its port or field, and what is wrong. The validator catches:

- unknown types and fields;
- values of the wrong kind or out of range;
- duplicate ids;
- wires to ports that don't exist, between types that don't connect, or doubled on one input;
- loops;
- unit-bound values read outside their units;
- groups that don't resolve;
- a graph with no Emit.

The engine runs only a graph without problems.

## Running a graph

`runNodeEffect` (or `runDocumentGraph` for a graph Line) gives the generated Lines, the furigana Styles they need and the problems. `applyNodeEffect` writes them as one undo step, "Apply node effect":

- It removes the graph's earlier Lines and puts the new ones where the first of them stood, or at the end.
- Lines of other graphs and of the templater stay as they are. Regenerating one graph never touches another's Lines.
- A run limited to some karaoke Lines (`sources`) replaces only those Lines' output.
- Each source that got Lines becomes a Comment with Effect `karaoke`, as the templater leaves it.
- A file read back has no record of which Line came from which source. A complete run passed as `owners` matches its Lines up again, so a partial run after a reload still replaces the right Lines.

**Exactly the templater's layout.** Units, positions and times are worked out as karaskel and the stock templater (Aegisub 3.2.2) work them out. This makes a graph that does what a template does give the same bytes. Graphs of five KE3 presets are checked against the presets' goldens:

- Furigana Styles are added for every Style.
- The script/video aspect correction applies.
- `$li` counts the Script Info and Style lines first. It leaves out the graph's own `fx` Lines, so it doesn't change between runs.
- Numbers are written as LuaJIT's `%.14g`, whatever the locale.
- Variables are rounded as `floor(x + 0.5)`.
- Times are truncated to whole milliseconds.
- Line Emits come first, then syllables and characters, then furigana. Within each, units go in order, then Emits in their `order`, then characters, then repeats.

**Deterministic.** The same graph, seed and Lines give byte-identical Lines on every run, with any number of threads.

- Each karaoke Line is evaluated on its own, in parallel.
- A Random node draws from the graph's seed, its own seed and id, the karaoke Line's start and text, and its scope (the element, the unit or the Line), never from a clock.
- The engine's sources are built without floating-point contraction (`-ffp-contract=off`, `/fp:precise`), so Linux and Windows agree.

**Cached.** A `NodeEffectCache` keeps each Emit's Lines for each karaoke Line, keyed on:

- the graph's name and seed;
- everything upstream of that Emit;
- the karaoke Line and its Style.

A change to one branch therefore re-runs only the Emits that read it, and only for the Lines that changed.

## Lua

The `lua.code` node is the only place Lua runs. A graph that holds one writes nothing until the user trusts the Document's Lua, the same prompt templates holding Lua use. The engine then calls a `LuaNodePort` once per karaoke Line with every row the node needs.

Its inputs and outputs are Number, Text, Colour, Alpha, Time, Point, Bool or Tags.

The port's helper-process implementation is not in NE1. The automation helper has no command for it yet, so until it does, a trusted Lua node reports that it has no Lua helper to run it.

## The graph library

`NodeGraphLibrary` keeps the user's graphs for reuse across files. Each graph is a file `<name>.json` in the user folder's `node-effects` folder ([distribution](distribution.md)), in the same format, written to a `.partial` file and then renamed into place.

## On the Hikari API

- `nodes.catalogue` lists the engine's nodes with their ports and field schemas.
- `nodes.validate` checks a graph and returns `{valid, problems}`. Each problem has its `node`, `port` and `message`.

Both are read operations of API version 1 ([Hikari API](api.md)).
