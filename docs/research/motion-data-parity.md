# Apply motion data: parity with Aegisub-Motion

AU8 #842. What the bundled Aegisub-Motion 1.0.9 (`a-mo.Aegisub-Motion.moon`, MotionHandler 1.1.8) writes, and what the native command does with the same data and options.

## How it was measured

- `tests/backends/motion_parity_tests.cpp` runs the bundled script's **Apply** (and **Revert**) in HikariSub's Lua helper. The script is unchanged.
- Input: `tests/fixtures/motion-data/`. It holds `motion.ass` (PlayRes 1280 × 720, three styles, 14 Lines) and 35 cases in `cases.json`. The data is Mocha-style After Effects 6.0 keyframe data, 13 frames, tracked at 1920 × 1080. `main.txt` is CRLF and has an Anchor Point section and one exponent value. There are also `clip.txt`, `static.txt` and the 5-frame `short.txt`.
- The video is 24000/1001 fps, frame starts truncated to ms, at frame 104.
- Goldens: `tests/fixtures/motion-data/amo/<case>.json`. The tags of each override block are sorted (see N1).
- **Unstable** cases: a-mo's values change from run to run (N2). Their goldens list every variant seen in 40 runs. The native test picks one variant, or records a departure.
- The same test runs **Revert** both ways: a-mo's on native output and native's on a-mo's (`revert`, `several-lines`). The documents come out the same.
- a-mo's licence file grants no rights, so the native code is written from its behaviour only. No code is copied.
- Each run gets a fresh automation folder. a-mo's **Write config** is on by default, and otherwise options leak from one run into the next.

## Results

"Match" means native output equals the golden after the tags are sorted. **D*n*** is a ledgered departure, accepted 2026-11-09 (Q201–Q206): [AU8-motion-data](../compatibility-decisions.md#au8-motion-data). A case that departs keeps a native golden in `tests/fixtures/motion-data/native/` that names its departures.

### Data and options

| Behaviour | a-mo (cases) | Native |
|---|---|---|
| Data format | Needs line 1 `Adobe After Effects 6.0 Keyframe Data`. Source Width and Height must be the 3rd and 4th non-blank lines. Reads Position, Scale and Rotation. Ignores Anchor Point. Accepts exponents. CRLF is fine. | Match. A new `parseTransformData` sits next to `parsePowerPin`. Width and height can be on any line. |
| Scale to the script | Position × PlayRes / Source size, per axis. Scale uses X percent only, for both axes. Rotation is negated (AE is clockwise). | Match |
| Not data | Refuses: "You put something in the main data box but it is wrong…" (`not-tracking-data`) | Refused in the dialog's status line, like FA13 |
| Wrong length | Refuses when the data's frames ≠ the selection's frames (`wrong-length`, 5 vs 13) | Match, through FA13's `checkTracking` and its message |
| Relative frame | 1 … N. 0 becomes 1 (`frame-zero`), −1 is the last frame (`last-frame`). The field starts at the video frame's position in the selection; `defaults` starts at 5 from frame 104. | Match for 1 … N, with the same start value. FA13's spin box takes only 1 … N, so 0 and −1 can't be typed (**D1**) |
| Absolute frame (Relative off) | The video frame number; out of range is refused (`absolute-frame`, 104 = relative 5) | Not offered. FA13's spin box already shows "of N · video frame F". Recorded under **D1** |
| X / Y off | That axis's track is held at the reference frame's value. The offset is still scaled and rotated, so Y moves with scale: `x-only` turns y 360 into 357.08 … 365.85. | Match |
| Data from a file path | The text box may hold a path to a data file | **D2**: no paths in the text box. The dialog gets an **Open file…** button that loads the text. |
| Shake rotoshape data (SRS) | Used for vector clips only | **D3**: not supported natively. The script still ships for it. |

### Frames and times

| Behaviour | a-mo | Native |
|---|---|---|
| Frames covered | From `frame_from_ms(start)` to `frame_from_ms(end) − 1` (Aegisub START frames). The fixture Lines 4.17–4.68 cover frames 100–112, so 13. | Match (FA13 `trackedFrames`) |
| Each frame's times | Start `floor(ms_from_frame(f) / 10) × 10`, end the same for f + 1: 4150–4190 … 4650–4690. The first and last pieces can start earlier or end later than the Line did (4170 → 4150, 4680 → 4690). | Match, from FA15 `lineFrames` (`startTimeFor` / `endTimeFor`); the native test checks it |
| Lines in parts | Each Line is split over its own frames. Times relative to the data come from the first selected frame (`lines-in-parts`). | Match |
| Identical neighbours | Merged when text and style are equal and the times touch (`static-frames-merge`: 4 frames become 4150–4310) | Match |

### Tags, per frame

Values are rounded to 2 decimals and written with `%g`, so trailing zeros are dropped.

| Tag | a-mo | Native |
|---|---|---|
| `\pos` | `(p − ref) × ratio`, rotated by the rotation change, plus the current track point (`defaults`, `rotation-from-frame-5`) | Match |
| No `\pos` | Inserts `\pos` at the alignment's default point, from the margins (`default-position`, an7 with Alt's margins) | Match |
| Absolute | `\pos` is exactly the track point (`absolute-position`) | Match |
| `\move` | Replaced by the `\pos` at the frame's time from the first frame, then tracked. It isn't clamped to the move's times, so it overshoots: `move` reaches x 917.95, where clamping gives 842.6. | **D4**: clamp to the move's times, as renderers do. FA15's `moveToPos` already clamps. A 4-argument `\move` runs from 0 to the Line's duration. |
| `\fscx` `\fscy` | × the scale ratio (X percent for both axes) | Match |
| `\bord` `\xbord` `\ybord` `\shad` `\xshad` `\yshad` | × ratio when **Border** or **Shadow** is on (`blur-scale` turns them off) | Match |
| `\blur` | × `1 − (1 − ratio) × blurScale`; `\be` isn't touched (`blur-scale`, 0.5) | Match |
| `\frz` / `\fr` | + the rotation change, when **Rotation** is on | Match |
| `\org` | **Origin** on: moved like `\pos` (`origin`). Off: kept (`origin-kept`). | Match |
| Every override block | Each block's tags are scaled, not only the first's (`defaults` second block, `style-reset`) | Match |

