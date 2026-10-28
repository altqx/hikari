# Cross-platform general playback to replace DirectShow and GStreamer

Research for altqx/hikari#11 (`general-playback`). Completed desk research, 2026-09-27. Current-code inventory is at `20d647c4`; Qt references resolve to 6.11.2. No playback benchmark, package-size measurement or ARM64 build was performed.

## Summary

A separate unindexed playback path is justified if immediate viewing, hardware decode and fullscreen track/chapter workflows remain requirements. It need not expose a separate player UI or replace FFMS2 for indexed editing. Qt Multimedia's FFmpeg backend is the smallest Qt-facing candidate to prototype; libmpv has richer player controls but its public render API brings an OpenGL/software boundary. Direct FFmpeg offers the most control over an application-owned audio clock at the highest implementation cost. FFMS2-only reduces engines but retains indexing and does not provide a complete player.

## Implications for the decision

Recommendation, not the #25 decision: compare Qt Multimedia and the indexed FFMS2 path on the same fixture corpus. Let a general player own its audio/sync while active, and stop it before handing control to the editing transport. If one shared external audio output is non-negotiable for both modes, reassess the direct-FFmpeg option rather than assuming Qt or mpv exposes a drop-in PCM callback. Share subtitle settings, timeline identity, user controls and rendering tests across engines. Packaging, Windows ARM64 and seek latency remain measured gates.

## Detailed findings

### 1. Today's behaviour in HikariSub

**Two renderers, picked per file.** By default `VideoBox::LoadVideo` selects FFMS2 when the *Video indexing* toggle is enabled/checked and the open is not fullscreen; otherwise it selects DirectShow on Windows or GStreamer on Linux. There are exceptions: explicit `customFFMS2` overrides the toggle/fullscreen rule, dummy videos force FFMS2, and Linux falls back to FFMS2 when the menu item is absent (`HikariSub/VideoBox.cpp:290-322`). The general path invokes play but immediately pauses in non-fullscreen editor mode or when `dontPlayOnStart` is set. It shows a volume slider; the FFMS2 path renders a still and hides that slider (`:370-393`).

**What the general path buys today:**
- It avoids the up-front FFMS2 indexing step. FFMS2 needs a suitable existing index or must index the file with progress and write the index (`HikariSub/ProviderFFMS2.cpp:162`, `:300-320`). This is not a measured claim of instant open; demux/probe/decoder startup still takes time.
- It has its own audio output and volume (`HikariSub/VideoBox.cpp:389-392`). The FFMS2 path has to open the file's audio in the tab's audio panel (`HikariSub/RendererFFMS2.cpp:467-472`).
- It chooses audio tracks, both by hand from the context menu (`HikariSub/VideoBox.cpp:997-1045`) and automatically from a preferred-language list, `ACCEPTED_AUDIO_STREAM` (`HikariSub/VideoBox.cpp:1474-1510`). `ChangeStream` returns early unless the general path is active (`:1476`).
- It shows embedded subtitles in player mode through the DirectVobSub "autoload" filter (`HikariSub/dshowplayer.cpp:125-165`), used when the tab has no editor (`HikariSub/VideoBox.cpp:328`).

