# A node editor for karaoke effects in HikariSub

> **Decided 2026-10-28.** The owner settled the open questions at the end of this note in a grilling session. The decisions are in the wave-8 tracking issue [#410](https://github.com/altqx/hikari/issues/410) (cards NE0–NE5, #422–#427); where they differ from a recommendation here, #410 wins.

This note supports a feature the owner asked for after wave 7: "our own implementation of
[Pyxis-Effector](https://github.com/Mandholine/Pyxis-Effector), a node editor built into Hikari". It
covers five things:

- what Pyxis Effector is;
- what the other ASS effect tools offer;
- how established node editors evaluate, group and inspect graphs;
- which Qt/QML libraries could draw the canvas;
- how a graph would fit HikariSub's existing karaoke templater, its preview and the planned plugin and AI
  features.

Sources read:

- **Pyxis Effector** at commit `375ad89b49` (v1.1.6, 2026-10-25), cloned into an empty cache directory
  and only read. None of its scripts, its embedded Python or its binaries were run. Short links such as
  [nodes_library.py:97] point to that commit; the list is at the end.
- **HikariSub `origin/qt` at `62446e2b5`** (2026-10-28), read from a `git archive` export. The local `qt`
  checkout is 367 commits behind and has neither KE2 nor KE3. Paths such as
  `src/application/generated_runs.cpp:5` refer to that commit.
- **Other tools' first-party docs and source:**
  - the Aegisub manual;
  - The0x539's templater doc;
  - PyonFX source;
  - arch1t3cht's templaters guide;
  - the Blender manual and developer docs, SideFX docs, Epic docs and the Foundry NDK guide;
  - ComfyUI docs, RFCs and source;
  - the READMEs and licence files of the candidate libraries.
- **Repository metadata** (licence, last push, stars, releases) comes from `gh api`, fetched on
  **2026-10-28**. All web pages were fetched on 2026-10-28.
- **The earlier karaoke survey** (`hikari-wt/wave6/research/karaoke.md`, 2026-10-20). It was used as a
  lead list. Every claim taken from it here was checked again, except two, which are marked "(from the
  2026-10-20 survey)".

Terms follow [CONTEXT.md](../../CONTEXT.md): Line, Document, ASS override tag, Automation macro. The note
also uses these terms:

- **Karaoke Line:** a Line the templates apply to (`isKaraokeLine`,
  `src/application/include/hikari/application/template_preview.h:27-29`).
- **Generated Line:** a Line a run writes.
- **Unit:** a line, word, syllable, character or furigana syllable that an effect iterates over.
- **Graph:** a node network.

## 0. Summary

- **What Pyxis is.**
  - It is a **GPL-3.0** node editor for karaoke effects, launched from an Aegisub macro.
  - Its stack is Python 3.11 with LiteGraph.js in a pywebview window, plus an embedded Python with numpy,
    pyopencl, lupa and pythonnet.
  - It runs on **Windows only** (Aegisub 3.2–3.4).
  - It is **two months old**: 24 commits from 2026-08-26 to 2026-10-25, one committer, 6 stars, 0 issues,
    and three releases with 37, 7 and 4 downloads. The README says it was built with Google Gemini's help.
  - It ships **68 node types** in 8 categories.
  - It calls itself a rebuild of Kara Effector 3.6, which has no licence.
- **Pyxis's model.**
  - "Loop" wires decide how often a branch runs: per line, word, syllable or character, then per
    sub-loop, frame or particle.
  - "Data" wires carry **strings**. Most fields are Python expressions passed to `eval()`.
  - Every `Output` node evaluates its own upstream tree lazily, with per-cycle memoisation.
  - Generated lines get Effect `pyxis_fx` and Actor = queue id. The source lines are commented out.
  - Preview is an embedded JASSUB (libass in WebAssembly) player with a per-frame render-cost heat map.
  - Graphs are saved as LiteGraph JSON (format 0.4) in `%APPDATA%`, not in the subtitle file.
  - There are no node groups, the ports are untyped, and opening a shared graph runs its Python.
- **"Our own implementation" should be clean-room.**
  - Hikari is GPL-3.0 as well, so copying is legally possible with notices.
  - But Pyxis is Python and JavaScript, which Hikari cannot run in-process.
  - Its Lua function libraries come from the Kara Effector line, which has no licence. Hikari already
    keeps its KE3 presets free of that code (`docs/karaoke-presets.md:5`).
  - Reuse the ideas: loop/data separation, per-iterator random scopes, queue ordering, the render-cost
    heat map. Do not reuse the code.
- **What Hikari already has.**
  - The KE1–KE3 templater stack covers the hard half of the feature:
    - generated Lines are tied to their source (`LineRecord::generatedFrom`);
    - the selected Lines can be regenerated on their own;
    - every run returns a JSON report with per-template timings and placed problems;
    - a per-Line preview drawn on the video costs about 1.2 ms;
    - templates holding Lua need the user's consent before they run;
    - the Grid folds generated Lines.
  - A node editor should **produce its Lines through the same seams**:
    - `MacroResult`-style output;
    - `templatePreviewDocument` for the video;
    - generated-run folding.
- **Prior-art lessons.**
  - Typed sockets, plus Blender's "field" idea: a value is a function evaluated per element in the context
    of the node that uses it.
  - Lazy pull evaluation (Blender, Nuke, Pyxis).
  - A signature cache that re-runs only the nodes whose inputs changed (ComfyUI).
  - Node groups as functions (Blender, ComfyUI subgraphs).
  - Inspection from the last evaluation: socket values, node warnings, node timings (Blender).
- **Canvas.**
  - A **custom Qt Quick canvas with Qt Quick Shapes wires** fits Hikari best. Hikari already uses
    `QtQuick.Shapes` and owns its theming, focus and accessibility (ADR 0001).
  - QuickQanava (BSD-3, QML, Qt ≥ 6.5) is the only maintained QML library. It is usable, but its last
    release is 2.5.0 from 2024-09-22.
  - QtNodes (BSD-3) is QGraphicsView/Widgets only. A QML frontend is on its "Help Needed" list.
  - imnodes/Dear ImGui (MIT) lacks accessibility and full internationalisation by its own README.
- **Recommendation:**
  - Build a native, typed, deterministic graph engine in the application layer.
  - It writes ordinary ASS Lines as a templater run does.
  - Store the graph inside the Document.
  - The first milestone is to re-express all ten KE3 presets as graphs whose Lines equal the existing
    goldens byte for byte.

## 1. Pyxis Effector

### 1.1 What it is, licence, stack and maturity

| Item | Finding | Source |
| --- | --- | --- |
| Purpose | "Visual Node-Based Karaoke Automation Engine for Aegisub". It "renovates and extends the technical architecture of the legacy Kara Effector 3.6 project, replacing sequential monolithic script execution with a visual environment structured around directed acyclic graphs (DAGs)". | [README.en.md] |
| Licence | GPL-3.0. The `LICENSE` file is the GPLv3 text, and the source headers read "Copyright (C) 2026 @mandholine, @effector2, @pholguinc … GNU General Public License … version 3". | [LICENSE], [nodes_library.py:1-15], `gh api` |
| Authors | Trota/@mandholine (UI and Python engine); Itachi/@effector2 ("Lua core function libraries … Historical developer of legacy Kara Effector"); Zaph/@pholguinc (the Python–Lua bridge). Every commit is by one committer. | [README.en.md], `git log` |
| AI disclosure | "the technical design and web interface architecture of this project were developed with technical assistance from Google Gemini via Antigravity" | [README.en.md] |
| Stack | Python 3.11 engine (`src/node_engine.py` alone is 8,575 lines), LiteGraph.js canvas, Ace editor, ArtPlayer and JASSUB/libass WASM player, Clipper2/PyClipper, Potrace, Lupa (Lua 5.1/LuaJIT inside Python), PyWebView, FFmpeg. The repo vendors an embedded CPython with numpy 2.4, pyopencl, lupa 2.8, pythonnet and pywebview: 2,436 files under `PyxisNodes/python/Lib`. | [README.en.md], file tree |
| Platform | Windows 10/11 64-bit, Aegisub 3.2.x–3.4.x. The Aegisub bridge calls `kernel32` and `shell32` through LuaJIT FFI and starts Python with `ShellExecuteA`. | [README.en.md], [Pyxis_engine.lua:49-63], [Pyxis_engine.lua:537] |
| Maturity | Repo created 2026-08-26, last push 2026-10-25. 24 commits, 6 stars, 1 fork, 0 open issues, one merged PR. Releases: v1.1.4 (2026-10-16, 37 downloads), v1.1.5 (2026-10-20, 7), v1.1.6 (2026-10-25, 4). | `gh api`, `git log` |
| Documentation | A 12-volume manual in English and Spanish (`PyxisNodes/docs/en`, about 520 KB), plus 69 per-node screenshots in `docs/img`. | [docs/en] |

### 1.2 How the graph runs

**Two kinds of wire.**

- **Flow ("loop") links** set "how many times each branch executes (per dialogue line, syllable,
  character, sub-particle, or video frame)". They connect `Main Loop`, `MaxLoop`, `Script Code` and
  `Output` ([01 Visual Paradigm.md:18-26]).