### Tags a-mo adds

| Behaviour | a-mo | Native |
|---|---|---|
| Style values | When missing from the first block, it adds `\fscx` `\fscy` (when scaling), `\bord`, `\shad` (style value ≠ 0) and `\frz` (when rotating, even `\frz0`). The same goes after each `\r`, with that style's values (`style-reset`, `no-outline-style`). | Match |
| Alphas, with **Interpolate transforms** (on by default) | Adds `\1a` `\3a` `\4a` (and `\2a` with karaoke) or `\alpha&H00&`, even when the Line has no fade (`defaults`, `karaoke`, `no-outline-style`) | **D13**: added only when the Line has a fade to bake. 27 cases differ from a-mo by these alphas alone. |
| **Clip only** | Clips move. `\pos` stays, but the style tags above are still added (`clip-only`). | Match |

### Clips

| Behaviour | a-mo | Native |
|---|---|---|
| Rect `\clip`/`\iclip` | Moved and scaled with the track, never rotated (`rect-clip`) | Match the variant where the Line's `\pos`/`\frz` keep the rotation (**N2a**) |
| Vector clip | Every point goes through the `\pos` maths, rotation included. `\clip(2,…)` is first divided by 2^(s−1) to plain coordinates, and `\iclip` stays `\iclip` (`vector-clips`). | Match |
| Rect → vector | Turned into `m l` points first, then rotated like a vector clip (`rect-to-vector`) | Match |
| Rect Clip / Vect Clip off | That clip kind stays (`clips-untouched`) | Match |
| **Absolute** with a clip | The clip moves by the `\pos` delta (`absolute-position`) | Match the variant that uses the same frame's delta (**N2b**) |
| **Track \clip separately** | Second data and relative frame for clips (`clip-separately`) | Match the variant where the clip data moves the clips (**N2c**) |

### Transforms, fades, karaoke

| Behaviour | a-mo | Native |
|---|---|---|
| **Interpolate transforms** on | Each frame gets the values `\t` has reached at the frame's mid-time, written `%g` unrounded (`\frz1.17647`). `\fad` becomes `\fade`, then scales every alpha by the fade at that time (`interpolate-transforms`). | Match. FA15's `bakeAnimations` does the same work; where the two differ, a-mo's numbers win for this command. |
| Off | `\t` and `\fade` times shift by the frame's offset, and a `\t` that has ended becomes plain tags. But a-mo's dedupe then keeps only one `\t` per block, and sometimes drops the block's `\fscx` (`shift-transforms`, 2 variants). | **D5**: keep every shifted `\t` and no tag is lost; a-mo's output is plainly broken here. |
| Karaoke | Each frame shortens the leading `\k` by the time elapsed; ended syllables lose their tag (`karaoke`: `\k10` → `\k8` → `\k4` → gone; the last frame reads `Karaoke`) | Match |