Chapters come from all three paths: DirectShow's `IAMExtendedSeeking` (`HikariSub/dshowplayer.cpp:512-530`), GStreamer's TOC (`HikariSub/RendererGStreamer.cpp:354-371`), and `FFMS_GetChapters` (`HikariSub/ProviderFFMS2.cpp:184`), which exists only in the HikariSub FFMS2 fork, not upstream ([fork ffms.h](https://github.com/altqx/ffms2/blob/hikarisub/include/ffms.h), [upstream ffms.h](https://github.com/FFMS/ffms2/blob/master/include/ffms.h)). So chapters alone do not justify the general path.

**DirectShow (Windows).**
- `DShowPlayer` builds a filter graph from whatever source filter and decoders are installed on the system (`HikariSub/dshowplayer.cpp:92`, `:144`). An explicit LAV Video insertion is commented out (`:96-99`).
- It adds HikariSub's own video renderer filter and the stock DirectSound audio renderer (`:102-122`).
- The custom renderer accepts only system-memory YV12, NV12, YUY2 or RGB32 samples (`HikariSub/DshowRenderer.cpp:109-132`). Any hardware decoding therefore happens inside a third-party decoder that copies frames back to the CPU.
- Colour conversion and scaling go through a D3D9 DXVA2 video processor (`HikariSub/RendererDirectShow.cpp:101-156`, `:349-402`).
- libass or VSFilter subtitles are drawn by `SubtitlesProvider::DrawOverlay` into a separate texture (`HikariSub/RendererDirectShow.cpp:223`).
- A seek is a plain `IMediaSeeking::SetPositions` (`HikariSub/dshowplayer.cpp:358`). The renderer corrects its clock from the first sample after the seek (`HikariSub/DshowRenderer.cpp:54-71`).
- The frame timebase is only *estimated* from the fps (`HikariSub/RendererDirectShow.cpp:508`).
- A/V sync belongs to the DirectShow graph clock, which is the audio renderer.

**GStreamer (Linux).** The header describes the design (`HikariSub/RendererGStreamer.h:18-25`).
- `playbin` decodes, and its video sink is `videoconvert ! appsink` producing BGRA (`HikariSub/RendererGStreamer.cpp:276-328`). Playbin's own text rendering is switched off (`:330-335`).
- Every frame is copied to the CPU, where libass is drawn into it (`:444-448`).
- A/V sync belongs to the GStreamer pipeline clock.
- A user seek is `FLUSH | KEY_UNIT`, which lands on a keyframe, not on the requested time (`HikariSub/RendererGStreamer.cpp:586-588`). Only the redraw after a subtitle change uses `ACCURATE` (`:489-491`).
- The timebase is estimated from the fps (`:258`).
- Stream selection covers audio only, via `n-audio` and `current-audio` (`:660-690`).
- There is no hardware-decode configuration and no zero-copy path.

**FFMS2 (both platforms).**
- `FFMS_CreateVideoSource` is called with one decoder thread per CPU core and the configured seek mode (`HikariSub/ProviderFFMS2.cpp:335-341`). The default mode is `2` (`HikariSub/config.cpp:360`), which is `FFMS_SEEK_UNSAFE` ([ffms.h](https://github.com/FFMS/ffms2/blob/master/include/ffms.h)).
- The Windows playback route can use `RendererVideo::PlaybackClock`, which reads the audio player's position and falls back to `timeGetTime` (`HikariSub/RendererVideo.cpp:1154-1164`). `NextPlaybackFrame` skips frames more than 20 ms late (`HikariSub/Playback.cpp:18-67`). Linux's separate playback loop uses elapsed `timeGetTime` directly, so the audio-master description must not be generalized across both platforms.
- On Linux, a playback thread polls about every 4 ms and calls `FFMS_GetFrame` for each frame it presents (`HikariSub/RendererFFMS2.cpp:66-100`).
- Output is initially BGRA; Windows can switch to NV12 for the GPU-conversion option (`HikariSub/ProviderFFMS2.cpp:418-422`). Screenshots/scripts temporarily request BGRA. This is CPU decoding plus GPU colour conversion, not evidence of hardware decode.

**libass settings.** The common subtitle-provider abstraction can use libass (Windows also has the VSFilter provider). `SubtitlesLibass` calls `ass_set_fonts(..., "Arial", ..., 1, nullptr, true)` and `ass_set_frame_size(video w, h)`, and does not call `ass_set_storage_size` (`HikariSub/SubtitlesLibass.cpp:60-63`, `:118-119`). A new path must use the same libass configuration or establish equivalent rendering by reference images; simply using another player's default libass settings is insufficient.

**Build and licence facts.**
- The Windows manifest specifies a shared GPL FFmpeg 7.1.1 build from gyan.dev (`Thirdparty/dependencies.json:83-88`). Its rationale that a GPL app *requires* a GPL FFmpeg build is not a valid general dependency rule: FFmpeg is LGPL by default, with GPL applying when optional GPL components are enabled. Choose features/build license deliberately and preserve obligations, rather than repeating that manifest rationale. [FFmpeg licensing](https://ffmpeg.org/legal.html)
- Linux links the distribution's FFmpeg, FFMS2, libass and GStreamer (`CMakeLists.txt:47-61`).
- The map makes replacing FFMS2 or libass out of scope ([#1](https://github.com/altqx/hikari/issues/1)).

### 2. Qt Multimedia with the FFmpeg backend

Qt's current main backend is FFmpeg, default on desktop Windows/Linux; codec/hardware behavior is not identical across targets. Installer packages include dynamically linked FFmpeg, and deployment must provide compatible FFmpeg libraries. This adds Qt Multimedia/backend binaries even if FFmpeg already exists for FFMS2; matching major versions may permit reuse, but that must be tested rather than shipping conflicting DLLs. [Qt Multimedia backend/deployment](https://doc.qt.io/qt-6/qtmultimedia-index.html)

`QMediaPlayer` exposes time-position seeking, audio/subtitle track selection, playback rate and video sinks. The public API does not guarantee indexed frame-number stepping or offer a chapter-list API in the version inspected. Track support is convenient; chapters would need separate demux metadata (for example FFmpeg) and seeks to those times. Disable the player's subtitle track and overlay our own libass output to retain editing settings. [QMediaPlayer](https://doc.qt.io/qt-6/qmediaplayer.html)

`QAudioBufferOutput` (6.8+) emits decoded audio with the FFmpeg backend, but is not an external master-clock contract. Qt explicitly cautions against using it for external playback without handling rate changes, video sync and flushes on stop/seek. Prefer QMediaPlayer/QAudioOutput-owned A/V sync for general playback, or budget substantial work for an external sink. [Audio buffer output](https://doc.qt.io/qt-6/qaudiobufferoutput.html), [external-output warning](https://doc.qt.io/qt-6/qmediaplayer.html#audioBufferOutput-prop)

Hardware decoding is backend/codec/device dependent; test success and fallback, not just capability flags. Qt supports Windows ARM64 with MSVC 2022, but that does not independently verify every media plugin/codec combination. Linux desktop packaging must handle multimedia dependencies and actual audio backends. [Windows target support](https://doc.qt.io/qt-6/windows.html), [Multimedia Windows](https://doc.qt.io/qt-6/qtmultimedia-windows.html), [Multimedia Linux](https://doc.qt.io/qt-6/qtmultimedia-linux.html)

### 3. libmpv (render API)

mpv provides precise seek options that decode forward from an earlier keyframe; long GOPs and decoder speed affect cost, and some formats have limitations. It exposes audio/subtitle track IDs and chapter lists, making fullscreen-player behavior richer out of the box. Hardware decoding can fall back to software; it is not a universal zero-copy promise. [Seeking](https://mpv.io/manual/stable/#options-hr-seek), [track/chapter properties](https://github.com/mpv-player/mpv/blob/a1bf4b6559d6e644b967c22d87232dc77c7dd442/DOCS/man/input.rst), [decode/subtitle options](https://github.com/mpv-player/mpv/blob/a1bf4b6559d6e644b967c22d87232dc77c7dd442/DOCS/man/options.rst)

The inspected public render API exposes OpenGL and software renderers, not a general D3D/Vulkan texture sink for Qt RHI. Its Qt Quick example uses QQuickFramebufferObject. A Vulkan-capable native mpv video output does not imply a Vulkan libmpv render API. Choose OpenGL for that integration or explicitly develop an interop/copy path; compare this against #10 before adoption. [render API](https://github.com/mpv-player/mpv/blob/a1bf4b6559d6e644b967c22d87232dc77c7dd442/include/mpv/render.h), [example](https://github.com/mpv-player/mpv-examples/blob/master/libmpv/qml/main.cpp)

mpv normally owns audio output and its media clock. The inspected public client/render APIs do not provide a generic application PCM pull callback with an external master-clock interface. A custom internal audio-output module or separate PCM pipeline is possible engineering, not standard embedding. For editor subtitles, disable mpv subtitle decoding (`sid=no`) and compose ours, or rigorously synchronize libass fonts, margins, aspect, resolution and override settings. Avoid double rendering.

Default mpv licensing is GPL-2.0-or-later; `-Dgpl=false` excludes GPL-only code for an LGPL-2.1-or-later build, but linked dependencies can change the resulting license and some features are removed. GPLv3 HikariSub does not need the LGPL configuration merely to avoid GPL. [mpv Copyright](https://github.com/mpv-player/mpv/blob/a1bf4b6559d6e644b967c22d87232dc77c7dd442/Copyright)

Packaging adds libmpv and its enabled FFmpeg/libass/libplacebo/audio dependencies; no size in MB is claimed. Linux source/packages are available; upstream's installation page distinguishes third-party Windows binaries from first-party CI builds intended for testing. A usable Windows ARM64 libmpv bundle and its feature set were not built or verified here, so that is a release gate, not a supported-product claim. [Installation/build paths](https://mpv.io/installation/)

### 4. A player built on FFmpeg directly

libavformat/libavcodec provide demux and decode primitives, not a complete synchronized player. The examples show packet/frame processing and hardware-device decode; HikariSub would own packet queues, seek flushing, reordered frames, EOF/drain, timestamps, resampling, audio latency, clock drift, dropped frames and error recovery. Seeking requires decoding to the desired timestamp after a demux seek, not equating seek success with display-frame identity. [Demux/decode example](https://ffmpeg.org/doxygen/trunk/demux_decode_8c-example.html), [hardware decode example](https://ffmpeg.org/doxygen/trunk/hw_decode_8c-example.html)

It is the most direct fit for our own PCM sink and libass configuration because we own both pipelines, but requires the most correctness code. Audio streams and chapters are available in `AVFormatContext`; track switching and timeline continuity are ours to implement. Hardware frames may require transfer or backend-specific import before Qt presentation. [AVFormatContext](https://ffmpeg.org/doxygen/trunk/structAVFormatContext.html)

The incremental library payload may be small if it can reuse the pinned FFmpeg already needed by FFMS2; implementation/maintenance cost is not small. FFmpeg documents Unix and Windows builds including CLANGARM64; building a matching Qt/MSVC-compatible distribution still needs CI proof. License follows the configured FFmpeg components, not whether our wrapper is simple. [Platform builds](https://ffmpeg.org/platform.html), [license](https://ffmpeg.org/legal.html)

### 5. FFMS2 for everything

FFMS2 is designed around indexing and random-access frame/audio retrieval. Its seek mode affects accuracy, so the current `FFMS_SEEK_UNSAFE` default must be assessed before claiming exactness. It can feed continuous playback and our own cached PCM output, with fully controlled libass rendering, but it does not provide an audio device, complete A/V scheduler, chapter/track UI or general player transport. [FFMS2 API](https://github.com/FFMS/ffms2/blob/3af2ef2ae47bc30b64597c9e419e5b19c4bda7d8/doc/ffms2-api.md)

This minimizes independent decode engines and subtitle-setting drift; it keeps the indexing cost and cache footprint, and current HikariSub audio preparation decodes/cache-builds the track. The public API reviewed does not expose a portable hardware-decode device/texture pipeline comparable to a full player. Treat any custom-fork hardware feature as separately requiring evidence. Windows ARM64 still needs the patched FFMS2, FFmpeg and dependencies built/tested; a Windows x64 prebuilt manifest is not evidence of ARM64 availability. Fork-only chapter API support should be preserved deliberately if used.

### 6. Comparison matrix

These are capability/tradeoff assessments, not measured rankings:

| Dimension | Qt FFmpeg player | libmpv | Direct FFmpeg | FFMS2-only |
|---|---|---|---|---|
| Open without FFMS index | Yes | Yes | Yes, implement demux | No suitable-index bypass in normal workflow |
| Seek/step | Time seek; no exact frame guarantee | Precise seeks/stepping with format caveats | Implement decode-to-target | Indexed frame retrieval; mode matters |
| Hardware decode | Backend-managed, verify | Configurable, verify | Implement device/frame path | No portable player-style API found |
| Own audio/master clock | Awkward; Qt warns on external output | Internal/custom AO work | Full control, full responsibility | Natural cached PCM integration |
| Our libass | Disable internal subtitles + overlay | Disable/reconcile mpv subtitles + overlay | Render ourselves | Render ourselves |
| Tracks / chapters | Tracks; separate chapter extraction | Both | Both metadata; own switching | Track sources; fork chapter API |
| Payload | Qt Multimedia plugin + compatible FFmpeg | libmpv plus enabled dependencies | Existing FFmpeg potentially reused | Existing fork/dependencies |
| Integration risk | External audio, exactness, chapter gap | RHI/GL constraint, duplicate settings | Scheduler/hw/seek correctness | Indexing delay, performance/player features |

Measure seek-to-correct-frame and first-audible/visible latency on cached and cold local files, VFR/B-frames/long GOPs, multiple audio tracks, chapters, malformed timestamps and 4K HEVC/AV1. Record package manifests and size after stripping/deployment on Windows x64/ARM64 and Linux. No candidate is fastest or lightest by assertion.

### 7. Is a separate playback path still justified?

Yes, conditionally: it serves unindexed opening and player-oriented playback, while FFMS2 serves predictable indexed editing. Make the capability boundary explicit to users only where useful (indexing progress, stepping availability), and share controls. Do not run two audio clocks at once. Transition by stopping/flushing one engine, mapping the timeline position and presenting an acknowledged frame before resuming.

If users accept indexing before all viewing and the FFMS2 benchmark meets their codec/performance needs, a single path can be justified. Conversely, if immediate playback is essential but the own-audio constraint is relaxed only for player mode, Qt Multimedia is a reasonable first experiment. libmpv becomes more attractive when its player features outweigh the graphics/dependency constraint. Direct FFmpeg should be chosen only with budget for a maintained transport. These are decision criteria for #25, not an imposed final choice.

## Open questions

The factual comparison is complete. Remaining work belongs to #25 and prototypes: quantify index/open/seek costs; settle whether general playback must share the editor's audio sink; prove subtitle pixel parity and clock handoff; build ARM64 dependencies; compare actual packages and hardware-decoder fallback. Use QML/HTML reaction prototypes, Figma Starter for occasional handoff only. No measurement, user approval or architecture decision is implied by closing this research ticket.