- **Data links** carry "numerical values …, text strings, Cartesian coordinate pairs, … ASS colour values,
  ASSdraw vector paths …, and formatted ASS tag blocks" ([01 Visual Paradigm.md:28-30]). In the saved file
  the port types are only `"data"` and `"loop"` ([Example_Simple_Effect.json]). Every value is passed as
  a string: `get_input_value` returns `str(val)`, and several cables into one port are joined with `""`
  ([node_engine.py:1460-1524]).
- **Inline fields.** Each input has an inline text field, and "the data transported by the cable takes
  absolute precedence" ([02 Interface Anatomy.md:94-110]).

**Evaluation.**

- **Pull, per iteration.** "A data node is only computed when the active execution flow requests its value
  during the current iteration", and outputs can branch freely "thanks to internal per-cycle memoisation"
  ([01 Visual Paradigm.md:31-33]). The engine finds a wire's source by scanning the link list on each
  call ([node_engine.py:1515]).
- **Five phases** ([01 Visual Paradigm.md:37-83]):
  1. choose Lines by the `Output` node's scope (selected Lines, Lines of the selection's style, or all
     Lines);
  2. `Main Loop` sets the base unit: `syl`, `char`, `word` or `line`, with or without blanks;
  3. `MaxLoop` multiplies a unit N times, or splits its span into frames (`fbf`);
  4. data nodes compute positions, shapes, colours and `\t` timings;
  5. `Output` assembles and emits Lines.
- **Generator sub-loops.** Generator nodes wired to an `Output` (Slicer, Union & Divide, Concentric
  Borders, Array, Multi, Particles, IMG to pixels, Clip Gradient, Animation) open their own sub-loops
  automatically, "in topological post-order", each with its own counter (`s_i`, `a_i`, `p_i` …)
  ([01 Visual Paradigm.md:72-77]).
- **Isolated outputs.** Each `Output` node "evaluates its own connected upstream tree" with "a clean,
  private context dictionary for each branch and iteration" ([01 Visual Paradigm.md:85-88]).
- **Expressions.** Fields fall into three classes ([09 Variables.md:733-790]):
  - **Type A:** Python expressions, run through `eval()` with an LRU of 2,048 compiled expressions
    ([node_engine.py:934-1176], [string_evaluator.py:702]);
  - **Type B:** ASS tag templates with variable substitution and "Dark Tags" (shorthands such as `\fscxy`
    and `\tagR(...)`);
  - **Type C:** literal text.
- **Linter.** A validator parses Type A fields with `ast.parse` and flags undeclared variables before a run
  ([09 Variables.md:737-747]).
- **Variables.** Context variables follow the karaskel tradition: `line.*`/`l.*`, `syl.*`, `char.*`,
  `word.*`, `prev`/`next`, real ink metrics `ink.*`, `fx.*`, `sync.*` for gaps between Lines, and
  `meta.*` ([04 System.md:149-167], [09 Variables.md:152-397]).
- **State.** `Define Vars` adds variables for the current iteration. `Memory` keeps values across
  iterations, for accumulators ([01 Visual Paradigm.md:108-117]).
- **Parallelism.** The README claims "multi-process parallel evaluation and OpenCL GPU acceleration", and
  `main.py` manages "multiprocessing worker pools" ([README.en.md], [main.py:239]).

**Randomness and seeds.**

- `MultiRandom` has 15 modes. Its `Iterator` input picks the scope over which a value stays the same; left
  empty, "calculates a fresh value on every call" ([03 Basics.md:231-276]).
- The same scope is available in any expression as `by.<iterator>.<mode>(…)`, for example `by.syl.Rs(15)`.
  "Within the same cycle of the specified iterator, repeated queries return the identical memoised value"
  ([03 Basics.md:242-255]).
- `Rfake` is a hash of the counter, `(c·9301 + 49297) mod 233280` ([03 Basics.md:270]).
- Particles, Glitter, Filter (Noise) and Bezier take explicit seeds ([node_engine.py:2237],
  [node_engine.py:3677-3679], [node_engine.py:5519], [node_engine.py:5812]).
- v1.1.6 had to fix particle seeds so they are "fully independent, guaranteeing consistent and repeatable
  particle distributions across render cycles" ([Changelog_v1.1.6.en.md]). Determinism was added after
  the fact, not designed in.

**Output and write-back.**

- **What a generated Line looks like.**
  - Effect `"pyxis_fx"`, Actor = `Queue ID`, Layer, Start and End from the inputs or the context, and
    text `{Tags}Return` ([node_engine.py:7207-7221], [04 System.md:38-66]).
  - Shape payloads get `\fscx100\fscy100` unless the user set a scale ([04 System.md:42-47]).
- **Order.** Lines are emitted queue by queue: the lowest `Queue Order` first, ties broken by queue name,
  then by source-Line index ([01 Visual Paradigm.md:90-106]).
- **The bridge.** The Aegisub macro writes the Document to a temporary ASS file. Selected Lines get a
  `PyxisMetrics` Comment marker ([Pyxis_engine.lua:399-480]). The macro waits on a lock file, reads the
  result file, comments out the used source Lines, appends the new Lines with `subs.append`, and sets
  one undo point ([Pyxis_engine.lua:600-667]).
- **Re-runs.** Lines with Effect `pyxis_fx` are left out of the export ([Pyxis_engine.lua:408]). However,
  the macro has no `subs.delete` call, so I found no code that removes the previous run's Lines on a
  re-run. This was not run to confirm.

**Preview.**

- The `System/Previewer` node is a JASSUB/libass WebAssembly player inside the canvas
  ([04 System.md:73-125]). It has two sources:
  - a sandbox script, `%APPDATA%\PyxisNodes\preview_base.ass`, reloaded when its SHA-256 changes;
  - the active Aegisub project's video, audio, styles and Lines.
- In project mode "only original lines replaced by the `Output` scope … are commented out", so other Lines
  stay visible ([04 System.md:83-89]).
- Playback is clamped to the effect's span plus a 2,000 ms cushion on each side
  ([11 Base Environment and Customisation.md:56-80]).
- A **thermal heat map** under the seek bar plots each frame's render cost against the frame budget
  (`1000/FPS`), measured by a bundled native profiler, `bin/libass_profiler.exe`
  ([04 System.md:105-109], [01 Visual Paradigm.md:407-460]).
- The `Animation` and `IMG to Shape` nodes have their own small players inside the node
  ([Changelog_v1.1.6.en.md]).

### 1.3 Saved graph format

A template file is JSON ([Example_Simple_Effect.json]):

```json
{ "schema_version": 1,
  "metadata": { "template_id": "...", "name": "...", "comment": "...", "created_at": "...",
                "aegisub_version": "3.4.2", "os": "...", "pyxis_engine_version": "1.1.6",
                "node_count": 22, "node_types": ["Basics/Number", "..."], "is_favorite": false },
  "graph": { "version": 0.4, "last_node_id": 33, "last_link_id": 51,
             "nodes": [ { "id": 19, "type": "Basics/Number", "pos": [x, y], "size": [w, h],
                          "order": 0, "mode": 0, "outputs": [ { "name": "Number", "type": "data", "links": [18] } ],
                          "properties": { "value": "l.start_time + syl.start_time" } } ],
             "links": [ [18, 19, 0, 15, 2, "data"] ],
             "groups": [], "config": {}, "extra": { "canvas_zoom": 0.505, "canvas_offset": [494.6, -290.5] } } }
```

- **The format.**
  - `graph` is LiteGraph's own serialisation, version 0.4.
  - A link is `[id, origin node, origin slot, target node, target slot, type]`, the same array form
    ComfyUI's 0.4 workflows use (ComfyUI started from LiteGraph; see §3.5).
  - Node `properties` hold the inline field strings, including Python expressions.
