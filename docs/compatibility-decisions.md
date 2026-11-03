# Aegisub compatibility decisions

Where HikariSub's automation API and file formats knowingly differ from what they were, or from Aegisub 3.5.1, and why. HikariSub follows Aegisub 3.5.1 and the Lua API additions of arch1t3cht's fork, so scripts written for them run unchanged. Each entry names its card. This is not the legacy parity ledger that wave 7 removed.

## FA4-getpixel-rgb

`frame:getPixel(x, y)` returns three integers, r, g and b, as arch1t3cht's fork does. It used to return one packed integer (`b * 65536 + g * 256 + r`). Outside the frame it returns nil. [FA4 #561](https://github.com/altqx/hikari/issues/561), owner decision 2026-10-31.

## FA4-timecodes-file

`aegisub.project_properties().timecodes_file` is always `""`, because HikariSub has no way to load a timecodes file yet. `audio_file`, `keyframes_file` and `video_zoom` are real. [FA4 #561](https://github.com/altqx/hikari/issues/561), coordinator decision 2026-11-02.

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

## FA15-bake-default

"Split lines into frames" bakes animations by default (Grid ▸ Split lines ▸ Bake animations, `grid.splitFramesBakeAnimations`). Each frame's Line gets the values its `\t` tags have reached, its `\fad` or `\fade` applied to every alpha, and its `\move` as a `\pos`, by the rules of arch1t3cht's FBF-ifier (`arch.Line2Fbf`). Without the bake, every frame keeps the `\t`, `\fad` and `\fade` tags and only `\move` becomes `\pos`, so with the option on the split differs from that output:
- `\move` follows FBF: times given in the wrong order are swapped, and a `\move` without times, or with both at 0 or below, runs over the whole Line. A `\move` whose two points are the same stays.
- A `\t` that has not finished gives its value so far, with its acceleration; one that cannot be in between (such as `\an`) applies only once it has finished.
- A later block's `\t` starts from the value an earlier block's `\t` has reached, as the renderer shows it.
- Tags written by the bake keep FBF's values but not its tag order or number formatting.

With the option off, the split is byte for byte what it was before FA15. [FA15 #572](https://github.com/altqx/hikari/issues/572), coordinator decision 2026-11-03; the default waits on the owner.
