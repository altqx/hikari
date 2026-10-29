# Legacy observations: W2 VSFilterMod frames (Winix VM, 2026-10-15)

The legacy app's own frames with VSFilterMod drawing the fixture scripts in [`scripts/`](scripts/), taken in the Winix VM (Windows 11 build 26200) by [`probe/run.ps1`](probe/run.ps1). `VsfilterMod.MatchesTheLegacyAppsCaptures` (`hikari_backends_vsfiltermod_tests`, Windows) compares HikariSub's VSFilterMod overlay with them.

## What ran

- **Legacy app:** legacy main at `97bb7a20a`, the Windows x64 package of Build run [38667849493](https://github.com/altqx/hikari/actions/runs/38667849493) (artifact `hikarisub-windows-x64`; `HikariSub.exe` SHA-256 `91892adb...0b06f265`), winix job `job-976a4f99cf5b4f31a02866a7f40287d5`. That main pins VSFilterMod r5.3.1-hikari.2 and hands the script's `[Graphics]` to CSRI renderers.
  - The files were first taken from the `ci/lc-round2` package at `e374feb19` (Build run [38631162505](https://github.com/altqx/hikari/actions/runs/38631162505), `HikariSub.exe` `dcd805b1...f6370fc5`, job `job-07044340c16e4c6bb4f409e61d786532`), before the round-2 land reached main.
  - Main adds only the signed stride for frames stored bottom line first (`73149ff7d`, DirectShow), and both packages saved byte-identical files for all 16 captures, subtitled and plain.
  - The legacy main before that land (`a8e4a9b4b`) pinned r5.3.1-hikari.1. hikari.1 draws BGRA with inverted alpha, so legacy's default overlay path showed no VSFilterMod subtitles with it.
- **Renderer:** `Csri/VSFiltermod.dll` x64 from altqx/VSFilterMod [r5.3.1-hikari.2](https://github.com/altqx/VSFilterMod/releases/tag/r5.3.1-hikari.2), SHA-256 `e4386c63280bac1cdbb0ef6434643a4af797d45ad3e49b194c2db002574e2bfe`, chosen as `VSFILTER_INSTANCE=vsfiltermod_textsub`.
- **Video:** `w2-gradient.mkv`, 640x360, 20 frames a second, 6 s, one still gradient, lossless H.264 yuv420p with every frame a keyframe (SHA-256 `99b69118...5517b500`; `python make_fixtures.py <dir> --video` makes it with ffmpeg; it is not committed, since `frames/background.png` holds the frame legacy decoded from it). Even-sized YUV video takes legacy's default NV12 path with GPU conversion, so VSFilterMod is asked for BGRA first (`RequestCsriFormat`).
- **Per capture:** the app's `Config` folder is deleted and refilled with [`probe/Config.base.txt`](probe/Config.base.txt) and [`probe/Hotkeys.txt`](probe/Hotkeys.txt) before every start (repeated runs shrink `VIDEO_WINDOW_SIZE`). The app opens a copy of the fixture script with `Active Line: <n>` added, then the video. With `OPEN_VIDEO_AT_ACTIVE_LINE` the video opens paused at that Line's start: one Comment per capture time at the end of each script. F8 runs "Save frame with subtitles as PNG" (`VIDEO_SAVE_SUBBED_FRAME_TO_PNG`) and F7 "Save frame as PNG" (`VIDEO_SAVE_FRAME_TO_PNG`). Both go through `RendererVideo::SaveFrame`, which writes `<video>_<n>_<time>.png` beside the video. The time in each name equals the capture time (`observations.jsonl`). A subtitled frame is `RendererFFMS2::GetFrameWithSubs`: the decoded frame with VSFilterMod's `csri_render` drawn onto it.

## Files

- `captures.json`: the captures (fixture, time in ms, the Active Line index the probe sets). `make_fixtures.py` writes it and `scripts/`.
- `scripts/*.ass`: the fixtures. positions, styles, clip, karaoke and the three YCbCr Matrix scripts are the fixtures of `VsfilterMod.OverlayMatchesDrawingOnTheFrame`. `img-attached` draws the fork's `\1img` from the script's own `[Graphics]` three ways: plain, with offsets `(32,16)`, and with the offsets animated by `\t`.
- `frames/background.png`: the plain frame. All 16 "Save frame as PNG" files were these bytes (SHA-256 `cb8b27da...36c7dd87a3`).
- `frames/<fixture>-<ms>.png`: "Save frame with subtitles as PNG", the legacy files unchanged.
- `observations.jsonl`: one line per capture, with legacy's file names and the SHA-256 of both files.

## Compared with HikariSub

For each capture the test loads the script as the application does: `loadAss`, then `encodeAss` in renderer mode, which keeps `[Graphics]`. It draws the frame time through the CSRI adapter with the pinned VSFilterMod, lays the overlay over `background.png` (`o + bg * (255 - a) / 255`, rounded) and compares the result with legacy's subtitled frame. Every channel must be within 2 levels. That is the rounding allowed between the adapter's overlay and VSFilterMod drawing onto the frame itself (`OverlayMatchesDrawingOnTheFrame`). It was run with the hikari.1 pin asked for BGR_ (onto black and onto white) and with the hikari.2 pin asked for BGRA, the qt head this change sits on. Both pass; the W2 row of `docs/coverage.md` has the jobs.
