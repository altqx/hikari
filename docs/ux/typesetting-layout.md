# Typesetting layout

The docked layout for signs, in Quiet ([#1205](https://github.com/altqx/hikari/issues/1205)). The arrangement is [workspaces.md](workspaces.md)'s (TP1) and the Styles panel's behaviour is described there too. [Full-screen typesetting](full-screen-typesetting.md) (#1208) is the full-screen form; this layout borrows its words. Its Discord mode is Typesetting ([Discord presence settings](discord-presence.md#modes)).

The owner chose this design from the mode gate board on 2026-11-17 ("Lgtm", Q720–Q724; see [Decisions](#decisions)). The app doesn't change until the code card lands after the beta.4 tag.

## The window

| Region | Size | Holds |
| --- | --- | --- |
| Video, upper left | About two thirds of the height, three quarters of the width | The tool rail, the frame, [the inspector bar](#the-inspector-bar), the seek bar and the transport row |
| Styles, upper right | About a quarter of the width | See [The Styles panel](#the-styles-panel) |
| Line editor, lower left | Half the width | As in Editing |
| Grid, lower right | Half the width | As in Editing |
| Side bars | Closed when Typesetting is chosen (Q723); they open as usual | |

## The inspector bar

Q720. When a tool other than the crosshair is on, one 34-unit row sits under the frame:

- the tool's name in semibold;
- its fields, labelled in [full-screen typesetting's](full-screen-typesetting.md#the-inspector) plain words (Move: Start x and y, End x and y, Starts after, Ends after), 24 high, monospace values;
- at the trailing end, the tool's one-line hint in `muted` ("Drag the start (circle) or end (square)") and **?** (How *tool* works), which opens the same guide as #1208's.

Labels drag to change their field's value, as today. The tool rail keeps its icons with names on hover.

## Into full screen

Q721. The Video panel's header has a 24-unit **Full screen typesetting** button beside its title, with the `fullscreen` glyph. It opens #1208's mode with the current tool and Line.

## The Styles panel

Restyled. A filter field, then one row per Style: a 64 × 34 "Aa" sample over the checkered ground in the Style's font, primary and outline colours; the name in semibold, with an `accent` dot on the current Line's Style; under it in `muted` the font, size and how many Lines use it. The chosen row has the `select` fill. The header's commands (Apply to the selected lines, New style, Edit style, Duplicate, Delete) are icon buttons named on hover.

- **Missing fonts** (Q722). A Style whose font isn't installed adds a third line in `warning` with the warning glyph: "Font not installed: shown in Liberation Sans" (the fallback that renders).
- **No Styles.** The panel's empty state: "No styles in this file", **New style** and **From the catalog…** (quiet), as [Editing's empty panels](editing-layout.md#empty).

## Panel states

Q724. While HikariSub reads the system's fonts to match the file's Styles, a small card at the bottom right of the window says "Finding fonts" and "Reading 214 font files to match this file's styles · 61 %" over a 2-unit progress line. Nothing waits for it; the missing-font lines appear when it ends.

Video keeps Editing's empty, busy and error states; with no video the rail and the inspector bar are disabled.

## Discord

Presence shows **Typesetting** (`typesetting`), unchanged from #599; full-screen typesetting shows it too (Q694).

## Decisions

The owner said "Lgtm" to the mode gate board on 2026-11-17 ([#1209](https://github.com/altqx/hikari/issues/1209#issuecomment-6940698713)):

| Q | Decision |
| --- | --- |
| Q720 | The inspector bar is one row under the frame in #1208's plain words, with a one-line hint and ? |
| Q721 | A Full screen typesetting button in the Video header, beside its title |
| Q722 | A Style whose font isn't installed says so in warning colour on its row, naming the fallback |
| Q723 | Choosing Typesetting closes the side bars |
| Q724 | Finding fonts shows a small progress card at the bottom right; nothing waits |
