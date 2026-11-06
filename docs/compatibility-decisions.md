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