- **Where graphs live.** Graphs and sessions live in `%APPDATA%\PyxisNodes\` ([README.en.md]). I found
  no code that writes a graph into the subtitle file, so an `.ass` file does not carry the effect that
  made it.
- **Safety.** Loading someone else's template means running its Python. Every Type A field is passed to
  `eval()`, and a `Script Code` node runs arbitrary Python or Lua ([04 System.md:127-175]).

### 1.4 UI

- **Layout.** The README shows only the logo. The manual describes:
  - a top bar for save/export, the template library and Generate;
  - the LiteGraph canvas;
  - a floating toolbar for zoom, centring and undo/redo;
  - toasts in the bottom-right corner ([02 Interface Anatomy.md:7-32]).
- **Nodes.** A node has:
  - a coloured header with a mode combo on the right;
  - input sockets, each with an inline text field;
  - output sockets on the right;
  - footer badges for the counters a node adds (`s_i, s_n`, `a_i, a_n` …)
    ([02 Interface Anatomy.md:94-134]).
- **Colours.** Categories are colour-coded: Basics blue, Shape green, Time orange, Functions purple,
  Variables turquoise, Loops red, Audio amber, System grey ([02 Interface Anatomy.md:136-151]).
- **Screenshot.** [docs/img/previewer.png] shows the Previewer node:
  - a purple header reading "Previewer";
  - a field listing the connected outputs ("Main, Hilight");
  - a media line (H.264, 1920×1080, 23.98 fps);
  - four header buttons (the manual names source toggle, pin, refresh and hardsub render, [04 System.md:83-93]);
  - the video with the effect, and a purple render-cost curve labelled "PEAK 2.39 ms" with a
    "TELEMETRY ↗" link;
  - a seek bar and transport controls.
  The other 68 images show one node each.

### 1.5 Every node type Pyxis ships

`nodes_library.py` registers 68 system nodes ([nodes_library.py:97-1040]). The manual counts 67
([02 Interface Anatomy.md:138]). The descriptions below paraphrase the "Purpose" line of each node's
manual entry.

| Category | Node | Key ports (in → out) | What it does |
| --- | --- | --- | --- |
| Loops | Main Loop | — → Loop, Iterator (i), Total (N) | Sets the base unit (`syl`, `char`, `word`, `line`; with or without blanks) and starts the flow ([nodes_library.py:97]) |
| Loops | MaxLoop | Loop In, Start, End → Loop Out, Counter, Total | Multiplies the current unit N times (`normal`), or splits its span into frames (`fbf`), with counter `j` ([nodes_library.py:106]) |
| Loops | Module | Iterator, Total, Sub-step → Data | Turns counters into a 0–1 progress or modular steps ([nodes_library.py:119]) |
| System | Output | Loop/Domain, Layer, Start, End, Tags, Return, Queue ID, Queue Order → Preview | Terminal node: builds and emits the ASS Lines, with scope and queue order ([nodes_library.py:130]) |
| System | Previewer | Output → — | The JASSUB/libass player with the render-cost heat map ([nodes_library.py:140]) |
| System | Script Code | Loop In, In1…InN → Loop Out, Result | Runs inline Python 3.11 or Lua 5.1/LuaJIT with the context in scope ([nodes_library.py:150]) |
| System | Benchmark Math | Iterations → Out, Time ms | Times a CPU benchmark (diagnostics) ([nodes_library.py:164]) |
| Variables | Sys Var | — → value | Reads one context metric: line/syl/char/word fields, ink, sync, meta ([nodes_library.py:173]) |
| Variables | Define Vars | In → … | Declares local variables for the current iteration ([nodes_library.py:180]) |
| Variables | Get Var | — → Data | Reads any variable by name, as a wireless link ([nodes_library.py:187]) |
| Variables | Memory | Key, Value → … | Keeps a value across iterations (remember/retrieve) ([nodes_library.py:194]) |
| Basics | Number | — → Number | A number or an expression ([nodes_library.py:252]) |
| Basics | String | — → String | A literal string, with macros ([nodes_library.py:224]) |
| Basics | Text | text → text | String operations, tag stripping, kana transliteration, romaji to `\k` ([nodes_library.py:213]) |
| Basics | Concatenate | In1…InN → out | Joins inputs with a separator, or interleaves lists (`zip`) ([nodes_library.py:231]) |
| Basics | MultiTag | → tags | Builds ASS override tags from fields ([nodes_library.py:238]) |
| Basics | Dynamic Format | → text | printf-style formatting, applied over lists ([nodes_library.py:245]) |
| Basics | Color Picker | — → colours | Colours and palettes, ASS `&HBBGGRR&` ↔ `#RRGGBB` ([nodes_library.py:259]) |
| Basics | Math | A, B → out | 8 families: arithmetic, trigonometry, rounding, easing, waves, utilities, clamping ([nodes_library.py:436]) |
| Basics | MultiRandom | Min, Max, Step, Total, Iterator → Data, Batch | Seeded or scoped random values in 15 modes ([nodes_library.py:277]) |
| Basics | Once | in → out | Caches a value for one scope ([nodes_library.py:994]) |
| Basics | Only | True, False → Out | Routes by a condition on the context ([nodes_library.py:1005]) |
| Functions | Interpolator | Iterator, Total, Accel, V1, V2 → Data, Batch | Interpolates numbers, colours, alphas, tag blocks or shapes ([nodes_library.py:206]) |
| Functions | Conditioner | Condition, If True, If False → out | `A if cond else B` ([nodes_library.py:266]) |
| Functions | Polar to Cartesian | r, θ, origin → x, y, `\pos` | Polar to Cartesian coordinates ([nodes_library.py:284]) |
| Functions | Color Gradient | Progress, colours, accel → colour/tag | Multi-stop gradient with easing ([nodes_library.py:292]) |
| Functions | Clip Gradient | Cols, Rows, Angle, Overlap, Order → Output, Progress, Index … | Splits a unit's ink box into `\clip` stripes or a mesh for gradients (a sub-loop) ([nodes_library.py:428]) |
| Functions | Bezier Curve | points → path, pos, angle | N-degree Bézier curves, arc-length motion, tangents ([nodes_library.py:443]) |
| Functions | Particles | Path, Particle, Density, Seed … → shapes, `\pos`, X/Y arrays | Particle swarms sampled from outlines or fills (a sub-loop) ([nodes_library.py:485]) |
| Functions | IMG to pixels | Image, Particle, Frame … → shapes, colours, `\pos` | Turns image pixels into coloured particles (a sub-loop) ([nodes_library.py:503]) |
| Functions | Text to Shape | Text, Font, Size, Scale → Shape, Width, Height | Glyph outlines as ASS drawings ([nodes_library.py:520]) |
| Functions | Text on Path | text, path → tags | Places glyphs along a path with tangent `\frz` ([nodes_library.py:533]) |
| Functions | Move Text | Text, deltas, path, t1, t2, accel → Text | Moves text using `\fsp`/`\fscy` compensation ([nodes_library.py:554]) |
| Functions | Move Tag | path, waypoints, jitter → tags | `\pos`/`\t` motion blocks from a path, waypoints or jitter ([nodes_library.py:566]) |
| Functions | Tag Manipulator | Tags In, Target Tag → Tags Out, Value, Raw | Inverts, resets or extracts tags ([nodes_library.py:909]) |
| Functions | Animation | Shape list, Step, Start, End → … | Plays a list of shapes frame by frame or by alpha (a sub-loop) ([nodes_library.py:921]) |
| Functions | Tag Oscillator | Start, End, Step, Trans, Duty … → Tag Out … | Periodic `\t` chains: strobe, waves, rhythms ([nodes_library.py:963]) |
| Functions | Glitter | Start, End, Density, In 1 → List, TAG 1 | Random sparkle and flash `\t` chains ([nodes_library.py:976]) |
| Time | Retime | Tags/`\t`, Offset In, Offset Out, Accel → `\t` Out, Start, End, Duration | Retimes to a phase (`syl`, `presyl`, `postsyl`, `line`, `preline`, `postline`, `start2syl`, `syl2end`, `abs`, `sylpct`) and rescales `\t` blocks ([nodes_library.py:938]) |
| Time | Lead In | Delay → Data | Start offset stepped by unit index ([nodes_library.py:300]) |
| Time | Lead Out | Delay → Data | End offset stepped by distance from the last unit ([nodes_library.py:307]) |
| Time | Middle 1 | Delay → Data | Offset symmetric about the Line's centre, for entrances ([nodes_library.py:314]) |
| Time | Middle 2 | Delay → Data | The same, anchored to the Line's end ([nodes_library.py:321]) |
| Time | Loop 1 | Delay → Data | Offsets by the MaxLoop counter `j` ([nodes_library.py:328]) |
| Time | Loop 2 | Delay → Data | Offsets by the nested counter `J` ([nodes_library.py:335]) |
| Time | Ipol | Duration, Points, Accel → Data | Splits a span into eased keyframe intervals ([nodes_library.py:342]) |
| Time | Duration | Start, End → ms | End minus start, or the unit's duration ([nodes_library.py:353]) |
| Time | Offset | Base → ms | Adds an offset to a time ([nodes_library.py:363]) |
| Time | Synced | Constant, Fade … → … | Extra hold time from the gap to the next Line ([nodes_library.py:370]) |
| Time | Parse ASS Time | ASS Time → ms | `H:MM:SS.CC` to ms ([nodes_library.py:385]) |
| Time | Format ASS Time | ms → ASS time | ms to `H:MM:SS.CC` ([nodes_library.py:392]) |
| Time | Time to Frame | ms, FPS → frame | ms to frame ([nodes_library.py:399]) |
| Time | Frame to Time | frames, frame dur → ms, ASS time | Frame to ms ([nodes_library.py:409]) |
| Shape | Library | — → shape | Built-in shapes in a 0–100 box ([nodes_library.py:419]) |
| Shape | IMG to Shape | Image, Frame, Threshold … → Shape, frames | Vectorises images and GIFs with Potrace ([nodes_library.py:606]) |
| Shape | Array | Particle, Cols, Rows … → Shape, `\pos`, X, Y, angle, index … | Grid, hexagonal, ring, spiral or scatter copies (a sub-loop) ([nodes_library.py:648]) |
| Shape | Slicer | shape, mode … → pieces | Cuts a shape into 23 tessellations: grid, hex, Voronoi, jigsaw … (a sub-loop) ([nodes_library.py:660]) |
| Shape | Stroke | Shape/Text, Width → Shape | Turns an open path into a stroked outline (Clipper2) ([nodes_library.py:680]) |
| Shape | Walker | path, progress → partial path, `\pos`, angle | Draws a path on progressively; position and tangent along it ([nodes_library.py:693]) |
| Shape | Manipulator | Shape/Text, Factor, Scale … → Shape | Scale, borders, deform, move, redraw, 3D rotate, reflect, matrix ([nodes_library.py:707]) |
| Shape | Transform | Shape/Text, Factor, Ratio → Shape | Inflate, skew, round corners, bend, morph ([nodes_library.py:732]) |
| Shape | Filter | Shape, Expr X, Expr Y, Factor, Subdivs → Shape | Per-vertex expressions, stepped transforms, noise ([nodes_library.py:754]) |
| Shape | Clipper | Shape A, Shape B, Mode … → Shape, list, bbox | Boolean operations, offsets, Minkowski (Clipper2) ([nodes_library.py:772]) |
| Shape | Union & Divide | Shape In, Fill Rule … → Shape, list, bbox | Splits a shape into islands, or merges pieces (a sub-loop) ([nodes_library.py:830]) |
| Shape | Multi | Mode, Size … → Shape | Procedural nested patterns: rings, stripes, rosettes (a sub-loop) ([nodes_library.py:862]) |
| Shape | Concentric Borders | Shape, Base Mode, Max Rings … → Shape, `\pos`, Index, Total | Concentric offset rings and ripples (a sub-loop) ([nodes_library.py:890]) |
| Audio | Modulator | Audio, Tag, Min, Max, Gain, Step … → tags | Drives a tag from audio RMS or FFT energy as `\t` chains ([nodes_library.py:1016]) |
| Audio | Waveform | Audio, Width, Height, Samples … → Shape, `\pos`, arrays | Draws a waveform or spectrum as ASS drawings ([nodes_library.py:1037]) |

