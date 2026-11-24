# Node editor

How the node effects editor is used: menus, the Node panel, adding nodes, choosing source lines, the canvas and its keys ([#1436](https://github.com/altqx/hikari/issues/1436)). [Node effects mode](effects-mode.md) says how the mode looks in Quiet, and this document builds on it. What the engine does with a graph is [node-effects.md](../node-effects.md)'s.

The owner took every recommendation on the node editor gate board on 2026-11-24 (Q814–Q817; see [Decisions](#decisions)). The design assumes that an undo is never overwritten by a pending edit ([#1437](https://github.com/altqx/hikari/issues/1437)), and that the preview follows every edit ([#1440](https://github.com/altqx/hikari/issues/1440)). The app doesn't change until the code lands.

## Right-click menus

ne-1, ne-2, ne-3. Every menu is a `ShellMenu` of `ShellMenuItem`s ([context-menus.md](context-menus.md)). The Menu key and Shift+F10 open the menu of whatever has focus: the node, the wire, the port or the canvas at its centre. Each row shows its bound key.

**On a node, or on a selection that holds it.** The menu acts on the whole selection, and right-clicking a node outside the selection selects that node first.

| Row | Key | Notes |
| --- | --- | --- |
| Rename… | F2 | Renames the id in place on the card; a refused name says why under the field |
| Duplicate | Ctrl+D | Copies land 20 units down and right, wired to each other as the originals were |
| Copy, Cut | Ctrl+C, Ctrl+X | |
| Disconnect all | | |
| Bypass | B | A check row. See [Bypass](#bypass) |
| Collapse | H | A check row. See [Collapse](#collapse) |
| Group | Ctrl+G | |
| Put in a frame | Ctrl+J | |
| Save to the library… | | Saves the selection as a snippet |
| About “Transform” | F1 | Opens the user guide at the node's page |
| Delete | Delete | In `danger` |

Separators fall after Cut, Collapse and Save to the library…. With two or more nodes the first row reads “3 nodes” as a heading and Rename… and About are left out.

**On empty canvas.** The node lands where the user right-clicked.

| Row | Key |
| --- | --- |
| Add node ▸ | Shift+A |
| Insert from the library ▸ | |
| Paste | Ctrl+V |
| Select all | Ctrl+A |
| Add a frame | Ctrl+J |
| Fit the graph | Home |
| Zoom to 100 % | Ctrl+0 |

Add node ▸ has a search field at the top, then Recently added, then the categories as submenus. Insert from the library ▸ lists the library's tiers (see [The library](#the-library)). Paste is disabled when the clipboard holds no nodes.

**Wires.** A wire is an object: it brightens to 2.4 units under the pointer, a click selects it (`accent`), Delete removes it, a + at its middle opens Add node to insert a node into it, and a double-click adds a reroute point there. Dropping a dragged node onto a wire inserts it when one of its inputs takes the wire's type and one of its outputs feeds the far end's input. Its menu has a heading, “sweep › colour → pop › colour”, then:

- Insert a node here…
- Add a reroute point
- Go to “sweep”, Go to “pop” (select and centre that end)
- Disconnect (Delete)

**Ports.** Its menu has a heading, “pop › scale · Number”, then:

- Connect from…: a list of the outputs that can feed this input, searchable, so wiring works from the keyboard. On an output it reads Connect to… and lists inputs.
- Disconnect, when wired.
- Reset to 100 % (the field's default), when the field was changed.
- Show on the node, a check row (inputs only). See [Values on the node](#values-on-the-node).

## The Node panel

ne-4. The Node panel is a form with real controls. From the top: the title and id, the summary in `muted`, **Fields**, **Wired**, **On line 46**, **Outputs**, and **Problems** when there are any.

| Value | Control |
| --- | --- |
| Number, Time, Alpha | A scrub field: drag left or right to change it, Shift for fine steps, ‹ › step it, typing works. It shows its unit (%, ms, px) and its range in `muted` (“10 – 400”). A value outside the range isn't taken: the field gets a `danger` edge and “Between 10 and 400” under it, and the old value stays |
| A value from 0 to 1 | A slider with its number field |
| Colour | A swatch with its hex field and alpha |
| Point | An X and a Y scrub field and **Pick on the video**, which takes the next click on the frame |
| A choice of up to 4 | A segmented control (In \| Out \| Both) |
| A longer choice | A combo |
| Text | A text field |

- **Changed values.** A field that differs from the node's default has an `accent` dot before its label and ↺ after it; ↺ sets the default back.
- **Wired inputs.** In place of its control, a wired input shows its source as a chip, “sweep › colour”, with **Go to** and **Disconnect**.
- **On line 46.** Each value from the last run, in monospace, with the run's timing: “On line 46 · from the last run · 0.3 ms a line”.
- **Nothing selected.** The graph's name, its node count and “Select a node to see its fields.” in `muted`.

Edits from the Node panel go into the same “Edit graph” undo step as canvas edits.

## Several nodes

ne-6.

- **One type.** The panel heads “3 Fade nodes” and edits all of them. A value that differs between them reads “Mixed” until it is set. Wired, On line and Outputs are left out.
- **Mixed types.** The panel heads “5 nodes” and shows only the shared actions: Group, Put in a frame, Bypass and Delete.
- **Moving.** Dragging any selected node, or Ctrl+arrows, moves the whole selection.

## Values on the node

ne-5. Cards show their values read-only, as now. A port's Show on the node makes that input's value editable on the card, with the same control as in the Node panel, at 22 units high. It is saved with the graph (`shown`, see [The graph format](#the-graph-format)).

## Adding nodes

ne-7, Q815. One popup adds nodes, however it is opened: a double-click on empty canvas, typing a letter, Shift+A, Add node ▸ on the canvas menu, the Graph header's +, Insert a node here… on a wire, or a wire dropped on empty canvas. It opens at the pointer, or at the canvas centre from the keyboard or the header, and the node lands there.

- **Search.** “Search nodes, snippets and plugins” at the top. It matches names and summaries. Typing a letter on the canvas opens the popup with that letter in the field.
- **Chips.** All, Units, Maths, Colour, Tags, Text and Snippets, and one chip for plugin nodes when a plugin adds any. They filter the list.
- **Empty query.** Recently added, then each category open, as a list of names.
- **Preview.** The highlighted node's title, summary, inputs and outputs, at the popup's trailing side.
- **A dropped wire, or a wire's Insert.** The list keeps only nodes that fit, and its heading says why: “Nodes with a Number input, for p › value”. The new node is wired as today.
- **Snippets** are library graphs. Adding one inserts its nodes as a group at the pointer.
- **Keys.** The footer reads “↑↓ move · Enter add · Shift+Enter add and keep open · Esc close”. Shift+Enter adds the node and keeps the popup open with the search cleared, for adding several nodes in a row.

## Source lines

ne-8, Q814. The Source lines panel chooses the Lines a graph runs on, so the user never leaves the mode for the Grid.

- **The list.** Every Line of the Document that can be a source (as [Typesetting graphs](../node-effects.md#typesetting-graphs) says; graph, template and generated Lines never show), each with a tick box, its number and start in `muted` monospace, its Style, and its text. The Line the preview shows has the `select` fill.
- **Filters.** A Style combo, a “Text, actor or time” field, and All \| Chosen.
- **Keys.** Space ticks the current Line; Shift+↑↓ extends a range and Space ticks it all. The list follows the video as today.
- **Header actions.** “Choose all Sign” (the Style filter's Style), Clear, and **Use the Grid's selection**, which ticks the Lines selected in the Grid before the mode opened.
- **Karaoke graphs.** Their Style's karaoke Lines are ticked by default, and the user can untick some to leave them out.
- **The mode bar.** Next to Seed, “Sources: 4 chosen lines”. Choosing opens Source lines.
- **Apply** runs on the ticked Lines. Apply ▾ also has “Apply to the line at the playhead”. Subtitles › Typesetting graph… still opens the mode with the Grid's selection ticked.

**Saving the choice.** The choice is part of the graph and goes into its “Edit graph” undo step. While the file is open, the choice follows each Line however it is edited or moved. In the file, the graph keeps one reference per Line: its start, end, Style and a hash of its text (`sources` for a typesetting graph, `skip` for the Lines a karaoke graph leaves out). References are rewritten whenever the graph Line is written. On loading, a reference takes the first Line with the same four values, then the first with the same start and Style. One that matches nothing is dropped, and Source lines says “2 chosen lines weren't found” in `muted` until the choice changes.

## The canvas

- **Selecting.** Left-drag on empty canvas draws a selection box (`accent` at 12 % with a 1-unit `accent` edge); it selects every node it touches. Shift adds to the selection, Ctrl toggles. Ctrl+A selects all. ne-9, Q816.
- **Panning.** Middle-drag, or Space with a left-drag. The wheel zooms at the pointer, as today.
- **Zoom.** The Graph header has − 100 % + and Fit. Ctrl+0 zooms to 100 %, Ctrl+= and Ctrl+− zoom in and out, Home fits the graph and F frames the selection. ne-14.
- **Minimap.** Off by default; the Graph's ⋯ menu has a Minimap check row. When shown it sits at the canvas's lower right, 180 × 120, on `panel` at 90 % with a 1-unit `line` edge, the visible area as an `accent` outline; dragging in it pans.
- **Copy and paste.** Copy puts the nodes, the wires between them, and their frames on the clipboard as graph JSON. Paste places them at the pointer and renames ids that clash (`fade` becomes `fade_2`). Text pasted from another app that isn't graph JSON is ignored.

### Bypass

ne-11. A bypassed node doesn't run. Each of its outputs passes on the first input of the same type, and an output with no such input is unwired for the nodes it feeds, so they read their own fields. The card is drawn at 50 % with a 1-unit dashed `line` edge and a “Bypassed” chip in its title row. It doesn't hold Apply back. Export as template lines exports a bypassed node as its pass-through.

Each bypassed node is also one problem in the shared Problems list ([#1227](https://github.com/altqx/hikari/issues/1227)):

- **Severity:** `info` (“Information”), with an `accent` squiggle, an `accent` Grid mark and a row in the Problems list.
- **Text:** “bypassed: passes scale through”, naming the input it passes on.
- **Source:** a built-in problems source, `effects`, on the graph's Line. It re-syncs when the graph changes, as `src/app/reading_problems.cpp` does for reading problems.
- **The mode's own marks stay:** the problems pill, the card's edge and the Node panel's Problems section work as now. The list entry is added to them.

### Collapse

A collapsed card shows only its title row and one port on each side, each carrying all its wires. Collapse doesn't change what the graph does.

### Frames

A frame is a titled box behind nodes: `raised` at 40 % with a 1-unit `line` edge, radius 6, its title in semibold at its top left. Put in a frame (Ctrl+J) wraps the selection with 20 units of margin; Add a frame draws an empty one at the pointer. A node belongs to the frame its position falls in. Dragging the title moves the frame with its nodes; dragging an edge resizes it. A double-click on the title renames it. Deleting a frame keeps its nodes.

### Reroutes

A reroute point is a 6-unit dot on a wire that the wire bends through. Double-clicking a wire, or Add a reroute point, adds one; it drags like a node and Delete removes it.

## The preview badge

[The preview badge](effects-mode.md#the-preview-badge) always says why the preview shows nothing new, with its dot in `warning` and its text in `muted`:

| Why | Badge |
| --- | --- |
| No source Line is chosen, or none is at the playhead | “Preview · no source line here” |
| A node has a problem | “Preview · line 46 · last run that worked”, as now; with no earlier run, “Preview · fix the problems to see it” |
| Lua isn't trusted | “Preview off until you trust this file”, as now |

## Feedback

ne-12. A toast, with Undo where it can be undone as a plugin update's has ([plugins.md](../plugins.md)), confirms three things:

- Deleting nodes: “Deleted 3 nodes” with **Undo**.
- Deleting a graph: “Deleted “Romaji fade” and its 412 lines” with **Undo**.
- Saving to the library: “Saved to your library”. When an entry of that name exists it asks first: “Replace “Romaji fade” in your library?” with **Replace** and **Cancel**.

A refused node or graph name keeps the field open with the reason under it in `danger`, such as “Another node is called “fade”.”

## The library

ne-13. The library has three tiers, as [ADR 0030](../adr/0030-open-a-folder-as-a-project.md) has for Project things:

1. **This project:** `.hikari/node-effects/<name>.json` in the window's Project, shown only when there is one.
2. **Yours:** the user folder's `node-effects` folder, as now.
3. **Plugins:** graphs a plugin ships. The heading stays hidden for now: a manifest key for plugin graphs is future work outside #1436.

From the library ▸ and Insert from the library ▸ list the tiers under headings. Save to the library… asks which tier when there is a Project. **Manage library…**, the last row of From the library ▸, opens a dialog listing each tier's graphs with Rename… and Delete, and Move to project or Move to yours.

## Keys

ne-10. A new hotkey context, **Node graph**, holds the canvas's keys. Settings › Shortcuts lists them under it, and they can be changed there. They act while the Graph panel has focus and claim their keys at ShortcutOverride ahead of Global keys, as the Video panel does with its nudge keys ([visual-tools.md](../visual-tools.md)), so F1, F2, the arrows and Ctrl+arrows act on the graph there. Existing canvas keys become rows with today's defaults.

| Id | Action | Context | Default | Today |
| --- | --- | --- | --- | --- |
| 5244 | Node effects (View) | Global | Ctrl+Shift+E | no key |
| 5338 | Typesetting graph… | Global | Ctrl+Shift+G | no key |
| 6001 | Add node… | Node graph | Shift+A | new |
| 6002 | Copy | Node graph | Ctrl+C | new |
| 6003 | Cut | Node graph | Ctrl+X | new |
| 6004 | Paste | Node graph | Ctrl+V | new |
| 6005 | Duplicate | Node graph | Ctrl+D | new |
| 6006 | Select all | Node graph | Ctrl+A | new |
| 6007 | Rename… | Node graph | F2 | new |
| 6008 | Delete | Node graph | Delete | fixed key today |
| 6009 | Bypass | Node graph | B | new |
| 6010 | Collapse | Node graph | H | new |
| 6011 | Group | Node graph | Ctrl+G | fixed key today |
| 6012 | Put in a frame | Node graph | Ctrl+J | new |
| 6013 | Go into the group | Node graph | Enter | fixed key today |
| 6014 | Go out of the group | Node graph | Backspace | fixed key today |
| 6015 | Fit the graph | Node graph | Home | new |
| 6016 | Frame the selection | Node graph | F | new |
| 6017 | Zoom to 100 % | Node graph | Ctrl+0 | new |
| 6018 | Zoom in | Node graph | Ctrl+= | new |
| 6019 | Zoom out | Node graph | Ctrl+− | new |
| 6020 | Select the nearest node left | Node graph | ← | fixed key today |
| 6021 | Select the nearest node right | Node graph | → | fixed key today |
| 6022 | Select the nearest node up | Node graph | ↑ | fixed key today |
| 6023 | Select the nearest node down | Node graph | ↓ | fixed key today |
| 6024 | Move the selection left | Node graph | Ctrl+← | fixed key today |
| 6025 | Move the selection right | Node graph | Ctrl+→ | fixed key today |
| 6026 | Move the selection up | Node graph | Ctrl+↑ | fixed key today |
| 6027 | Move the selection down | Node graph | Ctrl+↓ | fixed key today |
| 6028 | About this node | Node graph | F1 | new |

Escape (cancel a drag or close a popup), Space (pan with a drag), and the Add popup's and Source lines' list keys stay fixed. Typing a letter without Ctrl or Alt still opens Add node with that letter; Shift+A opens it empty.

The new context is `NodeGraphHotkey` after `AudioHotkey` in `HotkeyWindow`, with the 6xxx id range; the next free id is 6029. The implementing PR checks that Ctrl+Shift+E and Ctrl+Shift+G are still free at its main merge, and recounts the rows `hotkeys_tests` and `settings_import` expect.

## The graph format

ne-11. The format stays 1. Every key below is optional and add-only, and is written only when it is used, so a graph that doesn't use them stays exactly as now. A build from before these keys refuses a graph that uses them, as it refuses any unknown key; nothing with this format is released yet, and versions stay 1.

| Key | On | Value |
| --- | --- | --- |
| `bypass` | a node | `true` |
| `collapsed` | a node | `true` |
| `shown` | a node | The input names shown on the card, such as `["scale"]` |
| `via` | a wire | Reroute points, `[[x, y], …]`, in order from the output |
| `frames` | the graph | `[{"title": "Intro", "at": [x, y], "size": [w, h]}, …]` |
| `sources` | the graph, `target` `lines` | Line references `{"start": "0:01:02.34", "end": "0:01:04.10", "style": "Sign", "text": "<hash>"}` |
| `skip` | the graph, `target` `karaoke` | Line references in the same shape, for Lines left out |

node-effects.md gets these rows when the code lands.

## Quiet restyle

#1205 area 8, the [Node effects mode](effects-mode.md) design (Q730–Q734), is built with these parts rather than on its own: the canvas look and the Lua trust bar with part 1, the Node panel with part 2, and the window arrangement, the mode bar, the problems pill and list, and the preview badge with part 4. Fixed with it:

- the node card is 180–200 wide (now a fixed 156), radius 6, with 26-unit title rows and 22-unit port rows;
- the typesetting graph's empty text says “lines”, not “karaoke lines”;
- the empty canvas's “From the library” loses its “…”, since it opens a submenu ([context-menus.md](context-menus.md)).

## Implementing

Five PR-sized parts for two packs. Pack 1 builds parts 1 then 5; pack 2 builds parts 2, 3 then 4. Each pack puts its new `NodeEffectsController` methods in its own source file, so the only file both packs change is `node_effects_controller.h`, with add-only rows. New strings follow [the translations rule](../localisation.md), and RF reviews each part against the gate board.

| Part | Pack | What | Files |
| --- | --- | --- | --- |
| 1. Canvas, selection, menus and wires | 1 | Right-click menus, box select, Space and middle-drag pan, multi-move, copy, cut, paste, duplicate, wire objects and insert on a wire, Connect from…, zoom keys and header, minimap, the Node graph hotkey context with every row in [Keys](#keys), the canvas restyle and the Lua trust bar | `NodeGraphCanvas.qml`, `NodePort.qml`, new `NodeCanvasMenus.qml` and `NodeMinimap.qml`, `node_graph_editing.cpp/.h` (paste, duplicate, move many), new `node_effects_controller_canvas.cpp`, `hotkeys.h/.cpp`, `command_names.cpp`, `key_router.cpp` if the claim needs it, `node_graph_editing_tests.cpp`, new `tests/ui/node_canvas_tests.cpp` |
| 2. The Node panel and several nodes | 2 | The form controls, ranges and refusals, changed dots and ↺, Pick on the video, wired chips, “Mixed” and the shared actions, the panel restyle | `NodeInspector.qml`, new `NodeFieldEditor.qml` and `ScrubField.qml`, new `node_effects_controller_form.cpp`, `node_graph_editing.cpp/.h` (set a field on many nodes), new `tests/ui/node_form_tests.cpp` |
| 3. Adding and the library | 2 | The one Add popup, chips, preview, Shift+Enter, snippets, library tiers, Manage library…, the save confirmations and toasts | `NodeAddPopup.qml`, `NodeLibraryMenu.qml`, new `NodeLibraryDialog.qml`, `node_graph_library.cpp/.h` (rename, tiers), new `node_effects_controller_library.cpp`, `node_graph_library` tests |
| 4. Source lines | 2 | The picker, filters and keys, Use the Grid's selection, Sources on the mode bar, Apply on the ticked Lines, `sources` and `skip` in the format, the window arrangement, the mode bar, the problems pill and the preview badge | `NodeSourceLines.qml`, `NodeModeBar.qml`, `Main.qml` (`nodeEffectsLayout` and Typesetting graph…), `node_graph.cpp/.h` (the two keys), `node_effects.cpp` (runs on the choice), new `node_effects_controller_sources.cpp`, `node_effects_tests.cpp` |
| 5. Bypass, collapse, frames and reroutes | 1 | The four features, their menu rows and keys from part 1, `bypass`, `collapsed`, `shown`, `via` and `frames` in the format, the bypass problem, Values on the node | `NodeGraphCanvas.qml`, `NodeCanvasMenus.qml`, `node_graph.cpp/.h` (five keys), `node_effects.cpp` and `node_validation.cpp` (bypass), new `src/app/effects_problems.cpp/.h` (the `effects` problems source), `node_graph_editing.cpp/.h`, `node_effects_controller_canvas.cpp`, `node-effects.md` (the format rows), `node_effects_tests.cpp` |

Two files are shared. `node_effects_controller.h` takes add-only rows from both packs. `node_graph.cpp`'s reader and writer get part 5's keys first (pack 1), and part 4 (pack 2) merges them before adding `sources` and `skip`.

## Decisions

The owner picked from the node editor gate board on 2026-11-24 ([#1436](https://github.com/altqx/hikari/issues/1436)):

| Q | Board | Decision |
| --- | --- | --- |
| Q814 | ne-8 A | Source lines becomes the picker for the graph's Lines, with filters, All \| Chosen and Use the Grid's selection; the choice is saved with the graph |
| Q815 | ne-7 A | One Add popup for every way in, at the pointer, with categories open, chips, Snippets, a preview and Shift+Enter |
| Q816 | ne-9 | Left-drag on empty canvas draws a selection box; middle-drag or Space+drag pans |
| Q817 | ne-1–6, ne-10–14 | Every other recommendation: node and canvas menus (ne-1, ne-2 A), wires and ports as objects (ne-3), the form Node panel (ne-4 A), values on the node by choice (ne-5), editing several nodes (ne-6), the Node graph keys (ne-10), bypass, collapse, frames and reroutes in format 1 (ne-11), toasts with Undo (ne-12), library tiers and Manage library (ne-13), zoom controls and an optional minimap (ne-14) |
