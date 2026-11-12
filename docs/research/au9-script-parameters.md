# AU9 research: Aegisub script parameters for seeded node graphs

Read-only survey, 2026-11-12. Local fixtures are under
`tests/fixtures/automation-thirdparty/`. Remote files were read raw from GitHub
at the ref given in each section. The GitHub fetch tool summarised the remote
files, so before a value is pinned in a test, check it against the raw file.

"Unit" means what one output Line stands for. "fbf" means one Line per video frame.

## Summary

| Script | Per-unit | Knobs (default) |
|---|---|---|
| arch.FocusLines | per frame × layer (source deleted) | numlines 40, layers 5, color, width 0.8·W, height 0.7·H, sizechange 10 %, minblur 5, maxblur 10, minlinewidth 1 %, maxlinewidth 1 %, linepointiness 70 % |
| myaa.Bounce | per character × frame (source commented) | layout Vertical, invertDirection off, hideCharacters off, spaceEven on, characterDistance 40, columnDistance 100, maximumOffset 100, finalOffset 30, duration 200 ms, delay 100 ms, split `\|` |
| phos.wave | per Line, in place | fscx 2, fscy 2, fsp 2 (strengths), frequency 0.1 s |
| phos.wobble | per Line, in place (drawing) | freq X/Y 0, strength X/Y 0, wavingSpeed 0; Animate: start/end/accel per knob; Warp: type, magnitude 30, frequency 3 |
| phos.AddGrain | 2 × intensity Lines per Line (source deleted) | intensity 1, color1 white, color2 black |
| phos.ChromaticAbberation | up to 7 Lines per Line (source deleted) | xOffset 2, yOffset 2, color1–3 (Y/M/C), keepBaseColor, textToShape |
| l0.ShakeIt (Position) | per Line group, in place on existing fbf | offX/offY min 0 / max 10, groupLines + field, interval 1, angle 0–180, sign rules, seed, repeat every 12 |
| l0.ShakeIt (Scalar Tag) | per Line group, in place | tag `\frz`, targets, absolute offset 0–10, group diff 0–10, sign rule, seed, repeat |
| ua.FadeWorks (By Letter) | per Line, per character `\t` (in place) | fadein, fadeout, letterfade 120 ms, RTL, `\ko`/word variants |
| ua.Relocator shake | per Line, in place on existing fbf | post (radius), force (scale), Teleport X/Y, smooth, scaling, rotate, layers (on) |
| ua.Relocator randomove | per Line, in place (`\move` only) | Time + slowdown 50 %, Space + x1/y1/x2/y2, distance, ±directions |
| petzku.Shake | per Line, in place on existing fbf | astart 50, aend 30 |
| petzku.Typewriter | per character (or per char × frame) (source commented) | mode menu; static_frames 1 / fade_frames 1 |
| zah.shadshake | per Line, `\t` chain at fixed ms steps | minshake −1, maxshake 1, shaketime = line length, interv 42 ms |
| zah.shapeshaker | per Line, in place | none (±2 px jitter hardcoded) |
| garret.karaoke2alpha | per syllable (source commented) | none |
| ua.Significance letters/explode | per character (source deleted/commented) | explode: distances, direction, proportional, fade, sequence, implode |

Not found: a Thai-fork Typewriter (searched; no public copy surfaced). There is also
no separate unanimated "Typewriter"; its nearest relatives are in ua.Significance (below).

---

## arch.FocusLines (local)
`arch1t3cht-Aegisub-Scripts@5c3c78219699/macros/arch.FocusLines.moon`, v1.0.1.

Dialog: `numlines` int 0–1000 = 40 ("Lines per Layer"); `layers` int 0–50 = 5;
`color` color (no default); `width` float 0–2W = round(0.8·W, −2); `height` float
0–2H = round(0.7·H, −2); `sizechange` float 0–100 = 10; `minblur`/`maxblur` float
0–10 = 5/10; `minlinewidth`/`maxlinewidth` float 0–100 = 1/1; `linepointiness` float
0–100 = 70. The macro needs video, and W×H is `aegisub.video_size()`.

Output: for every frame of the source and every layer `i = 1..layers`, one Line:
- `layer = src.layer + i − 1`, timed to that single frame;
- the text is `{\an7\p1\1c<color>\bord0\shad0\blur<b>\pos(W/2,H/2)}` followed by
  `numlines` 5-point spikes, all in one drawing;