## 2. Other ASS effect tools

### 2.1 Aegisub's karaoke templater (the stock templater, 2.1.7)

- **Template and code Lines** are declared in the Effect field. Inline `!…!` code replaces the old `%…%`
  ([karaoke templater overview]).
- **Classes.** `pre-line`, `line`, `syl` (the default), `furi`, `syl furi`, and `once` for code Lines.
- **Modifiers** ([template modifiers]):
  - `char` ("work per-character instead of per-syllable");
  - `all` ("Apply template to all styles");
  - `repeat n`/`loop n` ("must be a constant integer number");
  - `notext`, `keeptags`;
  - `noblank` (skips blank and zero-length syllables);
  - `multi` ("per-highlight in multi-highlight timed karaoke");
  - `fx name` (inline-fx);
  - `fxgroup name`.
- **Generated Lines.** A run writes Dialogue Lines with Effect `fx`
  (`Automation/automation/Autoload/kara-templater.lua:746`, `:1058`). The next run deletes every
  non-Comment Dialogue whose Effect is exactly `fx` (`kara-templater.lua:129-131`, `:158-161`).
- **`retime` modes:** `syl`, `presyl`, `postsyl`, `line`, `preline`, `postline`, `start2syl`, `syl2end`,
  `set`/`abs`, `sylpct` (`kara-templater.lua:442-472`). Pyxis's `Retime` node offers the same ten modes
  ([nodes_library.py:938]).
- **Randomness.** The template environment gets `math` and `_G` (`kara-templater.lua:426-433`), so
  templates use the unseeded `math.random`. Hikari's selected-Lines run names "the template uses random
  values" as a reason it cannot match Lines (`kara-templater.lua:1459`).

### 2.2 Hikari's own templater stack (KE1–KE3, KP1)

