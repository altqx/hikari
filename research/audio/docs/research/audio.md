# Audio output, scrubbing and waveform/spectrum rendering in Qt Quick

Research for [altqx/hikari#13](https://github.com/altqx/hikari/issues/13), completed 2026-09-27. Sources describe Qt 6.11 and the explicitly pinned upstream revisions below. This is a source investigation, not a latency benchmark or a backend decision.

## Summary

All five output libraries can consume application-supplied PCM and therefore implement exact source-frame ranges. None makes disk access, resampling, scrubbing or audible-device timing correct automatically. PortAudio has the most directly useful *documented callback timing contract* for this requirement: estimated DAC time paired with a stream clock. Qt 6.11 adds a low-latency callback path and has convenient device-change integration, but `processedUSecs()` is not a documented DAC timestamp. Prototype those two first if audible A/V synchronization is the deciding requirement; retain SDL3, miniaudio and RtAudio as alternatives with different integration tradeoffs.

For the view, retain source-independent peak/spectrum caches and draw only the visible region. A painted reference implementation is credible: current Audacity development sources use precisely this approach, with cached waveform bitmaps. Compare that with QSG waveform geometry plus tiled spectrogram textures; keep the moving cursor and editable markers outside the static image. No source examined establishes a measured winner for HikariSub's workload.

## Implications for the decision

Decide the transport contract before the library: source sample frames, device sample frames and monotonic time are separate units; the clock must report its uncertainty and become invalid during a seek/device reset. Device loss should stop and explain the failure rather than silently continue a stale cursor. Choose whether default-device changes follow automatically or retain the explicitly selected output.

Proposed first comparison: identical cached PCM range/scrub commands through PortAudio and Qt 6.11, with loopback measurements of onset, stop tail and drift on Windows WASAPI and Linux PipeWire/PulseAudio. Audacity is evidence that PortAudio and a painted Qt view are viable integration choices, not proof that its large audio engine should be imported. Build agent QML/HTML reaction prototypes; retain Figma Starter for occasional handoff only.

## Detailed findings

### 1. Today's behaviour in HikariSub

All paths below are relative to [`HikariSub/` at commit `20d647c4c769ab7f5d383cf3c1c33f03876a94e9`](https://github.com/altqx/hikari/tree/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub).

**Decode and caches.** Audio is decoded by FFMS2 and resampled to interleaved
signed 16-bit (`FFMS_FMT_S16`), stereo kept for playback
(`ProviderFFMS2.cpp:465-469`); the waveform and spectrum read a mono downmix
(`ProviderFFMS2.cpp:751-756`). The decoded PCM lives in RAM or in a disk cache
file under `AudioCache` (`ProviderFFMS2.cpp:87`, `ProviderFFMS2.cpp:812-864`).
Because the whole track is decoded up front, any sample can be fetched at random
with `GetBuffer`/`GetPlaybackBuffer` (`Provider.h:63-67`). That property is what
allows frame-addressed ranges. A disk cache still needs prefetching: filesystem reads, page faults and cache misses cannot be assumed cheap or safe in an audio callback.

**Output on Windows: DirectSound.** `DirectSoundPlayer2Thread` (an Aegisub 2
descendant, `AudioPlayerDSound.h:17`) opens the *default* device only,
`DirectSoundCreate8(&DSDEVID_DefaultPlayback, ...)`
(`AudioPlayerDSound.cpp:483`), with `DSSCL_PRIORITY` and a secondary buffer with
`DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_GLOBALFOCUS`
(`AudioPlayerDSound.cpp:491`, `:516`). The ring buffer is
`WantedLatency * BufferLength` = 100 ms x 5 = 500 ms
(`AudioPlayerDSound.cpp:1106-1107`, `:510`), refilled from a worker thread that
waits on Win32 events (`AudioPlayerDSound.cpp:560-572`). A play range is
`Play(start, count)` in sample frames and the fill loop writes silence past
`end_frame` (`AudioPlayerDSound.cpp:818-870`), so the range end is already
sample-accurate at the PCM level.

**Position clock.** `AudioPosition::Frame()` estimates the played frame as
"frames written minus bytes the play cursor has not reached", from
`IDirectSoundBuffer::GetCurrentPosition`
(`AudioPlayerDSound.cpp:800-815`). It is shared (via `shared_ptr`) with the video
renderer, which slaves its clock to it: `RendererVideo::PlaybackClock()` returns
the audio frame in ms when audio is playing and falls back to `timeGetTime()`
otherwise (`RendererVideo.cpp:1153-1163`); seeks call
`AudioPosition::Restart()` so the clock holds until new data is written
(`RendererFFMS2.cpp:607-610`, `AudioPlayerDSound.h:37-43`). **So audio is already
the master clock for A/V sync on Windows**, and any replacement must expose an
equivalent played-frame estimate with an explicit uncertainty; a DirectSound play cursor is not a physical acoustic measurement.

**Output on Linux: GStreamer.** The non-Windows branch of the same class pushes
PCM into `appsrc -> audioconvert -> audioresample -> autoaudiosink`
(`AudioPlayerDSound.cpp:54-59`, `:97-100`), forces `sync=FALSE` on the sink
(`AudioPlayerDSound.cpp:122-127`), and estimates position from a
`steady_clock` stopwatch started at play (`AudioPlayerDSound.cpp:192`, `:430-438`).
`AudioPosition::Frame()` returns `-1` there (`AudioPlayerDSound.cpp:455-458`), so on
Linux the video clock never locks to the sound card. There is no macOS path.

**Device selection.** None in practice. `AudioDeviceEnumeration.h:17` sets
`AUDIO_DEVICE_ENUMERATION 0`; the disabled code enumerated WASAPI render
endpoints via `IMMDeviceEnumerator::EnumAudioEndpoints`
(`AudioDeviceEnumeration.cpp:26-60`) and its only caller is the DirectShow
player (`dshowplayer.cpp:109`). There is no hot-plug handling.

**Scrubbing.** The audited entry point is range playback (`AudioDisplay::Play`, `AudioDisplay.cpp:1551-1590`); no dedicated continuous scrub transport was found. A missing symbol alone does not prove the absence of every scrub-like gesture. The rewrite must define whether scrubbing means short repeated audition windows, reverse playback or variable-speed audio.

**Playback cursor.** On Windows a dedicated thread polls the player every 16 ms
with `timeBeginPeriod(1)` (`AudioDisplay.cpp:2616-2633`) and repaints only the
cursor by blitting a cached static surface and drawing a 2 px line on top
(`AudioDisplay.cpp:714-762`). On Linux a 17 ms `wxTimer` does the same with
`RefreshRect` of the cursor strip (`AudioDisplay.cpp:1581-1585`, `:2641-2656`).
The display auto-scrolls when the cursor gets within 50 px of an edge
(`AudioDisplay.cpp:2680-2690`).

**Waveform.** `Provider::BuildPeaks` builds a min/max table with one entry per
256 samples (`Provider.h:101`, `Provider.cpp:103-115`,
`WaveformPeaks.cpp:20-46`). When zoomed out beyond 4 blocks per pixel the
column min/max comes from that table; when zoomed in it scans raw samples
(`Provider.cpp:117-164`). On Windows the result is drawn as one Direct3D 9
`D3DPT_LINELIST` of `2*w` vertices, one vertical line per pixel column
(`AudioDisplay.cpp:1051-1081`). On Linux the same data goes to a `wxDC`
(`AudioDisplay.cpp:1758`, `:1854+`). There is a single level of pyramid, not a
multi-level mip chain.

**Spectrum.** `AudioSpectrum` runs a 2048-point real FFT
(`GFFT/GFFT.h:23-24`, `line_length = 1 << 10`) with **no window function** (the
input is copied straight into the FFT buffer, `GFFT/GFFT.cpp:274-287`), caches
magnitude lines in 16-line sub-caches computed on one thread per CPU
(`AudioSpectrum.cpp:35-100`, `:390-391`), raises the overlap count up to 24 when
zoomed in (`AudioSpectrum.cpp:145-148`), and maps power to a 256-entry palette
with optional log-frequency ("non-linear") rows (`AudioSpectrum.cpp:171-235`).
The CPU writes a BGRA image the width of the view which is locked into a D3D9
off-screen surface and `StretchRect`-ed (`AudioDisplay.cpp:1086-1115`, `:363`); on
Linux it becomes a `wxImage`/`wxBitmap` (`AudioDisplay.cpp:1813-1845`). The
image is regenerated whenever the scroll position changes
(`AudioDisplay.cpp:1818-1821`): there is no tile cache.

**Overlays.** Keyframes, line boundaries, the inactive neighbouring lines,
karaoke syllable boundaries and syllable text are all drawn into the same
static surface on each full redraw (`AudioDisplay.cpp:563-628`, `:802-915`,
`:2851+`). Karaoke editing, syllable splitting and boundary dragging are done
in `OnMouseEvent` with hit-testing against the syllable time array
(`AudioDisplay.cpp:2174-2424`).

**Controls.** `AudioBox` holds horizontal zoom, vertical zoom and volume
sliders and toggles for spectrum, non-linear spectrum, karaoke, auto-scroll and
auto-commit (`AudioBox.h:70-84`).

**Compatibility baseline:** random-access PCM from a decoded cache;
`Play(start, count)` in frames with a sample-accurate end; an audio-derived
"frame audible now" clock that the video renderer uses as master; a 60 Hz
cursor that does not repaint the waveform; waveform peaks and a cached
spectrum. **Requested investigation:** device selection/hot-plug, Linux/macOS timing and scrubbing. A Hann or other analysis window is a proposed spectrum change, not a compatibility requirement; select its time/frequency tradeoff and invalidate caches when it changes.

### 2. Audio output backends

| Candidate | Range, start and clock | Devices and platform coverage | Assessment |
| --- | --- | --- | --- |
| Qt `QAudioSink` | QIODevice pull/push; since 6.11 a soft realtime callback supplies an interleaved span. `reset()` discards queued data. `processedUSecs()` means processed audio, not a specified hardware presentation timestamp. | `QMediaDevices` enumerates and signals output/default changes. Windows, CoreAudio, PipeWire/PulseAudio; Linux ALSA is experimental. | Smallest Qt integration. Needs measured clock calibration/uncertainty or a native extension if a strict DAC clock is mandatory. |
| PortAudio | Callback supplies `outputBufferDacTime` and `currentTime` in the same domain as `Pa_GetStreamTime()`. `paComplete` drains; `paAbort` stops as soon as possible. Fill all callback frames, using silence after range end. | WASAPI, CoreAudio, ALSA, JACK. Latest published release queried is v19.7.0; upstream master adds PulseAudio. Core public v19 API has enumeration but no portable device-added/removed callback. | Best documented timing fit of this set. Host timing accuracy and hotplug recovery still need verification; pin release/commit and supported host APIs explicitly. |
| miniaudio | Low-level `ma_device` callback is a compact PCM sink. High-level sound cursor/engine time tracks the processing graph; no corresponding portable DAC timestamp was found in the inspected public interface. | WASAPI, CoreAudio, ALSA, PulseAudio, JACK. Device IDs and enumeration; notifications for stopped/rerouted/interrupted, with backend-dependent omissions. | Attractive small integration (public domain or MIT-0 choice). Scrub/clock policy remains ours. Prefer low-level device API when FFMS2 already decodes. |
| RtAudio | Callback stream with stream time and reported latency in frames. Latency can be zero where unsupported; this is weaker evidence than a callback buffer's timestamp at the DAC. | WASAPI/CoreAudio/ALSA/PulseAudio/JACK. v6 device queries refresh IDs; poll/reconcile or implement native notifications and reopen after loss. | Straightforward C++ alternative; no demonstrated advantage over PortAudio for the requested timing contract. |
| SDL3 | `SDL_AudioStream` supports on-demand callbacks, conversion and queue clearing. Queued bytes are input to the conversion stream, not frames currently audible. | WASAPI, CoreAudio and direct PipeWire/PulseAudio/ALSA implementations; audio device added/removed/format-change events. | Convenient hotplug and desktop backend coverage. Adds another event integration layer and still needs a clock strategy. |

The Qt facts come from [QAudioSink](https://doc.qt.io/qt-6/qaudiosink.html), [QMediaDevices](https://doc.qt.io/qt-6/qmediadevices.html) and [Linux backend requirements](https://doc.qt.io/qt-6/qtmultimedia-linux.html). Buffer-frame APIs arrived in 6.10, the callback in **6.11**; do not assume a 6.8 LTS build has it. Qt's callback does not use the configurable QIODevice buffer-size path, and cannot block, allocate or perform disk I/O. Device-list notification does not itself define automatic stream migration.

PortAudio sources: [timing structure](https://portaudio.com/docs/v19-doxydocs/structPaStreamCallbackTimeInfo.html), [API overview](https://portaudio.com/docs/v19-doxydocs/api_overview.html), [completion/abort contract](https://portaudio.com/docs/v19-doxydocs/portaudio_8h.html), and [timing implementation guidance](https://github.com/PortAudio/portaudio/wiki/BufferingLatencyAndTimingImplementationGuidelines). These explicitly describe *estimated* hardware times; neither their existence nor `PaStreamInfo::outputLatency` proves every host includes every downstream buffer. Compare [v19.7.0 host implementations](https://github.com/PortAudio/portaudio/tree/v19.7.0/src/hostapi) with [inspected master `17967f3`](https://github.com/PortAudio/portaudio/tree/17967f32de95f2179e7f7caa9632a79c5d59a2ea/src/hostapi): do not attribute master's PulseAudio backend to the released version. PipeWire through JACK/ALSA/Pulse compatibility is a different deployment route from a native PipeWire backend.

miniaudio [manual](https://miniaud.io/docs/manual/) documents enumeration, device selection and callback lifetime. The inspected [v0.11.25 header](https://github.com/mackron/miniaudio/blob/9634bedb5b5a2ca38c1ee7108a9358a4e233f14d/miniaudio.h) documents notification gaps and lists no native PipeWire backend. A sound's decoded-frame cursor must not be exposed as an acoustic clock. The author's [timing discussion](https://github.com/mackron/miniaudio/discussions/490) reinforces that limitation but is older evidence, not a benchmark.

RtAudio's inspected [public header](https://github.com/thestk/rtaudio/blob/c0a533d7bb16e8ca0d96cdb2e3fcfb6d1d095df4/RtAudio.h) specifies device IDs, host APIs, requested versus actual buffers and zero for unavailable latency. [Implementation](https://github.com/thestk/rtaudio/blob/c0a533d7bb16e8ca0d96cdb2e3fcfb6d1d095df4/RtAudio.cpp) shows stream-time bookkeeping; don't equate it with a sampled hardware DAC counter. No native PipeWire enum is present in that revision.

SDL sources: [stream callback](https://wiki.libsdl.org/SDL3/SDL_AudioStreamCallback), [queued-byte semantics](https://wiki.libsdl.org/SDL3/SDL_GetAudioStreamQueued), [clear](https://wiki.libsdl.org/SDL3/SDL_ClearAudioStream), [device events](https://wiki.libsdl.org/SDL3/SDL_AudioDeviceEvent) and [backend source tree](https://github.com/libsdl-org/SDL/tree/main/src/audio). Clearing an application stream cannot recall sound already passed to the device. Qt owns the UI event loop; poll/forward SDL audio events without creating a competing UI loop.

**Shared transport design (proposal).** Keep the device running while feeding silence if measurements show reopen latency harms audition. Publish immutable, generation-tagged commands into a bounded queue. Prefetch cache pages on workers; callbacks copy only ready PCM and report underruns. Track source start/end, playback rate, device-frame position and monotonic anchor separately. A seek invalidates old queued generations and the old clock; bound unavoidable downstream sound. At source-range end, zero-fill the remainder; resampling needs a deliberate boundary/filter-tail policy. Click-free fades change edge samples and should be an explicit audition mode, not silently alter the exact-range reference.

For PortAudio, map each callback's first device frame to its predicted DAC time, then interpolate at stream time within the known queued interval. Do not extrapolate indefinitely over underflow, pause or device replacement. For other APIs, distinguish a processed-frame estimate plus latency compensation from a timestamp-backed estimate. All backends require empirical error bounds, especially Bluetooth and mixed sample rates. Reverse/variable-speed scrubbing requires an application resampler and gesture policy; rapidly calling start/stop is not a complete implementation.

### 3. Waveform and spectrum rendering in Qt Quick

| Renderer | Appropriate work | Costs/limits |
| --- | --- | --- |
| `QQuickPaintedItem` | Reference view, familiar QPainter text/lines, composition of cached bitmaps. | Default CPU image rasterization plus upload; repaint only invalidated area. Qt 6.9+ FBO acceleration applies only to OpenGL. |
| QSG geometry | Visible min/max waveform envelope as triangles; separate playhead and selected-range geometry. | Own geometry lifecycle and antialiasing. Wide native lines are not equally portable; screen-space quads are safer. No full-track geometry allocation. |
| Tile textures | Spectrogram blocks and optionally waveform bitmaps keyed by time/zoom. Scroll reuses tiles; only missing/new tiles upload. | Bound CPU/GPU memory, manage asynchronous stale results and texture lifetime, avoid seams at fractional zoom. |
| `QQuickRhiItem` | A measured need for GPU spectrum/palette processing or unified custom renderer. | More shader/resource/synchronization work and an offscreen render target; QRhi has limited compatibility guarantees. Not justified just to draw a cursor. |

See Qt's [painted-item contract](https://doc.qt.io/qt-6/qquickpainteditem.html), [QSGGeometry](https://doc.qt.io/qt-6/qsggeometry.html), [scene graph custom geometry](https://doc.qt.io/qt-6/qtquick-scenegraph-customgeometry-example.html) and [QQuickRhiItem](https://doc.qt.io/qt-6/qquickrhiitem.html). The first renderer comparison should share the exact same caches, viewport and overlays so it measures presentation costs rather than different decoders.

**Cache proposal.** Build a multiresolution min/max hierarchy off-thread (optional RMS as a separate statistic). At overview zoom, choose a level near a pixel's sample interval and combine only the covered blocks; near sample zoom show raw samples or stems. Key by media content/version, channel/downmix policy and source sample rate. Disk cache access stays outside GUI/render/audio threads. Use bounded time tiles for spectrum magnitudes; keys include FFT size, window, hop and frequency mapping. Separate magnitude values from palette where practical so recoloring need not rerun FFT. A raw PCM file's byte offset is not a universal time index after rate changes.

Markers, neighboring-line boundaries, karaoke handles/labels, selection and cursor should be separate QML/QSG layers sharing one time-to-x transform. Cursor animation should use the transport clock; a 60 Hz timer does not establish 60 Hz presentation or correct timing. Repaint the waveform only for content, scale or color changes. Snap/drag commits source times through a command model, one undo operation per gesture, with keyboard movement and numerical alternatives. Expose selected range, marker values and commands through accessible controls; raster pixels alone do not provide accessible semantics.

### 4. How Audacity 4 draws clips and waveforms

Inspected Audacity development commit [`36146d838c934ae429cb164e8eb3d39af75a65ff`](https://github.com/audacity/audacity/tree/36146d838c934ae429cb164e8eb3d39af75a65ff), not a claim about every released Audacity binary. [`WaveView`](https://github.com/audacity/audacity/blob/36146d838c934ae429cb164e8eb3d39af75a65ff/src/projectscene/view/tracksitemsview/waveview.h) derives directly from `QQuickPaintedItem`. Its [paint method](https://github.com/audacity/audacity/blob/36146d838c934ae429cb164e8eb3d39af75a65ff/src/projectscene/view/tracksitemsview/waveview.cpp) selects the plot representation by zoom and delegates to a painter; nearby code handles sample-level editing.

[`WaveformPainter`](https://github.com/audacity/audacity/blob/36146d838c934ae429cb164e8eb3d39af75a65ff/src/projectscene/view/tracksitemsview/au3/WaveformPainter.cpp) keeps per-channel `WaveDataCache` and `WaveBitmapCache`, looks up the visible time range, clips cache elements and draws QImages with QPainter. [Cache definitions](https://github.com/audacity/audacity/tree/36146d838c934ae429cb164e8eb3d39af75a65ff/au3/libraries/au3-wave-track-paint/waveform) preserve data/bitmap separation. This is stronger evidence for *reuse existing audio data/cache algorithms behind a Qt presenter* than for rebuilding every layer as GPU compute.

The current [`ClipChannelSpectrogramView`](https://github.com/audacity/audacity/blob/36146d838c934ae429cb164e8eb3d39af75a65ff/src/spectrogram/view/clipchannelspectrogramview.cpp) is also painted, observes its viewport and delegates spectrogram drawing. [`Au3AudioEngine`](https://github.com/audacity/audacity/blob/36146d838c934ae429cb164e8eb3d39af75a65ff/src/au3audio/internal/au3audioengine.cpp) wraps retained `AudioIO`, exposes stream time and device changes, and logs PortAudio hardware latency. Thus a new Qt UI does not imply replacing the established audio engine. None of these files provides transferable startup-latency or FPS measurements for this app.

## Open questions

1. What error bound is acceptable for the displayed audible clock and A/V drift? Measure an impulse train via wired loopback, not only callback wall times. Include 44.1/48 kHz, resampling, pause/resume, seeks, underrun, USB removal/default change, sleep/wake and Bluetooth as a separately reported case.
2. Compare first-start and warm-start latency, stale audio after repeated scrub commands, exact generated source-frame counts, p50/p95/p99 callback time and underruns under CPU/disk pressure. Report device, driver, host API, buffer request/actual size and library commit. No measurements were run for this research.
3. Run the same Windows WASAPI, Linux PipeWire native/compatibility, PulseAudio and direct ALSA fixtures; add macOS CoreAudio before advertising support. Direct ALSA may contend with the desktop sound server; it is a support-policy choice, not an automatic fallback guarantee.
4. Render long mono/stereo fixtures at overview and sample zoom, fractional DPI and 4K; record frame-time tails, CPU/GPU memory, cache hit rate, texture bytes uploaded and reaction latency while dragging karaoke boundaries. Compare PaintedItem against QSG+tiles only with identical workload/data. Screen-reader operation requires native human verification.
5. Decide scrub semantics, click suppression, FFT window/hop, default-device migration and cache budget with the user. These remain product/implementation choices, separate from the verified API facts above.
