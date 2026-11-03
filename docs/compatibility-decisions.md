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

## XD1-karaoke-marks

A templater Apply's save is no longer byte for byte Aegisub 3.2.2's. Each generated Line and its karaoke Line get a `_hikari_kara_from` or `_hikari_kara_source` entry in `[Aegisub Extradata]`, so a reopened file still knows which karaoke Line each Line came from. There's no setting to turn this off. Aegisub 3.5.1 keeps the entries through its own load and save. Apart from them, the output is still 3.2.2's. [XD1 #659](https://github.com/altqx/hikari/issues/659), owner decision 2026-11-03.

## FA8-borderstyle-values

A Style's BorderStyle is kept when it is 1, 3 or 4. Any other value reads as 1, the outline that libass draws for it, and is saved as 1, as legacy HikariSub did. Aegisub 3.5 keeps any integer as written, so a value such as 30 survives an Aegisub save but becomes 1 in a HikariSub one. [FA8 #565](https://github.com/altqx/hikari/issues/565), coordinator decision 2026-11-03.

## FA8-lua-borderstyle

A Lua style's `borderstyle` is the Style's BorderStyle as an integer, 1, 3 or 4, both when a macro reads it and when it writes it back, as in Aegisub 3.5 (`auto4_lua_assfile.cpp`). It used to read 1 for an opaque box and 0 for anything else, and to write BorderStyle 3 only for −3, so a macro that rewrote a Style turned an opaque box into an outline. This fixes that confirmed legacy defect. [FA8 #565](https://github.com/altqx/hikari/issues/565), coordinator decision 2026-11-03.