- **KE1 ([#283](https://github.com/altqx/hikari/issues/283)).** Output stays byte-identical to 2.1.7 on a
  golden corpus (`docs/karaoke-templater.md:9-17`). KE1 adds ([karaoke-templater.md:23-29]):
  - template problems that name the Line;
  - "Apply karaoke template to selected lines";
  - per-template timing;
  - a render-cost overlay on the video.
- **Linking generated Lines to their source.**
  - Each generated Line carries the source Line's id in memory, `LineRecord::generatedFrom`, which is
    never saved (`src/core/include/hikari/core/document.h:115`; `karaoke-templater.md:35`).
  - A file opened without these marks is matched by running the templates again. The match is refused
    when the template or the Lines changed, or when the template uses random values
    (`karaoke-templater.md:42`).
- **The run report.** Every run returns a version-1 JSON report: `mode`, `seconds`, `prepare_seconds`,
  `karaoke_lines`, `generated`, `problems`, per-template `{line, effect, style, seconds, generated}`, and
  placed `issues` with `start`/`length` (`karaoke-templater.md:46-61`).
- **The `aegisub.hikari` table.** Scripts get `sources()`, `set_source(i, source)`, `report(text)` and
  `clock()` (`karaoke-templater.md:75-86`). Any generator running in the Lua helper can use them to tie its
  Lines to their source.
- **KE2 ([#284](https://github.com/altqx/hikari/issues/284)): the Template editor**
  (`karaoke-templater.md:88-104`). Each edit runs "Apply karaoke template to selected lines" on a
  **reduced snapshot**: Script Info, Styles, the template and code Lines, and the current karaoke Line
  (`template_preview.h:52-59`).
  - **Where it runs.** In a helper of its own, separate from the Automation menu. A newer run replaces the
    one in flight, and a run past the grace period loses its helper
    (`src/backends/include/hikari/backends/karaoke_preview_runner.h:3-10`). It may use only read-only host
    services (`template_preview.h:103-107`).
  - **On the video.** `templatePreviewDocument` puts the preview in place of the Line's generated Lines
    and keeps only the Lines over the current window (`template_preview.h:72-101`).
  - **Cost.** The median preview is about 1.2 ms on `stock-tutorials`
    (`docs/compatibility-decisions.md:263`).
  - **Consent.** Nothing runs until the user chooses Preview for a Document whose templates hold Lua
    (`karaoke-templater.md:93`).
- **KE3 ([#285](https://github.com/altqx/hikari/issues/285)): ten presets**, each one ASS file of
  template and code Lines (`src/application/karaoke-presets/*.ass`).
  - The presets are original work. They use "no random values … in integer arithmetic, so the output is the
    same on every platform", and carry no state from Line to Line, so regenerating one Line alone matches
    a whole run (`docs/karaoke-presets.md:5`, `:19`).
  - Goldens lock them against the unchanged 2.1.7 script (`karaoke-presets.md:42-51`).
- **KP1 ([#286](https://github.com/altqx/hikari/issues/286)): folding.**
  - The Karaoke arrangement puts the Template editor next to the video (`docs/ux/workspaces.md:34`).
  - The Grid folds runs of generated Lines, meaning those whose Effect is exactly `fx` or
    `Effector [fx]` (`src/application/generated_runs.cpp:5-8`; `workspaces.md:36`).
- **Automation host.** Each script runs in its own helper process (ADR 0006), on LuaJIT (`vcpkg.json:25`;
  `docs/automation.md:9`). Host services include `TextExtents`, `FrameFromMs`, `Keyframes` and
  `FrequencyPeaks` (`src/application/include/hikari/application/automation.h:191-196`).

### 2.3 Kara Effector

- **Activity.** No licence (`gh api` licence `none`); last push 2022-06-01; 77 stars.
- **Hikari already ships it.** Hikari bundles its auto4 library unchanged as
  `Automation/automation/Include/effector-auto4.lua` (`karaoke-templater.md:3`).
- **What it offers.** A dialog of modes: lead-in, hi-light, lead-out, shape, translation and function.
  Settings include start/end expressions, loops, tags and variables, and it ships thousands of presets.
  Its Lines carry Effect `Effector [fx]` and are removed on the next run (from the 2026-10-20 survey,
  citing [Effector-3.6.lua]).
- **Relation to Pyxis.** Pyxis's mode names (Lead In, Lead Out, Middle) and its `Tag Oscillator`/`Glitter`
  families continue this tradition ([README.en.md]).

### 2.4 PyonFX

- **Activity.** LGPL-3.0, last push 2025-08-31 (`gh api`).
- **Model.** An `Ass` object gives `meta, styles, lines = io.get_data()`. Effects are Python functions that
  loop over `line.syls`/`line.chars` and call `io.write_line(l)` after setting a copy's layer, times and
  text ([PyonFX example 2-1]).
- **Output.** `save()` writes a new file ([ass_core.py:1358-1380]). `keep_original` keeps the input Lines
  as Comments ([ass_core.py:599]).
- **Preview.** `open_mpv` starts mpv. On later runs it sends `sub-reload` over IPC
  ([ass_core.py:1448-1525]).
- **Profiling.** `@io.track` times each effect function ([PyonFX example 2-1]).

### 2.5 NyuFX

- **Status.** LGPL-3.0, **archived** (`gh api`, last push 2020-10-27). Its README sends users to PyonFX
  (from the 2026-10-20 survey, citing the [NyuFX repo]).
- **What it was.** A standalone C++/wx IDE with Lua and ASS editors. Its only preview was opening the
  output file (same source).

### 2.6 The0x539's KaraTemplater

- **Status.** No licence (`gh api`), last push 2026-05-07.
- **Components.** "The first token must be `code`, `template`, or `mixin`." **Mixins** "modify the output
  produced by `template` components, generally adding tags" ([0x.KaraTemplater.md]).
- **Classes:** `once`, `line`, `syl`, **`word`**, `char`.
- **Conditions:** `style`, `anystyle`, `actor`, `noactor`, `layer`, `if`/`unless`, `sylfx`/`inlinefx`.
  Restrictions of different types are joined with AND, repeated ones with OR.
- **Loops are named and nest.** "`loop foo 2 loop bar 3` produces 6 output lines", and `maxloop()` sets
  counts at run time.
- **Utilities.** `retime` gains `presyl2postline`, `preline2postsyl`, `delta` and `clamp`. Also
  `util.fbf`, gradient helpers (`make_grad`, `get_grad`, `gbc`), and `skip()`/`unskip()`.
- **Randomness.** `util.rand.*` exists, and the doc says nothing about seeding.
- **Limits.** No furigana support ([arch1t3cht templaters guide]).

### 2.7 arch1t3cht's guide and other modern tools

- **The guide's choice.** It names three "major templaters": stock, KaraOK and The0x539's. It makes
  The0x539's the default, because "there isn't really anything the stock templater or KaraOK can do that
  cannot be easily ported to The0x's templater", "Except for furigana and other uses of `furi` or `multi`
  templates" ([arch1t3cht templaters guide]).
- **Debugging advice.** "examine the ASS lines it outputs" rather than "blindly mess with the template"
  (same guide).
- **Other tools** (`gh api` and the 2026-10-20 survey):
  - KaraOK, `ln.kara`: no licence;
  - ILL, Clipper2 shapes, image tracing and Line To FBF: MIT, active;
  - aegsc, a template compiler: MIT;
  - pykara: MIT.

### 2.8 What the node set should cover

Across these tools the same building blocks recur:

- iteration over line, word, syllable, character and furigana units;
- loops, and frame-by-frame sampling;
- retime modes;
- unit geometry (left, center, right, top, middle, bottom, width, height);
- style fields;
- tag assembly with `\t`, `\move`, `\fad` and `\clip`;
- gradients across units or clips;
- drawings, from literals, glyph outlines or Clipper operations;
- conditions on style, actor and inline-fx;
- mixin-like tag additions;
- per-scope random values.

The stock templater lacks `word`, mixins and nested or named loops. The0x539 adds them. Pyxis adds
geometry, audio and image nodes on top.

## 3. Node editors: evaluation, groups, typed ports, preview, inspection

### 3.1 Blender Geometry Nodes

- **Fields.**
  - "Fundamentally, a field is a function … A field's result can then be calculated many times with
    different input data" ([Blender fields]).
  - "All field nodes work in the context of the data flow node they are connected to", and the same field
    tree "will be evaluated for every data flow node".
  - Field links are drawn dashed. Wiring a non-field socket into a field socket draws a red line (same
    page).
- **Evaluation.**
  - The 3.0 evaluator is lazy and multi-threaded: "only the required inputs and outputs are computed. …
    Switch computes only the input it actually uses", and "all field evaluation is parallelized"
    ([Blender 3.0 notes]).
  - The later "lazy-function" system converts each node group into a lazy-function graph. "Node groups are
    no longer inlined into their parents" ([Blender rB4130f1e]).
- **Groups.**
  - "Similar to functions in programming: reusable, composable, and parametrizable".
  - They can be nested, but "a group cannot contain itself".
  - Group Input and Output nodes define the interface, and "Input values that do not affect the output will
    be grayed out" ([Blender node groups]).
- **Inspection** ([Blender inspection]):
  - socket inspection shows "the value in a socket during the last evaluation";
  - the Viewer node and the Spreadsheet show intermediate geometry;
  - node warnings show "in the title" of the node;
  - the node timings overlay shows "how long a node took to execute the last time";
  - a "geometry randomization" debug mode catches setups that depend on unstable element order.

### 3.2 Houdini

- **VOPs.** "VOP nodes let you define a program … by connecting nodes together", and "Houdini then compiles
  the node network into executable VEX code" ([Houdini VOP]). A graph that compiles to a text program is
  the model of design option A (§6).
- **SOP flags.** The display flag marks what is shown, bypass "pass[es] its input geometry through …
  unchanged", and lock outputs "frozen" geometry "instead of cooking" ([Houdini flags]).
- **Compiled blocks.** These let "multithreaded for-each loops … run the same block of nodes over a large
  number of separate pieces" across cores ([Houdini compiled blocks]). Per-syllable effect graphs have
  the same shape.

### 3.3 Unreal Blueprints and Niagara

- **Blueprints.** Execution pins order side effects. Pure functions "are automatically executed by the
  compiler when the data relying on them is required … called one time for each node it is connected to",
  and they "do not cache their results" ([UE functions], [UE UFunctions]). Pyxis's loop/data wire split is
  the same distinction.
- **Niagara.** It is organised as a system of emitters, each a **stack** of groups: Emitter Spawn, Emitter
  Update, Particle Spawn ("called once per particle, when that particle is first born"), Particle Update
  ("per particle on each frame"), event handlers and Render. "Modules are processed sequentially from top
  to bottom" ([Niagara overview]). This maps onto karaoke effects: an effect holds layers (lead-in,
  highlight, lead-out, particles), and each layer is a stack of modules applied per unit.

### 3.4 Nuke and Natron

- **Nuke is pull-based.** "The only time image processing occurs is when an output node asks its input
  nodes for an image. This is known as a 'pull' system." `_request` passes the region of interest upstream,
  and rows are cached "to avoid computing the same Rows multiple times" ([Nuke NDK 2D architecture]).
- **Natron** is the open-source compositor of the same family: GPL-2.0, last push 2026-07-24, last
  release v2.5.1-pre2 (2024-09-13) (`gh api`). I did not read its engine docs.

### 3.5 ComfyUI

- **Two graph formats.**
  - The UI workflow JSON (version 1): `nodes`, `links` as objects `{id, origin_id, origin_slot, target_id,
    target_slot, type}`, `groups`, `reroutes`, `extra` ([ComfyUI workflow JSON]).
  - The older 0.4 form keeps LiteGraph's array links. Subgraph definitions sit in
    `definitions.subgraphs[]`, and their instances are nodes whose `type` is the subgraph's UUID
    ([ComfyUI blueprints doc], [ComfyUI subgraph RFC]).
  - The **API ("prompt") format** is separate and minimal: `{"3": {"class_type": "KSampler", "inputs":
    {"model": ["4", 0], "cfg": 8, …}}}`. A link is `[node id, output index]` ([basic_api_example.py]).
- **Custom nodes.** A custom node declares `INPUT_TYPES` (required, optional, hidden), `RETURN_TYPES`,
  `FUNCTION`, `CATEGORY`, `OUTPUT_NODE`, `IS_CHANGED` and `VALIDATE_INPUTS` ([ComfyUI server overview]).
  `/object_info` serves the catalogue of node types ([ComfyUI routes]).
- **Execution.**
  - Comfy "caches outputs, and only executes nodes that might produce a different result than the previous
    run"; output nodes always run, and it traces back from them ([ComfyUI server overview]).
  - The cache key is an **input signature**: the class type, `IS_CHANGED`, and each input as a constant or
    as `(ANCESTOR, index, socket)`, over the ordered ancestry ([caching.py:83-150]).
  - Lazy inputs (`lazy: True` plus `check_lazy_status`) evaluate only what a node asks for
    ([ComfyUI lazy evaluation]).

### 3.6 What carries over to Hikari

| Concern | Prior art | For a karaoke graph |
| --- | --- | --- |
| Evaluation | Pull and lazy (Blender, Nuke, Pyxis, ComfyUI lazy inputs) | Pull from Emit nodes; evaluate per unit as a field |
| Caching | ComfyUI input signature; Pyxis per-cycle memo | Memo per (node, unit); keep results across edits per (node, Line) keyed by input signature, so an edit re-runs only downstream nodes and changed Lines |
| Groups | Blender groups as functions; ComfyUI subgraphs | Node groups with typed inputs and outputs, saved as reusable presets |
| Typed ports | Blender socket kinds, field vs single value; ComfyUI type strings | Typed ports with explicit conversions; Pyxis's string-everything is what to avoid |
| Preview | Pyxis in-canvas player; PyonFX mpv reload; KE2 video preview | Hikari's video panel, through `templatePreviewDocument` |
| Inspection | Blender socket values, warnings, timings, spreadsheet; Pyxis heat map | Values on the current Line's units, warnings in node titles, per-node times (KE1 report style), KE1 render-cost overlay |

## 4. Implementation options in Qt/QML

| Candidate | Licence | Qt 6 | UI tech | Maintenance (`gh api`, 2026-10-28) | Fit |
| --- | --- | --- | --- | --- | --- |
| QtNodes ([paceholder/nodeeditor]) | BSD-3-Clause | Yes, `-DUSE_QT6=on` (README) | `QGraphicsScene`/`QGraphicsView`, i.e. Widgets. "QML frontend" is under "Help Needed" (README). Its model can run "headless" over an `AbstractGraphModel` | Last push 2026-07-31; tag 3.0.16 (2026-02-10); 87 open issues; 3.7k stars | Its push-style `DataFlowGraphModel` ("Each change in the source node is immediately propagated through all the connections") suits small graphs, not per-unit fields. The Widgets view would need `QQuickWidget`-style mixing in a QML app |
| QuickQanava ([cneben/QuickQanava]) | BSD-3 (`licence.txt`) | Qt ≥ 6.5.0, recommended 6.6.1 (README). Links `Qt6::Quick`/`QuickControls2` | QML components (`Node.qml`, `Port.qml`, `Group.qml`, `GraphView.qml`); edges are `QtQuick.Shapes` `ShapePath`s (`EdgeStraightPath.qml`) | Commits to 2026-08-12 on `develop`; last release 2.5.0 (2024-09-22) | The only maintained QML graph library. It brings its own drag, selection and styling layer, which Hikari would have to adapt to its focus, navigation and accessibility rules (ADR 0001) |
| Custom Qt Quick and Shapes | — (Hikari code) | Hikari is on Qt 6.11.2 (ADR 0004) | `QtQuick.Shapes` is already used in `src/ui/ShapeDrawing.qml`, `KanjiTimerDialog.qml`, `AutomationManager.qml`, `Main.qml`. Nodes are ordinary Hikari controls in a pannable, zoomable item | Hikari's own | Full control of theming, keys, accessible names, translation and undo. The cost is writing hit-testing, wire routing, selection and minimap. QuickQanava (BSD) can be read as a reference |
| imnodes ([Nelarius/imnodes]) / imgui-node-editor | MIT | Not Qt; needs Dear ImGui rendered into a Qt Quick item | Immediate-mode | imnodes: commits 2026-10-17, last release v0.5 (2022-03-09). imgui-node-editor: push 2026-10-26 | Dear ImGui's README: it is for "content creation tools and visualization / debug tools (as opposed to UI for the average end-user)" and lacks "full internationalization … and accessibility features" ([imgui README]). Hikari ships `.ts` translations (ADR 0002) and accessibility checks (ADR 0001), so this fits poorly |
| LiteGraph.js in a WebEngine view (what Pyxis does) | MIT | Would need Qt WebEngine | HTML canvas | jagenjo/litegraph.js last push 2024-08-01; Comfy-Org fork 2026-01-14 | Adds Chromium and a second UI toolkit and theme. Not considered further |

## 5. How a graph fits Hikari

### 5.1 Output: ordinary ASS Lines, reversible and regenerable

- **Lines, not a runtime.** A graph run should end as a templater run does: Dialogue Lines written into the
  Document as one undo step, with each source karaoke Line commented out (`karaoke-templater.md:95`).
  Players then see plain ASS, whatever produced it.
- **The marker.**
  - The stock templater deletes every Dialogue whose Effect is exactly `fx` on its next run
    (`kara-templater.lua:158-161`).
  - A graph that marked its Lines `fx` would have them deleted by any stock template run in the same file.
    A graph that used another marker would not be folded by KP1, which matches only `fx` and
    `Effector [fx]` (`generated_runs.cpp:5-8`).
  - So the marker needs a decision: see question 4 in §7.
- **Regeneration.**
  - The engine can set `LineRecord::generatedFrom` directly. Since it is in-process, it needs no
    `aegisub.hikari.set_source` round trip.
  - That gives "regenerate the selected Lines" and the per-Line preview for free, as long as the graph is
    deterministic.
  - With random nodes seeded by the graph seed, the node id and the unit's identity, a reopened file's Lines
    can be matched again by a re-run, as KE1 does (`karaoke-templater.md:42`). The stock templater cannot
    do this for random templates.
- **Where the graph lives.**
  - Pyxis keeps graphs out of the subtitle (§1.3), so the `.ass` loses its source.
  - Hikari's document model keeps unknown sections (C03-preservation, `docs/document-model.md:55-63`).
    Whether Aegisub keeps an unknown section on save is unconfirmed.
  - A **Comment Line with an Effect such as `graph <name>` and the graph as JSON text** would travel through
    any editor:
    - the stock templater reads only Comments whose Effect starts with `template` or `code`
      (`template_preview.h:23-26`);
    - Comments are never drawn;
    - it would sit beside the template Lines KE2 already lists.

### 5.2 Preview through libass on the video

The KE2 path already does what a graph preview needs:

1. Build the current Line's generated Lines.
2. Call `templatePreviewDocument(doc, source, preview, …)` to swap them in for the Line's existing output.
3. Keep only the Lines over the current window.
4. Let `LibassRenderer` draw.

The renderer keeps parsed tracks by script hash (`template_preview.h:72-101`;
`src/backends/include/hikari/backends/libass_renderer.h:12-14`). The badge, the eye toggle, Loop the Line
and the render-cost overlay can be reused as they are (`karaoke-templater.md:95`, `:65-74`). Pyxis's
heat-map idea (cost per frame across the Line's span) would add to KE1's overlay, which shows only the
current frame.

### 5.3 Performance with thousands of syllables

- **What is measured.**
  - The KE2 preview of one Line through the Lua helper has a median of about 1.2 ms
    (`compatibility-decisions.md:263`).
  - The video draws only the Lines over the preview window (`template_preview.h:72-82`).
  - No measurement exists for a whole-file graph run. Hikari's performance contract has no karaoke
    workload (`docs/performance.md:21` lists the fixtures G, E, V, A).
- **What the design can rely on.**
  - **Generated Lines are independent per Line.** Determinism and no Line-to-Line state (KE3's rule,
    `karaoke-presets.md:19`) let the engine evaluate Lines in parallel and cache per Line, as Houdini's
    compiled for-each blocks and Blender's parallel field evaluation do (§3.1–3.2).
  - **Text metrics must match the templater's.** karaskel measures through the host's `TextExtents`
    (`automation.h:192`). A native engine should call the same implementation, so a graph and a template
    agree on `$left`/`$center` to the pixel.
  - **Render cost scales with bitmap area**, and blur and vector clips are the expensive parts
    (arch1t3cht's renderer-internals gist, from the 2026-10-20 survey). Per-node Line counts in the run
    report, as KE1 gives per template, keep that visible.
  - **The Grid handles the volume.** KP1's folding already turns runs of generated Lines into one row
    (`workspaces.md:36`).

### 5.4 Lua/LuaJIT plugin nodes and AI-authored graphs

- **Plugins.**
  - Card [#366](https://github.com/altqx/hikari/issues/366) ("Hikari plugins", next wave) is open and needs
    "its own design session first". It lists "renderers, providers, panels, commands" as extension points.
  - A **node** is a natural extension point. A ComfyUI-style declaration (typed inputs and outputs,
    category, a function) maps onto a Lua module.
  - **Where it runs.** In the per-script helper of ADR 0006, called **once per Line with arrays of units**
    rather than once per syllable, so IPC cost stays per Line.
  - **What it may do.** Only read-only host services, as the preview runner allows today
    (`template_preview.h:103-107`).
  - **Consent and determinism.** Plugin nodes need the same trust prompt as template Lua
    (`karaoke-templater.md:93`), and a determinism contract: the node gets the seed and never calls
    `math.random`.
- **AI authoring.**
  - No "Hikari AI" card exists: `gh issue list --search` for AI found none, and the brain has no page.
  - What helps an AI author graphs is what helps plugins:
    - a machine-readable **node catalogue** (as ComfyUI's `/object_info`);
    - a **minimal graph format** (as ComfyUI's API format next to its UI format);
    - a **validator** that rejects bad wiring before load (as `VALIDATE_INPUTS` and Pyxis's
      `node_validator.py`).
  - An AI-made graph then enters as one undo step, previews on the current Line like any edit, and holds no
    Lua unless the user consents.

## 6. Proposed first Hikari node set

The aim: every KE3 preset (`docs/karaoke-presets.md:27-40`) can be built from these nodes and give its
golden output byte for byte. Port types:

- **Number**
- **Text**
- **Colour**
- **Alpha**
- **Time** (ms)
- **Point**
- **Tags**: a list of override tags
- **Drawing**
- **Bool**
- **Units**: a field domain

Values from a Unit node are fields, evaluated per unit by the node that uses them.

| Node | Ports (in → out) | Stock templater equivalent | Pyxis equivalent |
| --- | --- | --- | --- |
| Karaoke Lines | style, actor, effect filters → Lines | template Style; `all`; Effect `karaoke` | Output `apply_to` |
| For each | Lines, unit (line, word, syl, char, furi), skip blanks → Units | `line`/`syl`/`char`/`furi` classes; `noblank` | Main Loop |
| Repeat | Units, count → Units, j, maxj | `loop n` / `j`, `maxj` | MaxLoop (normal) |
| Frames | Units, start, end, step → Units, frame | (code-line loops) | MaxLoop (fbf) |
| Unit info | — → text, i, n, start, end, duration, left, center, right, top, middle, bottom, width, height, style fields | `$`-variables (`$i`, `$center` …) | Sys Var |
| Number / Text / Colour | — → value | literals | Number / String / Color Picker |
| Math | a, b, op → Number | `!…!` arithmetic | Math |
| Progress | i, n, ease → Number (0–1) | `$i/$syln` in expressions | Module, Interpolator |
| Random | min, max, step, scope → Number | `math.random` (unseeded) | MultiRandom (`by.<iter>`) |
| Choose | Bool, a, b → value | `if`-style expressions | Conditioner / Only |
| Retime | Units, mode, start offset, end offset → start, end | `retime(mode, a, b)` | Retime |
| Stagger | i, n, step, from (start, end, centre) → Time | `$i * k` in retime | Lead In / Lead Out / Middle 1–2 |
| Tag | kind (`\pos`, `\an`, `\1c` … `\blur`, `\fsc`, `\frz`, `\clip`), values → Tags | literal tags with `$`/`!…!` | MultiTag |
| Move | from, to, t1, t2 → Tags | `\move(…)` | Move Tag |
| Fade | in, out → Tags | `\fad(…)` | MultiTag |
| Transform | t1, t2, accel, Tags → Tags | `\t(…)` | Retime (`\t` mapping) |
| Gradient | colours, progress → Colour | code-line helpers | Color Gradient |
| Drawing | literal or library shape → Drawing | `\p1` text | Library |
| Emit | Units, layer, start, end, Tags, text or Drawing, keep text, keep tags → Lines | the template Line itself; `notext`, `keeptags`, Layer | Output |
| Group, Group input, Group output | typed → typed | (none) | (none) |
| Lua (gated) | typed → typed | `code` Lines, `!…!` | Script Code |

Later, after the first milestone: Text to drawing, Clipper operations, a clip gradient, particles or arrays,
furigana and `multi` highlights, audio peaks (`FrequencyPeaks`), and inline-fx conditions.

## 7. Design options for Hikari

**Option A: the graph compiles to stock template and code Lines.**

- How it works: the canvas edits a graph, and Hikari writes `template …` and `code …` Lines from it (as VOPs
  compile to VEX). KE1/KE2/KE3 then run it unchanged.
- **For:**
  - Every existing seam is reused: preview, regenerate, report, problem placement, goldens.
  - Files stay portable to Aegisub with no Hikari needed to re-run them.
  - It is the smallest engine.
- **Against:**
  - Expressiveness stops at 2.1.7: no `word`, no nested or named loops, no mixins, and `loop` counts are
    constants.
  - Node-level inspection needs a mapping from generated Lua back to nodes.
  - Hand edits to the generated template Lines break the round trip.
  - Every graph is Lua, so every graph needs the trust prompt.

**Option B: a native graph engine that writes Lines directly (recommended).**

- How it works: a typed, deterministic, field-style engine in `hikari::application`, with no Qt. It
  evaluates Emit nodes per Line, in parallel, with per-(node, Line) signature caching. It sets
  `generatedFrom`, returns a report like the templater's, and previews through `templatePreviewDocument`.
- **For:**
  - Graphs without Lua run with no trust prompt.
  - Per-node values, warnings and timings come straight from the engine.
  - Output is reproducible, so regeneration and matching always work.
  - It is fast enough for whole-file runs.
  - Plugin Lua nodes and AI authoring target one catalogue.
- **Against:**
  - It is the largest build: the engine, text metrics through the shared `TextExtents`, and the
    tag/drawing model.
  - Re-running a graph needs Hikari, though the output is plain ASS.
  - Parity with template semantics (retime modes, blanks, furigana) has to be pinned by tests. The
    KE3-goldens milestone does that.

**Option C: a native engine plus an export to template Lines.**

- How it works: Option B, plus "Export as template Lines" for the subset of graphs Option A can express.
- **For:** Graphs can be shared with Aegisub users.
- **Against:** It maintains two back ends. The export subset shrinks as nodes grow.

## 8. Open questions for the owner

1. **May "our own implementation" reuse Pyxis code, or must it be clean-room?**
   - *Recommended:* clean-room. Reuse ideas and naming only, and credit Pyxis as inspiration in the docs.
   - Why: Pyxis is GPL-3.0 like Hikari, but it is Python/JS, its Lua core comes from the unlicensed Kara
     Effector line, and Hikari already keeps KE-derived code out of its presets.
2. **Which engine: A, B or C (§7)?**
   - *Recommended:* B, with C's export kept as a later option.
3. **Where is a graph stored?**
   - *Recommended:* in the Document, as a Comment Line `graph <name>` holding the graph JSON, beside the
     template Lines.
   - Also a graph library in the user folder (UD1) for reuse, as KE3 presets are reused.
4. **What Effect do generated Lines carry?**
   - *Recommended:* `fx <graph name>`, with KP1's `isFoldableGeneratedLine` extended to it.
   - Why: the stock templater deletes exactly `fx`, so a template run in the same file would wipe graph
     output. A per-graph marker also lets one graph be regenerated without touching another's Lines.
5. **Are random values allowed, and how?**
   - *Recommended:* yes, but only a seeded Random node. The seed is (graph seed, node id, Line index, unit
     index, loop j), and its scope is chosen as in Pyxis's `by.<iter>`.
   - No `math.random` in core nodes, so preview equals Apply and regenerate-selected always matches.
6. **What expression language do node fields take?**
   - *Recommended:* numbers and typed constants in fields, and wires for anything computed.
   - Lua only in an explicit Lua node, under the existing trust prompt. Pyxis's `eval()` of Python in every
     field is what to avoid.
7. **What is the first milestone?**
   - *Recommended:* the §6 node set, with all ten KE3 presets rebuilt as graphs whose Lines equal the
     committed goldens byte for byte.
   - It exercises loops, retime, staggers, drawings and Layers, and it reuses existing tests.
8. **Which canvas technology?**
   - *Recommended:* a custom Qt Quick canvas with `QtQuick.Shapes` wires, reading QuickQanava (BSD-3) as a
     reference.
   - Not QtNodes (Widgets) and not imnodes (no accessibility or i18n).
9. **Where does the editor sit in the UI?**
   - *Recommended:* a dock panel in the Karaoke arrangement, tabbed with the Template editor.
   - The video preview, badge, eye, Loop the Line and the Apply/Apply-to-selected split behave as in KE2.
10. **Should Hikari import Pyxis graphs?**
    - *Recommended:* no. Their fields are Python expressions that Hikari cannot run safely, and the
      sub-loop semantics live in Pyxis's code, not its format.
11. **Should the node catalogue and graph format be designed now for plugins (#366) and a future AI
    feature?**
    - *Recommended:* yes, in the node editor's design session:
      - a versioned catalogue (types, ports, categories);
      - a minimal graph format, like ComfyUI's API format;
      - a validator.
    - Plugin Lua nodes and AI authoring both target these later.

## Sources

Pyxis Effector at `375ad89b4969c04bfb0cc27ac9358a30779eede5`
(base `https://github.com/Mandholine/Pyxis-Effector/blob/375ad89b4969c04bfb0cc27ac9358a30779eede5/`):

- [README.en.md]: https://github.com/Mandholine/Pyxis-Effector/blob/375ad89b4969c04bfb0cc27ac9358a30779eede5/README.en.md
- [LICENSE]: https://github.com/Mandholine/Pyxis-Effector/blob/375ad89b4969c04bfb0cc27ac9358a30779eede5/LICENSE
- [Changelog_v1.1.6.en.md]: https://github.com/Mandholine/Pyxis-Effector/blob/375ad89b4969c04bfb0cc27ac9358a30779eede5/Changelog_v1.1.6.en.md
- [Pyxis_engine.lua:49-63]: https://github.com/Mandholine/Pyxis-Effector/blob/375ad89b4969c04bfb0cc27ac9358a30779eede5/Pyxis_engine.lua#L49-L63
- [Pyxis_engine.lua:399-480]: https://github.com/Mandholine/Pyxis-Effector/blob/375ad89b4969c04bfb0cc27ac9358a30779eede5/Pyxis_engine.lua#L399-L480
- [Pyxis_engine.lua:408]: https://github.com/Mandholine/Pyxis-Effector/blob/375ad89b4969c04bfb0cc27ac9358a30779eede5/Pyxis_engine.lua#L408
- [Pyxis_engine.lua:537]: https://github.com/Mandholine/Pyxis-Effector/blob/375ad89b4969c04bfb0cc27ac9358a30779eede5/Pyxis_engine.lua#L537
- [Pyxis_engine.lua:600-667]: https://github.com/Mandholine/Pyxis-Effector/blob/375ad89b4969c04bfb0cc27ac9358a30779eede5/Pyxis_engine.lua#L600-L667
- [nodes_library.py:1-15], [nodes_library.py:97-1040] and the per-node lines in §1.5: https://github.com/Mandholine/Pyxis-Effector/blob/375ad89b4969c04bfb0cc27ac9358a30779eede5/PyxisNodes/src/nodes_library.py
- [node_engine.py:934-1176], [node_engine.py:1460-1524], [node_engine.py:1515], [node_engine.py:2237], [node_engine.py:3677-3679], [node_engine.py:5519], [node_engine.py:5812], [node_engine.py:7207-7221]: https://github.com/Mandholine/Pyxis-Effector/blob/375ad89b4969c04bfb0cc27ac9358a30779eede5/PyxisNodes/src/node_engine.py
- [string_evaluator.py:702]: https://github.com/Mandholine/Pyxis-Effector/blob/375ad89b4969c04bfb0cc27ac9358a30779eede5/PyxisNodes/src/string_evaluator.py#L702
- [main.py:239]: https://github.com/Mandholine/Pyxis-Effector/blob/375ad89b4969c04bfb0cc27ac9358a30779eede5/PyxisNodes/src/main.py#L239
- [Example_Simple_Effect.json]: https://github.com/Mandholine/Pyxis-Effector/blob/375ad89b4969c04bfb0cc27ac9358a30779eede5/PyxisNodes/templates/Example_Simple_Effect.json
- [docs/en]: https://github.com/Mandholine/Pyxis-Effector/tree/375ad89b4969c04bfb0cc27ac9358a30779eede5/PyxisNodes/docs/en
  - [01 Visual Paradigm.md], [02 Interface Anatomy.md], [03 Basics.md], [04 System.md], [09 Variables.md],
    [11 Base Environment and Customisation.md]: files of that folder, cited with line numbers.
- [docs/img/previewer.png]: https://github.com/Mandholine/Pyxis-Effector/blob/375ad89b4969c04bfb0cc27ac9358a30779eede5/PyxisNodes/docs/img/previewer.png

HikariSub `origin/qt` at `62446e2b59ffd19b5081d292b1a85793b57f3133`
(base `https://github.com/altqx/hikari/blob/62446e2b59ffd19b5081d292b1a85793b57f3133/`):

- [karaoke-templater.md:23-29], `docs/karaoke-templater.md`, `docs/karaoke-presets.md`,
  `docs/compatibility-decisions.md`, `docs/ux/workspaces.md`, `docs/document-model.md`,
  `docs/automation.md`, `docs/performance.md`, `docs/adr/0001…`, `0002…`, `0004…`, `0006…`, `0013…`
- `src/application/generated_runs.cpp`, `src/application/include/hikari/application/template_preview.h`,
  `src/application/include/hikari/application/automation.h`, `src/core/include/hikari/core/document.h`,
  `src/backends/include/hikari/backends/karaoke_preview_runner.h`,
  `src/backends/include/hikari/backends/libass_renderer.h`, `src/application/karaoke-presets/`,
  `Automation/automation/Autoload/kara-templater.lua`, `vcpkg.json`

Other tools:

- [karaoke templater overview]: https://aegisub.org/docs/latest/automation/karaoke_templater/
- [template modifiers]: https://aegisub.org/docs/latest/automation/karaoke_templater/template_modifiers/
- [0x.KaraTemplater.md]: https://github.com/The0x539/Aegisub-Scripts/blob/trunk/doc/0x.KaraTemplater.md
- [arch1t3cht templaters guide]: https://github.com/TypesettingTools/arch1t3cht-Aegisub-Scripts/blob/main/doc/templaters.md
- [Effector-3.6.lua]: https://github.com/KaraEffect0r/Kara_Effector/blob/master/Effector-3.6.lua
- [PyonFX example 2-1]: https://github.com/CoffeeStraw/PyonFX/blob/master/examples/2%20-%20Beginner/1%20-%20First%20Simple%20Effect.py
- [ass_core.py:599], [ass_core.py:1358-1380], [ass_core.py:1448-1525]: https://github.com/CoffeeStraw/PyonFX/blob/master/pyonfx/ass_core.py
- [NyuFX repo]: https://github.com/Youka/NyuFX

Node editors:

- [Blender fields]: https://docs.blender.org/manual/en/latest/modeling/geometry_nodes/fields.html
- [Blender inspection]: https://docs.blender.org/manual/en/latest/modeling/geometry_nodes/inspection.html
- [Blender node groups]: https://docs.blender.org/manual/en/latest/interface/controls/nodes/groups.html
- [Blender 3.0 notes]: https://developer.blender.org/docs/release_notes/3.0/nodes_physics/
- [Blender rB4130f1e]: https://developer.blender.org/rB4130f1e
- [Houdini VOP]: https://www.sidefx.com/docs/houdini/nodes/vop/index.html
- [Houdini flags]: https://www.sidefx.com/docs/houdini/network/flags.html
- [Houdini compiled blocks]: https://www.sidefx.com/docs/houdini/model/compile.html
- [UE functions]: https://dev.epicgames.com/documentation/en-us/unreal-engine/functions-in-unreal-engine
- [UE UFunctions]: https://dev.epicgames.com/documentation/en-us/unreal-engine/ufunctions-in-unreal-engine
- [Niagara overview]: https://dev.epicgames.com/documentation/en-us/unreal-engine/overview-of-niagara-effects-for-unreal-engine
- [Nuke NDK 2D architecture]: https://learn.foundry.com/nuke/developers/latest/ndkdevguide/2d/architecture.html
- [ComfyUI workflow JSON]: https://docs.comfy.org/specs/workflow_json
- [ComfyUI server overview]: https://docs.comfy.org/custom-nodes/backend/server_overview
- [ComfyUI lazy evaluation]: https://docs.comfy.org/custom-nodes/backend/lazy_evaluation
- [ComfyUI routes]: https://docs.comfy.org/development/comfyui-server/comms_routes
- [ComfyUI blueprints doc]: https://github.com/Comfy-Org/workflow_templates/blob/main/docs/BLUEPRINTS.md
- [ComfyUI subgraph RFC]: https://github.com/Comfy-Org/rfcs/blob/main/rfcs/0005-subgraph.md
- [basic_api_example.py]: https://github.com/Comfy-Org/ComfyUI/blob/master/script_examples/basic_api_example.py
- [caching.py:83-150]: https://github.com/Comfy-Org/ComfyUI/blob/master/comfy_execution/caching.py#L83-L150

Libraries:

- [paceholder/nodeeditor]: https://github.com/paceholder/nodeeditor
- [cneben/QuickQanava]: https://github.com/cneben/QuickQanava
- [Nelarius/imnodes]: https://github.com/Nelarius/imnodes
- [imgui README]: https://github.com/ocornut/imgui/blob/master/README.md
- imgui-node-editor: https://github.com/thedmd/imgui-node-editor
- LiteGraph.js: https://github.com/jagenjo/litegraph.js and https://github.com/Comfy-Org/litegraph.js
