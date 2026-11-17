# Export as and Collect fonts in Quiet

How Export as and the font collector look in Quiet ([#1205](https://github.com/altqx/hikari/issues/1205)). What they write is [export.md](../export.md)'s and [fonts.md](../fonts.md)'s; this document says how they look, where the report goes and what a run looks like with its window closed. While an export, Collect fonts or a muxing run is going, Discord shows Exporting ([Discord presence settings](discord-presence.md#modes)).

The owner chose this design from the mode gate board on 2026-11-17 ("Batch 4 lgtm", Q751–Q755; see [Decisions](#decisions)). The app doesn't change until the code card lands.

## Export as

A window, radius 6, 760 wide. The form keeps today's rows in a two-column grid, labels in `muted` on the left: Format, Lines (All, Selected, Shown as a segmented group with counts), Save to (the path in monospace and **Browse…**, quiet), Options (the format's check boxes).

**The report** (Q751). After Export, the form stays and the report sits under it in a panel with a 1-unit `line` boundary, radius 6:

- a header row on `raised`: the `notice-warning` glyph and "Saved. 9 lines lost something SRT can't carry." in semibold, and **Copy list** (quiet);
- one group per [kind](../export.md#the-report) that occurred, headed "Position · 2" in semibold, then one row per Line: its Grid row in `muted` monospace and what was lost;
- choosing a row selects the Line in the Grid; the chosen row has the `select` fill.

The footer says "Choose a line to select it in the Grid." in `muted` with **Show the file** (quiet) and **Close** (primary). When nothing was lost, the report is one line on `success` at 14 %: "Saved sprite-fright.en.srt · 412 lines · nothing lost".

A write that fails shows, under the form, the `notice-warning` glyph in `danger`, the reason ("Couldn't write sprite-fright.en.srt: the folder is read-only."), **Choose another folder…** and **Details**; Export stays.

## Collect fonts

Q754. The window and its command are named **Collect fonts** (today "Font collector"), as [text-style.md](text-style.md) names a window after the command that opens it. Action keeps **Check fonts | Copy to a folder | Zip archive** and Save to with **Browse…**. The footer holds **Close** and **Start ▾** (primary), whose menu has **Start on all tabs**.

**The list** (Q753) replaces the log. A panel with a 1-unit `line` boundary, radius 6, with a header row on `raised` (Font and style, File, Lines) and one row per font and style:

| Status | Glyph | File column |
| --- | --- | --- |
| Found | `karaoke-check` in `success` | The file name in `muted` monospace |
| Missing | `notice-warning` in `danger` | "Not on this computer" in `danger` |
| Made by the renderer | `notice-warning` in `warning` | The file and face, then "simulated bold" or "simulated italic", in `warning` |

The family is in semibold, its style after it in `muted`; Lines is how many Lines use it. Under the rows, in `muted`, a summary: "4 fonts · 1 missing · 1 made bold by the renderer. Copy to a folder copies 3 files." Before Start the list says "Start lists the fonts the subtitles use, and whether this computer has each one."

## Running

Q752. While an export or a collection runs, its window shows a 2-unit `accent` progress line and what it's doing ("Writing sprite-fright.en.srt · 214 of 412 lines", "Checking · 2 of 4 fonts"; rows arrive as they're found), with **Stop** (quiet) and "Closing the window keeps it going." in `muted`; Export and Start are disabled.

Closing the window leaves a run card at the bottom right of the main window: `panel`, radius 6, 340 wide, with the popup shadow; the mode's glyph and "Exporting" (or "Collecting fonts") in semibold and a hide button; what it's doing in `muted`; the progress line; **Stop** and **Open** (quiet), which reopens the window. The card goes when the run ends; the window's result is there when it's opened.

## Muxing

Q755. HikariSub has no muxing run today, so nothing is drawn for it. When one exists (a plugin or a later card), it uses the same run card and Discord shows Exporting.

## Discord

Presence shows **Exporting** (`exporting`). Its art is the `export` glyph on #C5CAE9 ([Art](discord-presence.md#art)).

## Decisions

The owner said "Batch 4 lgtm" to the mode gate board on 2026-11-17 ([#1209](https://github.com/altqx/hikari/issues/1209#issuecomment-6943248299)):

| Q | Decision |
| --- | --- |
| Q751 | Export as keeps its form and shows the report under it, grouped by kind; nothing lost is one success line |
| Q752 | Runs show progress in their window; closing it leaves a run card with Stop and Open |
| Q753 | Collect fonts lists one row per font with its status, file and Lines, and a summary |
| Q754 | The window is named Collect fonts after its command; Close and Start ▾ with Start on all tabs inside |
| Q755 | No muxing is drawn until a muxing run exists; then it uses the run card |
