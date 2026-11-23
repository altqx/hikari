# Export as and Collect fonts in Quiet

How Export as and the font collector look in Quiet ([#1205](https://github.com/altqx/hikari/issues/1205)). What they write is [export.md](../export.md)'s and [fonts.md](../fonts.md)'s; this document says how they look, where the report goes and what a run looks like with its window closed. While an export, Collect fonts or a muxing run is going, Discord shows Exporting ([Discord presence settings](discord-presence.md#modes)).

The owner chose this design from the mode gate board on 2026-11-17 ("Batch 4 lgtm", Q751–Q755) and its Before export steps from their own board on 2026-11-18 (Q767–Q772); see [Decisions](#decisions). The app doesn't change until the code card lands.

## Export as

A window, radius 6, 760 wide. The form keeps today's rows in a two-column grid, labels in `muted` on the left: Format, Lines (All, Selected, Shown as a segmented group with counts), Save to (the path in monospace and **Browse…**, quiet), Options (the format's check boxes).

**The report** (Q751). After Export, the form stays and the report sits under it in a panel with a 1-unit `line` boundary, radius 6:

- a header row on `raised`: the `notice-warning` glyph and "Saved. 9 lines lost something SRT can't carry." in semibold, and **Copy list** (quiet);
- one group per [kind](../export.md#the-report) that occurred, headed "Position · 2" in semibold, then one row per Line: its Grid row in `muted` monospace and what was lost;
- choosing a row selects the Line in the Grid; the chosen row has the `select` fill.

The footer says "Choose a line to select it in the Grid." in `muted` with **Show the file** (quiet) and **Close** (primary). When nothing was lost, the report is one line on `success` at 14 %: "Saved sprite-fright.en.srt · 412 lines · nothing lost".

A write that fails shows, under the form, the `notice-warning` glyph in `danger`, the reason ("Couldn't write sprite-fright.en.srt: the folder is read-only."), **Choose another folder…** and **Details**; Export stays.

## Before export

Q767–Q772 ([#1216](https://github.com/altqx/hikari/issues/1216) PM-4). Export as can run steps on a copy of the Document before it writes, as Aegisub's export filters do. The Document never changes.

**The row** (Q767). Under Options, a **Before export** row holds the recipe combo, its **Project** or **Yours** chip, "changed" in `muted` when the steps differ from the saved recipe, and **Add steps** (no recipe) or **Show steps** / **Hide steps** (quiet). The default is **No steps**, and the row then says "Writes the document as it is." in `muted`.

**The steps** open under the row in a panel with a 1-unit `line` boundary, radius 6. Its header on `raised` says "Run in this order on a copy; the Document doesn't change." in `muted`, with **Save recipe…** as a quiet split button. The steps are:

| Step | Command it runs | Settings |
| --- | --- | --- |
| Apply karaoke template | The built-in karaoke templater (`karaoke.apply_template`) | None beyond the file's templates |
| Remove tags | **Remove tags…** with Clean Tags rules, whole file ([AU4](../compatibility-decisions.md#au4-remove-tags)) | Remove tags' choices |
| Transform frame rate | Aegisub's Transform Framerate | Input frame rate with **From video**; Output Variable (the loaded timecodes) or Constant; Reverse the transformation |
| Set unknown styles to Default | Aegisub's Fix Styles | None |
| A Lua export filter | The script's `register_filter` function | The script's config dialog |

**Rows** (Q768). One row per step: a grip, an on box, the name (semibold when on, `muted` when off) and a one-line summary of its settings in `muted`, a **Built-in** or **Lua · script.lua** chip, a settings button and ⋯ (Move up, Move down, Move to top, Reset settings). The settings open inline under the step, one step at a time; the open row has the `select` fill. HikariSub draws a Lua filter's config dialog inline, headed "Settings from sign-fx.lua" in `muted`.

**Order** (Q769). A new list follows Aegisub's priorities: Apply karaoke template and Lua filters by their priority (2000 for templaters), then Transform frame rate (1000), Remove tags (0) and Set unknown styles to Default (−5000). Dragging, Alt+↑/↓ or ⋯ reorders, and the recipe keeps the order.

**Recipes** (Q770). A recipe holds the steps, their order and their settings, not the format. Recipes are presets with [ADR 0030](../adr/0030-open-a-folder-as-a-project.md)'s tiers:

- the menu lists **No steps**, then "This project", then "Yours", then **From this file (Aegisub)**, **Save recipe…** and **Manage recipes…**;
- a project recipe hides your recipe of the same name, which shows struck through in `muted`;
- **Save recipe…** asks for a name and **This project | Yours**, This project by default in a Project; a name clash says so and the button reads **Replace**;
- a file's Aegisub `Export Filters` line shows as **From this file (Aegisub)**, and the last steps used are written back to it on the next save.

**A missing script** (Q772). A step whose Lua script isn't loaded, or whose Project script isn't trusted, stays in the list with its on box off and "sign-fx.lua isn't loaded · Export skips it" in `warning`. The footer says "1 step won't run: its script isn't loaded." Export stays on.

**Running.** The progress line names the step: "Before export · Transform frame rate · step 3 of 3", then the write (Q752). After a clean export, one `muted` line under Q751's success line names the steps that ran: "Ran before export: Apply karaoke template (42 lines made), Sign blur pass, Transform frame rate."

**A failing step** (Q771). The export stops and nothing is written. Under the form, a panel with a header on `raised`: the `notice-error` glyph in `danger` and "Not saved. “Sign blur pass” failed on line 58." in semibold, with **Turn it off and export** and **Details** (the traceback), both quiet. Each step is a row: done with `karaoke-check` in `success` and what it did, failed with `notice-warning` in `danger` and the error in `danger` monospace, or "didn't run" in `muted`; the write is the last row. The footer says "Nothing was written, and the document is unchanged." with **Close** and **Export**.

**Lua steps** are the loaded scripts' `register_filter` filters ([C06-register-filter](../compatibility-decisions.md#c06-register-filter)).

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

The owner said "yes to all lgtm" to the Before export board on 2026-11-18 ([#1209](https://github.com/altqx/hikari/issues/1209#issuecomment-6957457130)); the step names Remove tags and Set unknown styles to Default were approved with it:

| Q | Decision |
| --- | --- |
| Q767 | A Before export row in Export as; the steps open under it; No steps is the default |
| Q768 | One row per step with grip, on box, summary, source chip, settings and ⋯; settings inline, Lua dialogs drawn by HikariSub |
| Q769 | New lists follow Aegisub's priorities; drag, Alt+↑/↓ or ⋯ reorders; the recipe keeps the order |
| Q770 | Recipes hold steps, order and settings with ADR 0030's Project and Yours tiers; the file's Export Filters line reads as a recipe and is written back on save |
| Q771 | A failing step stops the export; the report lists each step's outcome with Turn it off and export and Details |
| Q772 | A step whose script isn't loaded or trusted stays, marked; Export skips it and says so |
