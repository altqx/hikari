# Timing layout

The layout for setting start and end times by ear and by keyframe ([#1205](https://github.com/altqx/hikari/issues/1205)). It replaces the Timing row of [workspaces.md](workspaces.md)'s preset table. It is drawn in [Quiet](visual-language.md), and its Discord mode is Timing ([Discord presence settings](discord-presence.md#modes)).

The owner chose this design from the mode gate board on 2026-11-17 ("yes lgtm", Q710–Q714; see [Decisions](#decisions)). The app doesn't change until the code card lands after the beta.4 tag.

## The arrangement

Q710. Today Timing is Audio and the Line editor across the full width, at about the Editing heights, with the Grid below. The Audio panel is the work surface, so it grows:

| Region | Size | Holds |
| --- | --- | --- |
| Audio | The full width of the main area, about 36 % of its height | The waveform or spectrum, the ruler, [the toolbar](editing-layout.md#the-audio-toolbar) |
| Line editor | Under Audio, its natural height, at least 170 | As in Editing |
| Grid | The rest, the full width | As in Editing |
| Right side bar | Open, on the Timing postprocessor | See [The side bar](#the-side-bar) |
| Video | Closed | View › Panels › Video opens it as a floating panel at the top right; its place and size are kept between sessions |

Like the other layouts it is built the first time Timing is chosen, from the window's size, and Reset this layout gives it again.

## The side bar

Q711, Q712. Choosing Timing opens the right side bar on the **Timing postprocessor**. **Shift times** and **History** are tabs beside it. History is a side-bar tab in this layout; elsewhere it stays where the user puts it.

### Timing postprocessor

Q713. Restyled; what it does is in [Shift times](shift-times.md) (#598):

- the preset choice and its ⋯ menu, then **Lines** (Selected lines (6), All lines);
- four rules, each a check box with its fields indented under it: Lead-in (Start earlier by), Lead-out (End later by), Close small gaps, Snap to keyframes (Within);
- **"6 lines change"** in semibold, **Show changes** at the trailing end, then the first three changes in monospace ("46 0:03:34.30 → 0:03:39.97", "→ kf" when snapped);
- at the bottom: "One undo step: “Timing postprocessor”" in `muted` and **Apply** as the primary button.

Show changes opens the full list in the panel. It replaces today's Preview / Assistant segmented group and the switches.

### History

Each step is a 26-unit row: its glyph, its name, and what it touched in `muted` at the trailing end ("line 46", "6 lines"). The current step has the `select` fill. A click goes back to that step; Undo and Redo walk the same list. Under it: "Click a step to go back to it. Undo and Redo walk the same list."

## States

Q714. The Audio panel's empty, busy and error states are [Editing's](editing-layout.md#panel-states).

## Discord

Presence shows **Timing** (`timing`), unchanged from #599. Kanji timing shows Timing too (Q694).

## Decisions

The owner said "yes lgtm" to the mode gate board on 2026-11-17 ([#1209](https://github.com/altqx/hikari/issues/1209#issuecomment-6939502207)):

| Q | Decision |
| --- | --- |
| Q710 | Audio takes the full width at about a third of the height, the Line editor and Grid below; Video is closed and opens as a floating panel |
| Q711 | History is a side-bar tab in Timing, next to the Timing postprocessor and Shift times |
| Q712 | Timing opens the right side bar on the Timing postprocessor |
| Q713 | The postprocessor uses check boxes, a change count with the first rows and Show changes, and Apply as the primary button |
| Q714 | The Audio panel's states follow Editing's |
