# CX7 #810 gate 1b: menu bar audit for beta.4

This is a read-only audit at `main` 8dbf88f41. It covers every menu-bar menu: File, Edit, Subtitles, Video, Audio, View, Automation and Help, plus the window's own menus. The baseline is 1d122c203 (CX2) and the approved menu 28. Gate 1 (`../audit.md`, Q542 "yes to all", 1.1 option A, rule 19) holds throughout. Where a menu-bar row shares a command with a right-click or ⋯ row, it takes gate 1's name.

Mockup: `menubar/index.html`, built by `menubar/build.py`. That script reuses the gate-1 renderer from `mockup/build.py` unchanged and reads `menubar/menus-1b.js`. The shots are in `menubar/shots/NN-<id>-hcb.png` and `-hcw.png`, taken with `menubar/shots.sh` under vx11.

## Inventory

The full tables are in the three slice files. Each has file:line, commands, default keys, icons, what changed since 1d122c203 (with the PR), and its raw findings:

| Menu | File | Top-level rows | All items | Submenus | Separators | Changed since 1d122c203 |
| --- | --- | --- | --- | --- | --- | --- |
| File | [F-file-edit.md](F-file-edit.md) | 21 | 28 | 2 | 0 | +Export as… (EX1), +Import subtitles from MKV… (CX4), +Open folder…, +Close project (PJ2), +Close window (MW6), +Import lyrics… (AU23); −Import legacy settings…; "Open autosave…" renamed (#596) |
| Edit | [F-file-edit.md](F-file-edit.md) | 13 (12 with Review off) | 36 (29) | 3 (2) | 1 + 2 in Review | +Review ▸ (AI2), +Find previous, Find…/Replace… split (#597), +Insert hard space (FA7), +Sort by Text and Text without tags (FA11); −Fix minor errors |
| Subtitles | [S-subs-video-audio.md](S-subs-video-audio.md) | 16 | 34 | 2 | 1 | went from 11 to 34 items: +Timing helpers ▸ (AU19, 16 rows), +Blur and glow layers…, +Build layers… (AU18), +Typesetting graph… (AU9), +QC notes… (#577), +Timing postprocessor… (#598), +Set FPS ▸ (CX4); −Conversion ▸ |
| Video | [S-subs-video-audio.md](S-subs-video-audio.md) | 32 | 48 | 7 | 1 | went from 22 to 48 items: timecodes ×4 (FA21), Align to video… (FA14), Reload (FA9), Video details… (FA19), Tracking ▸ (AU8, FA13), Track colour…, Find fade… (AU11), Shift by frames ▸ (FA17), Centre times (FA16); renames: Reset zoom and pan, Render cost, Play or pause |
| Audio | [S-subs-video-audio.md](S-subs-video-audio.md) | 8 | 9 | 1 | 1 | +Reload audio (FA9), +Join with previous line… (FA17) |
| View | [V-view-auto-help-window.md](V-view-auto-help-window.md) | 13 | 108 (with Review and Agent on) | Layout ▸, Panels ▸ (22) and one per panel | 4 | +side bar rows (DK3), +Node effects (NE2), +Explorer (PJ4), +Lock layout (DK4); Panels ▸ gained 7 panels and plugin panels (PL4); panel ▸ rows from CX6 |
| Automation | [V-view-auto-help-window.md](V-view-auto-help-window.md) | 10 | 10 + {dyn} | {dyn} plugin ▸ | 1 | +Plugins… (PL1), +plugin ▸ commands (PL2) |
| Help | [V-view-auto-help-window.md](V-view-auto-help-window.md) | 8 | 8 | 0 | 0 | the curly apostrophe in What’s new |
| Title bar », right-click, other windows | [V-view-auto-help-window.md](V-view-auto-help-window.md) | {dyn} / OS menu | — | — | — | unchanged |

Raw findings: 49 (FE1–13, SV1–20, VW1–16). Merged and de-duplicated, they become the 8 menu sections and 6 rule sections below. Each number matches a section of the mockup.

## Menus that change visibly (mockup sections 24–31)

Every menu-bar menu changes visibly. The window's own menus don't, so there is no section 32 for them; the number 32 goes to the first rule section.

24. **File** (FE1, FE2, FE4, FE5, FE6, FE8, FE13). **PR 6.**
    - 21 rows in 7 groups:
      1. open
      2. import
      3. save
      4. autosave and session
      5. close (Remove subtitles from the editor, Close tab, Close project)
      6. Settings
      7. Close window, Exit
    - "Open…" → "Open subtitles…". "Close" → "Close tab". "Remove temporary files" → "Delete old autosaves…".
    - Last session's startup choice becomes a heading, "At startup", with three radio rows.
    - "Save subtitles using the video name" → "[x] Name new files after the video".
    - Questions 24.1–24.6.
25. **Edit** (FE3, FE5, FE9). **PR 6.**
    - Groups: undo | Review ▸ (its separator hides with it) | find and Select lines… | sort | Insert hard space.
    - Review › "Show panel" → "Show in Review panel".
    - Review ▸ takes the review-panel icon. Previous AI change takes chevron-up (36.1).
    - Questions 25.1–25.3.
26. **Subtitles** (SV6, SV7, SV11, SV12, SV14; gate 1 1.5). **PR 6.**
    - Groups: timing | typesetting | document | checks | Translation mode. QC notes joins the checks.
    - Convert Aegisub folds to line groups gets its menu-bar home after Set FPS ▸. It is greyed with a reason, not hidden.
    - The Timing helpers ▸ rows take their verb-first shortcut names.
    - The karaoke check window is titled "Check karaoke".
    - Questions 26.1–26.5.
27. **Video** (SV1, SV2, SV3, SV5, SV8, SV10, SV14, SV16, SV17; gate 1 23.1, 23.2). **PR 6.**
    - 34 rows in 6 groups, with no new Timing menu.
    - It gains [x] Full screen and Change aspect ratio….
    - [x] Zoom video becomes a check item.
    - Audio track ▸ uses radio rows, and chapter times move to the detail column.
    - Renames: "Set start/end time from video", "Go to previous/next frame", "Apply Power-Pin data…", "Copy Power-Pin data from lines". Revert motion data goes last.
    - Questions 27.1–27.7.
28. **Audio** (SV5, SV6, SV9, SV18, SV19). **PR 6.**
    - Close audio goes last in the file group.
    - "Open audio…".
    - "Set audio mark to video time".
    - "Apply the audio selection and end the previous line there", with the icon extend-previous-to-start.
    - Recently opened audio ▸ uses the shared RecentFilesMenu.
    - Questions 28.1–28.4.
29. **View** (VW1, VW2, VW3, VW4, VW11, VW13; gate 1 23.4). **PR 6.**
    - Groups: Layout ▸ (which gains Restore the previous layout) | side bars | panels | Node effects | tag display.
    - Check items: Left side bar, Right side bar, Explorer, Log, and "Hide tags in the Grid".
    - Reset side bars takes restore-default.
    - Panels ▸ is grouped in display order.
    - Rows greyed by the layout lock show "Layout is locked".
    - Questions 29.1–29.5.
30. **Automation** (VW5, VW6, VW7, VW8; gate 1 23.3). **PR 6.**
    - Groups: load | run again | Show panel, Plugins | plugin ▸ | macros. A separator shows only when rows follow it.
    - Refresh autoload scripts takes the refresh icon and is greyed during a run.
    - "Plugins" loses its "…".
    - "Run the last macro again". Repeat last run names its macro in the detail column.
    - Macro and plugin shortcuts show in the key column.
    - Questions 30.1–30.3.
31. **Help** (VW8, VW14, VW16). **PR 6.**
    - Groups: learn | support | updates | about.
    - "Report an issue…".
    - "Getting &started".
    - Question 31.1.

## Rules across the menu bar (mockup section 32, tables only)

32. **Shortcut names follow the menu row** (FE7, FE8, FE9, SV7, SV15, VW9, plus the renames from 24–31). Thirty-five commands are listed. FE7 and SV15 add names for 4 unlisted commands: GLOBAL_LOAD_EXTERNAL_SESSION, GLOBAL_SAVE_EXTERNAL_SESSION, GLOBAL_DELETE_TEMPORARY_FILES and GLOBAL_OPEN_DUMMY_VIDEO. These are the CX2 1.3 precedent. **PR 6**, except "Open subtitles…", "Open video…" and "Change aspect ratio…", which gate 1 8.3 already gave to **PR 2**. Questions 32.1, 32.2.
33. **Ellipsis** (FE5, SV6, VW8; gate 1 rule 19):
    - adds "…" to Select lines, Save translation, the two session rows, Resample subtitles, Open/Save keyframes and timecodes, Open dummy video and Open audio;
    - removes it from QC notes, Video details and Plugins;
    - Report an issue… is a question of its own.
    - **PR 6.** Questions 33.1, 33.2.
34. **Status-bar help** (FE11, SV13, VW10): one third-person clause, verb first, and help added where a row has none. **PR 6.** Question 34.1.
35. **Mnemonics** (FE10, SV20, VW16): one letter per row in each menu, and rare rows go without. **PR 6.** Question 35.1.
36. **One icon, one meaning** (FE9, SV14, VW4, VW5).
    - Ten new roles, each drawn as a stand-in:
      - `chevron-up`
      - `timing-helpers`, `shift-frames-to-video`, `tracking` (the three submenus)
      - `start-frame-earlier`, `start-frame-later`, `end-frame-earlier`, `end-frame-later`
      - `audio-to-selection-start`, `audio-to-selection-end` (for the audio » rows, area B)
    - Four rows switch to existing icons: restore-default, refresh, review-panel and extend-previous-to-start.
    - **PR 5** draws the roles. PR 6 uses stand-ins until then, and PR 2 uses them for the audio » rows. Questions 36.1–36.4.
37. **Greyed rows say why** (VW13, SV13's dead Tracking help; this is gate 1 1.6 applied to the menu bar). **PR 6.** Question 37.1.
38. **Where gate 1's rule 23 is built.** The four held items (Audio track ▸, chapter times, macro and plugin shortcuts, Left/Right side bar) touch only menu-bar rows. Recommend PR 6, so the area PRs don't touch Main.qml's menu bar. Question 38.1.

## Gate-1 reconciliations

Each of these rows shares a command or a name with a right-click or ⋯ row:

| Menu-bar row | Gate 1 source | Result |
| --- | --- | --- |
| File › "Open…" | 8.3 (shortcut "Open subtitles…"); the video menus | "Open subtitles…" |
| Video › new Change aspect ratio… | 8.3 | the same name with "…" |
| Video › Streams ▸, Chapters ▸ | 23.1, 23.2; CX5's video menu | "Audio track ▸" with radio rows; time in the detail column |
| Video › Unload video | approved 10, kept by gate 1 section 8 | kept; SV8's "Close video" not taken (27.6) |
| Subtitles › Check spelling | 2.1 ("Check spelling as you type" is the toggle) | stays the Spellchecker command name |
| Subtitles › Typesetting graph…, Blur and glow layers…, Build layers with the last values | 1.2 | names and "…" unchanged |
| Subtitles › Convert Aegisub folds to line groups | 1.5 | new menu-bar home |
| Disabled menu-bar rows | 1.6 | reason in place of the key (37) |
| Audio › Set audio marker to video time | 9.1 ("mark") | "Set audio mark to video time" |
| File › Remove temporary files | rule 20 (Delete for data) | "Delete old autosaves…" |
| View › Layout ▸ | 15.3 (radios), 19.3 ("Manage layouts") | the same; it is the switcher's menu |
| View › Toggle left/right side bar | 23.4 | "Left side bar", "Right side bar" check items |
| Automation macros and plugin commands | 23.3 | the key column shows the binding |
| Edit › Review › Show panel | the Grid row's "Show in Review panel" (AI2) | "Show in Review panel" |
| Automation › Refresh autoload scripts | the Automation panel ⋯ row | refresh icon and the same enabled state |
| AI actions, Check spelling as you type, Add QC note…, Delete all notes…, More options, About %1 | 1.1/16, 2.1, 1.3, 6.2, rule 17, 7.3 | **no menu-bar row**: `menu_bar.txt` has none of these commands, so nothing to reconcile. Subtitles › QC notes is the panel command, not Add QC note… |

## PR mapping

| PR | Sections and items | Fixes |
| --- | --- | --- |
| PR 1 Grid and Line editor | none | 0 |
| PR 2 video, audio, node effects | the shortcut names "Open subtitles…", "Open video…", "Change aspect ratio…" (already gate 1 8.3); uses `audio-to-selection-*` in the audio » | 0 new |
| PR 3 panels, tabs, Explorer, fonts | VW15: a doc note that Show stays enabled on a shown panel | 1 |
| PR 4 automation and AI | none. VW7 touches the Automation panel strip tooltip, but it is one string, so it stays with PR 6 | 0 |
| PR 5 icons | 36: ten new roles (FE9's chevron-up, SV14's nine) | 2 ids, 10 roles |
| **PR 6 menu bar (new)** | 24–31, 32–35, 37, 38 and the non-visible items below | 47 ids (FE1–13, SV1–20 but SV14, VW1–16 but VW15) + 4 rule-23 items |

**PR 6 is needed.** No area PR owns the File, Edit, Subtitles, Video-structure, Audio or Help rows, the shortcut names or the mnemonics. Folding them into PRs 1–4 would add menu-bar edits and `menu_bar.txt` churn to PRs that are already in progress. PR 6 regenerates `menu_bar.txt` and the docs export, carries TR1 for the four catalogues, and adds a "What CX7 built: menu bar" note to docs/ux/context-menus.md. It should merge after PRs 2 and 5, or rebase on them, for the shared names and the icons.

## Owner questions

There are 46, each answerable with "yes":
- 24.1–6, 25.1–3, 26.1–5, 27.1–7, 28.1–4, 29.1–5, 30.1–3 and 31.1;
- 32.1–2, 33.1–2, 34.1, 35.1, 36.1–4, 37.1 and 38.1.

The uncertain findings are explicit questions with a recommendation, not silent changes:

| Finding | Uncertainty | Question and recommendation |
| --- | --- | --- |
| SV1 | a new top-level Timing menu, as Aegisub has | 27.1: no new menu, just group Video |
| SV6 | whether Typesetting graph… keeps its "…" | 26.5: keep it, as in gate 1 1.2 |
| SV8 | "Unload video" or "Close video" | 27.6: keep "Unload video" |
| SV16 | "Power-Pin" or "Perspective tracking" as the feature name | 27.7: "Power-Pin data" |
| SV19 | the long new name | 28.3: as drawn |
| SV20, FE10, VW16 | whether Qt Quick cycles duplicate mnemonics, and how much keyboard menu use to expect | 35.1 |
| FE7, SV15 | whether legacy left these commands unnamed on purpose | 32.2: name them |
| FE13 | renaming the check item and its shortcut name | 24.6: rename both, keep the id |
| VW5 | what a refresh during a run does | 30.2: grey it during a run, as the panel does |
| VW7 | the AD2 names may be the owner's | 30.3 |
| VW8 | whether Report an issue is a form or a window | 33.2: add "…" |
| VW11 | registry order or grouped order | 29.5: grouped, in display order only |
| — | where rule 23 gets built | 38.1: PR 6 |

## No visible change (straight into PR 6, no approval needed)

- **SV4:** give Video › Unload video `command: "VIDEO_DELETE_FILE"`, so a bound key shows and `menu_bar.txt` lists the command.
- **FE12, SV18:** build the recent-file tooltip and help as one string, `qsTr("Opens %1")` or `qsTr("Open %1")` (same pattern as C16, which is PR 3's). Use RecentFilesMenu for audio, with `revealRecent`.
- **VW12:** `menu_bar.txt` outlines View › Panels one level deeper for one panel, and marks hidden rows "(hidden)".
- **VW15 (PR 3):** say in "What CX7 built" that Show stays enabled on a shown panel and raises it.
- **Checked and clean:**
  - the title bar » (More menus);
  - the title bar right-click (the OS window menu);
  - secondary windows (the same menu bar);
  - no native or macOS menu bar.
