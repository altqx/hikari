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

The `lua.code` node is the only place Lua runs (NE1b, [#753](https://github.com/altqx/hikari/issues/753)).

**Trust.** A graph that holds a Lua node writes nothing until the user trusts the Document's Lua. Until then the run's problem is "runs only once you trust this Document's Lua".

- Templates and graphs share one choice per open Document, made in the Template editor or the node-effects mode.
- The choice covers each Lua node's code and ports as they were when the user chose:
  - edits made in the node-effects mode keep it (`TemplateEditorController::editKeepingTrust`), and so does an Undo of them;
  - a Lua node added, or its code or ports changed any other way (a paste, an import, the Line editor), asks again;
  - graph edits that don't touch Lua never ask, and a graph without a Lua node never asks;
  - a reload from the file asks again.

**Batches.** The engine calls a `LuaNodePort` once per karaoke Line, with every row the node needs. A run with a Lua node uses one thread. `backends::LuaNodeHelper` is the port: it runs the batch in a `hikari-lua-helper` of its own, with the automation helper's `RunNodes` command.

**Ports.** Inputs and outputs are Number, Text, Colour, Alpha, Time, Point, Bool or Tags. In Lua:

- Number, Time and Alpha are numbers;
- Text and Tags are strings, tags without braces;
- Bool is a boolean;
- Colour is a string `#RRGGBB`;
- Point is a table `{x = …, y = …}`.

An output that is unset, or of another type, fails the batch and names the output.

**The environment.** The same graph and Lines always give the same output, so the code gets only:

- the base functions `assert`, `error`, `ipairs`, `next`, `pairs`, `pcall`, `rawequal`, `rawget`, `rawset`, `select`, `tonumber`, `tostring`, `type`, `unpack`, `getmetatable`, `setmetatable` and `xpcall`, with `_G` and `_VERSION`;
- `string` without `dump`;
- `table` and `bit`;
- `math` without `random` and `randomseed`.

It has no `io`, `os`, `require`, `load`, `loadstring`, `dofile`, `debug`, `ffi`, `jit`, `coroutine` or `aegisub`. Randomness comes only from the seeded Random node. Code is loaded as text; precompiled bytecode is refused.

**Fresh globals per row.** Each row runs in new globals that hold only that environment and the row's inputs. Nothing a row sets, not even a change to `string`, reaches the next row or Line. A counter across units comes from `stagger` or unit info, not from Lua.

**Limits.** A Line's batch may take 10 seconds. Past that, or when the helper crashes:

- the helper is ended;
- the node's problem says it "took longer than 10 seconds and was stopped", or "the Lua helper stopped";
- the rest of the run stops and it writes nothing;
- the next run starts a new helper.

A Lua error fails only its batch, with Lua's message (`code:3: …`), and the helper stays.

**The sandbox.** The helper starts under `hikari_sandbox` ([ADR 0029](adr/0029-the-ai-sidecar-runs-in-an-os-sandbox.md)) with the profile `HikariSub.Lua.Nodes`: the helper's folder to read and execute, nothing else, and network `none`. Where the sandbox can't apply, Lua nodes still run, and the host logs what is missing under `hikari.nodes.sandbox`.

**Protocol.** `RunNodes` is command 5 of the Lua helper protocol, version 9 ([`lua_protocol.h`](../src/backends/include/hikari/backends/lua_protocol.h)). It needs no `Load`. Version 9 changes nothing else. A version 8 helper is refused at the handshake, so an application and a helper from different releases never run a script together.

## The graph library

`NodeGraphLibrary` keeps the user's graphs for reuse across files: the mode's Save to the library writes one, and From the library… adds one to the Document. Each graph is a file `<name>.json` in the user folder's `node-effects` folder ([distribution](distribution.md)), in the same format, written to a `.partial` file and then renamed into place.

## The node effects mode

NE2 ([#424](https://github.com/altqx/hikari/issues/424)) edits a Document's graphs on a canvas. The approved design is the [#424 gate](https://github.com/altqx/hikari/issues/424#issuecomment-6619660089). `NodeEffectsController` (`src/ui`) holds the mode's state; `node_graph_editing.h` holds the edits it makes, with no Qt.

- **Switching.** View › "Node effects" (`GLOBAL_NODE_EFFECTS`, no default key) or the title bar's node-effects button turns the mode on and off, and the mode bar's "Leave node effects" turns it off. Entering keeps the tab's arrangement and shows the mode's own layout; leaving brings the tab's arrangement back and never asks.
- **Layout.** Ordinary docks: Video and **Source lines** on the left, **Graph** in the middle, **Node** on the right (panel registry 15, 16, 17). The Grid, the Line editor and the audio close. The user may rearrange the docks; the arrangement is kept in `<layout file>-node-effects.json` beside the layout file, and View › Layout › "Reset this layout" builds the default again.
- **The mode bar** picks the graph (the Document's `graph <name>` Lines) and has its menu: New graph, From a preset, From the library…, Rename…, Duplicate, Save to the library and Delete graph. Style sets the graph Line's Style or "All styles"; Seed sets the graph's `seed`. It shows the problem count, whose list selects and centres a node, and Apply ▾ ("Apply to every karaoke line", "Apply to the selected lines").
- **Undo.** Each canvas edit goes into one "Edit graph" step on the graph Line; edits within 800 ms of each other, or one drag, make one step. New graph, Rename graph, Duplicate graph and Delete graph are their own steps, and Delete removes the Lines the graph wrote. Apply is one "Apply node effect" step.
- **Canvas.** A node shows its title and id, its fields, its inputs on the left and its outputs on the right. A port's shape and colour give its type: Units a diamond, Tags a square, every other type a circle; Number, Time and Alpha share a colour, and high contrast keeps the shapes and drops the colours (`Theme.ports`). An input that reads its field has a hollow port with the constant written in. Unit info shows only its wired outputs and folds the rest. A node the catalogue doesn't know is a dashed placeholder that keeps its fields and wires; a graph whose `format` is newer opens read-only.
- **Wiring.** Dragging from an output dims the inputs it can't feed. A drop on one is refused with the reason, a drop on a wired input replaces its wire, a wire that would make a loop is refused, and a drop in empty space opens Add node filtered to nodes that take the output. Dragging a wire off an input moves it or, dropped in empty space, removes it.
- **Add node** opens from the Graph header, a double-click on the canvas or a typed letter. It lists the catalogue by category with "Recently added", searches names and summaries, and lists Lua last.
- **Keyboard.** The arrows move the selection to the nearest node, Ctrl+arrow moves the node, Delete deletes it, Ctrl+G groups the selection, Enter goes into a group (the Graph header shows the path) and Backspace comes out. Each node is announced as "Choose, palette: 1 of 1 inputs wired", and the Node panel lists every wire.
- **Node panel.** The node's summary, its id (renamable), typed field editors, "From node › port" with Disconnect for each wired input, its values on the current source Line from the last run (`NodeValueTrace`), its timing and its problems.
- **Preview.** While the mode is shown, the graph runs on the current source Line on every edit, with NE1's cache. The video draws that Line's output in place of its generated Lines, with the badge "Preview · line N · not applied"; Source lines has the eye and Loop the line. A graph with problems keeps the last run that worked, dimmed.
- **Lua trust** is the Template editor's choice for the open Document: trusting the file's Lua in either one covers both.
- **Presets.** "From a preset" lists the KE3 presets that exist as graphs under their Template-editor names: Classic fill, Bounce, Sparkles, Falling letters and Colour per line. NE3 adds the other five.

## On the Hikari API

- `nodes.catalogue` lists the engine's nodes with their ports and field schemas.
- `nodes.validate` checks a graph and returns `{valid, problems}`. Each problem has its `node`, `port` and `message`.

Both are read operations of API version 1 ([Hikari API](api.md)).
