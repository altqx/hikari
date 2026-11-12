# Aegisub compatibility decisions

Where HikariSub's automation API and file formats knowingly differ from what they were, or from Aegisub 3.5.1, and why. HikariSub follows Aegisub 3.5.1 and the Lua API additions of arch1t3cht's fork, so scripts written for them run unchanged. Each entry names its card. This is not the legacy parity ledger that wave 7 removed.

## FA4-getpixel-rgb

`frame:getPixel(x, y)` returns three integers, r, g and b, as arch1t3cht's fork does. It used to return one packed integer (`b * 65536 + g * 256 + r`). Outside the frame it returns nil. [FA4 #561](https://github.com/altqx/hikari/issues/561), owner decision 2026-10-31.

## FA4-timecodes-file

`aegisub.project_properties().timecodes_file` is the editing tab's loaded timecodes file, or `""` without one. Until FA21 it was always `""`. [FA4 #561](https://github.com/altqx/hikari/issues/561), coordinator decision 2026-11-02; [FA21 #648](https://github.com/altqx/hikari/issues/648).

## FA21-timecodes-headers

A timecodes file may start with mkvmerge's `# timestamp format v1`/`v2`/`v4` as well as Aegisub's `# timecode format v1`/`v2`. Aegisub 3.5.1 accepts only `timecode`, so it rejects what mkvextract writes today. Save timecodes still writes `# timecode format v2`, so Aegisub reads HikariSub's files. [FA21 #648](https://github.com/altqx/hikari/issues/648).

## FA21-timecodes-v4

v4 files open: their times are sorted into frame order, which gives the frames mkvmerge muxes. Aegisub 3.5.1 doesn't read v4. [FA21 #648](https://github.com/altqx/hikari/issues/648).

## FA21-timecodes-rounding

