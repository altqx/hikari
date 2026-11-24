# Node effects mode

How the node effects mode looks in Quiet ([#1205](https://github.com/altqx/hikari/issues/1205)). What the mode does (switching, the docks, the canvas, wiring, Add node, the keyboard, the preview, presets) is [node-effects.md](../node-effects.md#the-node-effects-mode)'s (NE2); this document says how it looks. The Template editor looks as [the Karaoke layout](karaoke-layout.md#the-template-editor) says. While either is open, Discord shows Effects ([Discord presence settings](discord-presence.md#modes)).

The owner chose this design from the mode gate board on 2026-11-17 ("Lgtm", Q730–Q734; see [Decisions](#decisions)). It is built with the [node editor](node-editor.md) parts in beta.6 ([#1436](https://github.com/altqx/hikari/issues/1436)), which also change the Node panel, the Source lines and the canvas; where the two differ, node-editor.md wins.

## The window

Q730. The mode's default arrangement:

| Region | Size | Holds |
| --- | --- | --- |
| Mode bar | 36 high, the full width, under the title row | See [The mode bar](#the-mode-bar) |
| Video, upper left | About 30 % of the width, 46 % of the height | The frame with [the preview badge](#the-preview-badge), the seek bar and the transport row |
| Source lines, lower left | Under Video | The graph's karaoke Lines: number and start time in `muted` monospace, then the text; the current one has the `select` fill. The header names the Style and the count ("Romaji · 64 karaoke lines") and holds the eye and Loop the line |
| Graph, middle | The rest of the width | The canvas; its header names the graph and holds Add node, Zoom and Fit the graph |
| Node, right | 340 wide, the full height | See [The Node panel](#the-node-panel) |
| Side bars | Closed while the mode is shown; they open as usual | |

The title bar's node-effects button shows `on` while the mode is shown.

## The mode bar

Q731. One 36-unit row on `panel` with a 1-unit `line` under it, in this order:

1. the `node-editor` glyph and "Node effects" in semibold;
2. the graph picker (180 wide) and ⋯ More options;
3. a 1-unit divider, then Style and Seed with their labels;
4. at the trailing end, [the problems pill](#problems), **Apply ▾** (primary) and **Leave node effects** (quiet, with the `exit` glyph).

Apply is disabled with no graph and while a run is applying.

## The canvas

- The ground is `bg` with a 1-unit dot every 20 units in `muted` at 35 %.
- A node is `panel`, radius 6, 180–200 wide, with a 1-unit `line` boundary; the selected one has a 2-unit `accent` one. Its title row (26 high) holds the title in semibold and the id in `muted` monospace at the trailing end; ports are 22-unit rows, inputs at the left edge, outputs at the right, a constant written in monospace at the trailing end.
- Ports keep NE2's shapes: Units a diamond, Tags a square, every other type a circle; an input that reads its field is hollow. Wires are 1.6-unit curves in their output's type colour; the selected node's wires are 2.4 units in `accent`. High contrast keeps the shapes and draws wires in `text`.

## Problems

Q732.

- **The pill.** "No problems" on `raised` when the graph validates; "2 problems" with the `notice-warning` glyph, `danger` text and a 1-unit `danger` edge when it doesn't.
- **The list.** The pill opens a popover (`panel`, radius 6, the popup shadow) headed in `muted` "2 problems · Apply waits until they're fixed". Each row is the `notice-warning` glyph in `danger`, the node id in semibold and its port or field ("sweep · at"), and under it in `muted` what's wrong. Choosing a row selects and centres the node.
- **On the canvas.** A node with a problem has a 2-unit `danger` edge and the `notice-warning` glyph in `danger` before its title.
- **In the Node panel.** A Problems section lists the node's own problems the same way.

## The preview badge

Q733. A pill at the video's top left, 10 units in: `raised`, radius 12, a 1-unit `line` boundary, an `accent` dot and "Preview · line 46 · not applied". While the graph has problems the dot is `warning`, the text `muted`, it reads "Preview · line 46 · last run that worked", and the frame is dimmed.

## Lua trust

Q734. When the file's templates or graphs hold Lua that isn't trusted yet, one 34-unit bar sits under the Graph's header and the Template editor's, on `warning` at 16 % over `panel`:

- the `notice-warning` glyph in `warning`;
- "This file's templates and graphs run Lua code with your rights. Preview them if you trust the file.";
- **Preview** (primary) and **Details** (quiet).

The canvas is dimmed and the video's badge reads "Preview off until you trust this file". Trusting in either panel covers both, as [node-effects.md](../node-effects.md#the-node-effects-mode) says.

## The Node panel

From the top: the node's title in semibold with its id in a monospace field at the trailing end; its summary in `muted`; **Fields** with labelled editors (colours as a swatch and a hex field); **Wired**, one row per wired input ("At · p › value") with **Disconnect** (quiet); **On line 46**, the values from the last run in monospace with labels in `muted`; then **Problems** when there are any. With nothing selected: "Select a node to see its fields." in `muted`.

## Panel states

| State | Shown |
| --- | --- |
| No graph | In the Graph: the `node-editor` glyph, "This file has no node effects yet" in semibold, "A graph turns karaoke lines into new lines. It is saved in the file as a Comment line, and the lines it writes are marked fx with its name." in `muted`, then **New graph** (primary), **From a preset…** and **From the library** (quiet). Source lines: "Pick or add a graph to see the karaoke lines it applies to." |
| Applying | Under the Graph's header: "Applying node effect · 212 of 412 karaoke lines" with **Stop** (quiet) over a 2-unit `accent` progress line |
| Problems | See [Problems](#problems) |
| Lua not trusted | See [Lua trust](#lua-trust) |

## Discord

Presence shows **Effects** (`effects`) while the mode or the Template editor is open. Its art is the `node-editor` glyph on #A6E3E9 ([Art](discord-presence.md#art)).

## Decisions

The owner said "Lgtm" to the mode gate board on 2026-11-17 ([#1209](https://github.com/altqx/hikari/issues/1209#issuecomment-6941093771)):

| Q | Decision |
| --- | --- |
| Q730 | Video and Source lines on the left, the Graph in the middle, the Node panel at full height on the right; side bars closed |
| Q731 | One 36-unit mode bar under the title row, ending with the problems pill, Apply ▾ and a labelled Leave node effects |
| Q732 | Problems as a danger pill whose list selects the node; failing nodes get a danger edge and the warning glyph |
| Q733 | The preview badge is a pill at the video's top left; with problems it dims and says "last run that worked" |
| Q734 | One Lua trust bar over the Graph and the Template editor in today's words, with Preview and Details |
