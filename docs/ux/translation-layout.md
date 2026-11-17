# Translation layout

The layout that carries Translation mode, in Quiet ([#1205](https://github.com/altqx/hikari/issues/1205)). The arrangement, the stacked fields and the reference tray are [translation-comparison.md](translation-comparison.md)'s; the question and the Original text panel's commands are [workspaces.md](workspaces.md)'s (TM1). This document says how they look and what the panels show while they work. Its Discord mode is Translation ([Discord presence settings](discord-presence.md#modes)).

The owner chose this design from the mode gate board on 2026-11-17 ("Lgtm", Q715–Q719; see [Decisions](#decisions)). The app doesn't change until the code card lands after the beta.4 tag.

## The window

The Editing arrangement ([editing-layout.md](editing-layout.md)) with Translation mode on:

| Region | Holds |
| --- | --- |
| Video, upper left | As in Editing |
| Original text, upper right | The Line's Original text, read-only, on `bg` with a 1-unit `line` boundary so it reads as not editable; the panel's tools in its header |
| Line editor, under it | Titled "Line editor · Translated"; its text field is the Translated text |
| Grid, the full width | See [The Grid](#the-grid) |
| Right side bar | Find, as the preset opens it |
| Reference tray, under the Grid, when open | See [Reference tray](#reference-tray) |

An untranslated Line's field shows the hint "Type the translation. Ctrl+Shift+V pastes the original."

## The question

Q715. Choosing Translation for a Document that can take the mode and has it off asks, in a popover hanging from the layout switcher (not a dialog):

- title, semibold: "Turn on Translation mode for this file?";
- in `muted`: "Each line keeps its text as the Original text and gets a Translated text beside it. The file saves both until you turn the mode off.";
- **Not now** (quiet) and **Turn on** (primary, the default).

The popover is `panel`, radius 6, 360 wide, with the popup shadow (a 1-unit `line` boundary in high contrast). Esc is Not now. When it is asked and what each answer does stay as [workspaces.md](workspaces.md) says.

## The Original text panel

Q718. The header holds the panel's five commands as 24-unit icon buttons, named on hover: Paste all, Paste the selected text, Comment out original, Not confirmed (a toggle), Tags from original. They fold into the ⋯ menu when the panel is narrow.

## The Grid

- **Text** (Q716). One Text column holds the Translated text. An untranslated Line shows its Original text there in `muted` italic, so the row is never blank and reads as not done.
- **Progress** (Q717). While Translation mode is on, the Grid's header shows "38 of 412 lines translated" in `muted` after the filter field.

## Panel states

Q719. AI translation reports in the Original text panel, as [Editing's panel states](editing-layout.md#panel-states) do:

| State | Shown under the Original text header |
| --- | --- |
| Running | A 2-unit `accent` progress line and "AI translation · 24 of 60 lines" with **Stop** (quiet) |
| Failed | The `notice-warning` glyph in `danger`, one line with the reason ("AI translation stopped: the provider refused the key."), the next step (**Providers…**) and **Details** (the Log entry) |

Video and Audio keep Editing's empty, busy and error states.

## Reference tray

Restyled only. A panel under the Grid titled "Reference · *file name*", with **Independent | Linked** as a segmented group in its header. Rows are read-only: the Line number and start time in `muted` monospace, then the text. Under them in `muted`: "Read-only. Independent: it moves on its own; Linked follows the editing line by time."

## Discord

Presence shows **Translation** (`translation`), unchanged from #599.

## Decisions

The owner said "Lgtm" to the mode gate board on 2026-11-17 ([#1209](https://github.com/altqx/hikari/issues/1209#issuecomment-6940698713)):

| Q | Decision |
| --- | --- |
| Q715 | The question is a popover under the layout switcher with Turn on (default) and Not now, in today's words |
| Q716 | One Text column holds the Translated text; an untranslated Line shows its original in muted italic |
| Q717 | "N of M lines translated" in the Grid's header while Translation mode is on |
| Q718 | The Original text panel's five commands are icon buttons named on hover in its header |
| Q719 | AI translation's progress and failures show in the Original text panel |
