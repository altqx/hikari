# H3 #126: Windows host observations (2026-10-15), all **uncalibrated**

Host record: [docs/qt/perf/hosts/ruby.md](../../../docs/qt/perf/hosts/ruby.md) / [ruby.json](../../../docs/qt/perf/hosts/ruby.json) (in this branch). Build: the package from run 38686574527 ([../setup/host.md](../setup/host.md)).
Every number here is an **observation**: no calibration, no presentation traces, no input-to-photon capture. The host does not match the reference class (6 cores, 32 GiB, discrete GPU, 144 Hz primary). None of these numbers is a pass.

**Conditions.** Desktop on mains, Balanced plan (max processor state 100 %), both monitors at 100 %, primary 2560×1080 @ 144 Hz (the contract asks for a 1536×768 logical viewport at 60 Hz; not set). The owner closed Discord, Steam and the browser. Background left: this Claude desktop app (~4–8 % of the machine, sampled) and Task Manager. TextInputHost (the IME host) was spinning one core after the E6 IME session; the first startup set ran with it and is kept as contaminated ([raw/startup-warm-20-contaminated-textinputhost.json](raw/startup-warm-20-contaminated-textinputhost.json)). The owner ended it and the runs below came after. CPU samplers: [raw/startup-warm-samples.txt](raw/startup-warm-samples.txt), [raw/open-g50k-samples.txt](raw/open-g50k-samples.txt) (machine total during the open runs: mean 14 %, max 31 %, mostly the measuring PowerShell and the Claude app).

**Method.** A PowerShell harness (not committed; described here) starts `HikariSub.exe` with `Process.Start` and a `Stopwatch`, then polls every ~20 ms (each poll also costs a UI Automation query, so the resolution is a poll, roughly 20–60 ms): `EnumWindows` for the main window, its title, UI Automation for the "Line text" edit, the Grid's `GridPattern.RowCount`, and the stderr libass line. It waits 1.5 s, reads private bytes, and closes the window (exit code 0 every time). "W" here = same binary and fixture, OS file cache warm, fresh process each run, one priming launch before the set. Cold (C) was not run: it needs a reboot per sample.

Fixtures (hashes: [raw/fixture-sha256.txt](raw/fixture-sha256.txt)):
- `g50k.ass`: 50,000 Dialogue lines from a seeded generator (Python `random.seed(126)`): 3 styles, 60 % with one override tag block, 20 % with `\N`, every 97th line 400 words long, the rest 3–20 words. This is **not** the contract's pinned G generator.
- `v10min.mkv`: ffmpeg `testsrc2` 1920×1080, 24000/1001 fps, 10 min, libx264 veryfast, GOP 48, 3 B-frames, silent PCM 48 kHz stereo. Not the contract's V (no pinned frame identities). `v10min.ass`: 120 lines, one every 5 s, with `\fad`, `\blur2`, a colour override and `\N`.

## Results

| Workload (contract endpoint) | Measured endpoint here | n | min | median | p95 (nearest rank) | max | Raw |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Empty startup, W | process start → main window visible with "Line text" in UIA (both at the same poll) | 20 | 895 ms | 929 ms | 964 ms | 1036 ms | [raw/startup-warm-20.json](raw/startup-warm-20.json) |
| (same, TextInputHost spinning) | same | 20 | 1096 | 1124 | 1158 | 1171 | contaminated, not used |
| G open, W (50,000 rows) | process start with the file → Grid "Subtitle lines" reports 50,000 rows in UIA | 20 | 1726 ms | 1753 ms | 1795 ms | 1795 ms | [raw/open-g50k-warm-20.json](raw/open-g50k-warm-20.json) |
| (same run) | → main window visible, title "g50k.ass - HikariSub" | 20 | 1186 | 1208 | 1232 | 1235 | same |
| (same run) | → libass logs "50000 events" (renderer parse) | 20 | 311 | 332 | 339 | 346 | same |
| Private bytes after startup | 1.5 s after the marks | 20 | 262.5 MiB | 264.7 | 267.8 | 268.9 | startup json |
| Private bytes after G open | 1.5 s after the marks | 20 | 398.4 MiB | 401.2 | 404.4 | 405.6 | open json |

Statistics: [stats.json](stats.json). Startup also starts 12 `hikari-lua-helper.exe` processes (2–9 MiB private each).

**Long playback (helper and memory growth).** `v10min.ass` with its video and audio loaded, played from 19:06:33 for ~9.4 min; sampled every ~6 s ([raw/playback-10min.jsonl](raw/playback-10min.jsonl), app log [raw/playback-app.log](raw/playback-app.log)):

| Series | At load (paused) | Start of play | End | Fitted slope over the playback |
| --- | --- | --- | --- | --- |
| App private bytes | 343 MiB | 550 MiB | 603 MiB (max 605) | +4.7 MiB/min, slowing (565 at 2 min, 586 at 4 min, 595 at 6 min, 603 at 9 min) |
| GPU dedicated (`GPU Process Memory` counter, app pid) | 49 MiB | 175 MiB | 179 MiB | +0.24 MiB/min, flat after ~3 min |
| Handles / threads | 1082 / 65 | 1406 / 101 | 1406 / 98 | flat |
| Media helpers (2) private | 140 MiB | 140 | 140 | flat |
| Lua helpers (12) private | 47 MiB | 47 | 47 | flat |
| App CPU time | — | 29 s | 175 s | ~26 % of one core while playing |

The app kept responding throughout. This is one long playback, not the contract's memory workload (5 priming plus 30 open/edit/zoom/close cycles with 5 s quiescence); it says nothing against the 32 MiB / 0.5 MiB-per-cycle gates.

## Not run, and why

| Workload | Why not | What it needs |
| --- | --- | --- |
| Grid paint (scroll presented-frame interval), frame lookup, exact step, seeded seek, frame delivery, overlay rendering | no Windows build of `hikari_core_perf` / `hikari_ui_perf` exists for this head (not in the package or any CI artifact), and there are no presentation traces | build the perf targets for Windows (or add them to the Windows CI artifacts), plus PresentMon/ETW presentation traces for presented-frame intervals |
| Keystroke / composition latency (E) | no event-to-visible correlation available | ETW input + DXGI present events, or calibrated high-speed camera capture |
| Waveform / spectrum zoom (A) | no harness on Windows; the A fixture is not packaged | the perf harness and the pinned A fixture |
| A/V offset and drift | **calibration hardware missing** | a calibrated A/V loopback: a light sensor on the screen plus a line-in or USB audio capture from the output, sampled together (e.g. a photodiode and audio into a two-channel USB interface, or a high-speed camera with an audio-synced reference), ≤5 ms measurement uncertainty; three 30-minute wired runs |
| Cold startup / open (C) | needs a reboot and empty caches per sample (20 reboots) | an unattended reboot loop with a logon task |
| 150 % repeat | skipped for time | — |