- the blur is a linear lerp, `b = lerp(minblur, maxblur, (i−1)/(layers−1))`, so
  `layers = 1` divides by zero.

Randomness: plain `math.random()` with no seed. Each spike draws its own angle
U[0, 2π), width U[min, max]/100 and three independent ±sizechange/100 radial
jitters. Nothing is shared between layers or frames.

Source Line: deleted. The new Lines are inserted in its place.

## myaa.Bounce (local)
`Myaamori-Aegisub-Scripts@f2a52ee38eeb/macros/myaa.Bounce.moon`, v0.0.2. Uses ASSFoundation.

Dialog (persisted): `layout` dropdown Vertical|Horizontal = Vertical;
`invertDirection` = false; `hideCharacters` = false; `spaceEven` = true;
`characterDistance` int = 40 px; `columnDistance` int = 100 px;
`maximumOffset` int ≥0 = 100; `finalOffset` int ≥0 = 30;
`duration` int ≥10 = 200 ms; `delay` int = 100 ms; `split` edit = `|`.

Layout:
- The text is split into rows (or columns) at `split`, then into characters.
- Character j of row i gets `\pos += (distance·(j−1) + Σwidth·[!spaceEven],
  column_distance·(i−1))`. In vertical layout the axes swap.

Animation: one Line per character per frame.
- `\t` is first baked at the frame time.
- `shift = dir · sin(π·s) · maximumOffset` is added to `\pos` y (x in vertical
  layout), where `s = clamp((t − delay·(j−1))/duration, 0, (2·max − final)/(2·max))`.
- The cap on `s` means the sine settles at `finalOffset`.
- With `hideCharacters`, frames where s = 0 are skipped.

Randomness: none. Source Line: commented. The new Lines are inserted after it.

## phos.wave (local)
`PhosCity-Aegisub-Scripts@0c1acd0d1465/macros/phos.wave.lua`, v1.0.3.

Dialog: `fscx`, `fscy`, `fsp` floatedit = "2"; `frequency` floatedit = "0.1" (seconds per step).

Output: one Line, edited in place. Every byte j of the tag-stripped text gets:
- a base `\fsp(sp+5·sin j)\fscx(sx+10·sin j)\fscy(sy+10·sin j)`;
- then, for each step k·frequency up to (dur+100 ms), a
  `\t(t, t+freq, \fsp(sp+5·sin(fsp·t₂+j))\fscx(…fscx·t₂+j)\fscy(…))`.

The base values come from the style, overridden by the first `\fscx`/`\fscy`/`\fsp`
in the Line. The dialog "strengths" are angular rates; the amplitudes 5 and 10 are
hardcoded. Only the first tag block is kept. Iteration is per byte, so multi-byte
characters (Thai) break. Randomness: none.

## phos.wobble (remote)
`PhosCity/Aegisub-Scripts@0c1acd0d1465:macros/phos.wobble.moon`, v2.0.6.

Modes and their fields:
- **Static:** `wobbleFrequencyX/Y` 0–100 step 0.5 = 0, and `wobbleStrengthX/Y`
  0–100 step 0.01 = 0.
- **Animate:** `{freq,strength}{X,Y}{Start,End}` with the same ranges, plus `*Accel` = 1.
- **Wave:** the Static fields plus `wavingSpeed` 0–5 step 0.1 = 0.
- **Warp:** `warpType` Field|Radial ("Radial" is "Coming Soon"), `warpMagnitude`
  0–100 = 30, `warpFrequency` 0–100 = 3.

Frequency percent mapping: `p < 50 → 0.0000825·1.212^p`, else `1.25p/50`.

Per point: the shape is first split into ~1 px segments. Then
`x += sin(y·fX·2π + phase)·sX` and `y += sin(x·fY·2π + phase)·sY`.
- Wave mode: the phase grows by `wavingSpeed` per selected Line, and the first Line
  already gets 2×.
- Animate mode: the factor is `(i−1)^acc/(n−1)^acc` across the selection, so
  n = 1 gives NaN.
- Warp mode: needs line2fbf Lines. It uses Perlin noise with a fixed seed of 1985,
  depth 4, scale 500, and displaces by `(noise−0.5)·mag·2`.

Text is converted to a shape. Only the last text section survives, and the script
warns if `\an` ≠ 7. The output is one Line per Line, in place:
- everything after the first section is replaced by the drawing;
- the font and spacing tags are removed and `\fscx\fscy` are set to 100.

