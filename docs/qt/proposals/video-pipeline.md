# Proposed video pipeline

**Reviewed alternatives: active general-player audio/clock ownership was accepted on 2026-09-27.** The [media contract](../media.md) records the accepted direction and outstanding native gates; this is not a verified implementation. This fits the [Hikari-owned architecture](../architecture.md). FFMS2 and libass remain required. General playback is existing capability; this proposal retains it. Default adapters must serve all supported platforms without preventing a later macOS port. Windows integrations remain optional.

## Recommended direction

Keep **FFMS2 for indexed editing** and first prototype **Qt Multimedia's FFmpeg-backed player for unindexed general playback**, behind one application transport interface. Keep shared controls, timeline identity and subtitle settings. Qt's inspected player supports time seeking and track selection, but does not guarantee exact indexed frame stepping or supply the required chapter list; chapter metadata and embedded-subtitle/player workflows need explicit parity work. FFMS2's current unsafe seek default also requires characterization before claiming exactness. [Completed playback research](https://github.com/altqx/hikari/blob/4c90a86bead00528e5636f012e690c618e803f4f/docs/research/general-playback.md).

Let the general player own its audio output and A/V clock while active; the editing transport uses the [accepted PortAudio direction](../media.md). Never run competing clocks. Switching stops and flushes the old transport, maps the position, invalidates old results and acknowledges the destination frame before resuming. This does not promise immediate opening: it removes mandatory FFMS indexing from general playback.

Start presentation with owned CPU frames and a CPU-BGRA/libass reference, then prototype a **Qt Quick scene-graph texture/material presenter** with independent subtitle and tool layers. Avoid requiring OpenGL or native-texture sharing throughout the application. Keep offscreen RHI rendering or native imports as measured alternatives. Qt convenience sinks do not themselves establish exact scheduling; libmpv's inspected public rendering API adds an OpenGL/software integration boundary. [Completed presentation research](https://github.com/altqx/hikari/blob/26d7ac2a940c9f0edf2c6e65f5b1db05ea1a2507/docs/research/qtquick-video.md).

## Responsibility contracts

| Boundary | Proposed responsibility |
| --- | --- |
| Video source | Open/index with progress and cancellation; identify tracks; produce owned immutable frames with source generation, frame identity, rational time, dimensions/SAR/crop, planes/strides and color metadata. Report unsupported operations explicitly. |
| Transport | Own active mode, clock, seek/step requests and bounded queues. Playback may drop late frames; paused stepping must acknowledge the requested indexed frame, never silently substitute a later one. |
| Subtitle renderer | Render a document/font/configuration snapshot for an explicit video time and target geometry. Copy library-owned output before its lifetime expires; preserve blend order and alpha convention. Invalidate independently while paused. Editing subtitles use Hikari's libass settings; prevent duplicate player rendering. |
| Presenter | Own upload/composition and graphics resources on Qt's render thread; never wait for decoding there. Retain frames until use completes. Share one source/script-to-view transform and inverse with hit testing; distinguish logical coordinates from physical pixels. |
| Tools | Own commands and geometry independently of video resources. Start with separate Shapes/QML handles and numeric alternatives; merging into the video pass requires profiling. Preserve operation with dummy video or decoder failure. |
| Lifecycle | Serialize each decoder/renderer instance through its owner. Tag results with generation and relevant document/configuration revisions; reject stale completions after seek, cancellation or replacement. Report recoverable/fatal errors, preserve editing, and release graphics resources on their owning thread during device loss/window recreation/shutdown. |

The tool separation and shared-coordinate proposal follows [completed overlay research](https://github.com/altqx/hikari/blob/fb510f491048b3a073ace4076cf12bc949345c53/docs/research/visual-overlays.md).

Color handling must carry range, matrix, primaries and transfer separately, honor the existing ASS matrix override, and record missing-metadata fallback. Establish SDR CPU/GPU and subtitle-alpha reference parity first; this is not an HDR claim or an approved capability exclusion. Keep DirectShow behind an optional Windows playback adapter and xy-VSFilter/CSRI behind an optional subtitle-renderer adapter; neither may dictate core types or the default graphics backend.

## Human choice and evidence gate

**Allow player-owned audio for general playback (recommended), or require one shared application audio sink in both modes?** The latter favors a direct-FFmpeg transport with substantially more scheduling/seek/audio responsibility; Qt's decoded-audio callback is not an external-clock contract.

That is the product/maintenance tradeoff needing judgment now. Presenter API, queue depth and optimization follow native evidence. Before adoption, verify existing player capabilities, exact stepping/seeks, clock handoff, color/alpha, cancellation, mixed DPI, deployment/licence manifests and supported-platform behavior. No benchmark, package-size, hardware-decoding, accessibility or licence-compliance pass is claimed.
