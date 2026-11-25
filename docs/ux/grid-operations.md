# Grid operations and History

**Accepted UX resolution, 2026-09-27.** For [Prototype Grid selection, row operations, filters, Line groups and History](https://github.com/altqx/hikari/issues/56): [Toolbar + task review](https://github.com/altqx/hikari/issues/56#issuecomment-5853245325), the existing identity/target contract, and the live review's explicit **G56-contiguity** and **J56-selected-only-join** approvals. The [reviewed boundary companion](https://github.com/altqx/hikari/blob/4b1631c2e7c2ab5c58d899e7ae098021d0e1766b/docs/prototypes/grid-boundary-review-notes.md) makes those narrow outcomes concrete. Approval selects behavior; it does not establish implementation or native parity.

## Placement, target and selection

Keep the full-width bottom Grid in [Classic](workspaces.md). Commands remain reachable through the Grid toolbar, applicable context/header menus and shortcuts. Detailed scope/change review uses a task dialog; this does **not** mandate a preview or confirmation for every simple operation. History is a persistent tool following the Editing target by default, with optional pinning and a visible Document name.

Use Document-owned stable Line IDs; keep current, selected set, anchor, marked Line and keyboard focus distinct. Reorder/filter/collapse must not transfer an identity to an unrelated row. Retain hidden selected IDs and show their count; each command uses its characterized eligibility and names the affected scope. Selection in a Protected reference permits inspection/copying, never mutation. Focus does not retarget a prepared command or pinned History.

**Range interpretation:** arrows/Page/Home/End choose destinations in displayed order. Shift membership preserves the inclusive **Document-order interval** between anchor and destination, including hidden Lines; mouse Ctrl+Shift adds that interval. A hidden anchor retains its ID and needs no destination/current fallback. Preserve the existing active-on-selection setting's distinction between moving current and moving the extent. Empty results retain selection without invalid seeking. This combines source range membership with the already accepted retained-selection rule; it does not restore legacy filtering's selection clearing. Displayed-only range membership, fallback alternatives and independent display-only sorting from the HTML are retired optional proposals, not new defaults. [Characterization and fixtures](../proposals/grid-selection-group-rules.md).

**Header text filter ([#1287](https://github.com/altqx/hikari/issues/1287)).** The Grid's panel header holds a filter field (22 high, Quiet's field). It shows only the Lines whose Text (tags ignored; also the Translation in translation mode), Actor, Effect or Style contain the typed text, ignoring case. It is a view: it makes no undo step, changes no Line and saves nothing to the file, the settings or the project. It combines with the existing filter by AND, and clearing it gives back that filter's view. The text stays across Document switches and reloads and starts empty at each launch. Lines it hides count as hidden for actions (Shift times, Select Lines, Find, paste, the "(N hidden)" count), as the existing filter's do, and "Ignore filtering in actions" covers both; Copy copies the whole selection, as it does under the existing filter. The selection stays; when the text hides the active Line, the nearest shown Line becomes active. An active Line that matched stays shown until another Line becomes active, so editing it never hides it; undo re-checks the other Lines. It has no shortcut and Ctrl+F stays Find. Esc clears it and a second Esc gives the keyboard back to the Grid; Enter or Down goes to the first shown Line. When nothing matches, the Grid says "No lines match the filter".

## Two approved command outcomes

**G56-contiguity:** valid within-group edits, including ordinary member duplication, remain allowed. If a structural edit would actually break a valid contiguous group, reject before mutation and identify the affected description/members. Offer Cancel or explicit **Remove group**, showing that it deletes the description Line and leaves surviving members ungrouped/revealed. After removal, rebuild/revalidate the original operation; do not execute it silently. C07-atomic-rejection preserves content, selection, dirty state and history on rejection. Automatic group movement, splitting, annotation loss or retaining the description as a comment are not approved alternatives. Preserve malformed imported flags as provenance/diagnostic data; this approval does not permit rejecting legacy files or silently repairing imports.

**J56-selected-only-join:** change only Join's deletion set: remove the participating selected records other than the retained record, preserving intervening unselected Lines. With A/B/C selected in `A,B,X,C`, X survives unchanged. The two-selected `A,X,B` control already preserves X in legacy. Preserve each Join variant's text contributors, original/translated roles, separator, timing, property source, survivor and eligibility; “keep last” does not acquire a new identity or timing meaning. The first retained record keeps its stable ID. This applies to the affected Join paths, not an approval of unrelated split/clipboard/group fixes.

Both corrected outcomes become defaults under the [compatibility policy](../compatibility.md), with no per-fix opt-in. G56 governs resulting group validity as well; J56 does not authorize a group-breaking Join.

## Operation and evidence obligations

The [complete UI inventory][ui] and [core inventory][core] remain the preserved baseline, including context-only commands and settings. Implement these families with their source behavior except for explicitly accepted departures; incomplete sample controls do not remove capabilities.

| Family | Required implementation / characterization |
| --- | --- |
| Navigation and state | Modifier/range/drag selection, autoscroll, current/marked navigation, reveal, numbering, line/subtitle preview, video-visible selection and existing click/seek actions. Verify hidden/disjoint selections and empty results without changing targets. |
| Columns and display | Visibility, width/fitting, layer/times/style/actor/margins/effect, both text roles, CPS/wraps, comments/state cues and tag-hidden display. Preserve values and character metrics; native shaping, scaling and accessible cues follow the painted-Grid contract. |
| Clipboard | Row/column Copy/Cut/Paste and paste-special field choice; actual serialization, row-count/mapping behavior, text roles, format conversion and command-specific hidden eligibility. No blanket adoption of the sample's repeat-one-row or count-matching rule. |
| Structural edits | Before/after insertion, video/time/frame insertion, duplicate, delete/text-clear, move/swap; preserve field inheritance, selection options and timing prerequisites. New copies receive new IDs; retained records keep theirs. Validate actual group effects under G56. |
| Split / Join / timing | Time/frame/character/word/wrap splitting; Join/keep-first/keep-last/previous/next; previous/next continuity and FPS controls. Use [accepted time semantics](../time-semantics.md), preserve exact tag/translation/property behavior and apply J56's deletion change only. Even-time sample splitting is not a new algorithm. |
| Sort and filters | Preserve all/selected stable sort keys and selected-slot permutation; style/selection/comment/Unconfirmed/untranslated predicates, invert, add/do-not-reset, filter-after-load, ignore-filtered-in-actions, hide/reveal/off and partial hidden-block reveal. Compare truth tables and persistence; no universal hidden-scope substitution. |
| Line groups | Create from eligible runs, description edit, expand/collapse, add/copy/select/remove members and groups; preserve supported legacy markers and empty-description data. Exercise adjacent groups, EOF/single members, hidden gaps, structural edits, save/load and Undo. Cross-hidden creation must not silently manufacture a malformed group; characterize its exact eligibility/result before implementing it under G56. |
| History | Document Undo/Redo, history inspection/jump/rewind and last-save navigation; identify the target and any lost redo/future entries. Preserve the initial 500-state capacity and accepted C43 exact saved-content identity through pruning/branching. Do not invent a restorable saved entry after its content leaves retained history. |

## Nested line groups

Owner-approved design for [#696](https://github.com/altqx/hikari/issues/696) ([gate](https://github.com/altqx/hikari/issues/696#issuecomment-6594520322)); Aegisub fold conversion ([#562](https://github.com/altqx/hikari/issues/562)) builds on it.

- **File format.** A Line carries one `[tree_opened]` or `[tree_closed]` marker per group it is inside, outermost first, each giving that group's state; a description adds `[tree_description]` after them, and `[bookmark]` follows as before. A group is its description plus the contiguous run of Lines with more markers than the description; the Lines with one more are its members. Flat groups keep one marker per Line, byte for byte. The loader reads stacked markers only when the Actor field starts with two or more tree markers; otherwise it keeps legacy's one-kind reading. Legacy HikariSub opens a nested file as flat groups (it removes every copy of the first kind it finds), so nesting does not survive a legacy save.
- **Commands.** Make line group: each run of shown selected Lines becomes a closed group at its shallowest depth, taking in the hidden Lines of whole groups it contains; a run that cuts a group is refused under G56. Open/close flips only that group's marker, so groups inside keep their state and closing hides everything inside. Remove deletes the description and moves everything inside up one level. Select opens the group and selects everything inside, inner descriptions included. Add lines works at the group's own level, and a selected description brings its whole group, which nests inside. Copy takes the description and everything inside; Paste re-bases pasted groups from their shallowest Line to the depth where they land, and other pasted Lines join the innermost group there. An inserted Line copies the markers of the Line it is inserted beside. The Line editor and Automation see the Actor field without markers. Each command is one Undo step.
- **G56 at every depth.** An edit is refused when it orphans a Line or moves a Line into another group without rewriting its markers. Only the group commands rewrite markers, so only they can regroup Lines.
- **Grid.** The Text column is indented 12 px per depth level; the ⊞/⊟ box stays in the number column and shows that description's own group.

## Aegisub folds to line groups

Owner-approved design for [#562](https://github.com/altqx/hikari/issues/562) ([gate](https://github.com/altqx/hikari/issues/562#issuecomment-6594431237)).

- **Offer.** Opening a file (not a reference) whose `[Aegisub Extradata]` has convertible `_aegi_folddata` folds posts the notice "This file has N Aegisub folds." with a **Convert to line groups** button. The grid's right-click menu (after Make line group), a group row's right-click menu and Convert Aegisub folds to line groups (`GLOBAL_CONVERT_FOLDS_TO_GROUPS`, no default key) run the same conversion; the menu items are disabled while nothing converts.
- **Pairing.** Delimiters pair as arch1t3cht's Aegisub FoldController does: an ender closes the innermost open fold with its id, an ender past an inner opener drops the inner fold, and delimiters that do not pair are dropped.
- **Conversion.** Each fold becomes a line group, nested as the folds are, in one Undo step ("Converting folds to line groups"). A Comment description goes before the opener, holding the opener's text without override blocks, in the opener's style. Both delimiters and the Lines between become members; a collapsed fold closes its group. Converted delimiters drop `_aegi_folddata` from their values, so their blocks no longer name it; the section's `Data:` line stays, as unused entries do ([document-model.md](../document-model.md)), and Aegisub finds no fold there.
- **Skipped.** A fold with any Line already in a line group is left as it is, entries kept. The result notice counts the folds converted and the skipped ones, dropped delimiters included.

## Shared decisions and qualification

[Edit drafts, commands and undo](https://github.com/altqx/hikari/issues/43) owns remaining draft navigation/reconciliation, command/typing/gesture grouping, Save inclusion, restored editing context, and filter/view-history/dirty treatment. Its [partial decisions](../proposals/edit-transactions.md) already accept C07 and C43. This Grid resolution does not approve the sample's view-history policy, mandatory draft-commit/rebuild sequence, silent redo truncation, or a new Lua transaction policy. Keep characterized legacy behavior where compatible; expose destructive History consequences, and resolve shared boundaries there rather than inventing per-surface defaults.

The named G45 obligations transferred from [Painted-Grid accessibility and performance](https://github.com/altqx/hikari/issues/45) to the implementation sequence retain native table/cell/focus/selection semantics, real keyboard and assistive-technology checks, shaping/scaling and representative performance qualification from [Subtitle grid](subtitle-grid.md). Complete command/clipboard/filter/group fixtures belong to implementation validation under [Testing](../testing.md); the existing [UndoHistory tests][tests] cover the helper, not whole-Grid parity. Preserve old/new evidence for G56 and J56, including the two-selected Join control. Source reading, HTML walkthroughs and C++ in-process observations do not substitute for those results.

[ui]: https://github.com/altqx/hikari/blob/6f91fd28eab4c42d2c00f848606b47585c2b6bc8/docs/research/ui-inventory.md
[core]: https://github.com/altqx/hikari/blob/5d392a10c98b97c9b2b92f4b3f9dc49cd429752c/docs/research/core-inventory.md
[tests]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/tests/UndoHistoryTests.cpp

## Reading limits

[#1213](https://github.com/altqx/hikari/issues/1213), owner Q588. One `application::ReadingLimits` (`src/application/reading_limits.h`) holds the reading-speed and line-length limits. The Grid, the Line editor, AI Shorten and the problems list ([#1227](https://github.com/altqx/hikari/issues/1227)) read it; none keeps its own numbers.

| Limit | Setting | Script Info key | Default |
| --- | --- | --- | --- |
| CPS warning | `grid.cpsWarning` | `Hikari CPS Warning` | 15 |
| CPS error | `grid.cpsError` | `Hikari CPS Error` | 30 (Aegisub 3.5) |
| Characters per line | `grid.lineLength` | `Hikari Line Length` | 43 |
| Lines | `grid.maxLines` | `Hikari Max Lines` | 2 |

- **Settings › Grid › Counting** holds your limits. **Script properties › Reading limits** gives one subtitle file its own: "Use my settings" off writes all four keys as plain integers in the same undo step as the other properties, and on removes them. Each key the file holds replaces your setting; a key that isn't a positive integer is ignored. Aegisub keeps unknown keys, so the file round-trips.
- **Grid:** the CPS cell is tinted with the Grid warning colour at Aegisub 3.5's alpha, `min((cps − warning + 1) / (error − warning + 1), 1)`, so it deepens from the warning to the error level. An error level under the warning tints fully past the warning. The Wraps cell is tinted when a line is longer than the characters-per-line limit or there are more lines than the Lines limit.
- **Line editor:** the Wraps and CPS counters show the limits, turn the warning colour past the warning level and the danger colour at the error level.
- **AI Shorten** sends the Lines over the CPS warning and asks for that speed.
- **Problems list:** the `reading` source lists each Line past a limit, unless **Show reading-limit problems** (`problems.reading`) is off ([Language features and problems](../plugins.md#language-features-and-problems)).

## Tags in the Grid

[#1213](https://github.com/altqx/hikari/issues/1213), Owner decision Q765 (#1209). `grid.tagsMode` is Show (0), Replace with symbol (1) or Hide (2), as Aegisub 3.5's Hide Overrides.

- **View › Tags in the Grid** has the three modes as radio rows (5354 Show, 5355 Replace with symbol, 5356 Hide); Aegisub's `grid/tags/show`, `grid/tags/simplify` and `grid/tags/hide` import to them.
- **Cycle tags in the Grid** (`GLOBAL_HIDE_TAGS`, 5029, Aegisub's `grid/tag/cycle_hiding`) goes Show → Replace with symbol → Hide → Show and names the new mode in the status bar for 5 seconds. It is a shortcut only, not a menu row, as in Aegisub's View menu.
- **Replace with symbol** shows each override block as `grid.tagsSwapCharacter` (☀ by default); **Hide** leaves the blocks out even when a symbol is set. Translation mode shows the tags in every mode.
- **Migration:** the old `grid.hideTags` (and legacy `GRID_HIDE_TAGS`) becomes the mode once: off → Show; on with a non-empty symbol → Replace with symbol; on with an empty symbol → Hide. A profile that already has `grid.tagsMode` keeps it.

## Current-frame marks

[#1213](https://github.com/altqx/hikari/issues/1213). Aegisub 3.5's Highlight Subtitles in Frame: on by default (`grid.markCurrentFrame`).

- **Which lines:** the ones Select lines visible on video would select: dialogue (not comments) with Start − 5 ms ≤ video time < End − 5 ms, the line editor's unsaved times counting for the active line. Rows hidden by the filter are not shown, so they are not marked.
- **Grid:** a marked row gets the theme's warning colour at 18% over its usual background. Comment, selected and selected-comment colours win, as in Aegisub, where only an otherwise plain row takes the in-frame colour.
- **Scroll bar:** each marked row draws a 2-pixel tick in the warning colour at its place in the vertical scroll bar.
- **When:** at once on a seek, a step or a pause, and at most every 100 ms while playing (the video's status timer); after every edit, tab change and when the setting changes. With no video or the toggle off, nothing is computed and the marks are cleared.
- **Toggle:** View › "Mark lines on the current frame" (5358, no default shortcut, as Aegisub has no command for it).

## Split at the cursor

[#1213](https://github.com/altqx/hikari/issues/1213), Aegisub 3.5's `split_lines`. Line editor right click › **Split here** › Estimate times / Keep times / At the video frame, and 3120–3122.

- **Where:** the active Line's Original text at the caret (the selection's start). The draft is applied first. A caret inside a tag block, `\N`, `\n` or `\h` moves past it.
- **Text:** the Line keeps the text before the caret, right-trimmed (spaces and `\N` `\n` `\h`); a copy after it gets the text after, left-trimmed, starting with one block of every tag in effect at the caret: the Line's first block (with its `\pos`, `\move`, `\org`, `\fad`, `\clip`) plus the later blocks before the caret, as **Split lines at tag blocks** carries them (`\r` resets and redundant tags dropped; only karaoke tags right after the caret stay). The first Line keeps its LineId; the selection and active Line stay, as in Aegisub.
- **Times:** Estimate gives each half its share of the duration by visible characters (graphemes outside tag blocks and drawings; `\h` is one, `\N` and `\n` none), to the nearest centisecond. Keep leaves both halves the Line's times. At the video frame uses the end time of the shown frame, clamped to the Line's first and last frame.
- **Refused:** when either half has no visible text but spaces; the status bar says "Nothing to split: both halves need text." A refusal adds no undo step; a split is one undo step, "Splitting lines".

## Split by karaoke syllables

[#1213](https://github.com/altqx/hikari/issues/1213), Aegisub 3.5's `edit/line/split/by_karaoke`. Grid right click › Split › **By karaoke syllables**, and 5359.

- **Syllables:** as Aegisub reads them: each `\k`, `\K`, `\kf` or `\ko` starts one at its tag block; text before the first one is a syllable of no duration; an empty syllable of no duration between two tags merges into the next; the last one is stretched to the Line's end, and syllables past the end are cut there. A Line with fewer than two syllables stays as it is.
- **Lines:** each shown selected Line becomes one Line per syllable, from the syllable's start to its end, in place; the first keeps the LineId. The text is the syllable's, with the Line's first block (its `\pos`, `\move`, `\org`, `\fad`, `\clip`) and every tag in effect before it merged in front, as **Split lines at tag blocks** carries them, without karaoke tags. Aegisub keeps only the tags inside the syllable.
- **Keep layout** (the checkable row under it, `grid.splitKaraokeKeepLayout`, off by default; also Settings › Grid › Splitting): each Line also gets the `\pos` (or `\move`) and `\an` where its syllable sat, measured as Split lines at tag blocks measures a tag block, and loses spaces and `\N` at its ends.
- **Selection:** the new Lines; the active Line stays when it was split, else the first new Line is active. One undo step, "Splitting lines by karaoke"; refused when no Line splits, and a refusal adds no undo step.

## Join as karaoke

[#1213](https://github.com/altqx/hikari/issues/1213), Aegisub 3.5's `edit/line/join/as_karaoke`. Grid right click › Join › **As karaoke**, and 5360.

- **Lines:** 2 to 500 shown selected Lines, as Keep first; the first keeps its LineId and fields, from the earliest start to the latest end. The others are deleted (J56).
- **Text:** in Grid order, each Line's text after `{\kN}`, N its duration in centiseconds. A gap after the syllables so far adds an empty `{\kN}` syllable first (Aegisub adds none, so its later syllables run early). An overlapping Line's syllable starts after the one before, keeping its whole duration, as in Aegisub. Karaoke tags already in a text stay after the new one, as in Aegisub. A translation is joined the same way when any Line has one.
- One undo step, "Joining lines as karaoke"; the joined Line is selected.

## Commands

The Aegisub keys come from Aegisub 3.5's `default_hotkey.json`; Import settings maps the Aegisub ids from a `hotkey.json` (`settings_import_aegisub.cpp`).

| Command | Id | Aegisub id | Aegisub default | HikariSub default | Note |
| --- | --- | --- | --- | --- | --- |
| Split at the cursor: estimate / keep / at the video frame | 3120, 3121, 3122 (Editor) | `edit/line/split/estimate`, `edit/line/split/preserve`, `edit/line/split/video` | none | Ctrl+Alt+Enter / none / none | Line editor right click › Split here. `edit/line/split/before` and `after` stay unmapped: they split every selected Line and handle a frame outside it, which Split at video time (4521) does not. Differs from Aegisub: the second half carries every tag in effect at the caret, and a refusal adds no undo step |
| Move lines up / down | 4559, 4560 (Grid) | `grid/move/up`, `grid/move/down` | Alt+Up / Alt+Down | Alt+Up / Alt+Down | Each run of selected lines passes its neighbour; groups move as a unit; one undo step |
| Split by karaoke syllables | 5359 | `edit/line/split/by_karaoke` | none | none | Grid Split submenu, with the Keep layout check (`grid.splitKaraokeKeepLayout`). Differs from Aegisub: each Line carries every tag in effect before its syllable (Aegisub keeps only the syllable's own), and a refusal adds no undo step |
| Join as karaoke | 5360 | `edit/line/join/as_karaoke` | none | none | Grid Join submenu. Differs from Aegisub: a gap becomes an empty `{\kN}` syllable (Aegisub adds none, so later syllables run early) |
| Delete text, keep tags | 5357 | `edit/clear/text` | none | none | Grid row after Delete text; `edit/clear` imports to Delete text (5157) |
| Cycle tags in the Grid | 5029 (`GLOBAL_HIDE_TAGS`) | `grid/tag/cycle_hiding` | none | none | Q765; status message names the mode |
| Tags in the Grid: Show / Replace with symbol / Hide | 5354, 5355, 5356 | `grid/tags/show`, `grid/tags/simplify`, `grid/tags/hide` | none | none | View › Tags in the Grid radio rows |
| Mark lines on the current frame | 5358 | — (preference "Highlight Subtitles in Frame") | on | on | View toggle; `grid.markCurrentFrame` |
| Select lines: Intersect; Contains / Exact / Regex | — | Select Lines dialog | — | — | Dialog choices, no command |