Randomness: none (the noise is deterministic).

## phos.AddGrain (remote)
`PhosCity/Aegisub-Scripts@0c1acd0d1465:macros/phos.AddGrain.moon`, v1.1.8.

Dialog: `intensity` int ≥1 = 1; `color1` = &HFFFFFF&; `color2` = &H000000&.

Output: `2 × intensity` new Lines per source, same layer. The source is deleted.
- **Layer A:** text sections run `"!!"` → random char (a literal "!!" pattern).
  `\fn Grain\bord0\shad0\b0\1c<color1>` is set.
- **Layer B:** every char except `\` and `N` becomes a random char, with `\1c<color2>`.
- **Dense variant** (not reachable from the GUI): 3c/4c, `\1a&HFE&\3a&HFF&\shad0.01`.

Randomness: `math.random` over [0-9A-Za-z!"',.:;?], one draw per character, no seed.
The effect relies on a "Grain" font. A native port should seed per (Line, copy, char).

## phos.ChromaticAbberation (remote)
`PhosCity/Aegisub-Scripts@0c1acd0d1465:macros/phos.ChromaticAbberation.moon`, v1.0.4.

Dialog: `xOffset` float = 2; `yOffset` float = 2; `color1` &H00FFFF&;
`color2` &HFF00FF&; `color3` &HFFFF00&; `keepBaseColor` check; `textToShape` check.
Buttons: Apply, Revert, Reset GUI. Revert data is stored in extradata `phos.ca`.

Geometry: three copies of the shape, at −(x,y), 0 and +(x,y). The pathfinder
Difference/Intersect operations give up to 7 regions. Each region is coloured by
the per-channel CMY max of the copies that overlap there.
- **Drawing:** each region becomes its own Line with `\1c`.
- **Text:** each region becomes a `\clip(vector)` on a copy of the text, with
  `\pos` moved by ±offset for the outer regions.
- `\pos` is inserted if it is missing.

Output: up to 7 Lines, same layer. The source is deleted. Randomness: none.

## l0.ShakeIt (remote)
`TypesettingTools/line0-Aegisub-Scripts@b6deb78511a0:l0.ShakeIt.moon`, v0.2.0.
It is not in the local fixture, which holds only 4 l0 scripts.

**Shake Position** (all persisted except `seed`):
- `offXMin`/`offXMax` = 0/10 and `offYMin`/`offYMax` = 0/10 (float ≥0) are the
  magnitude bounds.
- `groupLines` = true with `groupLinesField` ∈ {start_time, end_time, layer,
  effect, actor} = start_time, and `interval` int ≥1 = 1 (groups merged per step).
- `angleMin`/`angleMax` 0–180 = 0/180 set the angle between successive offset vectors.
- `signChangeX`/`Y` ∈ {Allow Any, Force, Prevent} set sign flips between groups.
- `signChangeCmb` ∈ {Any number, At least one, Exactly one}.
- `seed` int = `os.time()`; `repeatPattern` = false; `repeatInterval` = 12.

**Shake Scalar Tag:**
- `tag` dropdown of numeric non-global tags = `\frz`.
- Targets: LineBegin = on, ExistingTags, TagSections.
- `absoluteOffsetMin/Max` = 0/10 and `groupOffsetMin/Max` = 0/10 (the difference
  from the previous group); `signChange`; then seed and repeat as above.

Algorithm:
- It does not split into frames. It expects Lines that are already fbf.
- Lines are grouped by the field and chunked by `interval`, and one offset is drawn
  per group.
- `math.randomseed(seed)` runs, then: magnitude U[min, max], sign by rule
  (random ±), and the angle/difference constraint is checked. A failure rerolls,
  up to 5000 times, then errors.
- With repeat on, group i reuses offset `1 + i % repeatInterval`.

Writes:
- `\pos += off`, or both `\move` endpoints;
- `\org += off` when the Line is rotated (style angle or any `\frz/\frx/\fry`);
- the Scalar mode adds to the chosen tag at the chosen targets.

Edited in place. This is the best model for a seeded native shake: an explicit seed
plus constraints.

## ua.FadeWorks (remote)
`unanimated/luaegisub@ade40a212b0a:ua.FadeWorks.lua`, v5.0. This is the same sha as the
local unanimated fixture, which does not include FadeWorks.

Main dialog:
- `fadein`/`fadeout` floats = 0. A value in (0, 1) is a fraction of the duration;
  a negative value counts from the end.
- Accel `inn`/`utt` = 1; `alf` Alpha/Colour; `crl`/`clr` + `c1`/`c2` (from/to colours).
- `tags` edit = `\blur3`, with `tgin`/`tgout`/`tai`/`tao`; `mult` (across lines);
  `ex` (Clip +3 px).
- `letterfade` dropdown {40…1500} = "120" ms per letter; `rtl`; `del`; `ko`; `word`;
  `mir`; `vin`/`vout` (current frame).

**By Letter** (the button):
- Units are `\S\s*` (a char plus its trailing spaces). The base alpha `al` comes
  from the first block, else 00.
- For unit k of n (reversed if RTL), with `lf` = letterfade:
  - `fin1 = floor((fadein−lf)/(n−1)·k)` and `fin2 = fin1+lf`;
  - `fout1 = floor((fadeout−lf)/(n−1)·k + dur−fadeout)` and `fout2 = fout1+lf`.
- Each unit gets `{\alpha&HFF&\t(fin1,fin2,\alpha&H<al>&)}`, then (if fading out)
  `\t(fout1,fout2,\alpha&HFF&)`.
- It strips `\fad` and skips `\p1` Lines. n = 1 divides by zero.
- It errors if a fade ≤ lf. One Line per Line, in place, no randomness.

`\ko` variant: inserts `{\ko<n>}` per letter or word, with
`n = round(fadein/(len−1))/10`, and adds `\2a&HFF&`.

Other modes:
- Apply Fade writes `\fad`.
- Alpha/Colour writes `\t` transforms.
- By Clip animates the `\clip` from the left edge to the right.
- Fadeworks makes fbf copies before and after the Line, with offsets and accel.

## ua.Relocator: shake and randomove (remote)
`unanimated/luaegisub@ade40a212b0a:ua.Relocator.lua`, v4.5.2. The fetch read only the
first ~100 kB of the file.

**Shake** (Repositioning → "Positron Cannon"):
- Radius `post` (main field), else Teleport `eks`/`wai` as separate X/Y radii.
- `force` is the scale radius. `smo` smooth, `sca` scaling, `rota` rotate,
  `layers` = on.
- Per Line: `off = math.random(−100,100)/100 · r`, independently for x, y, frz
  (r = post), fscx and fscy (r = force).
- Smooth blends `(new + 3·prev)/4`.
- With layers on, offsets are cached by `start_time`, so all layers on a frame shake alike.
- Writes `\pos` (created if missing; aborts on `\move`), adds to every `\frz`
  (inserting one if missing), and adds to `\fscx`/`\fscy` (default 100).
- Numbers are rounded to 2 decimal places. Edited in place, expects fbf input, no seed.

**Randomove** (Bilocator → "Hyperspace Travel"):
- Fields: `rmt` Time, `slow` 1–99 = 50, `rms` Space, `rm1..rm4` (x1, y1, x2, y2;
  x2/y2 on), `rmdist` = 0, `plus`/`minus` on.
- Time mode: `\move` t2 = `math.random(t2, t2·100/slow)`.
- Space mode: each ticked coordinate gets `+ math.random(−d·100·[minus], d·100·[plus])/100`.
- Only Lines with `\move` are changed, in place.

## petzku.Shake (remote)
`petzku/Aegisub-Scripts@4d22880da576:macros/petzku.Shake.moon`, v0.1.2.

Dialog: `astart` float = 50 and `aend` float = 30 (amplitude in px).

Per Line, in place: the amplitude is a linear lerp across the selection. The source
says it "iterates backwards", so the first/last mapping may be reversed. The offset is
`r = random()·a` with angle `random()·360°`, so it is uniform in r and not in area.
It writes `\pos` only, replacing the old one (`\move` is not handled). There is no
seed and no anti-repeat. It expects fbf input.

## petzku.Typewriter (remote)
`petzku/Aegisub-Scripts@4d22880da576:macros/petzku.Typewriter.lua`, v0.7.0.

Macros: `Typewriter/fbf`, `Typewriter/line`, and `Unscramble/{line, half, N fade, N static}`.
Dialogs exist only for N static (`static_frames` int = 1) and N fade (`fade_frames` int = 1).

Typewriter makes one Line per character, and each Line holds the full text:
`visible prefix{\alpha&HFF&}rest`. The alpha tag is dropped on the last Line.
- Character i starts at frame `start + (i−1)·framedur`, where `framedur` is 1 (fbf)
  or `dur_frames/len` (line).
- The last Line extends to the source end.
- `\t` and `\move` are retimed so they stay aligned.
- The leading block is copied. Inline blocks are not counted as characters.
- `\N`, `\n` and `\h` count as one character each.
- It is Unicode-aware (`unicode.chars`), but Thai combining marks still count as
  separate characters.

Unscramble makes one Line per frame per character. The scramble character is
`((time+byte)%26)^10 % 26` (deterministic), or a digit for digits; other characters
are unchanged. N fade adds `{\alpha&HXX&}`, ramping on the scrambled frames.

Source Line: commented. New Lines are inserted after it.

## zah.shadshake (remote)
`Zahuczky/Zahuczkys-Aegisub-Scripts@main:miscellaneous_scripts/zah.shadshake.lua`, v1.0.0.

Dialog: `minshake` int = −1; `maxshake` int = 1; `shaketime` int = first Line's
duration; `interv` int = 42 ms.

Per Line, in place:
- a `\xshad<r>\yshad<r>` base, then `floor(time/interv)+1` blocks of
  `\t((i−1)·interv, i·interv, \xshad r\yshad r)`;
- each r is `math.random(min, max)` (integer, inclusive, no seed). If either draw
  is 0, x is forced to 1.
- The same string is inserted before every `}` in the Line.

The steps are in ms and not frame-aligned.

## zah.shapeshaker (remote)
`…/miscellaneous_scripts/zah.shapeshaker.lua`, v1.0.0. No dialog.

Per Line, in place:
- every shape coordinate gets `+ math.random(−2, 2)`, with x and y independent;
- the text is replaced with `{\an7\pos(960,1030)\3c&H000096&\bord0\p1\c&H000096&\shad0}<shape>`.

Everything is hardcoded and there is no seed. It is only worth porting as the
"jitter points ±N" idea. (A related script, zah.timeshake, adds `random(0,1700)` ms to
`start_time` and has no dialog.)

## garret.karaoke2alpha (remote)
`garret1317/aegisub-scripts@master:karaoke2alpha.lua`, v1.1.0 ("K-Timing -> Alpha Timing").
The file shows no licence, so only a summary was taken.

There is no dialog. For each syllable i ≥ 1 (from `aegisub.parse_karaoke_data`),
it makes one Line:
- text = `syl[1..i].text .. {\alpha&HFF&} .. syl[i+1..].text`, with the alpha
  block omitted on the last Line;
- `start = line.start + syl.start_time`;
- `end = line.start + syl.end_time`, except the last Line, which keeps `line.end`.

The `\k` tags disappear. Syllable 0 (the leading tag block and any pre-`\k` text) is
dropped, which is a defect a native port should fix. Source Line: commented. The new
Lines are inserted after it, in syllable order. Randomness: none.

## ua.Significance letter tools (remote)
`unanimated/luaegisub@ade40a212b0a:ua.Significance.lua`, v3.5. This is the nearest
unanimated relative of a typewriter.

- **split into letters (alpha):** one Line per visible character, with the full
  text and every other character at `\alpha&HFF&`. Timing is unchanged, `\pos` is
  added, and the source is deleted.
- **explode:** the same split, then `\pos` → `\move(a, b, a+dx, b+dy, t1, dur)` and `\fad(0, FO)`.
  - The offsets are `math.random` over [−x, x], [−x, 0], [0, x] or the "mostly"
    ranges, or evenly stepped with "proportional".
  - With "sequence": `t1 = round(dur/n/(100/pct))·(i−1)`.
  - Implode reverses the move and uses `\fad(FO, 0)`.
  - Fields: `edistx`/`edisty` = 0, `hortype`/`vertype` = all, `xprop`/`yprop`,
    `xinv`/`yinv`, `ecfo`/`exfad`, `exseq`/`seqpc` = 100, `invseq`, `impl`, `rem`,
    `excom`.
- **transform \k to \t\alpha:** in place. Each `\kf`/`\ko` becomes
  `\alpha&HFF&\t(prev, prev+10k, \alpha&H<base>&)`. Plain `\k` is not matched, and
  text outside the matched segments is lost.

## Not found
- A Thai-fork Typewriter: no public copy found.
- A wave or wobble script apart from phos.wave and phos.wobble: none found.