### Linear mode

| Behaviour | a-mo | Native |
|---|---|---|
| Linear | One Line: `\move` from the first to the last frame and a `\t(begin,end,…)` per tag, at the frames' mid-times (`linear`: `\move(…,0,501)`). `\pos` always becomes `\move`, even when the track doesn't move. | Match, apart from D6 |
| Empty clip | Leaves `\clip()\t(0,501,\clip())` in every Linear Line (`linear`, `linear-move`) | **D6**: no empty `\clip` |
| Linear with `\move` | Starts from the `\move` position at the line's *absolute* time (progress 13.9), so it lands far off: `\move(1861.06,969.44,…)` for a sign near 600,300 (`linear-move`) | **D7**: the `\move`'s two points are the Line's own `\move` at the first and last frames' mid-times |
| Linear with clips | Falls back to frame by frame (`linear-with-clip`) | Match |
| Linear with karaoke | The script fails: "attempt to compare number with nil" in `Line.shiftKaraoke` (`linear-karaoke`) | **D8**: works; the `\k` are kept |

### The result

| Behaviour | a-mo | Native |
|---|---|---|
| Undo | One step | One step: "Apply motion data" |
| Selection after | None (a-mo returns no selection) | **D9**: the new Lines are selected, as FA13 does |
| Extradata | Every new Line gets `a-mo` = `{"originalText": …, "uuid": …}`; one uuid per source Line. It's skipped when the Line already has it (re-tracking). | Match the key and the JSON, so a-mo's **Revert** undoes a native apply and the reverse. No perspective plane is saved. |
| Revert | Puts back `originalText` on the first Line of each uuid, with the union of the pieces' times (4150–4690, not the original 4170–4680), and deletes the rest (`revert`) | Match as a native **Revert motion data** command, one step. a-mo returns no selection, so the old, deleted Lines stay selected; native selects the restored Lines (**D9**). |
| Comments and hidden Lines | Comments are skipped (`LineCollection` drops them) | Match FA13: shown, selected, uncommented Lines |

### N: a-mo isn't deterministic

- **N1, tag order.** The tags a-mo adds, and those it writes back after interpolating `\t`, come out in Lua `pairs()` order. In HikariSub's LuaJIT 2.1 that order changes from run to run (21 of 35 cases differ in raw text over 30 runs). Some of it is keyed by table addresses, so Aegisub's LuaJIT behaves the same way. → **D10**: native writes a fixed order: added style tags first, then the alphas, then the Line's tags in place. Parity is checked on sorted tags.
- **N2, values.** The callbacks run in `pairs()` order too, and three of them share state:
  - a. The rect-clip callback sets the shared track's rotation change to 0 (`rectClipData` *is* the main data). When it runs before `\pos` or `\frz`, those lose the rotation: `rect-clip` and `several-lines` have 4 variants each.
  - b. **Absolute** sets the delta in the `\pos` callback. The clip callback reads whatever is there, which may be another frame's delta (`absolute-position`, 2 variants).
  - c. `prepareConfig` loops `pairs{main, clip}`. If both dialogs ask for rect clips, the clip data is whichever comes last (`clip-separately`, 3 variants).
  - Native follows the evident intent in each case (named above). These goldens are checked only against native output, never against a-mo.

## Derive, Export Power Pin and extrapolation: read, not run

These four scripts aren't bundled (Q186), so they set no byte-identical target. The formats below come from reading their source:

- 0x.DeriveTrack: The0x539/Aegisub-Scripts `src/0x.DeriveTrack.moon` at 3cd439b9d. It has no licence, so it's reimplemented from behaviour.
- arch.DerivePerspTrack 1.1.3: from the third-party corpus, MIT.
- petzku.ExtrapolateMove 0.1.5: petzku/Aegisub-Scripts at 184c8287, ISC.
- phos.ExtrapolateTracking 1.0.2: GPL-3. Its algorithm is reimplemented from behaviour; no code is copied.

