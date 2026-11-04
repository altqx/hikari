# Aegisub feature audit: arch1t3cht's scripts and fork, and TypesettingTools' Aegisub

This note is Step 1 of FA1 ([#558](https://github.com/altqx/hikari/issues/558)); the card that writes it is FA2 ([#559](https://github.com/altqx/hikari/issues/559)). It covers three sources:

- every script and module in arch1t3cht's scripts repository;
- every feature of arch1t3cht's Aegisub fork;
- every TypesettingTools Aegisub change from 3.4.0 to 3.5.1.

For each one it records what it does and who it's for, HikariSub's state today, a proposal, the licence and whether code can be reused, a rough size, and dependencies. Step 2, the owner grilling, took place on 2026-10-31 (CX1/FA1). It used the catalogue posted on [#558](https://github.com/altqx/hikari/issues/558#issuecomment-6541750771), and its picks became cards FA3–FA20 ([#560](https://github.com/altqx/hikari/issues/560)–[#577](https://github.com/altqx/hikari/issues/577)). The "Card" column links each row to its card. A row without a card was not picked.

## Sources and method

Primary sources only. Each repository was cloned shallowly into `/tmp/fa1-src/` and read as data; nothing from it was run.

| Source | Revision read | What was read |
|---|---|---|
| [TypesettingTools/arch1t3cht-Aegisub-Scripts][scr] | `main` at `5c3c782` (2026-08-22) | `README.md`, `LICENSE`, `DependencyControl.json`, every file in `macros/` and `modules/arch/`, and the headers of the `doc/` files |
| [arch1t3cht/Aegisub][fork] | `feature` at `2cdf0043` (2026-11-01, "README: Add deprecation note"); `migration06` at `aff013f4`; tag `v3.2.2` | `README.md`; `automation/v4-docs/{gui,get-frame,misc}.txt`; the `src/auto4_lua.cpp` registrations at `v3.2.2`, `feature` and `migration06`; `CMD_NAME` lists in `src/command/*.cpp`; file licence headers; `git log tt/master..origin/migration06` (46 commits); release notes `feature_01`–`feature_12`, `lifesupport01`, `migration01-01`–`migration06-01` (GitHub releases API) |
| [TypesettingTools/Aegisub][tt] | `master` at `d8ad830` (2026-10-21) | `LICENCE`, `README.md`, `src/auto4_lua.cpp`, `src/command/*.cpp`, `automation/`; release notes v3.4.0–v3.5.1; the aegisub.org changelogs [3.4.1][cl341], [3.5.0][cl350] and [3.5.1][cl351] |
| Re-check of the unverified items (2026-11-02) | HikariSub `main` at `6b1285171`; the same three clones; the GitHub licence API for Zahuczky, The0x539, Akatmks and TypesettingTools/CoffeeFlux-Aegisub-Scripts; the ffmpeg port 7.1.2#5 that `vcpkg.json` pins (git-tree `bf414f1e` in the vcpkg checkout at `ee6a47dac`); FFmpeg `n7.1.2` `libavformat/matroskadec.c` | The files cited in each row |
| HikariSub | `main` at `3c9f13341` (2026-11-01) | `src/` (by grep), `docs/visual-tools.md`, `docs/ux/grid-operations.md`, `docs/editor-shortcuts.md`, `docs/adr/`, `tests/fixtures/automation-thirdparty/` (`manifest.json`, `expected.json`, `runs.json`), `Automation/automation/` |

The fork's GitHub wiki is empty, so it was not a source. The catalogue on #558 was used as a checklist, and each of its claims was checked again against the revisions above. Where a claim could not be checked, the row says **unverified**.

**HikariSub status words:** **present**, **partial**, **missing**, **different** (deliberately different), **n/a**, **unverified**.

**Proposals:**

- **run as is:** the script works in our Automation 4 host; recommend it, through FA6's list.
- **adapt:** a native feature replaces the script or fork feature, with a better UI.
- **better:** HikariSub does it better natively; the row says how.
- **skip:** the row says why.
- **present:** nothing to do.

**Licences and reuse.** HikariSub is GPL-3.0 (`LICENSE`), and every licence below except "none" is GPL-3.0-compatible.

| Code | Licence | Reuse |
|---|---|---|
| Scripts repo | MIT, © 2022 arch1t3cht ([LICENSE][scr-lic]) | Allowed with the notice |
| arch1t3cht's own fork files: `fold_controller.cpp`, `visual_tool_perspective.cpp`, `video_provider_bestsource.cpp`, `video_provider_vs.cpp`, `vapoursynth_wrap.cpp` | ISC headers | Allowed with the notice. HikariSub already keeps the ISC notice for the perspective code in `src/application/visual_perspective.cpp` (`docs/visual-tools.md` §VT3). |
| `dialog_align.cpp` | BSD-3, © 2019 Charlie Jiang | Allowed with the notice |
| `audio_player_xaudio2.cpp` | BSD-3, © 2019 Qirui Wang | Allowed with the notice |
| `auto4_lua.cpp` and the rest of Aegisub | BSD-3 ([TT LICENCE][tt-lic]) | Allowed with the notice. The official Windows and macOS *builds* are GPLv2 because they include fftw3 ([TT README][tt-readme] "License"). |

Sizes: **S** is up to about 2 days, **M** about a week, **L** more. Dependencies name the other FA cards and wave-8 work: HA1 [#411](https://github.com/altqx/hikari/issues/411) (Hikari API), PL0–PL9 [#412](https://github.com/altqx/hikari/issues/412)– (plugins) and NE0– [#422](https://github.com/altqx/hikari/issues/422) (node effects).

## Summary

The repository status these findings rest on:

- **The fork is archived.** Its `feature` branch is discontinued; the README says so and points to TypesettingTools ([fork README][fork-readme]).
- **The migration branch is what remains.** `migration06-01` is Aegisub 3.5.1 plus the features not yet upstream: folding, the perspective tool, the Lua API additions, Windows dark mode, the shape-handle size, Shift+wheel vertical audio zoom, mixin highlighting and the dummy-video limit ([migration01-01][mig01]; `git log tt/master..origin/migration06`).
- **The extra providers are gone.** BestSource, VapourSynth, AviSynth on Linux and XAudio2 were dropped from the migration releases ([migration01-01][mig01]).

What follows for HikariSub:

- **Most of what was audited is already present.** That includes the perspective tool (VT3 [#273](https://github.com/altqx/hikari/issues/273)), the fork's `gui.*`, `get_frame`, `get_audio_selection` and `set_status_text`, high-DPI, a theme system with dark mode, the colour picker on Wayland, and the prompt before loading a file's linked scripts (TT 3.5.0's CVE fix).
- **The biggest gap is extradata.** HikariSub drops `[Aegisub Extradata]` on load and gives scripts an empty `line.extra` (`src/helpers/lua/lua_helper.cpp` L1028–1029). That loses folds and perspective quads from Aegisub users' files. It also breaks five corpus or arch scripts: ConvertFolds, DerivePerspTrack's outer-quad mode, PerspectiveMotion's "Add quad to extradata", `phos.FoldOperations` and `myaa.MergeScripts`. FA3 and FA4 fix it.
- **Small editor and video items close the 3.5 parity gap.** These are FA7–FA11.
- **Five arch scripts become native features with a better UI.** These are FA13–FA18 and FA20.
- **Six arch scripts are recommended unchanged** through FA6.
- **Two uncarded defects turned up** while the unverified items were checked on 2026-11-02:
  - Resample overwrites LayoutRes with the new PlayRes, which changes `\blur` and 3D signs (§3).
  - FFmpeg is built without zlib, so zlib-compressed MKV subtitle tracks cannot be read (§2b).

  Both need cards.

Row counts per proposal, over the 104 table rows of §1–§4:

| Proposal | §1 scripts | §2 fork | §3 TT 3.4–3.5 | §4 Lua API | Total |
|---|---|---|---|---|---|
| run as is | 9 | — | — | — | 9 |
| adapt | 5 | 15 | 12 | 5 | 37 |
| better | 1 | 2 | 1 | — | 4 |
| skip | 2 | 10 | 11 | 1 | 24 |
| present | — | 14 | 8 | 8 | 30 |

The ranked list of the adapt and better rows is in §7. It splits multi-feature rows and merges the Lua API rows into card-sized items.

---

## 1. arch1t3cht-Aegisub-Scripts (14 macros, 3 modules)

**Corpus status.** HikariSub's third-party corpus stages 7 of the 14 macros at `5c3c782`: CenterTimes, ConvertFolds, FocusLines, Line2Fbf, NoteBrowser, RWTools and SplitSections. All three modules are staged too. `expected.json` pins each of the 7 as `loaded: true`, with its macro list, on Ubuntu and in the Windows VM. `runs.json` has no arch macro run (`tests/fixtures/automation-thirdparty/`).

**Bundled.** HikariSub bundles DependencyControl v0.9.1 and its Toolbox, Aegisub-Motion (`a-mo.*`), `l0.*` (Functional, ASSFoundation) and the `aegisub.*` include libraries (`Automation/automation/Include/`, `Automation/automation/Autoload/`; DC1 [#361](https://github.com/altqx/hikari/issues/361)). The `arch.*` modules are not bundled; DepCtrl fetches them on demand.

**DepCtrl feed.** The scripts repo's `DependencyControl.json` lists 12 macros and 3 modules. Center Times and Timing Binds are not in the feed; the README says they "aren't in DependencyControl" ([README][scr-readme] "Scripts for Timing").

The APIs listed are what each script calls beyond plain line access (grep of `aegisub.*`, `.extra`, `io.popen` and `lfs.*` in each file).

**Since FA4 ([#561](https://github.com/altqx/hikari/issues/561)):** all 14 macros scripts are in the third-party corpus, each with a recorded run (see [automation](../automation.md)). The "not in the corpus" notes below describe the audit's state. AegisubChain's recorded run is Manage Chains, because its Configure fails with DependencyControl v0.9.1 here and in Aegisub 3.5.1.

| Script (version) | What it does · audience | APIs it needs | HikariSub today | Proposal | Licence | Size | Deps | Card |
|---|---|---|---|---|---|---|---|---|
| **AegisubChain** `macros/arch.AegisubChain.moon` (0.4.1) | Records "chains" of other macros and plays them back, with one dialog or none. It works as a VM that loads other scripts and hooks their `aegisub.dialog.display` ([README][scr-readme] "AegisubChain"; `doc/aegisubchain.md`). · Power users of every kind. | `dialog.display` (19 calls), `register_macro`, `decode_path`, `lfs.dir`/`chdir`/`currentdir`, `karaskel`, DepCtrl (required) | **partial (different):** AD2's "Repeat last run" and dialog memory/presets ([#311](https://github.com/altqx/hikari/issues/311)) replay one macro. The script itself is not in the corpus. It loads the other scripts inside its own Lua state (`io.open`, `loadstring` or `moonscript.base`'s `loadstring`, then it swaps `_G` entries, L186–191 and L390–398), so our one-helper-per-script host ([ADR 0006](../adr/0006-isolated-lua-automation-host.md)) gives it everything it calls, all in one helper. One static mismatch: it scans `?user/automation/autoload/` and `?data/automation/autoload/` (L68), but our scripts live in `automation/Autoload` (`automationFolders`, `src/application/automation_services.cpp` L102–119). On a case-sensitive file system it therefore finds only the user's lowercase folder, unless its `path` setting is changed. Not run. | **skip** (§5): native macro chains are a later idea, on HA1. | MIT | — | HA1, PL2 | — |
| **Center Times** `macros/arch.CenterTimes.lua` (1.1) | Chooses the centisecond inside each frame so that shifting by *time* (SubKt syncs) never moves a line across a frame edge. The file has a long rationale (L7–). · Timing. | `frame_from_ms`, `ms_from_frame` (both 3.2.2) | **missing natively.** The script loads (corpus). | **adapt:** a Shift times option plus a command, tested with CFR and VFR timecodes. | MIT (reuse allowed) | S | — | FA16 [#573](https://github.com/altqx/hikari/issues/573) |
| **Convert Folds** `macros/arch.ConvertFolds.moon` (1.1.2) | Converts the old `Line Folds:` Project Properties format (feature_01–08) to extradata folds (`_aegi_folddata`) ([README][scr-readme] "Convert Folds"; [feature_09][f09] "BREAKING CHANGE"). · Users of the old fork. | `line.extra` write, `decode_path`, `file_name` | **partial:** it loads (corpus) but cannot work, because `line.extra` is always empty and writes are dropped (`lua_helper.cpp` L1028–1029). | **skip:** the source format dates from fork releases before 2023-01. FA5 converts current folds to line groups natively, and the script would run once FA3 lands. | MIT | — | FA3 | — |
| **Derive Perspective Track** `macros/arch.DerivePerspTrack.moon` (1.1.3) | Builds an AE Power-Pin track from the lines' outer quads (`subs[i].extra[...]`, L35) or from their tags, for use with Perspective-Motion ([README][scr-readme]). · Typesetting. | `line.extra` read, `frame_from_ms`; l0.ASSFoundation, arch.Math, arch.Perspective | **partial:** not in the corpus. The from-tags mode needs only what we serve: the bundled `l0.ASSFoundation` 0.5.0 and `a-mo.LineCollection` 1.3.0 (the versions it asks for, L8–22), arch.Math and arch.Perspective from DepCtrl, `karaskel`, `frame_from_ms`, `log` and `cancel`. It has no recorded run yet (FA4). The outer-quad mode cannot work, because VT3 keeps the outer quad in memory only (`docs/visual-tools.md` "The plane"). It reads the quad from `extra["_aegi_perspective_ambient_plane"]` as `x1;y1\|x2;y2\|x3;y3\|x4;y4` (L31–40). | **run as is** after FA3/FA4; recommended in FA6. | MIT | S | FA3, FA4 | FA6 [#563](https://github.com/altqx/hikari/issues/563) |
| **Fix KFX Boilerplate** `macros/arch.FixKFXBoilerplate.moon` (1.0.0) | Wraps `unicode.len(…)` arguments in old KFX template and code lines in an extra pair of parentheses, so that a call like `unicode.len(s:gsub(…))` passes one value (L16, L36–56). Since 3.5, TT's `argcheck` raises "Expected 1 arguments, got 2" on such calls (TT `automation/include/aegisub/internal/argcheck.moon` L66–70). · Karaoke. | `register_macro`, `log` | **not needed here today:** our bundled `Include/aegisub/argcheck.moon` (L64–67) never raises on extra arguments, so old templates still run. The script is not in the corpus. | **run as is;** recommended in FA6 for files shared with Aegisub 3.5 users. FA4 must not adopt TT's strict arity, or old templates break here too. | MIT | S | FA4 (corpus run) | FA6 [#563](https://github.com/altqx/hikari/issues/563) |
| **Focus Lines** `macros/arch.FocusLines.moon` (1.0.1) | Generates moving focus (speed) lines frame by frame. The author warns it is "dumb and horribly inefficient" ([README][scr-readme]). · Typesetting. | `video_size`, `frame_from_ms`, `ms_from_frame`; arch.Math | **present (loads):** corpus. | **run as is;** recommended in FA6. | MIT | S | FA4 (corpus run) | FA6 [#563](https://github.com/altqx/hikari/issues/563) |
| **FBF-ifier** `macros/arch.Line2Fbf.moon` (0.1.0) | Splits a line into frame-by-frame lines, replacing `\t`, `\fad` and `\move` with constant tags without changing the render ([README][scr-readme]). · Typesetting. | a-mo.LineCollection, l0.ASSFoundation, arch.Util | **yes:** "Split lines into frames" (`GRID_SPLIT_BY_FRAME`) bakes `\move`, `\t`, `\fad` and `\fade` per frame while "Bake animations" is on, the default ([FA15-bake-default](../compatibility-decisions.md#fa15-bake-default)). The script loads (corpus). | **adapt:** a "Bake animations" option in Split into frames, so there is one command instead of a script plus a split. | MIT (reuse allowed) | M | — | FA15 [#572](https://github.com/altqx/hikari/issues/572) |
| **Git Signs** `macros/arch.GitSigns.moon` (0.2.4) | Runs `git diff` against a ref and marks the changed lines with `[Git …]` in Effect (L63–65). · QC, editing. | `io.popen`, `decode_path("?script")`, `file_name` | **unverified as a run:** not in the corpus. The host decodes `?script` (`decodeAutomationPath`, `src/application/automation_services.cpp` L146–170) and does not remove `io` (no `popen`/`io` removal in `lua_helper.cpp`). Native git diff is **missing**: subtitle comparison compares files, not refs. | **run as is;** recommended in FA6. | MIT | S | FA4 (corpus run) | FA6 [#563](https://github.com/altqx/hikari/issues/563) |
| **Note Browser** `macros/arch.NoteBrowser.moon` (1.3.6) | Loads timestamped QC notes (`hh:mm:ss`/`mm:ss`, `[section]` headers, mpvQC files; header L15–31), jumps next/previous (also by author) and marks lines with `[QC-<section>]` in Effect (L247). · QC. | `clipboard`, `dialog.display` | **present (loads):** corpus, 6 macros. A native panel is **missing**. | **better:** a dockable QC notes panel that lists the notes, moves the grid and video to them, and marks lines in the Grid without writing into Effect. | MIT (reuse allowed) | M | — | FA20 [#577](https://github.com/altqx/hikari/issues/577) (owner: last) |
| **Aegisub Perspective-Motion** `macros/arch.PerspectiveMotion.moon` (0.4.1) | Applies AE Power-Pin tracking to lines; the perspective counterpart of Aegisub-Motion ([README][scr-readme]; `doc/perspective_motion.md`). · Typesetting. | `project_properties().video_position`, `video_size`, `frame_from_ms`, `clipboard`; optional `line.extra` write (L206–207) | **present natively** (FA13): Video › Perspective tracking… splits the selected lines frame by frame in one undo step, matches the script's corpus output within the written precision, and always saves the outer quad to `_aegi_perspective_ambient_plane`. "Track position" is dropped. | **adapt:** a native dialog beside motion tracking that reuses VT3's ported `perspective::` maths and writes the outer quad to extradata. | MIT (reuse allowed); the VT3 maths keeps ISC | L | FA3 | FA13 [#570](https://github.com/altqx/hikari/issues/570) |
| **Resample Perspective** `macros/arch.Resample.moon` (2.1.1) | Run after Resample to fix 3D-rotated lines. Its limits are listed in the [README][scr-readme]: one perspective per event, `\shad` shifts, shapes needing `\an7`. · Typesetting. | `video_size`; l0.ASSFoundation, arch.Math, arch.Perspective | **present** since FA18: Resample's "Adjust 3D rotations" option, on by default, finds a rotated line's tags again from its quad (`changeResolution` in `src/application/resample.cpp`). Like the script it keeps `\org`, skips `\move` lines and reports the lines it cannot adjust. | **adapt:** an "Adjust 3D rotations" option, on by default, so there is no second step. | MIT (reuse allowed) | M | — | FA18 [#575](https://github.com/altqx/hikari/issues/575) |
| **Rewriting Tools** `macros/arch.RWTools.lua` (1.3.2) | Keeps earlier rewrites of a line inside escaped braces, switches between them, shifts the line break by a word, and cleans escapes (6 macros, L483–488). · Translation, editing. | `aegisub.gui.set_cursor` (fork) | **present (loads):** corpus, 6 macros. `gui.set_cursor` is served (`kGui`, `lua_helper.cpp` L2263–2267). | **run as is;** recommended in FA6. | MIT | S | FA4 (corpus run) | FA6 [#563](https://github.com/altqx/hikari/issues/563) |
| **Split Tag Sections** `macros/arch.SplitSections.moon` (0.1.1) | Splits a line at its tag blocks, one event per section. · Typesetting. | l0.ASSFoundation | **present (loads):** corpus. | **run as is;** recommended in FA6. | MIT | S | FA4 (corpus run) | FA6 [#563](https://github.com/altqx/hikari/issues/563) |
| **Timing Binds** `macros/arch.TimingBinds.lua` (1.1.0; `script_name` "Timing Shortcuts") | Seven macros (L108–114): snap start or end to the frame while keeping the joined neighbour joined; join previous using the *audio selection*; shift by frames to the video position (start or end), for the selection or for all following lines. · Timing. | `project_properties().video_position`, `get_audio_selection`, `frame_from_ms`, `ms_from_frame` | **partial:** `GLOBAL_SET_START_TIME`/`END_TIME` snap to video but move only the active line (FA17). Shift times has "move to video time" in frames (`src/application/shift_times.cpp` L174–177). There is no audio-selection join. Not in the corpus; both APIs are served. | **adapt:** native commands with menu entries and shortcuts, and a snap that keeps joined lines joined by default. | MIT (reuse allowed) | M | — | FA17 [#574](https://github.com/altqx/hikari/issues/574) |
| **arch.Math** `modules/arch/Math.moon` (0.1.10) | Vector and matrix helpers. · Module. | none | **missing as a module** (DepCtrl fetches it). | **run as is** (fetched as a dependency). | MIT | — | — | FA6 |
| **arch.Perspective** `modules/arch/Perspective.moon` (1.2.1) | Quad-to-tags perspective library ([README][scr-readme] "Perspective"; `doc/perspective_math.md`). · Module. | `text_extents` | **present natively:** the same maths is ported to C++ as `perspective::` (`src/application/visual_perspective.cpp`, VT3). The Lua module is fetched by DepCtrl. | **run as is** (fetched as a dependency). | MIT | — | — | FA6 |
| **arch.Util** `modules/arch/Util.moon` (0.1.0) | Frame-exact time helpers and line FBF helpers. · Module. | `ms_from_frame`, `frame_from_ms` | **missing as a module** (DepCtrl fetches it). | **run as is** (fetched as a dependency). | MIT | — | — | FA6 |

**Docs in the repo:** a karaoke templater primer with a conversion table between the three templaters (`doc/templaters.md`, `doc/templaters_idioms.md`), plus `misc_kara.md`, `perspective_math.md`, `perspective_motion.md` and `aegisubchain.md`. They are worth linking from the help, and `templaters_idioms.md` from `docs/karaoke-templater.md`. That is not carded. The README also points to Akatsumekusa's Blender Power-Pin exporter as the successor of arch1t3cht's.

---

## 2. arch1t3cht/Aegisub, the fork

**Where each feature went:**

- **TT** means upstream in TypesettingTools 3.5.x.
- **mig** means only in the migration branch (`git log tt/master..origin/migration06`).
- **feat** means only in the discontinued feature releases.

The fork's commands that TT master lacks (a `CMD_NAME` diff) are:

- `audio/reload` and `video/reload`;
- `grid/fold/*` and `grid/sort/text*`;
- `time/align`;
- `video/pan_reset`;
- `video/subtitles_provider/reload`;
- `video/tool/perspective` and its `grid`, `plane`, `lock_outer` and `orgmode/*` sub-commands.

The Lua API rows are in §4.

### 2a. Grid, editing and timing

| Feature | What it does · audience | Source | Where now | HikariSub today | Proposal | Licence | Size | Deps | Card |
|---|---|---|---|---|---|---|---|---|---|
| Line folding | Groups and collapses lines in the grid, with a fold column, click-to-toggle and `grid/fold/*` commands. · Typesetting, editing. | [feature_01][f01], [feature_03][f03]; branch `folding`; commits `cc10df02`…`7a753611` | mig | **different:** Line groups use a description line plus member markers (`GRID_TREE_MAKE`; `docs/ux/grid-operations.md` G56). Aegisub folds are neither read nor kept. | **better:** keep one grouping model with G56's contiguity guarantees. FA3 keeps folds on save, and FA5 converts them to line groups (nested folds become nested groups). | ISC | M | FA3 | FA5 [#562](https://github.com/altqx/hikari/issues/562) |
| Extradata storage (folds, perspective plane, script data) | Since feature_09, folds live in `[Aegisub Extradata]`, so they survive scripts that re-insert lines, git merges and older Aegisub versions. Unused entries are kept on save (feature_10) and dropped on load (`043aae0a`). · Everyone opening Aegisub files. | [feature_09][f09] "BREAKING CHANGE"; [feature_10][f10] | mig | **missing:** dropped on load, and scripts get an empty `line.extra` (`lua_helper.cpp` L1028–1029; `docs/visual-tools.md` "the Document has no Extradata"). | **adapt:** the Document keeps `[Aegisub Extradata]` losslessly; it follows lines through undo, copy and split, and scripts read and write it. | ISC (clean format, no code needed) | M | — | FA3 [#560](https://github.com/altqx/hikari/issues/560) |
| Sort by text and by stripped text | In Sort All and Sort Selected (`grid/sort/text*`). · Editing, QC. | [feature_06][f06] | feat | **missing:** `GLOBAL_SORT_*` cover start, end, style, actor, effect and layer (`src/application/hotkeys.cpp`). | **adapt:** "By text" and "By text without tags", stable, one undo step. | BSD | S | — | FA11 [#568](https://github.com/altqx/hikari/issues/568) |
| Align to video (`time/align`) | wangqr/Charlie Jiang's tool: pick a pixel's position and colour with a tolerance, and the line is timed to the run of frames that keep it (`src/dialog_align.cpp` L86–154). · Timing (signs). | [feature_03][f03]; branch `wangqr_time_video` | feat (not in TT master: no `dialog_align.cpp`) | **missing.** | **adapt:** a native tool in the menu bar and shortcut editor, with a synthetic-clip test. | BSD-3 (reuse with notice in `packaging/licenses`) | M | — | FA14 [#571](https://github.com/altqx/hikari/issues/571) |
| Mixin lines highlighted as template lines | For The0x539's templater. · Karaoke. | [feature_02][f02]; `d72e9c1d` | mig | **partial:** `karaoke_checks.cpp` L33 knows `mixin`, but `editor_syntax.cpp` L13–15 highlights only `template` and `code`. | **adapt.** | BSD | S | — | FA7 [#564](https://github.com/altqx/hikari/issues/564) |
| Syntax highlighting for drawings and clips | X and Y coordinates in alternating colours; the end points of Bezier curves underlined. · Typesetting. | [feature_08][f08], [feature_09][f09] | TT 3.5.0 | **partial:** one role, `SplitAndDrawings` (`src/core/include/hikari/core/editor_syntax.h` L22). | **adapt,** in every theme. | BSD | S | — | FA7 [#564](https://github.com/altqx/hikari/issues/564) |
| Shift+Enter inserts `\n` (option) | Soft break, honoured only under `WrapStyle: 2`. · Translation, editing. | [feature_12][f12] | TT 3.5.0 | **missing:** Shift+Enter always inserts `\N` (`docs/editor-shortcuts.md`, `EDITBOX_SPLIT_LINE`). | **adapt** (setting; default unchanged). | BSD | S | — | FA7 [#564](https://github.com/altqx/hikari/issues/564) |
| Timestamps past 10 h (to about 596 h) | PR #104. · Long-form timing. | [feature_12][f12] | TT | **partial:** the loader parses any hours, but the time field caps at `kTimeFieldMaxMs = 35999999` (`editor_fields.h` L87). | **adapt.** | BSD | S | — | FA7 [#564](https://github.com/altqx/hikari/issues/564) |
| "Show original" checkbox state remembered | wangqr. · Translation. | [feature_10][f10] | feat | **different:** TM1 translation mode, `EDITBOX_HIDE_ORIGINAL`. | **skip:** our translation model differs. | — | — | — | — |
| "Apply" button in Select Lines | Applies and keeps the dialog open. · Editing, QC. | [feature_10][f10] | feat | **present:** "Select" runs without closing, and "Close" is separate (`src/ui/SelectLinesDialog.qml` L43–50, L159–162). | **present.** | — | — | — | — |
| Smooth touchpad scrolling in the grid | Pixel scrolling. · Everyone. | [feature_10][f10] | feat | **present:** the grid scrolls by the touchpad's pixel delta (`LineGrid::wheelEvent`, `src/ui/line_grid.cpp` L536–545, through `wheelScrollPixels`, `src/ui/wheel_scroll.cpp` L42–52). | **present.** | — | — | — | — |

### 2b. Video, visual tools and media

| Feature | What it does · audience | Source | Where now | HikariSub today | Proposal | Licence | Size | Deps | Card |
|---|---|---|---|---|---|---|---|---|---|
| Perspective visual tool | Inner and outer quad, lock-outer, grid, origin modes (`orgmode/center`, `keep`, `nofax`, `cycle`), drawings (feature_10) and LayoutRes (`c0b43524`). · Typesetting. | [feature_09][f09], [feature_10][f10]; mig commits `286bf28b`, `dec62ca5`, `c0b43524` | mig | **present:** VT3 [#273](https://github.com/altqx/hikari/issues/273) (`VIDEO_TOOL_PERSPECTIVE`; `src/application/visual_perspective.cpp`, which uses LayoutRes in `screenZ` L342–346; the Origin dropdown is in `docs/visual-tools.md` L221). Differences: the outer quad is not saved, because there is no extradata, and the origin modes are a dropdown, not separate commands. | **present;** the outer quad is saved by FA3 and FA13. | ISC (already reused) | — | FA3 | FA13 |
| Vector clip modes bindable to hotkeys | One command per mode: `vclip/drag`, `line`, `bicubic`, `convert`, `insert`, `remove`, `freehand`, `freehand_smooth` (TT `src/command/vis_tool.cpp`). · Typesetting. | [feature_01][f01]; [3.5.0 changelog][cl350] "Editing" | TT 3.5.0 | **missing:** the modes are inspector-bar toggles only; `hotkeys.cpp` has `VIDEO_TOOL_VECTOR_CLIP` alone. | **adapt.** | BSD | S | — | FA9 [#566](https://github.com/altqx/hikari/issues/566) |
| Vector clip coordinates rounded to 2 decimals, not integers | From AegisubDC. · Typesetting. | [feature_02][f02] | TT: `Vector2D::Str` defaults to 2 decimals (TT `src/vector2d.h` L73), and `Spline::EncodeToAss` uses it (`src/spline.cpp` L74–92). 3.2.2 wrote integers with `DStr`. | **different:** "the clip is written in whole units" (`docs/visual-tools.md` L75). | **skip** (not picked). The grilling took this for a fork-only change, but TT 3.5 writes 2 decimals too. **Owner to re-decide** whether to match it. | — | — | — | — |
| Shape handle size setting | Larger visual-tool handles. · Typesetting, high-DPI. | [feature_10][f10]; `2ab6686c` | mig | **missing** (no match for `handleSize` in `src`). | **adapt.** | ISC | S | — | FA9 [#566](https://github.com/altqx/hikari/issues/566) |
| Video zoom and pan | moex3's PR #150 with Ctrl+wheel zoom and drag pan (fork). TT 3.5 adds middle-drag or Shift+scroll pan, Ctrl+scroll zoom, gestures, configurable modifiers and a reset command. · Typesetting. | [feature_01][f01], [feature_03][f03], [feature_09][f09]; [3.5.0 changelog][cl350] "Video" | TT 3.5.0 | **partial:** the legacy zoom mode (`src/application/visual_view.cpp` `zoomDrag` L220, `zoomWheel` L277; `GLOBAL_VIDEO_ZOOM`, `GLOBAL_RESET_VIDEO_ZOOM`). | **better:** 3.5's model by default with the legacy mode kept behind a setting, visual-tool coordinates tested while panned, and modifiers in Settings. | BSD | M | — | FA12 [#569](https://github.com/altqx/hikari/issues/569) |
| Copy or save subtitles only, on a transparent background | From the video context menu. · Typesetting (tracing, previews). | [feature_05][f05]; [3.5.0 changelog][cl350] | TT 3.5.0 (`video/frame/copy/subs`, `save/subs`) | **partial:** `VIDEO_COPY_SUBBED_FRAME_TO_CLIPBOARD` and `VIDEO_SAVE_SUBBED_FRAME_TO_PNG` include the frame. | **adapt.** | BSD | S | — | FA9 [#566](https://github.com/altqx/hikari/issues/566) |
| `video/reload`, `audio/reload` | Reload the current media. · Everyone. | [feature_06][f06] | feat | **missing** (no command in `hotkeys.cpp`). | **adapt:** keeps the position. | BSD | S | — | FA9 [#566](https://github.com/altqx/hikari/issues/566) |
| Reload font provider (`video/subtitles_provider/reload`) | From AegisubDC. · Typesetting. | [feature_02][f02] | feat | **different:** fonts reload by themselves when the font catalogue changes (`fontsChanged` → `reloadSubtitleFonts`, `src/app/application.cpp` L1016–1017). | **present.** | — | — | — | — |
| Dummy video limit raised to 100000 | For rendering text to an image to trace. · Typesetting. | [feature_09][f09]; `b74c4c4a` | mig | **missing:** width 300–8000 and height 200–4500 (`src/ui/DummyVideoDialog.qml` L90, L98). | **adapt.** | BSD | S | — | FA9 [#566](https://github.com/altqx/hikari/issues/566) |
| Dummy video exact fractional fps | For frame-exact dummy timing. · Timing. | [feature_10][f10] | feat | **unverified** (free text fps field in `DummyVideoDialog.qml`). | **adapt:** store 24000/1001 exactly. | BSD | S | — | FA19 [#576](https://github.com/altqx/hikari/issues/576) |
| Overscan mask fix | Fixes the mask's size (per-side BBC values). · Broadcast QC. | [feature_08][f08] | TT has `video/show_overscan` | **missing:** there is no overscan mask (no match in `src`). | **skip:** a broadcast-safe-area aid that is rare in fansub work; not picked. | — | — | — | — |
| Compressed subtitles read from MKV | · Everyone. | [feature_11][f11] | TT: `read_subtitles` inflates zlib frames, with a 16 MiB cap (TT `src/mkv_wrap.cpp` L123–154) | **missing (static check):** we read subtitle packets with FFmpeg's Matroska demuxer (`readSubtitles`, `src/helpers/media/media_helper.cpp` L661–690). Neither `vcpkg.json` nor `ports/hikari-ffms2/vcpkg.json` turns on the ffmpeg port's `zlib` feature, and without it the port configures `--disable-zlib` (the pinned port 7.1.2#5, `ports/ffmpeg/portfile.cmake` L564–567). FFmpeg then logs "Unsupported encoding type" and passes the compressed bytes through, CodecPrivate (the ASS header) included (FFmpeg n7.1.2 `libavformat/matroskadec.c` L2070–2081). Not yet tried with a real file. | **adapt (uncarded):** turn on the `zlib` feature, with an mkvmerge `--compression 0:zlib` fixture. `vcpkg.json` belongs to the WebM thread (#600). | BSD | S | #600 | — |
| YCbCr Matrix applied by all providers | · Everyone. | [feature_12][f12] | TT 3.5.0 (rework) | **present:** `src/application/video_matrix.cpp`. | **present.** | — | — | — | — |
| Keep the video timecodes' non-zero start | Old CR files with a two-frame delay. · Timing. | [feature_12][f12] | feat | **present (by construction):** frame start times are the stream's PTS, converted without subtracting the first one (`src/application/video_session.cpp` L109–114). No fixture has a delayed first frame. | **present.** | — | — | — | — |
| High-DPI | wangqr's GUI merge. | [feature_08][f08] | TT 3.5.0 | **present** (Qt). | **present.** | — | — | — | — |
| Colour picker restricted to the window | Linux fix. | [feature_01][f01] | superseded by the TT portal picker | **present (different):** XDG portal PickColor (`src/ui/screen_sampler.cpp`). | **present.** | — | — | — | — |
| Dark mode on Windows | wx master's experimental dark mode. | [feature_11][f11]; `22efb2e0` | mig | **present (different):** themes with dark, light and high-contrast (K2 [#206](https://github.com/altqx/hikari/issues/206)). | **present.** | — | — | — | — |
| AviSynth provider (Windows; Linux in feat) | · Advanced media. | [feature_01][f01], [feature_04][f04] | Windows in TT; Linux feat | **missing.** | **skip** (§5). | — | — | — | — |
| BestSource video and audio provider | Frame- and sample-exact; fast seeking from R2; R8 in feature_12. | [feature_04][f04], [feature_12][f12] | feat (dropped in mig) | **missing:** FFMS2 only ([ADR 0016](../adr/0016-backend-values-and-isolated-ffms2.md)). | **skip** (§5). | ISC; BestSource MIT | — | — | — |
| VapourSynth provider, `aegisub_vs.py`, built-in keyframe generation | `.vpy` media, default scripts, VFR, keyframes (WWXD/SCXvid), progress. | [feature_04][f04], [feature_07][f07], [feature_10][f10], [feature_11][f11] | feat | **missing.** | **skip** (§5). | ISC | — | — | — |
| Provider selection rework | Ask instead of silent fallback. | [feature_10][f10] | feat | **n/a** (one indexed source). | **skip:** nothing to select. | — | — | — | — |
| ASSDraw launcher (`tool/assdraw`) | Opens the external ASSDraw3. | fork `CMD_NAME` list; absent from TT master | — | **different:** native drawing and shape tools (`VIDEO_TOOL_DRAWING`). | **skip.** | — | — | — | — |

### 2c. Audio

| Feature | What it does · audience | Source | Where now | HikariSub today | Proposal | Licence | Size | Deps | Card |
|---|---|---|---|---|---|---|---|---|---|
| XAudio2 player and stereo/multi-channel playback | wangqr, Shinon. | [feature_04][f04], [feature_05][f05] | feat | **different:** PortAudio output ([ADR 0010](../adr/0010-portaudio-editor-output.md)). Stereo is the default: `OutputFormat::channels = 2` (`src/application/include/hikari/application/audio_output.h` L26), and swresample downmixes from the source's layout (`media_helper.cpp` L515–524). | **skip** (§5). | BSD-3 | — | — | — |
| Downmix option (mono or stereo) | wangqr, AegisubDC. | [feature_02][f02] | feat | **missing** as a user setting (no mono option found). | **adapt:** a "force mono playback" setting. | BSD | S | — | FA19 [#576](https://github.com/altqx/hikari/issues/576) |
| Spectrum display improvements | EleonoreMizo's PR #94: frequency mapping and quality. · Timing, karaoke. | [feature_01][f01] | TT: a log power scale and a linear-to-log frequency curve (`scale_log`, TT `src/audio_renderer_spectrum.cpp`; the fork's `feature` has the same code at L230–330) | **missing:** our spectrum (`src/application/audio_spectrum.cpp`, A2 [#155](https://github.com/altqx/hikari/issues/155)) scales power linearly (L316, L367–376) and maps frequency bins linearly. | **skip** for now (not picked). Revisit with a timer's side-by-side. | — | — | — | — |
| Vertical audio zoom with Shift+wheel | · Timing. | [feature_01][f01]; `da2c5ce7` | mig | **present (different gesture):** Ctrl+wheel calls `applyVerticalZoom` (`src/ui/audio_controller.cpp` L560, L672). | **present.** | — | — | — | — |
| Lead-in/lead-out key fix | wangqr. | [feature_10][f10] | feat | **present** (`AUDIO_LEAD_IN/OUT`; no known defect). | **present.** | — | — | — | — |

### 2d. Automation host behaviour (non-API)

| Feature | Source | Where now | HikariSub today | Proposal | Card |
|---|---|---|---|---|---|
| Scroll bars in automation dialogs that grow too large (DepCtrl's Macro Configuration) | [feature_10][f10] | **unverified** in TT | **present:** a clipped `Flickable` (`src/ui/AutomationDialog.qml` L450–460) | **present** | — |
| Wider default `floatedit` bounds | [feature_04][f04] | feat | **present:** a `floatedit` without `min`/`max` is unbounded (`-DBL_MAX`…`DBL_MAX`, `src/helpers/lua/lua_helper.cpp` L549–556) | **present** | — |
| Memory-leak and crash fixes on `aegisub.cancel()` and script errors | [feature_10][f10]; `88eb6052` | mig | **different:** one helper process per script ([ADR 0006](../adr/0006-isolated-lua-automation-host.md)), so a crash ends only that script's process | **present** | — |

---

## 3. TypesettingTools/Aegisub 3.4.0 to 3.5.1

**Releases:**

- **3.4.0** (2024-12-19) was mostly a modernisation release ([release][tt-rel340]).
- **3.4.1** and **3.4.2** fixed bugs ([3.4.1 changelog][cl341]).
- **3.5.0** (2026-10-05) merged video pan and zoom, high-DPI, LayoutRes and more, and fixed CVE-2026-92705 ([3.5.0 changelog][cl350]).
- **3.5.1** (2026-10-17) fixed bugs and added `aegisub.version` ([3.5.1 changelog][cl351]).

TT master `d8ad830` registers no `get_frame` and no `gui` table (`src/auto4_lua.cpp`). It has no fold, perspective or align files.

Rows already covered in §2 because they came from the fork are not repeated here: pan and zoom, transparent subtitles, drawing highlighting, `\n`, hotkeyable vector clip modes, encoding `-1`, high-DPI and the matrix rework.

| Change | What it does · audience | Source | HikariSub today | Proposal | Size | Card |
|---|---|---|---|---|---|---|
| Prompt before loading a subtitle file's associated scripts (CVE-2026-92705) | A crafted file could run code through the scripts it references. · Security. | [3.5.0 changelog][cl350] "Security"; [v3.5.0 release][tt-rel350] | **present:** `AutomationShell::askToLoadDocumentScripts` with per-document trust decisions (`src/app/automation_shell.cpp` L573–668) | **present** | — | — |
| Confirmation before loading a linked AviSynth script; AviSynth only from the app or system dir | · Security. | [3.5.0][cl350] | **n/a** (no AviSynth) | **skip** | — | — |
| Built-in Lua modules always load from the installation | · Security. | [3.5.0][cl350] | **present:** a module that exists in the shipped Include loads from there first; only DepCtrl's packages may be replaced by a user copy (`userMayReplace`/`shippedFile`, `src/helpers/lua/lua_helper.cpp` L295–313, L752–762; `include()` L324). Pinned by `LuaHelper.StockModulesLoadFromTheShippedInclude` (`tests/backends/lua_helper_tests.cpp` L1818). | **present** (drop it from FA19) | — | — |
| Attachment extraction cannot leave the chosen folder; asks before overwriting | · Security. | [3.5.0][cl350] | **n/a:** there is no attachment manager. Attached fonts are only decoded for rendering (`src/core/ass_attachments.cpp`, `src/application/matroska.cpp` L83–90). | **skip** (FA19 keeps "asks before overwriting" for the font collector) | — | — |
| Hardened parsing of ASS attachments, SRT, MKV, Y4M, timecodes, dummy parameters and font tables | · Robustness. | [3.5.0][cl350] | **unverified** (our own parsers) | **skip** as a row; it is covered by HikariSub's own fuzz and fixture tests | — | — |
| Video Details shows the colour space | · Typesetting, QC. | [3.5.0][cl350]; `video/details` in TT commands | **missing:** no Video details window (no `VideoDetails` in `src`) | **adapt:** resolution, fps, frame count, colour space and matrix | S | FA19 [#576](https://github.com/altqx/hikari/issues/576) |
| Edit LayoutRes; resampling adjusts LayoutRes (3.5.1 fixes LayoutResY with borders) | · Typesetting. | [3.5.0][cl350]; [3.5.1][cl351]; [PR #594](https://github.com/TypesettingTools/Aegisub/pull/594) | **partial:** LayoutRes is edited in `src/ui/ScriptPropertiesDialog.qml`. 3.5.1's border case is n/a, because our Resample has no add or remove borders option. Until FA18 `changeResolution` overwrote an existing LayoutRes with the *target PlayRes*. TT keeps LayoutResY and only rescales LayoutResX when the aspect ratio changes (TT `src/resolution_resampler.cpp` L211–245, L279–282), because vertical `\blur` and the perspective focal length are in LayoutResY units. Since FA18 Resample does the same, and setting the resolution without resampling leaves LayoutRes alone. | **adapt:** compute LayoutRes the way TT does, with a test on a file where LayoutRes ≠ PlayRes. | S | FA18 [#575](https://github.com/altqx/hikari/issues/575) |
| Prompts when PlayRes, LayoutRes or YCbCr Matrix is unset or mismatched with the video; each can be turned off | · Typesetting. | [3.5.0][cl350] "Script Resolution and Color Matrix" | **partial:** script resolution prompt only (`src/application/script_resolution.cpp`) | **adapt** | M | FA9 [#566](https://github.com/altqx/hikari/issues/566) |
| Warnings for untagged matrices and HDR or wide-gamut video; "From Video" matrix button; new files not `None` | · Typesetting. | [3.5.0][cl350] | **partial:** new files get `TV.601` (`src/application/document_files.cpp`); warnings **missing** | **adapt** (with the matrix prompt) | S | FA9 [#566](https://github.com/altqx/hikari/issues/566) |
| Resample stops scaling `\blur`/`\be`; finer drawing quantisation | · Typesetting. | [3.5.0][cl350] | **present** for blur/be (not in `resample.cpp` L234–235's list); drawing quantisation **unverified** | **present** | — | — |
| Reworked video-position marker in the audio display, with snapping to frame middles | · Timing. | [3.5.0][cl350] "Audio"; [PR #609](https://github.com/TypesettingTools/Aegisub/pull/609) | **present** (FA10): the dashed line at the frame start, the previous and current frames shaded, and their middles snap with the keyframes (`audioVideoFrame`, `legacyBoundarySnap`) | **adapt** | M | FA10 [#567](https://github.com/altqx/hikari/issues/567) |
| Scroll the audio display to the line's start or end (`audio/go_to/start`, `/end`) | · Timing. | [3.5.0][cl350]; TT commands | **present** (FA10): `AUDIO_GOTO_START` (1033) and `AUDIO_GOTO_END` (1034) beside `AUDIO_GOTO` | **adapt** | S | FA10 [#567](https://github.com/altqx/hikari/issues/567) |
| Insert hard space `\h` (Ctrl+Shift+Space / Option+Space) | · Editing. | [3.5.0][cl350] | **missing** (no command in `hotkeys.cpp`) | **adapt** | S | FA7 [#564](https://github.com/altqx/hikari/issues/564) |
| Style editor: higher decimal precision | · Typesetting. | [3.5.0][cl350] | **present:** the Style editor's numbers are free-text fields that show and write the stored string as is (`StyleField`, `src/ui/StyleManager.qml` L861–880, L1526–1557), so there is no precision cap | **present** | — | — |
| Style border style `4` (opaque box per event, libass-only) | · Hardsub typesetting. | [3.5.0][cl350] | **yes:** a Border type choice of Outline (1), Opaque box (3) and Opaque box per line (libass) (4) replaced the checkbox; 1, 3 and 4 round-trip, Lua too ([FA8-lua-borderstyle](../compatibility-decisions.md#fa8-lua-borderstyle)), and other values become 1 ([FA8-borderstyle-values](../compatibility-decisions.md#fa8-borderstyle-values)) | **better:** a three-way Border style choice that replaces the checkbox and says which choice is libass-only | S | FA8 [#565](https://github.com/altqx/hikari/issues/565) |
| Style encoding `-1` (libass RTL/bidi shaping) | · Translation (RTL), hardsubs. | [feature_12][f12]; [3.5.0][cl350] | **yes:** "-1 - Detect text direction (libass)" heads the Text encoding list | **adapt** | S | FA8 [#565](https://github.com/altqx/hikari/issues/565) |
| Font collector faster and more reliable on Windows; `\r` and `\fn0` handling; grouped messages; fonts in drawings (fork feature_10) | · Release QC. | [3.5.0][cl350] "Fonts Collector"; [feature_10][f10] | **unverified** (`src/application/font_collector.cpp`, our own implementation) | **skip** as a port; FA19 adds the overwrite prompt | — | FA19 |
| Translation assistant Unicode-whitespace fix | · Translation. | [3.5.0][cl350] | **different** (TM1 translation mode) | **skip** | — | — |
| MoonScript 0.7.0 bundled | · Automation. | [3.5.0][cl350]; TT `automation/include/moonscript.lua` L2 (`"0.7.0"`) | **different:** we bundle 0.5.0 (`Automation/automation/Include/moonscript.lua` L4508), pinned by DC1's corpus | **skip** until a corpus script needs 0.7 (a bump re-baselines `expected.json`) | — | — |
| `luabins` deprecated; `aegisub.__raise_warning` (load and post-load warnings) | · Automation. | [3.5.0][cl350]; TT `src/auto4_lua.cpp` L537, L599 | **missing** (`__raise_warning` not in `src`) | **skip:** an internal hook TT uses for its own deprecations; we have no deprecated library to warn about | — | — |
| Installer stops bundling Yutils, luajson and ffi-experiments with DepCtrl | · Automation. | [3.5.0][cl350] | **different:** HikariSub bundles DepCtrl and its modules (DC1 [#361](https://github.com/altqx/hikari/issues/361)) | **skip** | — | — |
| `lfs.dir` raises on failure; `lfs.attributes` non-ASCII on Windows; `util.deep_copy` with cycles; clipboard and `re` leaks | · Automation. | [3.5.0][cl350] | **partial:** our `deep_copy` tracks `seen` (`Include/aegisub/util.moon` L30–37), but it maps a cycle to the original table, not the copy. Our `Include/aegisub/lfs.moon` is the pre-3.5 shim: `dir` raises with `error 2, err`, so the error value is the number 2 (L79). `chdir`, `mkdir`, `rmdir` and `touch` return `tonumber` of a C `bool`, which is always `nil` (L39–41, L85–90); TT returns `true` or `nil, err`. The native `dir_new` also treats an unreadable folder as empty instead of failing (`src/helpers/lua/lua_native.cpp` L297–305). `attributes` on non-ASCII Windows paths is not checked. The leaks end with the helper process. | **adapt** (fold the `lfs` and `deep_copy` behaviour into FA4) | S | FA4 [#561](https://github.com/altqx/hikari/issues/561) (fold in) |
| `include()`/`require()` show the real syntax error; include runtime errors no longer crash (3.5.1) | · Automation. | [3.5.1][cl351] | **missing:** both report the file or module name in place of the load error. `include()` puts `filename` where the error belongs, and the line is flagged `MUST KILL` (`src/helpers/lua/lua_helper.cpp` L336–340). `require`'s loader formats `checkString(L, 1)`, the module name (L781–782). Runtime errors in an include end only that script's helper. | **adapt** (fold into FA4) | S | FA4 [#561](https://github.com/altqx/hikari/issues/561) (fold in) |
| Always detect valid UTF-8 as UTF-8 | · Everyone. | [3.5.0][cl350] | **present:** the UTF-8 check runs before uchardet (`src/backends/legacy_text_file.cpp` L342) | **present** | — | — |
| No prompt for missing linked media; missing files reported | · Everyone. | [3.5.0][cl350] "Bugfixes" | **unverified:** association prompt in `src/app/application_tab_menu.cpp` | **adapt:** report which files are missing | S | FA19 [#576](https://github.com/altqx/hikari/issues/576) |
| Dialogs open in the script's folder; track selection shows language and title; filters list only supported formats | · Everyone. | [3.5.1][cl351] | **present / different.** The video dialog starts in the subtitle file's folder, then the last video's (`Application::videoDialogFolder`, `src/app/application_video.cpp` L136–147); the other pickers remember their own folder (`startFolder`, `src/ui/Main.qml` L5742). Track choices show the title and language for subtitle tracks (`offeredTracks`, `src/application/matroska.cpp` L76–77) and audio tracks (`src/application/audio_box.cpp` L75–87). The filters are a fixed list (`Main.qml` L6037), because there is one FFmpeg-based source and no provider to ask. | **present** | — | — |
| Font dialog underline sets `\u` | · Editing. | [3.5.1][cl351] | **present:** underline is written as `\u` (`flagStep(…, u'u', …)`, `src/core/editor_font_colour.cpp` L134; `tests/core/editor_font_colour_tests.cpp` L55) | **present** | — | — |
| Search and replace with multiple windows; dialogs cannot open twice; dav1d | · Robustness. | [3.4.1][cl341] | **different:** one window with tabs (ADR 0020). **dav1d is missing:** the ffmpeg port has a `dav1d` feature (`--enable-libdav1d`, `portfile.cmake` L310–314 of the pinned 7.1.2#5), and neither of our manifests turns it on. | **skip** here; dav1d is the WebM thread's [#600](https://github.com/altqx/hikari/issues/600) | — | — |
| Grid default font size 9 and the system UI font | · Everyone. | [3.5.0][cl350] | **different** (`docs/ux/visual-language.md`) | **skip** | — | — |
| Wayland screen colour picker via the XDG portal; macOS picker fix | · Typesetting. | [3.5.0][cl350]; [migration03-02][mig03] | **present:** `src/ui/screen_sampler.cpp` | **present** | — | — |
| "FHD vertical" in the video resolutions | · Typesetting (shorts). | [3.5.0][cl350] | **missing:** the dummy video presets stop at 3840x2160 and have no portrait size (`Application::dummyVideoDefaults`, `src/app/application_video.cpp` L186–193) | **adapt:** add 1080x1920 with FA9's dummy video change | S | FA9 [#566](https://github.com/altqx/hikari/issues/566) (fold in) |
| System language first on first launch | · i18n. | [3.5.0][cl350] | **different** (own localisation, `docs/localisation.md`) | **skip** | — | — |

---

## 4. Lua API: the fork and TypesettingTools vs our host

How each column was found:

- **Registration columns:** the 3.2.2, fork, migration and TT columns grep the `set_field`/`lua_setfield` names in each revision's `src/auto4_lua.cpp`.
- **HikariSub column:** greps `src/helpers/lua/lua_helper.cpp`. The host services table is at L2252–2257, `kGui` at L2263–2267 and the `aegisub.hikari` table at L1493–1500.

| Function or field | Semantics (source) | 3.2.2 | fork `feature` | mig06 | TT 3.5.1 | HikariSub | Scripts that need it | Proposal | Card |
|---|---|---|---|---|---|---|---|---|---|
| `aegisub.gui.get_cursor()` / `set_cursor(pos)` | 1-based caret in the edit box; setters apply after the subtitle changes ([gui.txt][gui]) | — | ✓ | ✓ | — | **present** (`kGui`; host `SetEditorCursor`, `automation_services.cpp`) | arch.RWTools | present | — |
| `aegisub.gui.get_selection()` / `set_selection(s, e)` | Edit-box selection; `e < s` puts the cursor left ([gui.txt][gui]) | — | ✓ | ✓ | — | **present** | (none in the arch repo) | present | — |
| `aegisub.gui.is_modified()` | Unsaved changes ([gui.txt][gui]; [feature_04][f04] names petzku's EncodeClip and GitSigns) | — | ✓ | ✓ | — | **present** | none found: feature_04 names petzku's EncodeClip and GitSigns as scripts it "will be useful for", but GitSigns 0.2.4 does not call it (grep); EncodeClip **unverified** | present | — |
| `aegisub.get_frame(n, withSubs)` → `:width()`, `:height()`, `:getPixelFormatted(x, y)` | Reads a video frame ([get-frame.txt][getframe]) | — | ✓ | ✓ | — | **present** (`getFrame` L1935–1964; BGRA over IPC) | (none in the arch repo) | present | — |
| `frame:getPixel(x, y)` → r, g, b | Three values since feature_12; one packed integer before ([feature_12][f12]; `d8c36615`) | — | ✓ | ✓ | — | **present** since FA4: r, g, b (FA4-getpixel-rgb in [compatibility-decisions](../compatibility-decisions.md)) | Scripts written for feature_12 and later | **adapt** | FA4 [#561](https://github.com/altqx/hikari/issues/561) |
| `frame:data()` → lightuserdata, pitch, flipped | Raw BGRA for FFI ([get-frame.txt][getframe]; `1b8c13ed`) | — | ✓ | ✓ | — | **present** since FA4 (`frameData`) | FFI frame readers; none in the arch repo | **adapt** | FA4 [#561](https://github.com/altqx/hikari/issues/561) |
| `aegisub.get_audio_selection()` → start_ms, end_ms | ([misc.txt][misc]) | — | ✓ | ✓ | ✓ | **present** (`audioSelection`) | arch.TimingBinds "Join Previous Line" | present | — |
| `aegisub.set_status_text(text)` | The docs say `set_status_bar_text`; the code registers `set_status_text` ([misc.txt][misc]) | — | ✓ | ✓ | ✓ | **present** (`statusText` L2162) | — | present | — |
| `aegisub.version` {`string`, `major`, `minor`, `patch`, `build`} | `push_version` (TT `src/auto4_lua.cpp` L120–131); [3.5.1 changelog][cl351] | — | — | ✓ | ✓ | **present** since FA4: reports 3.5.1, build 9914 | Scripts that gate on the Aegisub version | **adapt** (report 3.5.1) | FA4 [#561](https://github.com/altqx/hikari/issues/561) |
| `aegisub.__raise_warning` | Internal deprecation hook (TT L537, L599) | — | — | ✓ | ✓ | **missing** | none | **skip** (§3) | — |
| `line.extra` (read and write) | Per-line extradata: `_aegi_folddata` (folds, feature_09 "Pass fold data through Lua", `da3f4b32`), `_aegi_perspective_ambient_plane` (perspective plane) | ✓ (the field exists) | ✓ | ✓ | ✓ | **missing:** always an empty table; writes dropped (L1028–1029) | arch.ConvertFolds; arch.DerivePerspTrack (outer-quad mode); arch.PerspectiveMotion ("Add quad to extradata"); `phos.FoldOperations`; `myaa.MergeScripts`; `phos/AssfPlus` (corpus grep) | **adapt** | FA3 [#560](https://github.com/altqx/hikari/issues/560), FA4 [#561](https://github.com/altqx/hikari/issues/561) |
| `project_properties()` | Keys exist since 3.2.2 | ✓ | ✓ | ✓ | ✓ | **partial:** every key present. Until FA4 only `video_position` and `video_file` were real: `audio_file` and `keyframes_file` were always `""`, a mistake in this audit's first version. Since FA4 `audio_file`, `keyframes_file` and `video_zoom` are real, and `timecodes_file` stays `""` (FA4-timecodes-file) | arch.TimingBinds and arch.PerspectiveMotion read `video_position` (served) | **adapt** (real `timecodes_file` and `video_zoom`) | FA4 [#561](https://github.com/altqx/hikari/issues/561) |
| `decode_path("?script")` and the other path tokens | 3.2.2 | ✓ | ✓ | ✓ | ✓ | **present:** `?audio ?data ?dictionary ?local ?script ?temp ?user ?video` (`automation_services.cpp` L146–170) | arch.GitSigns, arch.ConvertFolds, arch.AegisubChain | present | — |
| `frame_from_ms`, `ms_from_frame`, `video_size`, `keyframes`, `text_extents`, `file_name`, clipboard | 3.2.2 | ✓ | ✓ | ✓ | ✓ | **present** (L2252–2257). Since FA4 frame and time round as Aegisub's START (FA4-frame-times) | most arch scripts | present | — |

**HikariSub-only additions:** `aegisub.get_frequency_peaks` and the `aegisub.hikari` table (`sources`, `set_source`, `report`, `clock`) (`lua_helper.cpp` L1493–1500, L2335). `set_undo_point` is not an addition: Aegisub registers it too (TT `src/auto4_lua_assfile.cpp` L791). Scripts that use them do not run in Aegisub.

**What blocks scripts today:**

- **Extradata** blocks the six scripts in the `line.extra` row.
- **`getPixel`'s return shape** would break any script written for feature_12 and later; none is in our corpus.
- Nothing else in the arch repo needs an API we lack. Every arch script's API needs are met once FA3 and FA4 land.

---

## 5. Considered, rejected

| Item | Why rejected | Source |
|---|---|---|
| **BestSource** video and audio provider | HikariSub indexes through one isolated FFMS2 helper ([ADR 0016](../adr/0016-backend-values-and-isolated-ffms2.md)). arch1t3cht dropped BestSource from the migration releases because "FFmpegSource has become much more reliable" ([migration01-01][mig01]). FFMS2 5.0 includes his seeking fixes ([feature_12][f12]). | [feature_04][f04], [feature_12][f12] |
| **VapourSynth** provider, `aegisub_vs.py`, and built-in keyframe generation (WWXD/SCXvid) | It needs a user-installed Python and VapourSynth plus plugins, and it is dropped from the migration releases. Keyframe generation is the only loss, and arch1t3cht says "you should be able to live without it" ([migration01-01][mig01]). A keyframe generator could come back later as a plugin (PL2) or a node, not a media provider. | [feature_10][f10], [feature_11][f11] |
| **AviSynth** (Windows; Linux in the fork) | Windows-centric, and the fork notes it "still occasionally crashes" ([feature_01][f01]). TT 3.5.0 had to add security confirmations for linked AviSynth scripts ([3.5.0][cl350] "Security"). Out of scope under ADR 0016. | [feature_04][f04] |
| **XAudio2** and the fork's audio-player rework | HikariSub's editor output is PortAudio ([ADR 0010](../adr/0010-portaudio-editor-output.md)). Dropped from the migration releases. | [feature_04][f04], [feature_05][f05] |
| **AegisubChain** | It is a Lua VM inside a script that loads other scripts and swaps their globals. That is untested under the one-helper-per-script host (ADR 0006), and it would need the dialog hooks to behave identically. AD2 already gives "Repeat last run" and dialog presets for one macro. **Later idea:** native macro chains over the Hikari API (HA1 [#411](https://github.com/altqx/hikari/issues/411)): record a sequence of commands and macros with their dialog answers, and replay it with one merged dialog. That fits the node effect editor (NE1 [#423](https://github.com/altqx/hikari/issues/423)) or plugins better than a Lua VM. | [README][scr-readme] "AegisubChain"; `doc/aegisubchain.md` |

---

## 6. Audit later

These other script collections are not audited here. Several are already in the third-party corpus (`tests/fixtures/automation-thirdparty/manifest.json`), and their load results are pinned in `expected.json`.

| Collection | In the corpus | Licence (from `manifest.json`, else the repository) | Note |
|---|---|---|---|
| unanimated (TypesettingTools/unanimated-Aegisub-Scripts) | 7 scripts: `ua.Cycles`, `ChangeCase`, `JumpToNext`, `HYDRA`, `MultiLineEditor`, `BlurAndGlow`, `iBus`; runs in `runs.json` | Unlicense | The Hydra strip already replaces `ua.HYDRA` natively (HY1 [#271](https://github.com/altqx/hikari/issues/271)). |
| PhosCity | 5: `phos.snap`, `wave`, `RemoveTags`, `EditTags`, `FoldOperations` | GPL-3.0 (compatible) | `FoldOperations` needs extradata (FA3). arch1t3cht pairs Timing Binds with PhosCity's Bidirectional Snapping ([README][scr-readme]). |
| petzku | 7: `SmartQuotify`, `Accenter`, `CellsBox`, `JumpToNextPlus`, `Typewriter`, `NewClipShape`, `Snapper` | **none** (no licence file; all rights reserved): clean-room only | EncodeClip is named as a future `gui.is_modified` user in [feature_04][f04]; not staged. |
| Zahuczky ([Zahuczkys-Aegisub-Scripts](https://github.com/Zahuczky/Zahuczkys-Aegisub-Scripts)) | — | BSD-3-Clause (GitHub licence API) | Not staged. |
| line0 (`l0.*`) | 4: `CascadeTransforms`, `Nudge`, `SplitLines`, `VerticalText` | MIT | ASSFoundation and DepCtrl are bundled. |
| lyger | 6 | MIT | — |
| Myaamori | 3: `Bounce`, `PasteFromPad`, `MergeScripts` | MIT | `MergeScripts` reads `line.extra`. |
| ILL (Zeref) | 2: `SplitText`, `ChangeAlign` | MIT | `ILL.SplitText` fails to load: clipper2 FFI not found (`expected.json`). |
| The0x539 ([Aegisub-Scripts](https://github.com/The0x539/Aegisub-Scripts): KaraTemplater, DeriveTrack, JoinPrevious) | — | **none:** no licence file, and none in the `src/` headers of KaraTemplater, JoinPrevious or DeriveTrack. Clean-room only. | Mixin highlighting (FA7) is for the0x's templater; Timing Binds' join is based on his JoinPrevious ([README][scr-readme]). |
| Akatsumekusa ([Akatmks/Akatsumekusa-Aegisub-Scripts](https://github.com/Akatmks/Akatsumekusa-Aegisub-Scripts): Blender Power-Pin export, others) | — | Apache-2.0 for the repo. It also ships MIT, GPL-3.0, LGPL-3.0, Zlib, CC-BY-3.0 and curl licence files for parts of it, so check per file before reuse. | It is the source of Power-Pin data for FA13. |
| CoffeeFlux ([TypesettingTools/CoffeeFlux-Aegisub-Scripts](https://github.com/TypesettingTools/CoffeeFlux-Aegisub-Scripts)) | — | MIT | It has a Scale Rotation Tags resample script ([README][scr-readme] "Resample Perspective"). |

---

## 7. Ranked list: adapt and better rows

The list runs from most valuable first. Value weighs how many users meet the row daily, whether files lose data today, and what the row unblocks, against its size. FA4 is first because the owner ranked it first among the FA picks (#561). FA3 is its blocker and ships before it. FA20 is last by the owner's word (#577).

| # | Row | Card | Size | Why |
|---|---|---|---|---|
| 1 | Lua API parity: `getPixel` r,g,b; `frame:data()`; `aegisub.version`; real `timecodes_file` and `video_zoom`; folds and plane through `line.extra`; all 14 arch macros with corpus runs | FA4 [#561](https://github.com/altqx/hikari/issues/561) | M | Owner's first pick. The fork's scripts then run unchanged, and FA6 depends on its corpus runs. |
| 2 | Keep `[Aegisub Extradata]` in the Document and round-trip `line.extra` | FA3 [#560](https://github.com/altqx/hikari/issues/560) | M | Files from Aegisub users silently lose folds and perspective quads when we save. Unblocks FA4, FA5, FA13 and six scripts. |
| 3 | Video pan and zoom, Aegisub 3.5 model by default | FA12 [#569](https://github.com/altqx/hikari/issues/569) | M | Every typesetter pans and zooms many times a session; matching 3.5's gestures removes the biggest muscle-memory break. |
| 4 | Snap start or end to video keeps joined lines joined | FA17 [#574](https://github.com/altqx/hikari/issues/574) | S | Today a snap breaks continuity with the neighbour; timers hit it constantly. |
| 5 | Native perspective motion tracking (Power-Pin) | FA13 [#570](https://github.com/altqx/hikari/issues/570) | L | The perspective counterpart of the bundled Aegisub-Motion, on VT3's maths. A typesetting feature no other editor ships natively. |
| 6 | Hotkeyable vector clip modes | FA9 [#566](https://github.com/altqx/hikari/issues/566) | S | Heavy clip work toggles modes many times per sign; 3.5 users have the bindings. |
| 7 | Shift by frames to video position (selection, and all following lines) | FA17 [#574](https://github.com/altqx/hikari/issues/574) | S | The retiming workflow after scene cuts; a frame-exact shift avoids millisecond drift. |
| 8 | Bake `\t`, `\fad` and `\move` in Split into frames | FA15 [#572](https://github.com/altqx/hikari/issues/572) | M | Split into frames is wrong for animated lines today. Fixing it makes the command trustworthy and replaces a script. |
| 9 | Resample adjusts 3D rotations | FA18 [#575](https://github.com/altqx/hikari/issues/575) | M | Resampling to another aspect ratio silently distorts every rotated sign; one option fixes it in place. |
| 10 | Resample keeps LayoutRes as TT does | FA18 [#575](https://github.com/altqx/hikari/issues/575) | S | Resample wrote the new PlayRes into LayoutRes, which changed `\blur` and 3D signs in files whose LayoutRes differs from PlayRes (§3). |
| 11 | Read zlib-compressed MKV subtitle tracks | — (needs a card; `vcpkg.json` is #600's) | S | FFmpeg is built without zlib, so such tracks come through as compressed bytes (§2b). A one-feature change plus a fixture. |
| 12 | LayoutRes and YCbCr Matrix prompts on opening video, with untagged and HDR warnings | FA9 [#566](https://github.com/altqx/hikari/issues/566) | M | Wrong matrix and LayoutRes values are among the commonest release bugs; 3.5 catches them at the source. |
| 13 | Video-position marker with frame shading and snapping to frame middles | FA10 [#567](https://github.com/altqx/hikari/issues/567) | M | Makes snapping to the video frame from the audio view safe, the reason 3.5 reworked it. |
| 14 | Subtitles-only copy and save on a transparent background | FA9 [#566](https://github.com/altqx/hikari/issues/566) | S | Used for tracing, previews and sharing signs; cheap given the renderer path. |
| 15 | Drawing and clip syntax highlighting (alternating X/Y, curve ends) | FA7 [#564](https://github.com/altqx/hikari/issues/564) | S | Long coordinate lists become navigable in the Line editor. |
| 16 | Insert hard space `\h` command | FA7 [#564](https://github.com/altqx/hikari/issues/564) | S | Daily for editors; 3.5 users expect Ctrl+Shift+Space. |
| 17 | Center times within frames | FA16 [#573](https://github.com/altqx/hikari/issues/573) | S | Prevents 1-frame flashes after time-based syncs (SubKt); a Shift times option makes it discoverable. |
| 18 | Join with previous using the audio selection | FA17 [#574](https://github.com/altqx/hikari/issues/574) | S | Lets timers who work without autocommit join without committing first. |
| 19 | Scroll the audio view to the line's start or end | FA10 [#567](https://github.com/altqx/hikari/issues/567) | S | Long lines at high zoom; small and expected by 3.5 users. |
| 20 | Convert Aegisub folds to line groups | FA5 [#562](https://github.com/altqx/hikari/issues/562) | S | Brings migrating users' structure into our one grouping model. |
| 21 | Border style as a three-way choice, including `4` | FA8 [#565](https://github.com/altqx/hikari/issues/565) | S | Replaces a checkbox that cannot represent the value; libass hardsubs use it. |
| 22 | Encoding `-1` for RTL and bidi shaping | FA8 [#565](https://github.com/altqx/hikari/issues/565) | S | The only practical way to typeset Arabic or Hebrew hardsubs with libass. |
| 23 | Shift+Enter inserts `\n` (setting) | FA7 [#564](https://github.com/altqx/hikari/issues/564) | S | For scripts with `WrapStyle: 2` that viewers can override. |
| 24 | Time fields past 9:59:59.99 | FA7 [#564](https://github.com/altqx/hikari/issues/564) | S | The loader already supports long files; the editor should not cap them. |
| 25 | Shape handle size setting | FA9 [#566](https://github.com/altqx/hikari/issues/566) | S | Accessibility and high-DPI precision for visual tools. |
| 26 | Reload video and reload audio | FA9 [#566](https://github.com/altqx/hikari/issues/566) | S | After re-encodes or replaced media, without losing the position. |
| 27 | Mixin lines highlighted as templates | FA7 [#564](https://github.com/altqx/hikari/issues/564) | S | Consistency for the0x KaraTemplater users; `karaoke_checks` already knows mixin. |
| 28 | Sort by text and by text without tags | FA11 [#568](https://github.com/altqx/hikari/issues/568) | S | Useful for QC (duplicates, signs lists); trivial. |
| 29 | Video details window with colour space and matrix | FA19 [#576](https://github.com/altqx/hikari/issues/576) | S | Diagnoses matrix problems; pairs with row 10. |
| 30 | Missing linked media reported | FA19 [#576](https://github.com/altqx/hikari/issues/576) | S | Today missing files can vanish silently from the association prompt (**unverified**). |
| 31 | Font collector asks before overwriting | FA19 [#576](https://github.com/altqx/hikari/issues/576) | S | Data safety; TT 3.5 asks too. |
| 32 | `lfs` 3.5 semantics (`dir` raises the message, `chdir`/`mkdir`/`rmdir`/`touch` return `true` or `nil, err`), `deep_copy` maps cycles to the copy, and `include()`/`require()` report the real load error | FA4 [#561](https://github.com/altqx/hikari/issues/561) (fold in) | S | Aegisub 3.5 library semantics; today a failing include shows only its own name. |
| 33 | Dummy video up to 100000 px, plus a 1080x1920 preset | FA9 [#566](https://github.com/altqx/hikari/issues/566) | S | The trace-a-render trick; a limit change. |
| 34 | Dummy video exact fractional fps | FA19 [#576](https://github.com/altqx/hikari/issues/576) | S | Frame-exact timing against dummy video. |
| 35 | Force mono playback setting | FA19 [#576](https://github.com/altqx/hikari/issues/576) | S | Accessibility and one-ear monitoring; small. |
| 36 | Align to video by key point | FA14 [#571](https://github.com/altqx/hikari/issues/571) | M | Clever for signs that appear and disappear, but niche; BSD-3 code available. |
| 37 | QC notes panel (Note Browser) | FA20 [#577](https://github.com/altqx/hikari/issues/577) | M | A strong QC workflow, but the script already runs as is; owner placed it last. |

---

## Checked items (2026-11-02)

The closing list of the first draft had 22 unverified items. Each was checked against the sources in the second row of "Sources and method", and its row now gives the evidence. The results:

| Item | Result | Row |
|---|---|---|
| AegisubChain under the per-script helper host | Statically compatible: it loads the other scripts inside its own Lua state. Its default scan path misses our `Autoload` folders on case-sensitive systems. Not run. | §1 AegisubChain |
| DerivePerspTrack's from-tags mode | Needs only bundled or served APIs. FA4 records the outer-quad mode, which reads the plane from `line.extra` | §1 |
| Fix KFX Boilerplate and our `argcheck` | Not needed here: our `argcheck` never raises on extra arguments. FA4 must keep that. | §1 |
| The fork's 2-decimal vector clip coordinates | In TT too. **Owner to re-decide** the skip. | §2b |
| Touchpad grid scrolling | Present | §2a |
| Compressed MKV subtitles | **Defect, uncarded:** FFmpeg is built without zlib | §2b, §7 row 11 |
| Non-zero timecode starts | Present by construction | §2b |
| Spectrum frequency mapping | Missing here; it is in TT | §2c |
| Stereo playback | Present (the default) | §2c |
| `floatedit` bounds | Present (unbounded) | §2d |
| Style decimal precision | Present (free text) | §3 |
| The Include path order | Present and tested; dropped from FA19 | §3 |
| Attachment extraction paths | n/a: there is no attachment extraction; dropped from FA19 | §3 |
| The `lfs` fixes | Missing: pre-3.5 shim; folded into FA4 | §3, §7 row 32 |
| `include()` error messages | Missing; folded into FA4 | §3, §7 row 32 |
| 3.5.1's dialog folders, track titles and file filters | Present; the filters are deliberately fixed | §3 |
| The font dialog underline | Present and tested | §3 |
| dav1d | Missing; the WebM thread's #600 | §3 |
| "FHD vertical" | Missing; folded into FA9 | §3, §7 row 33 |
| The 3.5.1 LayoutResY border case | n/a (no border option). **Defect, fixed in FA18:** Resample overwrote LayoutRes. | §3, §7 row 10 |
| Licences of Zahuczky, The0x539, Akatsumekusa and CoffeeFlux | BSD-3-Clause, none (clean-room only), Apache-2.0 (mixed per file), MIT | §6 |
| Git Signs and Timing Binds as recorded runs | Still open: FA4 adds the runs | §1 |

**Still marked unverified,** each owned by the card that will touch it:

- the Git Signs, Timing Binds and Perspective-Motion runs (FA4);
- missing linked media and the dummy video's exact fps (FA19).

The rest are rows already proposed as **skip**, where a check would not change the proposal.

[scr]: https://github.com/TypesettingTools/arch1t3cht-Aegisub-Scripts/tree/5c3c782
[scr-readme]: https://github.com/TypesettingTools/arch1t3cht-Aegisub-Scripts/blob/5c3c782/README.md
[scr-lic]: https://github.com/TypesettingTools/arch1t3cht-Aegisub-Scripts/blob/5c3c782/LICENSE
[fork]: https://github.com/arch1t3cht/Aegisub
[fork-readme]: https://github.com/arch1t3cht/Aegisub/blob/2cdf0043/README.md
[gui]: https://github.com/arch1t3cht/Aegisub/blob/feature/automation/v4-docs/gui.txt
[getframe]: https://github.com/arch1t3cht/Aegisub/blob/feature/automation/v4-docs/get-frame.txt
[misc]: https://github.com/arch1t3cht/Aegisub/blob/feature/automation/v4-docs/misc.txt
[f01]: https://github.com/arch1t3cht/Aegisub/releases/tag/feature_01
[f02]: https://github.com/arch1t3cht/Aegisub/releases/tag/feature_02
[f03]: https://github.com/arch1t3cht/Aegisub/releases/tag/feature_03
[f04]: https://github.com/arch1t3cht/Aegisub/releases/tag/feature_04
[f05]: https://github.com/arch1t3cht/Aegisub/releases/tag/feature_05
[f06]: https://github.com/arch1t3cht/Aegisub/releases/tag/feature_06
[f07]: https://github.com/arch1t3cht/Aegisub/releases/tag/feature_07
[f08]: https://github.com/arch1t3cht/Aegisub/releases/tag/feature_08
[f09]: https://github.com/arch1t3cht/Aegisub/releases/tag/feature_09
[f10]: https://github.com/arch1t3cht/Aegisub/releases/tag/feature_10
[f11]: https://github.com/arch1t3cht/Aegisub/releases/tag/feature_11
[f12]: https://github.com/arch1t3cht/Aegisub/releases/tag/feature_12
[mig01]: https://github.com/arch1t3cht/Aegisub/releases/tag/migration01-01
[mig03]: https://github.com/arch1t3cht/Aegisub/releases/tag/migration03-02
[tt]: https://github.com/TypesettingTools/Aegisub/tree/d8ad830
[tt-lic]: https://github.com/TypesettingTools/Aegisub/blob/d8ad830/LICENCE
[tt-readme]: https://github.com/TypesettingTools/Aegisub/blob/d8ad830/README.md
[tt-rel340]: https://github.com/TypesettingTools/Aegisub/releases/tag/v3.4.0
[tt-rel350]: https://github.com/TypesettingTools/Aegisub/releases/tag/v3.5.0
[cl341]: https://aegisub.org/changelog/3.4.1/
[cl350]: https://aegisub.org/changelog/3.5.0/
[cl351]: https://aegisub.org/changelog/3.5.1/
