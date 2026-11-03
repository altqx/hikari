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