| Script | Input | Output and rounding | Native |
|---|---|---|---|
| 0x.DeriveTrack (**Derive**) | At least 2 Lines with the same text once tags are removed, end to start, same layer and comment state. Per Line it reads the last `\pos` in any block, and `\fscx` `\fscy` `\frz` from the *last* block only, falling back to the style (`\fr` isn't read). No `\pos`: it logs "missing pos tag" and fails. | Written to the log window. `Units per Second` is hard-coded 23.976; Source Width/Height are PlayRes, so a-mo's scale factor is 1. Rows `\t%d\t%f\t%f\t0` (six decimals), frames from 0, one row per frame of each Line. Scale has Z 0, Rotation is `\t%d\t%f` of −`\frz`. There's no `End of Keyframe Data`. | **Copy motion data from lines**. It reads the effective first-block values (`\pos`, else the `\move` point at the frame, `\fscx` `\fscy` `\frz`/`\fr`, else the style), puts the text on the clipboard, writes the video's fps, ends with `End of Keyframe Data`, and refuses a Line without a position. Round trip: Apply motion data on the first Line, relative to frame 1, gives back the same Lines. |
| arch.DerivePerspTrack (**Export Power Pin**) | From Outer Quad: each Line's `_aegi_perspective_ambient_plane` (`x;y\|x;y\|x;y\|x;y`). From Tags: the quad of its tags (VT3's `transformPoints`); `\move` is refused. Overlapping Lines are refused. | Log window. `Units Per Second` 23.976, Source size = PlayRes. Corners 0002, 0003, 0004, 0005 take quad points 1, 2, 4, 3 (TL, TR, BL, BR). Rows `\t<f>\t<x>\t<y>` from frame 0. Outer-quad values are copied as the extradata text; tag values use Lua `tostring` (14 significant digits). A frame no Line covers holds the last quad. Ends with `End of Keyframe Data`. | **Copy Power-Pin track from lines**: the same layout and numbers, the saved plane first, the tags' quad otherwise. Round trip: FA13's `parsePowerPin` reads back the same quads. Differences: real fps instead of 23.976, and one command instead of two (ledger). |
| petzku.ExtrapolateMove | A 6-argument `\move` (the first one); 4-argument moves are left alone | `\move(%.2f,%.2f,%.2f,%.2f)`: the move's velocity carried back to time 0 and on to the Line's duration, so the 4-argument move covers the whole Line. Equal t1 and t2 divide by zero. | **Extrapolate \move to the line**: the same formula and format; equal times are refused. |
| phos.ExtrapolateTracking | Frame-by-frame Lines (same layer and text, touching times, no animation); their effective `\pos`, `\fscx`, `\fscy` and `\frz` | For each value it fits degree 1–3 by k-fold cross-validation (k = 80 % of the points), stopping at RMSE < 0.01. While RMSE ≥ 0.003 and there are at least 8 points, it halves the window towards the extended end. RMSE > 0.2 is "LOW CONFIDENCE", > 5 "VERY LOW". It then extends the first or last Line by N frames and runs a-mo's Apply on it (Interpolate transforms, origin optional, every other option on). Numbers go through Lua `tostring`. | **Extend tracked lines…**, plus the dialog's **Extrapolate past the data** for data shorter than the Lines. Same fit and thresholds; the confidence shows in the status line. The grown Line goes through the whole native Apply, so style tags are added and tags deduped as in Apply. phos calls a-mo's frame step without its preparation and clean-up passes. Lines must cover one frame each; `\t`, `\move`, `\fad` and `\k` are refused. One step, "Extend tracked lines". |

## Departures

| Id | Departure |
|---|---|
| D1 | Relative frame 1 … N only: no 0, −1 or absolute frame (FA13's control) |
| D2 | **Open file…** instead of a path in the text box |
| D3 | No Shake rotoshape (SRS) data |
| D4 | `\move` is clamped to its times |
| D5 | Shifted transforms keep every `\t` and every tag |
| D6 | Linear mode writes no empty `\clip` |
| D7 | Linear with `\move` takes its points from the Line's own `\move` at the first and last frames |
| D8 | Linear works with karaoke |
| D9 | The new Lines are selected |
| D10 | Fixed tag order (N1) and fixed callback order (N2) |
| D11 | Export Power Pin is one command: the saved plane, else the tags (arch has two) |
| D12 | Derive and Export write the video's fps, not a fixed 23.976 |
| D13 | With Bake transforms on, the Style's alphas are added only when a fade is baked |
