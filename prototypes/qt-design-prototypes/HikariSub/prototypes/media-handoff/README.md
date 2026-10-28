# Native media feasibility: available indexed slice, blocked handoff

Throwaway probe for [#50](https://github.com/altqx/hikari/issues/50), observed 2026-09-27 local time. **This is not a native player or a successful clock handoff.** The available Windows FFMS2 binary can exercise a small indexed-decode slice. The installed PySide6 runtime cannot import Qt Multimedia, and no PortAudio binary was found in the scoped search. No dependency installation, license acceptance, download, production edit or audio-device playback was performed.

Open [report.html](report.html) for actual decoded images and per-operation observations. [observations.json](observations.json) contains machine inventory, hashes, fixture commands, metadata, raw frame records and errors. The report explicitly shows **no clock owner, no clock estimate, and no executed handoff**; it does not replace missing native work with a simulated mode switch.

## Reproduce using existing local dependencies

From this directory:

```powershell
& "$env:LOCALAPPDATA/HikariSub/prototype-runtime/Scripts/python.exe" run.py --source C:/Work/Kainote
```

The script uses Python's standard library plus the already installed PySide6 for inventory and CPU PNG encoding. It loads `C:/Work/Kainote/x64/Debug/FFMS2.dll` in isolated native child processes, and invokes the repository's existing `Thirdparty/ffmpeg/bin/{ffmpeg,ffprobe}.exe`. No PATH-selected FFmpeg fallback is used. The native binding deliberately requires x64 FFMS2 runtime version `0x05010000`; another version is not silently treated as ABI-compatible.

Each run writes a new ignored `_run/<UTC>/` containing original generated media and native observations, then replaces `observations.json` and `report.html`. These are explicit experiment outputs; no user Document/session/configuration is touched. Child operations have a 45-second experiment bound; it is not a proposed production cancellation timeout. Generated media, raw frames and third-party binaries are not commit candidates. Intended tracked files: `.gitignore`, `run.py`, `ffms_probe.py`, `README.md`, `observations.json`, `report.html`.

## What was available

| Component | Observed fact | Boundary |
| --- | --- | --- |
| PySide6 / Qt | Essentials 6.11.2, Python 3.12.14; Core, Gui, Qml and Quick imports succeed | `PySide6.QtMultimedia` and `QtMultimediaWidgets` imports raise `No module named …`. Only their `.pyi` declarations are present, not importable implementations. |
| FFMS2 | Existing DLL loads and reports 5.1.0; SHA-256 `a677ac329e04460dae88dd34d17cbb7186a202e68aa8cd52d68fe21e589443a1` | Matching version and exports do not prove this binary was reproducibly built from the pinned fork. Source/header hashes and loaded DLL paths are recorded. |
| FFmpeg | Repository executable reports 7.1.1, gyan.dev shared GPL build; actual native dependencies resolve to adjacent `x64/Debug` DLLs | This is not a selected future package manifest or proof of Qt/FFMS2 FFmpeg coexistence. No dependency binaries are redistributed here. |
| libass | Existing `bin/x64/Debug/Libass.lib` is inventoried; the separate [font identity experiment](../font-identity/README.md) exercised native libass | This slice does not call libass or certify subtitle alpha/color composition. |
| PortAudio | No matching file in the inspected PySide6 runtime, `x64/Debug` or `bin/x64/Debug` | Scoped absence, not a whole-machine assertion. No editor output, host API, selected device or clock is instantiated. |
| C++ Qt kit / Linux | The [build prerequisite audit](../build-workflow/README.md) found no official Qt C++ SDK in its scoped locations, only VS2026 rather than the accepted MSVC2022 prerequisite, and no established Linux runner | This Python/native-DLL experiment does not qualify the accepted CMake provisioning workflow or Linux. |

Host: Windows 11 Pro 10.0.26200 x64, Ryzen 5 5600 (6 cores/12 logical processors), 34,276,634,624 bytes reported physical RAM, GTX 1070 Ti, driver 32.0.15.8266. The GPU is not used for presentation here. Windows inventory reports NVIDIA/Realtek and two USB audio devices, but **none was opened or identified as the test output device**; endpoint IDs, device drivers, sample negotiation and backend routing therefore remain unmeasured. This is not the accepted four-core/8-GiB/integrated-GPU reference machine.

## Fixtures and observed results

`run.py` generates 100 original 320×180 frames with an eight-bit visual frame-ID stripe, gradient pixels, two five-second mono 48-kHz signed-16 PCM tone/impulse tracks, two original subtitle streams (ASS vector paths and SRT text), and two chapters. No external media or font bytes are used. The original fixture pixels/tones/text/vector paths are dedicated to **CC0-1.0**; probe code follows the repository license.

Two H.264/Matroska fixtures contain 64 B, 34 P and 2 I frames each. CFR has 40-ms frame-start increments; VFR deliberately repeats 20/40/80-ms increments. `ffprobe` confirms every generated PTS against the requested timeline before the native probe runs. The second keyframe starts at 2.0 seconds in CFR and 2.3 seconds in VFR. These tiny four-to-five-second fixtures are not the ten-minute 1080p performance workload. Fresh container metadata may change file hashes on regeneration; the evidence hashes identify the exact observed files, not a bit-identical build promise.

| Executed native observation | Result | What it establishes here |
| --- | --- | --- |
| Sequential `FFMS_GetFrame`, `FFMS_SEEK_NORMAL`, one decode thread, BGRA output | 100/100 visual barcode identities matched for each fixture | Requested indexed identity was actually visible in these decoded pixels. |
| Reordered requests `[99,0,50,1,98,25,24,75,10,50]` | 10/10 identities and BGRA hashes matched the corresponding sequential frame, in each fixture | B-frame/VFR random-access behavior for these cases, not universal exact-seek certification. |
| Copy every packed BGRA row before another decode | Saved frame 25 remains equal after subsequent seeks and decoder destruction | The probe's owned CPU bytes survive the native buffer lifetime. No texture/GPU ownership is tested. |
| Request index 100 in a 100-frame source | Null frame; error type 5/subtype 27, `Out of bounds frame requested` | This out-of-range operation failed explicitly rather than returning a replacement frame. |
| Two audio sources, `[4800,4864)` source sample frames | Both 64-frame buffers match the original generated PCM bytes, for both containers | CPU source-range retrieval; no resampling, device output or audible-duration measurement. |
| Fork track/name/language and chapter exports | Five tracks, the requested track labels/languages, chapters Departure `[0,2000)` and Arrival `[2000,4000)` ms | Fork metadata availability for these fixtures, not Qt player control or arbitrary chapter-timebase correctness. |
| Fork subtitle extraction in a separate child | ASS and SRT each return two packets: starts 500/2000 and durations 1000 in the observed callback values | These values match the authored milliseconds for this container. No general callback-unit contract or rendered subtitle parity is inferred. Codec-private text is hashed as a NUL-terminated C string, not an arbitrary-length binary blob. |
| Indexing callback immediately returns cancellation | One callback; no index; error type 10/subtype 31, `Cancelled by user` | Cooperative cancellation on this indexing path. It does not prove threaded request cancellation, GUI responsiveness, safe forced termination or an audio stop/flush. |

The report includes one native frame per fixture, encoded into PNG using CPU `QImage`. Those are decode outputs, not screenshots or evidence that Qt Quick presented a frame. No timing benchmark, process-memory comparison, color-reference comparison, decoder-to-GPU completion trace or release-performance pass was performed.

## General-player interface: inspected declarations, not runtime proof

The installed `QtMultimedia.pyi` declares `audioTracks`, `videoTracks`, `subtitleTracks`, their active-track setters, `setPosition`, `position`, `isSeekable`, playback state and `setVideoSink`. It has no chapter-named `QMediaPlayer` method. This agrees with the completed [general playback research](https://github.com/altqx/hikari/blob/4c90a86bead00528e5636f012e690c618e803f4f/docs/research/general-playback.md): track/time-seek APIs exist in the candidate design, while chapter extraction needs a separate metadata path. A declaration file cannot prove plugin loading, codec support, actual selected-track behavior, subtitle suppression or seek accuracy. None was executed through Qt Multimedia here.

Keep the runtime result unknown until native evidence establishes it. In particular, a future `positionChanged` or successful `setPosition(ms)` must not stand in for exact indexed frame acknowledgement. Disabling an internal subtitle track also needs visible verification to exclude double composition when Hikari's libass overlay is active.

## Responsibility-level handoff contract to prove next

These are proposed experiment interfaces implementing the accepted [media](https://github.com/altqx/hikari/blob/qt/docs/qt/media.md) and [typed time](https://github.com/altqx/hikari/blob/qt/docs/qt/time-semantics.md) responsibilities, **not implemented production APIs**.

| Owner / request | Required observable response |
| --- | --- |
| Source `open(media identity, tracks, generation)` / `cancel(generation)` | Track capabilities and rational timestamp origins; indexing progress/error/cancel acknowledgement. Every result identifies the source generation. Own source metadata before releasing backend objects. |
| Indexed `requestFrame(index, generation)` | Owned frame planes/strides, the actual frame identity, PTS/timebase, geometry/SAR/crop and separate color metadata, or an error. A decoder index request is distinct from a player's time seek. |
| General-player `selectTrack` / `seek(time, generation)` | Actual selected track and observed destination video timestamp/accuracy. Chapter metadata has explicit source/provenance. Unsupported controls remain explicit. |
| Transport `snapshot()` | Active mode and generation, media identity, playback state, clock owner/domain, valid estimated position and uncertainty, pending request identity. No fabricated position when the clock is invalid. |
| `beginSwitch(destination, request)` | Stop old scheduling/output; flush or account for retained device tails; invalidate old generation/clock; reject late completions; open/map the destination; acknowledge its actual presentation; resume only with one active audio owner. Failure leaves a visible stopped/error state without rewriting Document timing. |
| Editor output / presenter | PortAudio consumes ready buffers without blocking/decode/QML; timestamped source-to-device mapping and uncertainty. Qt render-thread ownership, libass result copying and overlay transforms remain separate from transport scheduling. |

The current probe has no transport coordinator, device queue or presentation acknowledgement, so it cannot truthfully demonstrate the switch sequence. An owned CPU frame is a useful prerequisite, not a handoff snapshot.

## Smallest next native proof and reopening conditions

1. Supply an explicitly authorized matching Qt Multimedia runtime/plugin set and PortAudio build, or the accepted official Qt C++ kit through #48. Record exact artifact/backend origins and FFmpeg coexistence. No alternative Qt installer, player or editor audio fallback is selected by this report.
2. In one minimal native window, load these fixtures through QMediaPlayer/QVideoSink/QAudioOutput. Enumerate/select both audio and subtitle streams; distinguish metadata-only chapters from an actual chapter seek; disable internal subtitles; record delivered video timestamps and failures. The FFMS2 decode route is the comparison source, not an invented exactness guarantee for Qt's time-seek API.
3. Add CPU-BGRA/libass reference composition with original vector ASS fixtures, source/script/view transforms and owned buffers. Compare alpha/order/color against a QSG presenter on the actual graphics backend, including resize and mixed DPI. Record each color field separately; the tagged fixture alone cannot validate matrix/range handling.
4. Add PortAudio ready-buffer playback, instrument callback estimates and queue generations, then perform stop/flush/ack handoffs in both directions. Observe real output tails with calibrated loopback/video capture; exercise stale completion, unsupported track, decode failure, underrun, device removal/default change, sleep/wake and cancellation. Exact device-follow/reopen/scrub interaction remains a human choice informed by the Audio panel prototype.
5. Repeat independently on named Ubuntu/Fedora desktop sessions and the accepted lower hardware reference under the [performance contract](https://github.com/altqx/hikari/blob/qt/docs/qt/performance.md). This existing-machine functional probe satisfies none of those calibrated gates.

Reopen the **candidate** Qt player choice if required track/chapter/subtitle control cannot be made reliable, accurate enough position acknowledgement is unavailable for a required general-playback operation, backend/FFmpeg coexistence cannot be deployed, or the ownership split cannot prevent simultaneous/stale output. Merely lacking the local module today proves a prerequisite gap, not candidate failure. FFMS2/libass and the accepted PortAudio direction are not replaced here.

Pinned source context: [FFMS2 fork API](https://github.com/altqx/ffms2/blob/45d5f72100d88c52acdd54bfedcc0315a44c735d/include/ffms.h), [fork-only metadata/subtitle implementation](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/Thirdparty/Build/FFMS2/indexing_additional.cpp), [completed video presentation research](https://github.com/altqx/hikari/blob/26d7ac2a940c9f0edf2c6e65f5b1db05ea1a2507/docs/research/qtquick-video.md), and [audio research](https://github.com/altqx/hikari/blob/7e1aad3ba4f7ac217b9bb467a297035bc0639abd/docs/research/audio.md). Binary source equivalence, malformed media, attachment fonts, 4K/HDR, very long GOPs, network sources, asynchronous generation races and arbitrary subtitle/PTS formats remain unproved.

**Human review remains open.** This report makes the verified indexed slice and unavailable handoff explicit. It cannot yet collect an informed reaction to real clock/mode/error UI behavior; that gate awaits the native dependencies and next slice. No acceptance decision or ticket closure is implied.