A fractional time in a v2 or v4 file rounds to the nearest millisecond, as mkvmerge rounds it when muxing (`41.708` is frame time 42). Aegisub 3.5.1 drops the fraction (41), so its frame times can be 1 ms off the muxed video. v1 files round in both. [FA21 #648](https://github.com/altqx/hikari/issues/648).

## FA21-timecodes-lines

Blank lines, lines of spaces and `#` comments are skipped anywhere, as mkvmerge skips them. Any other line that isn't a time (v2, v4) or a range (v1) fails the load and names its line number. Aegisub 3.5.1 silently skips a v2 line that doesn't start with a number, which moves every later frame, and rejects a v1 line of spaces. [FA21 #648](https://github.com/altqx/hikari/issues/648).

## FA4-frame-times

`aegisub.frame_from_ms` and `aegisub.ms_from_frame` round as Aegisub 3.5.1 does. `frame_from_ms(ms)` is the first frame that starts at or after `ms`. `ms_from_frame(f)` is halfway from frame f−1 to frame f, rounded up. Past the last frame, both continue at the video's average rate.
- Before, scripts got the frame shown at `ms` and the frame's own start time, clamped to the last frame. With those values arch.CenterTimes failed its own check, and arch.TimingBinds and arch.Line2Fbf could be off by up to half a frame.
- Only Lua scripts see this. The app's timing commands, video seeking, the grid and the HikariSub API's `video.frame_at_time` and `video.time_of_frame` keep the frame shown at a time and its start.

[FA4 #561](https://github.com/altqx/hikari/issues/561), coordinator decision 2026-11-02.

## FA4-lfs-returns

`lfs.chdir`, `mkdir`, `rmdir` and `touch` return `true`, or `nil` and the error, as in Aegisub 3.5. The bundled `aegisub/lfs.moon` used to pass their boolean through `tonumber`, so they always returned `nil`. `lfs.dir` now raises its error message, where it used to raise the number 2. [FA4 #561](https://github.com/altqx/hikari/issues/561), owner scope 2026-11-02.

## FA4-load-errors

A failing `include()` reports `Error loading Lua include "<name>":` or `Error running Lua include "<name>":`, followed by Lua's message. A failing `require()` reports `Error loading Lua module "<path>":`, as Aegisub 3.5 does. A script that cancels inside an included file is still cancelled, not failed. [FA4 #561](https://github.com/altqx/hikari/issues/561), owner scope 2026-11-02.

## FA4-deep-copy-cycles

`aegisub.util.deep_copy` makes a table that refers to itself refer to the copy, as Aegisub 3.5 does. It used to point the copy back at the original. [FA4 #561](https://github.com/altqx/hikari/issues/561).

## XD1-karaoke-marks

A templater Apply's save is no longer byte for byte Aegisub 3.2.2's. Each generated Line and its karaoke Line get a `_hikari_kara_from` or `_hikari_kara_source` entry in `[Aegisub Extradata]`, so a reopened file still knows which karaoke Line each Line came from. There's no setting to turn this off. Aegisub 3.5.1 keeps the entries through its own load and save. Apart from them, the output is still 3.2.2's. [XD1 #659](https://github.com/altqx/hikari/issues/659), owner decision 2026-11-03.

## FA12-wheel-resize

Aegisub 3.5's wheel choice "Resizes the video box" is not offered, because HikariSub's video fills a docked panel whose size the layout sets. The plain wheel on the video therefore does nothing by default, where 3.5 resized the box. Ctrl+wheel zooms and Shift+wheel pans, as in 3.5, and Settings › Video › View › Zoom and pan sets what each wheel does: zoom, zoom reversed, pan, pan with X and Y swapped, or nothing. [FA12 #569](https://github.com/altqx/hikari/issues/569).

## FA8-borderstyle-values

A Style's BorderStyle is kept when it is 1, 3 or 4. Any other value reads as 1, the outline that libass draws for it, and is saved as 1, as legacy HikariSub did. Aegisub 3.5 keeps any integer as written, so a value such as 30 survives an Aegisub save but becomes 1 in a HikariSub one. [FA8 #565](https://github.com/altqx/hikari/issues/565), coordinator decision 2026-11-03.

## FA8-lua-borderstyle

A Lua style's `borderstyle` is the Style's BorderStyle as an integer, 1, 3 or 4, both when a macro reads it and when it writes it back, as in Aegisub 3.5 (`auto4_lua_assfile.cpp`). It used to read 1 for an opaque box and 0 for anything else, and to write BorderStyle 3 only for −3, so a macro that rewrote a Style turned an opaque box into an outline. This fixes that confirmed legacy defect. [FA8 #565](https://github.com/altqx/hikari/issues/565), coordinator decision 2026-11-03.

## FA15-bake-default

Grid ▸ Split ▸ "Into frames" bakes animations by default (Grid ▸ Split ▸ Bake animations, `grid.splitFramesBakeAnimations`). Each frame's Line gets the values its `\t` tags have reached, its `\fad` or `\fade` applied to every alpha, and its `\move` as a `\pos`, by the rules of arch1t3cht's FBF-ifier (`arch.Line2Fbf`). Without the bake, every frame keeps the `\t`, `\fad` and `\fade` tags and only `\move` becomes `\pos`, so with the option on the split differs from that output:
- `\move` follows FBF: times given in the wrong order are swapped, and a `\move` without times, or with both at 0 or below, runs over the whole Line. A `\move` whose two points are the same stays.
- A `\t` that has not finished gives its value so far, with its acceleration; one that cannot be in between (such as `\an`) applies only once it has finished.
- A later block's `\t` starts from the value an earlier block's `\t` has reached, as the renderer shows it.
- Tags written by the bake keep FBF's values but not its tag order or number formatting.

With the option off, the split is byte for byte what it was before FA15. [FA15 #572](https://github.com/altqx/hikari/issues/572), owner decision 2026-11-03 ("yes to all").

## FA14-search-step

Align to video checks every frame. Aegisub's Align subtitle to video by key point (wangqr's tool in arch1t3cht's fork) steps two frames at a time, so it can step over a single frame where the point does not hold and time the line across it. A run of matching pixels that reaches the frame's edge ends at the edge. In Aegisub it ended at the picked point, so a shape that touched the edge in one frame and had shrunk away from it in the next could still match. [FA14 #571](https://github.com/altqx/hikari/issues/571), coordinator decisions 2026-11-03.

## FA19-mono-scope

"Play in mono" (Settings › Audio › Playback, `audio.playMono`) covers HikariSub's own PortAudio output only: the audio panel and indexed editing playback. General video playback keeps the file's channels, because Qt Multimedia owns that sound and its PCM is not routed through PortAudio. Aegisub 3.5.1 has no mono option outside its Avisynth audio provider. [FA19 #576](https://github.com/altqx/hikari/issues/576), coordinator decision 2026-11-03.

## FA19-dummy-fraction

A dummy video's frame rate may be a fraction, such as `24000/1001`, and its path stores it as written (`?dummy:24000/1001:…`), as Aegisub 3.5.1 does. Frame n starts at n × 1000 × den / num ms, truncated, so timing stays exact over long durations. A decimal rate still writes `%f` (`?dummy:23.976000:…`) with the old float timing, byte for byte as before. Aegisub releases before 3.5 can't open a fractional dummy path. [FA19 #576](https://github.com/altqx/hikari/issues/576).

## FA9-dummy-drawn-size

A dummy video can be up to 100000 pixels on each side, as in Aegisub 3.5. The smallest is still 300×200. When a side is over 8192, the frames are drawn scaled down to fit 8192×8192, so a huge dummy doesn't need gigabytes of memory per frame. The size you asked for is what the visual tools, the PlayRes and LayoutRes checks, Script properties and Lua (`aegisub.video_size()`) see. Only the picture and `aegisub.get_frame()` have the smaller size. [FA9 #566](https://github.com/altqx/hikari/issues/566), coordinator decision 2026-11-03.

## FA9-clip-places

Vector clips are written with up to two decimal places, as Aegisub 3.5 writes them, by the vector clip tool and the Position shifter. They used to be rounded to whole pixels. Rectangle clips, and the clips the scale and rotation tools rewrite, keep whole pixels. Files from older versions read back unchanged. [FA9 #566](https://github.com/altqx/hikari/issues/566).

## FA9-video-property-prompts

When a video opens, HikariSub offers to set a missing or mismatched YCbCr Matrix and LayoutRes, as Aegisub 3.5 does, and each question has its own row in Settings › Video. "Don't ask again" sets that row. 3.5's extra choice to remember the last answer is left out. [FA9 #566](https://github.com/altqx/hikari/issues/566), coordinator decision 2026-11-03.

## 738-resample-stretch-axis

Resample subtitles with "Stretch" scales text by the smaller of the two resolution factors, and puts the rest on the axis that grew more: ScaleX and `\fscx` when the width grew more, ScaleY and `\fscy` when the height did. Drawings are scaled to match. Before, a taller change also went to ScaleX and `\fscx`, so stretched text came out wide when it should have been tall. A change that grows the width more resamples as before. [#738](https://github.com/altqx/hikari/issues/738), owner decision 2026-11-04.

## AU7-gradient

The Gradient visual tool does natively what the gradient scripts do (records in [AU7 #843](https://github.com/altqx/hikari/issues/843)). The bundled Gradient Factory and Skew gradient still ship unchanged, defects included; those defects are listed in [#885](https://github.com/altqx/hikari/issues/885). The departures below were accepted at the owner's gate on 2026-11-10 (Q227–Q229).

- **G1, rounding.** Colours and alphas round half up. The scripts truncate, except Gradient Factory's By character.
- **G2, both ends.** t runs from 0 to 1 inclusive: index/(n−1) for strips, and the last character or Line gets To. ua.Colorize and l0.LerpByChar stop one unit short.
- **G3, rotated strips.** Overlap is measured across the strip, the strips cover the rotated box, a vertical drag works, and a drag of no length is refused. phos.RotateGradient overlaps 0.75 px vertically, deletes every Line when P3 is on the base and divides by zero on a vertical base.
- **G4, originals.** The original Lines are commented and kept above their strips, never deleted. RotateGradient and zf.gradientCut delete them.
- **G5, merge.** Merge equal strips joins only neighbouring strips with the same tags and removes only the strips it merged. petzku's CombineGradientLines deletes Lines it didn't merge.
- **G6, along the clip.** Rings come from a true inset with round joins at reflex corners, so concave and self-touching clips work, and rect clips work. lyger's ClipGrad has no miter limit and crashes on a rect clip.
- **G7, hue.** Colours blend in RGB or HSL (the shortest way round the hue). l0.LerpByChar's HSV treats red as 360 against grey's 0.
- **G8, wedge thickness.** Thickness is limited to 0–1. phos declares a minimum of 1 and a maximum of 0.
- **G9, inline tags.** In the strip modes, inline instances of an interpolated tag are replaced, so no older `\1c` overrides a strip. Gradient Factory removes only the last channel's.

## AU11-video-analysis

Video › "Track colour…" and "Find fade…" do natively what zah.aegi-color-track 2.1.1 and phos.AutoFade 1.1.2 do (records in [AU11 #846](https://github.com/altqx/hikari/issues/846)). Both read one pixel a frame, decoded without subtitles. The departures below were accepted at the owner's gate on 2026-11-11 (Q271–Q278).

- **V1, repeated colours.** Skip repeated colours, on by default, writes no `\t` for a frame whose value is the same as the last one written. With it off, the tags are byte for byte color-track's.
- **V2, alpha hex.** A colour equal to the sign or background colour writes `&H00&` or `&HFF&`. color-track writes the decimal `&H0&` and `&H255&`.
- **V3, tracking data.** Tracking data moves the picked point by the data's offset from the video's frame. color-track reads the rows as absolute positions, which is the same when the picked point is the tracked point.
- **V4, each Line's own frames.** Each selected Line gets the colours of its own frames. color-track tracks over the first selected Line's frames and writes that to every Line.
- **V5, every frame.** Find fade checks every frame after the video's frame for the fade out. AutoFade first halves the distance to the end while the colour holds, so it can step over a flicker.
- **V6, unread frames.** A frame where the point is outside the picture counts as neither shown nor gone. AutoFade stops with an error.

## AU20-text-commands

Line breaks, Change case and Swap dialogue alternatives do natively what these scripts do: ua.LineBreaker, petzku.Autowrapper, l0.InsertLineBreaks, ua.ChangeCase, Flux.TitleCase, zf.allCharsTo and Flux.DialogSwapper. The bundled "Make text fullwidth" demo still ships unchanged. The behaviour record and the owner's answers are on [AU20 #854](https://github.com/altqx/hikari/issues/854) (Q279–Q286, 2026-11-11).

- **T1, balanced break.** The break goes where the two rendered halves are closest in width, measured with the Line's style and tags. The break points are ICU's, so Thai and CJK break between words and never inside a grapheme cluster. A point right after `. , ? ! : ;` wins when it is within 10% of the Line's width of the best one. Line Breaker's English word lists aren't ported.
- **T2, removing a break.** A `\N` becomes one space, or nothing where the script needs no space (CJK, Thai).
- **T3, `\q2`.** "Wrap only at \N" sets or clears `\q2` in the start block of the selected Lines. Autowrapper's file-wide pass and its Effect notes aren't ported.
- **T4, Sentence case.** Other letters are lowered only when the whole Line is in capitals. ua.ChangeCase's lists of months, weekdays, languages and honorifics aren't ported.
- **T5, Full-width.** The output is the demo's byte for byte (`tests/fixtures/au20/fullwidth-golden.tsv`, made by running the demo), except that `\N`, `\n`, `\h` and drawings stay as they are. The demo turns `\N` into `＼Ｎ`, so the break is lost, and it converts drawing commands.
- **T6, Half-width.** `｛`, `｝` and `＼` stay full-width, so the result never gains a tag or an escape.
- **T7, Dialog Swapper.** It works on the selection with the `*` delimiter. The script's style-name filter and its delimiter setting aren't ported. A Line whose Effect is `***` still has its comment state toggled.

## AU8-motion-data

Video › Tracking does natively what Aegisub-Motion 1.0.9, 0x.DeriveTrack, arch.DerivePerspTrack, petzku.ExtrapolateMove and phos.ExtrapolateTracking do. On the 35-case corpus it matches Aegisub-Motion's output where that output is stable (`docs/research/motion-data-parity.md`). The bundled scripts still ship unchanged. The departures below were accepted by the owner on 2026-11-09 (Q201–Q206). [AU8 #842](https://github.com/altqx/hikari/issues/842).

- **D1, relative frame.** The relative frame is 1 to the number of frames. Aegisub-Motion's 0, negative and absolute frames aren't offered.
- **D2, data from a file.** **Open file…** loads a data file into the text box. A path typed in the box isn't read.
- **D3, Shake rotoshape.** SRS data isn't read. Aegisub-Motion still reads it.
- **D4, `\move`.** A `\move` is clamped to its times before it is tracked, as renderers show it. Aegisub-Motion extrapolates past them, so the sign overshoots. A 4-argument `\move` runs over the whole Line.
- **D5, shifted transforms.** With Bake transforms off, every shifted `\t` and every tag is kept. Aegisub-Motion's dedupe keeps one `\t` a block, and sometimes drops `\fscx`.
- **D6, empty clip.** Linear mode writes no `\clip()\t(…,\clip())`.
- **D7, Linear with `\move`.** The Line's `\move` is read at the first and last frames' mid-times. Aegisub-Motion reads it at the absolute time, which puts the sign far off.
- **D8, Linear with karaoke.** It works. Aegisub-Motion fails with "attempt to compare number with nil".
- **D9, selection.** Apply selects the new Lines, and Revert selects the restored Lines. Aegisub-Motion returns no selection, so the deleted Lines stay selected after its Revert.
- **D10, fixed order.** The added tags and the callbacks run in a fixed order. Aegisub-Motion runs them in Lua `pairs()` order: tag order varies between runs, and five cases differ in their values (rect clips can drop the rotation, Absolute moves a clip by another frame's offset).
- **D11, one Power-Pin command.** Copy Power-Pin track from lines uses each Line's saved plane, else its tags' quad. arch.DerivePerspTrack has two macros, From Outer Quad and From Tags.
- **D12, frame rate.** The two copy commands write the video's frame rate. The scripts write 23.976.
- **D13, alphas.** With Bake transforms on, the Style's `\1a` `\3a` `\4a` (or `\alpha&H00&`) are added only when the Line has a fade to bake. Aegisub-Motion adds them to every Line.

## AU4-remove-tags

**Remove tags…** (Grid and Line editor menus) does natively what Clean Tags, Strip tags and phos.RemoveTags do, plus the Line cleanup actions of ua.ScriptCleanup and Masquerade's tag merge. "Clean Tags rules" gives byte for byte what the bundled `cleantags.lua` 1.301 gives (`hikari_core_cleantags_parity_tests` runs both on every fixture Line and 20,000 generated ones). The bundled Clean Tags and Strip tags scripts and the Clean Tags export filter still ship unchanged. The action list is in `docs/research/au4-cleanup-actions.md`. [AU4 #839](https://github.com/altqx/hikari/issues/839).

- **D1, Merge adjacent tag blocks.** Masquerade's `tagmerge` within a Line, byte for byte. Masquerade's Merge Tags button, which merges several Lines with the same text, stays in the script.
- **D2, Drop overridden tags.** In a block, a tag that a later one of the same name replaces goes (a relative `\fs+` keeps the size before it, and `\r` ends the run). Across the Line, a repeat of `\pos`/`\move`, `\org`, `\an`/`\a` or `\fad`/`\fade` goes, because renderers use the first. Script Cleanup's `duplikill` keeps the last `\an` instead, which changes where the Line renders; it also drops tags inside `\t` that a later tag outside replaces, and that is left alone here.
- **D3, Remove comments.** Drops blocks with no tag, `{\N}` included, as Script Cleanup's "Remove comments from lines" does. It doesn't trim the spaces round them.
- **D4, Strip tags.** Every family with Remove comments on removes what Strip tags removes, but an empty `{}` goes too.
- **D5, Whole file.** Changes every Line but Comments, as Clean Tags does. Selected lines changes Comments too, as Strip tags and phos.RemoveTags do.
- **D6, phos.RemoveTags.** Families replace its per-tag list, which lists only the tags the selection holds. Its "inline except last", "invert" and "from transforms only" choices stay in the script.

## AU22-interpolate

Interpolate across lines does natively what lyger.FbfTransform 2.0.1 and l0.CascadeTransforms 0.1.0 do (records on [AU22 #856](https://github.com/altqx/hikari/issues/856)). Neither is bundled, so there is no byte-identical target. The departures below were accepted at the owner's gate on 2026-11-11 (Q247–Q250).

- **I1, which tags.** The tags whose values differ between the ends are found and listed, all ticked. FbfTransform offers 30 fixed checkboxes and presets, and rewrites every ticked tag even when both ends agree.
- **I2, blocks.** Inline blocks line up by character. FbfTransform compares byte lengths, so it splits multibyte text mid-character, and it corrupts Lines whose text differs; here only the start block counts then, and the dialog says why.
- **I3, spellings.** `\1c` stays `\1c` and `\fr` is interpolated. FbfTransform rewrites `\1c` as `\c` and ignores `\fr`.
- **I4, clips.** Rect clips round to whole pixels and vector clips lerp point by point, `\iclip` too; a pair that can't be interpolated is shown with the reason. FbfTransform truncates rect clips and silently skips anything else.
- **I5, rounding.** Numbers keep two decimals, colours and alphas round half up (tag_lerp). FbfTransform writes three decimals.
- **I6, transforms.** `\t` lists of the same shape at both ends are interpolated. FbfTransform only strips its tags from `\t`.
- **I7, cascade.** Cascade transforms re-times each Line of the run as line0's script does, after the interpolation, with whole milliseconds.

## AU10-splits-and-overlaps

AU10 [#841](https://github.com/altqx/hikari/issues/841) adds two Grid splits and a Select lines field. They do what Split Tag Sections, ILL.SplitText's By Tags Blocks, l0.SplitLines' Split at Tags, petzku.SplitTimer and the bundled Select overlaps do. Select overlaps still ships unchanged.

- **D1, Overlapping lines.** This Select lines field finds the Lines that start while an earlier non-comment Line is still on, the same set as `select-overlaps.moon` (`SelectOverlapsFindsWhatTheOverlappingLinesFieldFinds` runs the script on 40 generated files). With several Lines selected, the script looks only at those. The field looks at every Line the Grid shows; use Deselect or Add to selection to narrow it.
- **D2, Split at tag blocks.** Each run of text and the blocks before it becomes one Line, placed with `\pos` (or `\move`) where it was in the Line, measured as the other text splits measure. Each piece carries the tags in effect for it. A `\r` drops the tags before it, and `\k` syllables before the piece become one `\k` delay. The positions come from the app's font metrics, not the renderer's. A Line without tag blocks is left as it is.
- **D3, `\org` on rotated splits (departure).** Character, word, wrap and tag block splits now add `\org` at the Line's position when the Line rotates (`\frx`, `\fry`, `\frz` or the Style's angle) and has no `\org`, so the pieces turn about the Line's point as ILL.SplitText's do. Before this, each piece turned about its own position. With a `\move`, the origin stays at the move's start.
- **D4, Split every N frames or seconds.** Pieces of N frames (cut on frame starts, CFR or VFR) or N seconds (on the next frame start when there is video). The first piece keeps the Line's start and the last its end. Each piece's `\move`, `\t` and `\fad`/`\fade` times move back by its offset, so each piece's animation is worked out for that piece (Q339) and the pieces render as the Line did: `split_render_tests` renders both with libass at nine times across a Line with a timed `\move`, a `\t` and a `\fad`, and the frames are pixel-identical. One that is over by the piece's start becomes its end state. One that starts after the piece is dropped, and so is a fade the piece only sees fully opaque. `\k` timing isn't moved.
- **D5, What stays in the scripts.** l0.SplitLines' Split at Even Interval, Split at Custom Interval and Split at Indexes stay in the script. ILL.SplitText's By Chars, By Words and By Line Breaks are the Grid's Into characters, Into words and By wraps.

## TH2-unicode-graphemes

`aegisub.unicode` has `graphemes(s)`, an iterator over UAX #29 extended grapheme clusters that returns each cluster and its index, as `chars` returns code points. It matches the app's own cluster splitting (PCRE2 `\X`), so `กิน` gives `กิ` and `น`, and `น้ำ` stays one cluster. Aegisub 3.5.1 has no such function. When the native part is missing, for example with HikariSub's `unicode.moon` in another host, it falls back to joining combining marks, the Thai vowel and tone marks and emoji ZWJ sequences onto the previous character. `aegisub.re`'s `\X` is unchanged. [TH2 #830](https://github.com/altqx/hikari/issues/830), owner decision Q142.

## TH2-bezier-clusters

The bundled BezierToText ("Bezier Curve Imposition", now 1.1) places one Line per grapheme cluster instead of per code point, so a Thai vowel or tone mark, or any combining mark, stays in its base character's Line instead of being placed and rotated on its own. A mark that an override block separates from its base joins the base's Line too, with that block before it. Text without combining marks gives the same output as 1.0. The 2.3 rewrite in Jibrill's copy is not adopted. [TH2 #830](https://github.com/altqx/hikari/issues/830), owner decisions Q143 and Q145.

## TH3-thai-auto-split

Auto split gives each Thai grapheme cluster its own syllable, sharing the Line's time equally: `กินน้ำ` becomes `กิ|น|น้ำ`, and ำ stays in its cluster. Before, a Thai word stayed one syllable and only spaces split it, as Aegisub's split at spaces does. Settings › Audio › Karaoke › "Split Thai into character clusters" is on by default; off keeps each Thai word whole. Text without Thai splits as before. Clusters aren't Thai syllables (`กิน` is one syllable); real Thai syllable breaks are a later card. This replaces bundling th2syl.lua. [TH3 #831](https://github.com/altqx/hikari/issues/831), owner decision Q144.

## TH4-grapheme-char-split

Grid › "Split lines into characters" and the node-effect `char` units split text by grapheme cluster (UAX #29). A base character keeps its marks: `กินน้ำ` gives `กิ`, `น` and `น้ำ`, `é` written as e plus a combining accent stays whole, and an emoji or an emoji ZWJ sequence is one piece. A mark after an override block joins its base's piece together with the block. Before, the Grid split each UTF-16 unit, which cut marks off and broke surrogate pairs in two, and node effects split by code point. Text without combining marks or surrogate pairs splits as before. [TH4 #832](https://github.com/altqx/hikari/issues/832), owner decision Q146.

## TH1-template-char-graphemes

The karaoke templater's `template char` splits a syllable by grapheme cluster (UAX #29, `unicode.graphemes`). A base character keeps its marks: `กิน` gives `กิ` and `น`, `น้ำ` stays one char, ำ is not decomposed, and a combining accent, an emoji ZWJ sequence or a flag is one char. Karaoke Templater 2.1.7 splits by code point, which puts each Thai mark on a Line of its own. A `code once` Line with `graphemes = false` splits as 2.1.7 does. Text without combining marks, joiners or surrogate pairs gives 2.1.7's output byte for byte, as the corpus shows. [TH1 #829](https://github.com/altqx/hikari/issues/829), owner decisions Q138–Q141.

## TH5b-builtin-templater

The Karaoke Templater is built in. The Lua helper runs it natively. Lua runs only the templates' `code` Lines and `!expressions!`. The Script Manager lists it as built-in: it can be reloaded, but not edited or unloaded. Its macros keep their names, and shortcuts still know them as `kara-templater.lua:0` and `:1`. `kara-templater.lua` is no longer bundled in Autoload; it stays in the tests as the oracle. A loaded script whose macro names are the built-in's is unloaded, and the Runs log says so once. The "Karaoke template" export filter isn't available, because HikariSub has no export filters. To generate the lines, run the "Apply karaoke template" macro. [TH5b #833](https://github.com/altqx/hikari/issues/833), owner decisions Q178 and Q179.

## TH5b-line-template-order

Named line templates run after the unnamed ones, in the order their names first appear. Unnamed line templates run in file order, as before. 3.2.2 took named templates from a Lua table with `pairs`, and LuaJIT seeds its string hash at random, so their order could change from one run to the next. [TH5b #833](https://github.com/altqx/hikari/issues/833).

## TH5b-no-leaked-globals

Evaluating an `!expression!` no longer sets the globals `f` and `err`. 3.2.2 compiled each expression into those two globals, so a template or `code` Line could read the last compiled expression's function or error. [TH5b #833](https://github.com/altqx/hikari/issues/833).

## TH5b-no-trace-lines

The templater no longer writes its level 5 trace lines ("Applying template to one syllable with text: …" and similar), even at trace level 5. Problems are still logged at levels 2 and 3 as before. [TH5b #833](https://github.com/altqx/hikari/issues/833).

## TH5b-unmeasured-text

When the host cannot measure a karaoke Line's text, the templater logs "could not measure text in the Style …" as a problem, counts that text as 0 wide and goes on. 3.2.2 stopped the run with a Lua error. [TH5b #833](https://github.com/altqx/hikari/issues/833).

## TH5b-native-layout

The templater lays karaoke Lines out from the Document's Styles as they were when the run started. A `code` Line that changes a Style table (`line.styleref`, the `styles` table) or replaces `aegisub.text_extents` no longer changes where later Lines' syllables go. `char` templates still measure each character with `aegisub.text_extents`, as 3.2.2 did. [TH5b #833](https://github.com/altqx/hikari/issues/833).

## TH5c-karaok-mode

A Document can say which template language it uses: `Karaoke Templater: karaOK` or `Karaoke Templater: Aegisub 3.2.2` in `[Script Info]`. Without that line, a template or code Line using syntax only karaOK has picks karaOK. That syntax is `lsyl`, `lword`, `lchar`, `furichar` or `word` in a template's Effect, `code word`/`char`/`furichar`, `k_retime(`, `$ci`/`$slen`/`$llen`/`$len`, or `tenv.word`/`char`/`syll`/`basechar`. `template char` and `style` alone don't. Otherwise the Document stays in 3.2.2 mode. In karaOK mode the built-in templater behaves as upstream karaOK (`lost-logarithm/karaOK` f749c4fa64e7) does, apart from the TH5c-karaok entries below. Upstream karaOK is a separate script with its own two macros; the built-in keeps "Apply karaoke template". Corpus: `tests/backends/karaoke-corpus/karaok/`, compared byte for byte with upstream. [TH5c #833](https://github.com/altqx/hikari/issues/833), owner decisions Q153, Q157 and Q166.

## TH5c-karaok-word-positions

Words count characters as the rest of the Line does. Upstream found words with `gmatch("()…")`, which gives byte positions, and compared them with character positions, so on any Line with non-ASCII text characters and syllables went to the wrong words and word tags to the wrong characters. Corpus: `tests/backends/karaoke-corpus/karaok/departures/word-positions.ass`. [TH5c #833](https://github.com/altqx/hikari/issues/833), owner decisions Q161 and Q160.

## TH5c-karaok-syl-char

`template syl char` is a `char` template. Upstream put it in both lists, so it ran once per syllable and again once per character. Another pair of unit classes on one template keeps the first and reports "Unable to combine … class templates with the … class this template has". Corpus: `tests/backends/karaoke-corpus/karaok/departures/syl-char.ass`. [TH5c #833](https://github.com/altqx/hikari/issues/833), owner decision Q161.

## TH5c-karaok-named-classes

A named line template keeps the unit class of its first Line. Upstream let each Line of a named template reset it, so `template lsyl X` followed by `template line X` lost its per-syllable part and wrote no text. A later Line with a different unit class (`lword` after `lsyl`) reports "Unable to combine … class templates with the … class this template has" and adds nothing. Corpus: `tests/backends/karaoke-corpus/karaok/departures/named-classes.ass`. [TH5c #833](https://github.com/altqx/hikari/issues/833), owner decision Q161.

## TH5c-karaok-k-retime

`k_retime` shifts the syllable for the current loop iteration only. Upstream shifted the shared syllable, so every later loop iteration and every later template on that syllable saw the times already moved. Corpus: `tests/backends/karaoke-corpus/karaok/departures/k-retime.ass`. [TH5c #833](https://github.com/altqx/hikari/issues/833), owner decision Q161.

## TH5c-karaok-style-text

`style X` runs a template on Lines whose Style name contains X as plain text. Upstream read X as a Lua pattern, so `.`, `-`, `(` and similar in X matched more Styles than written. Corpus: `tests/backends/karaoke-corpus/karaok/departures/style-plain.ass`. [TH5c #833](https://github.com/altqx/hikari/issues/833), owner decision Q161.

## TH5c-karaok-furichar-tags

Each furigana character of a `furichar` template takes its own syllable's override tags, without `\k`. Upstream matched each furigana character's index inside its furigana part against the tags' places in the main text. So the first character of every furigana part took the Line's first tags, whichever syllable it belonged to, and the other characters took none. With `{\k40\1c&H0000FF&}漢|かん{\k30\b1}字|じ`, upstream gives `{\1c&H0000FF&}か`, `ん`, `{\1c&H0000FF&}じ`; HikariSub gives `{\1c&H0000FF&}か`, `{\1c&H0000FF&}ん`, `{\b1}じ`. It shows with `keeptags` and wherever an expression reads a furigana character's `text`. Corpus: `tests/backends/karaoke-corpus/karaok/departures/furichar-tags.ass`. [TH5c #833](https://github.com/altqx/hikari/issues/833), owner decision Q343.

## TH5c-karaok-no-leaked-globals

A run no longer sets the globals `textbuffer` and `w`, which upstream wrote while it built Lines and words. Corpus: `tests/backends/karaoke-corpus/karaok/departures/leaked-globals.ass`. [TH5c #833](https://github.com/altqx/hikari/issues/833), owner decision Q161.

## TH5c-karaok-graphemes

In karaOK mode, `char`, `lchar`, `$ci`, `$slen`, `$llen` and `$len` count grapheme clusters, as TH1 does for 3.2.2's `char`. Upstream counts code points. `graphemes = false` in a `code once` Line counts code points again. Corpus: `tests/backends/karaoke-corpus/karaok/departures/thai.ass`. [TH5c #833](https://github.com/altqx/hikari/issues/833), owner decision Q160. Thai support by Jibrill.

Q161's seventh defect, text before the first `\k` shifting the character-to-syllable map, can't happen in HikariSub: that text belongs to syllable 1 for upstream and the built-in alike (corpus `karaok/leading-text.ass`), so it is not a departure.

## TH5d1-stock-additions

In 3.2.2 mode, karaOK's additions that don't clash with 3.2.2's syntax still work. Each one replaces a 3.2.2 warning or error:

- `style X` on a template runs it only on Lines whose Style name contains X as plain text. 3.2.2 logged "Unknown modifier in template: style" and ran the template on every Line of its Style.
- The `lsyl`, `lword`, `lchar`, `word` and `furichar` template classes run as they do in karaOK mode. 3.2.2 logged "Unknown modifier in template: lsyl" (and so on) and made the template a `syl` template.
- `code word`, `code char` and `code furichar` run once per word, character and furigana character. 3.2.2 logged "Unknown modifier in code template: word" (and so on) and ran the Line once, before the first karaoke Line.
- The template environment has `k_retime`, `table`, `pairs`, `ipairs`, `tonumber`, `tostring` and `type`. In 3.2.2 a call to one of them failed with "Runtime error in template code" or "Runtime error in template expression".

The one exception is karaOK's `$ci`, `$slen`, `$llen` and `$len`: in 3.2.2 mode they stay as text and log "Unknown variable name", as in 3.2.2 (owner decision Q346). `subs` and the kept `line.text` stay karaOK-only too (TH5c-karaok-mode). Without a mode line, every addition here except `style` and the environment names picks karaOK mode (TH5c-karaok-mode), so these apply as listed to a file whose mode line says `Aegisub 3.2.2`. Corpus: `tests/backends/karaoke-corpus/departures/stock-additions.ass`. [TH5d1 #833](https://github.com/altqx/hikari/issues/833), owner decisions Q159 and Q346.

## TH5d1-furigana-styles-false

A `code once` Line that sets `furigana_styles = false` adds no "-furigana" Styles to the file: the built-in drops the ones it made for the run, and the output equals upstream's. Upstream karaOK has a second macro, "Apply karaoke template (modded)", that does the same; the built-in has no extra macro. [TH5d1 #833](https://github.com/altqx/hikari/issues/833), owner decision Q163.

## TH5d1-node-effects-mode

Node effects follow the Document's template mode. In a karaOK-mode Document, node-effect places round to 0.1 px as karaOK's `$` variables do, and a syllable with no duration isn't blank, so For each's "skip blank" keeps it as karaOK's `noblank` does. In 3.2.2 mode both stay as before. "Export as template lines" writes karaOK templates (`line`, `lsyl`, `char`, `code char`) or 3.2.2 templates (`pre-line`, `line`, `syl char`, `code syl`). It remembers the last choice, karaOK by default, and a karaOK export sets the Document's mode line to karaOK. [TH5d1 #833](https://github.com/altqx/hikari/issues/833), owner decisions Q154, Q155 and Q162.

## TH5d2-ln-kara

HikariSub ships karaOK's `ln.kara` library, by logarithm with petzku, as `Automation/automation/Include/ln/kara.lua`, from `lost-logarithm/karaOK` f749c4fa64e7 with the fixes below. Upstream states no licence; HikariSub's changes are MIT, and the file's header says so. As for every stock module, `require "ln.kara"` loads the shipped copy before one in the script's folder or the user's Include, so a user who installed upstream's copy gets the fixed one. The Template editor completes `ln.` (docs/karaoke-templater.md). Corpus: `tests/backends/karaoke-corpus/karaok/ln-kara.ass`, compared byte for byte with upstream karaOK running upstream's unchanged `ln/kara.lua` (`karaok/include/`). [TH5d2 #833](https://github.com/altqx/hikari/issues/833), owner decisions Q164 and Q165.

## TH5d2-ln-kara-tags

`ln.line.tags` and `ln.syl.tags` give the tags they find, so `ln.line.c`/`gc` and `ln.line.a`/`ga` give the Line's `\Nc`/`\Na` before the Style's. Upstream's last commit made the found-tag test `ei == -1`, which never holds, so they always gave the defaults and the Style's colour. Corpus: `tests/backends/karaoke-corpus/karaok/departures/ln-kara-tags.ass`. [TH5d2 #833](https://github.com/altqx/hikari/issues/833), owner decision Q164.

## TH5d2-ln-kara-string-defaults

A string default for `ln.line.tag`, `ln.line.tags`, `ln.syl.tag` and `ln.syl.tags` is the first tag's default. Upstream indexed the tag list from 0, so a string default stopped the expression with "table index is nil". Corpus: `tests/backends/karaoke-corpus/karaok/departures/ln-kara-string-defaults.ass`. [TH5d2 #833](https://github.com/altqx/hikari/issues/833), owner decision Q164.

## TH5d2-ln-kara-pos-table

The table forms of `ln.tag.pos` and `ln.tag.kanjipos` read their fields from the table. Upstream replaced the table with its first field and then read the rest from that number, which stopped the expression. Corpus: `tests/backends/karaoke-corpus/karaok/departures/ln-kara-pos-table.ass`. [TH5d2 #833](https://github.com/altqx/hikari/issues/833), owner decision Q164.

## TH5d2-ln-kara-move-offsets

`ln.tag.move` with its start offsets left out (`ln.tag.move(nil, nil, 10, 20)`) starts at the anchor point. Upstream added `nil` to the anchor and stopped the expression. Corpus: `tests/backends/karaoke-corpus/karaok/departures/ln-kara-move-offsets.ass`. [TH5d2 #833](https://github.com/altqx/hikari/issues/833), owner decision Q164.

## TH5d2-ln-kara-syldur

`ln.syldur()` and `sd()` with no argument give the unit's whole duration, as karaOK's README says. Upstream gave 0. Corpus: `tests/backends/karaoke-corpus/karaok/departures/ln-kara-syldur.ass`. [TH5d2 #833](https://github.com/altqx/hikari/issues/833), owner decision Q164.

## TH5d2-ln-kara-shift

`ln.shapes.shift`, and `ln.shapes.circle`, which is built on it, write a pair of whole coordinates as whole numbers. Upstream compared the rounded x with y, so most whole pairs came out with two decimals. Corpus: `tests/backends/karaoke-corpus/karaok/departures/ln-kara-shift.ass`. [TH5d2 #833](https://github.com/altqx/hikari/issues/833), owner decision Q164.

## TH5d2-ln-kara-lerp

`ln.math.lerp` and `ln.math.xerp` between a string and a number stop with "lerp between mismatched variable types". Upstream tested the second value's type twice, so a numeric string as the first value was read as a number. Corpus: `tests/backends/karaoke-corpus/karaok/departures/ln-kara-lerp.ass`. [TH5d2 #833](https://github.com/altqx/hikari/issues/833), owner decision Q164.

## TH5d2-ln-kara-kanjipos

`ln.tag.kanjipos` in a 3.2.2-mode Document is a problem, "ln.tag.kanjipos works only in karaOK mode", on its template Line. It reads karaOK's per-character table, which 3.2.2 mode builds only for char and word templates; upstream stopped with "attempt to index a nil value". [TH5d2 #833](https://github.com/altqx/hikari/issues/833), owner decision Q164.

## TH5d3-new-documents-karaok

A Document made with File › New is in karaOK mode until a `Karaoke Templater` line or a template or code Line decides; then detection decides as for any file. The mode line is written only where templates come in: the Template editor's New and presets, and a node graph's export, each in the same undo step as the Lines. Saving writes nothing, so a new file with no templates saves as before. A file opened without a mode line or templates stays in 3.2.2 mode. Aegisub has no karaOK mode. [TH5d3 #833](https://github.com/altqx/hikari/issues/833), owner decision Q167.

## TH5d3-stock-presets-translated

A stock KE3 preset inserted into a karaOK Document has its Effects translated: `pre-line` becomes `line`, `line` becomes `lsyl`, and `syl` with `char` becomes `char`; code Lines stay as they are. The translated Lines give upstream karaOK's bytes. A karaOK preset is off in a 3.2.2 Document that has templates. [TH5d3 #833](https://github.com/altqx/hikari/issues/833), owner decision Q158.

## TH5d3-karaok-preset-departures

The karaOK presets give upstream karaOK's bytes (with upstream's `ln/kara.lua`) except where they reach a defect HikariSub already fixed. Word pop and Word wipe on Lines with non-ASCII text reach TH5c-karaok-word-positions: upstream found the word's syllables by bytes and gave the last word the Line's absolute times. Letter cascade, Letter shimmer, Line gradient and Thai fade on Thai Lines reach TH5c-karaok-graphemes: upstream split `กิ` and `น้ำ` into code points. Their goldens come from the built-in. [TH5d3 #833](https://github.com/altqx/hikari/issues/833). Thai support by Jibrill.

## TH5d3-editor-follows-mode

In a karaOK Document, the Template editor's New inserts the karaOK form of each entry and shows it, and the Effect picker reads and writes karaOK's class words (`lsyl`, `line`) through the same translation. 3.2.2 mode is unchanged. [TH5d3 #833](https://github.com/altqx/hikari/issues/833).

## 892-script-info-on-save

Every save but an autosave writes the Script Info values switched on in the `scriptProperties.*` settings (Title, Original Script, Original Translation, Original Editing, Original Timing, Script Updated By) into the file, as legacy's `SubsGrid::SwapAssProperties` does. They come from the window's settings, so a Project's values win over the user's. Both are set on the Script properties page of File › Settings, the user's on the Yours tab ([#966](https://github.com/altqx/hikari/issues/966)), where a value the Project sets says "Overridden in this project". With `scriptProperties.askForChange` on, a dialog first lists each value that would change, old and new, with a check box. OK writes the checked values, Skip saves without any of them, and Cancel stops the save. In legacy the check boxes are the switches themselves and OK stores them in the settings; here they apply to that one save. Legacy's third button, "Disable confirmation", turned the question off for good and then saved without the values; Skip keeps the question on. The values are one undo step, "Changing the subtitle header", and it stays when the save fails. [#892](https://github.com/altqx/hikari/issues/892), owner decision Q234 2026-11-11, refined the same day.

## AU19-timing-helpers

Subtitles › Timing helpers and the Timing postprocessor's Assistant tab do natively what Timing Assistant (phos), Bidirectional Snapping (phos), Snapper (petzku), ShiftCut and Join/Split/Snap (unanimated) and JoinPrevious (The0x539) do. None of them is bundled. The behaviour table is in [AU19 #853](https://github.com/altqx/hikari/issues/853#issuecomment-6790132429). The departures below were accepted at the owner's gate on 2026-11-11 (Q322–Q328).

- **L1, join text.** Merge with previous/next line (F4/F5) joins with `\N` and keeps every tag. Join/Split/Snap's tag pruning, `\N`-to-space and `...`/hyphen merging aren't copied; its same-text join is Join › Keep first.
- **L2, split.** Split › At video time splits at the video frame. There is no split at `\N` with estimated times and no keyframe snap after a split.
- **L3, neighbours.** Extend previous line to this start and Extend next line to this end skip commented Lines, and do nothing at the first or last Line. JoinPrevious writes to the row above, even a header.
- **L4, snaps.** Four explicit commands replace Bidirectional Snapping's press-again-to-flip. A time already on a keyframe moves to the one before or after; Snapper leaves it. Each Line keeps its own times; Snapper copies the audio selection into every selected Line. No keyframe that way leaves the Line unchanged; the scripts throw.
- **L5, leads.** The amounts are Settings › Audio's lead-in and lead-out, in ms. Adding never overlaps the neighbouring uncommented Line, and removing keeps at least one frame (ShiftCut allows zero). There is no style filter and no skip-on-keyframe option.
- **L6, frame shifts.** One frame per press. ShiftCut's "by frames per lines" isn't copied; whole-Line shifts stay in Shift times.
- **L7, linking and snapping in batch.** These stay the Timing postprocessor's close-gaps and snap rules, with windows in ms rather than frames and no CPS or 450 ms guard.
- **L8, assistant.** The thresholds are the postprocessor's rules and presets; a rule that is off isn't used. There is no CPS special case for a keyframe 850 ms or more away. A start already on a keyframe doesn't stop the end being timed. Leads are rounded to frame starts, and nothing is inserted after the last Line.
