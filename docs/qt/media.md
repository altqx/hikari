# Media transport, presentation and editor audio

**Accepted direction, 2026-09-27.** In the live review, altqx accepted general-player ownership of audio and clock while that mode is active, and **PortAudio editor output with QSG waveform geometry and cached spectrum tiles**. [ADR 0009](../adr/0009-active-mode-media-transport.md) and [ADR 0010](../adr/0010-portaudio-editor-output.md) record the choices for [Choose the video decode, playback and presentation pipeline](https://github.com/altqx/hikari/issues/25) and [Choose the audio output backend and waveform renderer](https://github.com/altqx/hikari/issues/26). They select responsibilities and implementation direction; they do not certify a native pipeline, timing accuracy or complete player parity.

## Active mode owns sound and playback time

HikariSub's application transport coordinates two modes behind explicit backend ports:

| Mode | Decode/output direction | Clock responsibility |
| --- | --- | --- |
| Indexed editing | FFMS2 supplies indexed video and decoded audio; PortAudio is the sole editor-output library. | An audio-derived estimate drives editor playback scheduling and the audio cursor. Intentionally silent playback uses an explicit monotonic-clock mode. |
| General playback | A cross-platform player handles unindexed playback, its audio device and A/V scheduling. Qt Multimedia's FFmpeg-backed player is the **first candidate to verify**. | The active player owns its clock; editor PortAudio output is stopped. |

Only the active mode owns playback audio and timing. This does not require routing the general player's PCM through PortAudio. Do not add Qt/SDL/another library as a silent editor-output fallback. A candidate player's missing capability must be surfaced and resolved through its adapter or an explicit revised choice.

For a mode switch, stop and flush the old transport, invalidate its generation and clock estimates, map the media identity and requested position, and acknowledge the destination presentation before resuming. Indexed destinations acknowledge the requested frame identity; a general player's acknowledgement must describe its actual position/accuracy, without pretending a time seek is an exact indexed step. Device-buffer tails and handoff latency need measurement. Handoff does not rewrite Document timing.

This accepts **clock ownership**, not the still-proposed canonical units, interval boundaries, frame rounding or format conversions in [Set time and frame semantics for editing and playback](https://github.com/altqx/hikari/issues/42). The backend implements that eventual core contract; renderer timestamps and global FPS cannot become authoritative Document state.

## Video, subtitles and visual tools

FFMS2 remains required for indexed editing and libass for subtitle rendering. General playback remains a capability distinct from indexing; the selected player must cover existing track, chapter, embedded-subtitle and fullscreen workflows. The inspected Qt player offers time seeking and track selection but does not establish exact indexed stepping or the required chapter-list API. Qt Multimedia is therefore a candidate, with explicit native parity work still required. [Playback research](https://github.com/altqx/hikari/blob/4c90a86bead00528e5636f012e690c618e803f4f/docs/research/general-playback.md).

Begin with owned CPU frames and a **CPU-BGRA/libass reference**, then implement and verify a **Qt Quick scene-graph texture/material presenter**, keeping subtitle overlays and visual-tool handles separate. The production item/API, pixel upload path and any native-texture optimization remain evidence-driven implementation work. No application-wide OpenGL requirement follows from the existing renderer or an example integration. [Presentation research](https://github.com/altqx/hikari/blob/26d7ac2a940c9f0edf2c6e65f5b1db05ea1a2507/docs/research/qtquick-video.md).

| Port or owner | Required responsibility |
| --- | --- |
| Video source | Open/index with progress and cancellation; identify tracks; return owned immutable frames with generation, frame identity, source timestamp/timebase, geometry/SAR/crop, planes/strides and color metadata. Distinguish unsupported operations from failures. |
| Transport | Own active mode, seeks/steps, bounded queues and clock validity. Playback may drop late frames; exact paused requests return the requested indexed frame or an error, not an unannounced later frame. FFMS2 seek settings must substantiate that promise. |
| Subtitle renderer | Render a Document/font/configuration snapshot at an explicit time and geometry. Copy library-owned output before expiry; preserve blend order/alpha. Redraw paused subtitles independently from video decode and prevent duplicate player subtitle rendering. |
| Presenter | Own graphics resources/upload/composition on Qt's render thread. Never wait for decoding there; retain frame data until use finishes. Distinguish logical coordinates from physical pixels. |
| Visual tools | Keep command state and overlay geometry independent of decoded textures. Share source/script-to-view transforms and inverse hit testing, with numeric/keyboard alternatives and useful behavior during dummy video or decoder failure. |

Keep range, matrix, primaries and transfer metadata distinct; honor the existing ASS matrix override and record missing-metadata fallback. Establish SDR CPU/GPU and subtitle-alpha parity as the first reference. This is neither an HDR correctness claim nor approval to remove HDR-related capability. The [overlay research](https://github.com/altqx/hikari/blob/fb510f491048b3a073ace4076cf12bc949345c53/docs/research/visual-overlays.md) supports separate tool presentation.

DirectShow may remain an optional Windows playback adapter and xy-VSFilter/CSRI an optional subtitle-renderer adapter. They cannot dictate core types, the default cross-platform backend or a new general-purpose plugin ecosystem. Preserve a possible later macOS implementation.

## PortAudio, analysis and caches

PortAudio supplies documented **estimated** callback-buffer DAC time and stream time in a shared domain; this supports mapping ready audio to its expected audible position. It is not an observed latency, drift or acoustic-accuracy guarantee. Keep source sample frames, resampled device frames and monotonic time distinct, and report clock uncertainty. Seek, underrun, device replacement and sleep/wake invalidate stale estimates; never extrapolate stale audio indefinitely. [Audio research](https://github.com/altqx/hikari/blob/7e1aad3ba4f7ac217b9bb467a297035bc0639abd/docs/research/audio.md).

FFMS2 decode/cache workers prepare PCM and prefetch disk pages. The output callback consumes ready buffers without disk access, blocking work, allocation or QML calls. Preserve source-range audition and silence beyond its end; resampling tails, reverse/variable-speed scrubbing and optional click-suppression fades need explicit semantics and tests.

Workers produce multiresolution min/max peaks; near sample zoom can use raw samples. Render visible waveform geometry through QSG and spectrograms from cached texture tiles. Bound CPU/GPU caches; key PCM/analysis by media identity, rate/channel policy and relevant processing settings. Spectrum magnitude tiles additionally identify FFT/window/hop choices; palette changes should reuse those magnitudes. Keep cursor, selection, keyframes and karaoke handles separate so cursor motion does not recompute static analysis.

WASAPI and a Linux desktop-compatible PortAudio route are initial verification candidates, not accepted host-API guarantees. Pin the actual PortAudio revision and supported host APIs, then test Ubuntu/Fedora desktop routing; native PipeWire and ALSA/Pulse compatibility routes are different claims. Automatic default-device following versus retaining an explicit device, reopen behavior and scrub gestures remain prototype questions. Engine acceptance does not answer them.

## Lifecycle and verification obligations

The [bounded Windows transport continuation](https://github.com/altqx/hikari/blob/4012f2ecf6cbf2ba267753c8c0f8417d2ca1f33a/HikariSub/prototypes/media-transport/README.md) records actual Qt audio/subtitle track selection, decoded frame events, FFMS2 owned frames, offscreen software presentation and 19,968 zero-filled PortAudio device frames. It supports continuing the Qt Multimedia candidate; it does not certify the pipeline. A CFR 1000-ms seek first delivered frame 24 before frame 25, and VFR 1035 ms delivered the following frame rather than its containing frame. Product acknowledgement must use delivered-frame evidence with explicitly chosen seek semantics; the probe's near-window sampling rule is not that policy.

The controlled CFR handoff rejected an intentionally delayed old-generation completion and observed software frame presentation, silent callbacks and stop/close. It did not acknowledge Qt device-buffer flushing or measure acoustic continuity. Qt and FFMS2 loaded different same-named FFmpeg DLLs in separate processes, leaving shared-process coexistence and production process boundaries unresolved. Real source-PCM output, resampling, GPU/libass integration, device changes, Linux and calibrated performance remain required. The earlier missing-prerequisite report remains historical evidence, not the current runtime state.

Serialize access to each decoder/renderer through its owner. Tag asynchronous results with source generation and relevant Document/font/configuration revisions, reject stale completions and bound queues. Define cancellation, error reporting and shutdown ordering; release graphics resources on their owning thread during window recreation/device loss. Media errors preserve subtitle editing and expose a recovery path.

Native prototypes must verify labelled controls, F6/Shift+F6 panel traversal, focus recovery, keyboard/numeric editing and screen-reader access to custom timeline state. HTML workspace observations do not establish native media accessibility.

Required evidence includes VFR/B-frame/long-GOP exact seeks; player chapters/tracks/embedded subtitles; clock handoff; loopback onset/stop tails/drift; resampling, underrun/device loss/sleep-wake; CPU/GPU color/alpha; mixed DPI and window recreation; long-track rendering/cache budgets; and clean supported-platform deployment. Audit Qt/FFMS2 FFmpeg ABI coexistence, fonts, plugins, licenses and package contents. Queue sizes, native interfaces, device policy and measured budgets must be settled by focused follow-ups under [Set measurable performance and resource budgets for the rewrite](https://github.com/altqx/hikari/issues/36) and [Choose the test strategy for the rewrite](https://github.com/altqx/hikari/issues/34). None of these gates is claimed passed here.
