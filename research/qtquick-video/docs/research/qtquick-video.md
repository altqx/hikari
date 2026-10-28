# Frame-accurate FFMS2 video with libass in Qt Quick

Research for altqx/hikari#10 (`qtquick-video`). Feeds: "Choose the video decode, playback and presentation pipeline".

Status: completed desk research, 2026-09-27. Qt API references resolve to 6.11.2; HikariSub inventory is at `20d647c4`. No GPU interoperability, frame-latency or pixel-parity benchmark was run.

## Summary

Keep frame identity/timing in the FFMS2-backed core and treat the Qt surface as a presenter. First establish a CPU-BGRA reference plus independently updated libass overlay. For the portable GPU path, compare a custom scene-graph material with `QQuickRhiItem`; both can integrate into Quick without imposing OpenGL. `QVideoSink`/`VideoOutput` is a useful public-API baseline, but its convenience does not answer subtitle composition or exact application scheduling. `QQuickFramebufferObject` is OpenGL-only; external textures are a specialized interoperability optimization, not the starting architecture.

## Implications for the decision

Recommendation, not a selected pipeline: prototype correctness first, then measure texture upload/conversion/composition and decode separately. Keep the overlay tools as independent scene items unless a measured need requires merging their rendering. Share one source-to-view transform and one explicit colour policy. The product decision in #25 must also consider general playback #11 and visual overlays #12; an mpv integration must not silently choose the graphics backend for the entire app.

## Detailed findings

### 1. Today's pipeline in HikariSub

`ProviderFFMS2` first configures BGRA output, reads encoded size/SAR/colour metadata, applies an ASS YCbCr Matrix override, and on Windows optionally changes to NV12 when GPU conversion is enabled and dimensions are even. `RendererFFMS2` feeds NV12 through DXVA2 and uses a separate subtitle overlay; BGRA remains necessary for screenshots/scripts. The renderer's current matrix choices are not proof of BT.2020/HDR correctness. [Provider](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ProviderFFMS2.cpp), [renderer](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/RendererFFMS2.cpp)

`FrameQueue` already provides bounded slots, generation-based resets and a take operation that may return a later decoded frame. That is useful playback behavior but must not define an exact paused-step operation. `SubtitlesLibass` renders by timestamp and CPU-blends image masks; its overlay path checks libass's change result and tracks dirty bounds. Preserve these useful separation points instead of carrying D3D9 surface ownership into the new model. [FrameQueue](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FrameQueue.h), [libass provider](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubtitlesLibass.cpp)

### 2. Presentation options in Qt Quick

| Approach | Verified mechanism | Main tradeoff for HikariSub |
|---|---|---|
| QVideoFrame → QVideoSink → VideoOutput | Sink accepts frames; frame carries buffer/format/timing metadata | Public convenience, but conversion/presentation behavior and overlay synchronization need validation |
| QQuickItem + QSG texture/material | updatePaintNode builds textured geometry; custom materials accept multiple textures | Direct scene composition and transform integration; custom upload/shader/lifetime code |
| QQuickRhiItem | Qt 6.7+ offscreen QRhi rendering composited into Quick | Convenient multipass renderer; extra offscreen target and limited QRhi API compatibility |
| QQuickFramebufferObject | Offscreen OpenGL renderer | Appropriate only if OpenGL is intentionally required |
| External/native textures | Native QSG texture interfaces and QRhi wrapping | Potential copy avoidance; backend-specific ownership, device and synchronization work |

`QVideoSink::setVideoFrame` can feed a `VideoOutput` sink without `QMediaPlayer`. A mapped `QVideoFrame` provides plane/stride access, and `QVideoFrameFormat` carries colour-space/range/transfer metadata. Do not create a fresh byte-array assumption that every frame is tightly packed BGRA. Preserve buffer lifetime through presentation; mapping hardware frames can incur a copy. The sink is a frame interface, not an FFMS2 seek/index API or an A/V scheduler. [QVideoSink](https://doc.qt.io/qt-6/qvideosink.html), [QVideoFrame](https://doc.qt.io/qt-6/qvideoframe.html)

A custom material can sample luma/chroma planes and subtitle textures in the Quick render pass. `QSGTexture` supports custom YUV/alpha-mask inputs and queued texture operations; Qt's custom-material example demonstrates portable shader packages. Prefer resource reuse over constructing a new texture every frame. [QSGTexture](https://doc.qt.io/qt-6/qsgtexture.html), [custom-material example](https://doc.qt.io/qt-6/qtquick-scenegraph-custommaterial-example.html)

`QQuickRhiItem` handles a DPR-aware offscreen colour buffer and separates UI state from its renderer via synchronization. It supports Vulkan, Metal, D3D11/12 and OpenGL, but not Quick's software adaptation. QRhi requires `Qt::GuiPrivate` and has no general source/binary compatibility guarantee across Qt minor versions. Pin the Qt minor and rebuild/test on upgrades; public QQuickRhiItem status does not remove this dependency risk. [QQuickRhiItem](https://doc.qt.io/qt-6/qquickrhiitem.html)

`QQuickFramebufferObject` is explicitly a legacy OpenGL-only integration. Native texture adoption can target individual APIs, but the producer must use a compatible device/context and obey resource lifetime and synchronization requirements. FFMS2 CPU output does not become zero-copy merely by wrapping the destination texture. [FBO item](https://doc.qt.io/qt-6/qquickframebufferobject.html), [native texture interfaces](https://doc.qt.io/qt-6/qsgtexture.html#nativeInterface), [QRhi](https://doc.qt.io/qt-6/qrhi.html)

### 3. YUV to RGB on the GPU

Use a frame descriptor containing pixel format/bit depth, plane strides, chroma subsampling/siting where known, range, matrix coefficients, transfer function, primaries, SAR and crop. Qt distinguishes colour space, transfer and range in `QVideoFrameFormat`; these are separate properties. BT.601/709/2020 are not interchangeable just because the output is RGB. [Frame format](https://doc.qt.io/qt-6/qvideoframeformat.html)

Recommended conversion contract: normalize coded Y/Cb/Cr with the selected full or limited range, apply the matrix appropriate to the source/override, then apply an explicit output transfer/colour-management policy. Account for high-bit-depth packing and sample alignment. Use libswscale's coefficient/range handling as the CPU reference and known test patterns for GPU parity. Never choose matrix solely from resolution when metadata is present; when metadata is missing, record the fallback and allow the existing ASS matrix override. [libswscale colour APIs](https://ffmpeg.org/doxygen/trunk/group__libsws.html), [current overrides](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ProviderFFMS2.cpp)

BT.2020 matrix conversion alone is not HDR support: PQ/HLG transfer, display primaries, precision and tone mapping remain separate. Do not label an 8-bit SDR texture path HDR-correct. The initial prototype should define SDR output explicitly and include black/white ramps, saturated patches, limited/full variants and a 10-bit fixture. Pixel tolerance and compatibility with legacy ASS colour handling are decisions to record, not facts established by this survey.

### 4. Compositing libass

libass returns a linked list of image masks with width/height/stride, destination coordinates and RGBA colour; the mask is one byte per pixel, and the final row need not have full stride padding. Process images in order. Treat returned memory as library-owned and copy any mask data needed beyond the render call's lifetime discipline. [libass public API](https://github.com/libass/libass/blob/master/libass/ass.h)

CPU blending is the simplest correctness oracle and fallback: the existing `BlendAssBitmap` computes effective opacity from coverage and the inverse alpha byte in ASS colour. GPU composition can upload masks to single-channel textures/atlases, multiply coverage by opacity, and use a consistent premultiplied-alpha convention. Preserve blend order and validate shadows, outlines, overlapping glyphs, clipping and partially transparent text. A dirty RGBA overlay texture is an intermediate option that retains current CPU blend semantics while avoiding reuploading unchanged video pixels. [existing blend implementation](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AssBlend.cpp)

Cache overlay output by subtitle revision, timestamp/frame identity, render size and font/configuration revisions. Redraw subtitles while paused without decoding the frame again. Whether blending occurs in legacy gamma-encoded RGB or a linear working space affects parity; preserve the chosen reference first and make any colour-model change explicit. Libass change flags are useful invalidation evidence, not a substitute for tracking changed application settings.

### 5. Zoom, pan, high-DPI

Keep source video pixels, ASS script coordinates, logical item coordinates and physical device pixels distinct. Use one explicit transform for SAR/letterboxing, zoom and pan, with an inverse for pointer hit testing. `VideoOutput.contentRect` provides the rendered content rectangle for its fit mode; a custom surface must compute the same concept. UI handle size may stay constant in logical pixels while video geometry scales. [VideoOutput](https://doc.qt.io/qt-6/qml-qtmultimedia-videooutput.html)

Qt uses device-independent coordinates for UI geometry; raw image buffers remain pixel-sized. Fractional scaling requires correct DPR at allocation and viewport boundaries, not multiplying every source coordinate by DPR twice. Rebuild size-dependent resources when moving a floating preview between screens. Provide a defined pixel-inspection mode (nearest filtering) separately from normal smooth scaling. Validate odd dimensions, non-square pixels, negative pan and 125/150% scaling. [High DPI](https://doc.qt.io/qt-6/highdpi.html)

### 6. Threading and frame queue

Qt scene-graph objects and native graphics operations belong to the render thread; `updatePaintNode`/synchronization is the handoff point. Decode and libass work must not block that thread. Qt can use basic or threaded render loops, so code must remain valid for both. [Scene-graph threading](https://doc.qt.io/qt-6/qtquick-visualcanvas-scenegraph.html)

Proposed queue contract:

1. A decode owner serializes FFMS2 access and produces immutable owned frames with index, rational timestamp and generation. A seek increments generation and cancels obsolete work.
2. Playback uses a small bounded queue and the audio-derived clock; it may discard late frames. Paused stepping requests an exact index and acknowledges that same frame, never a later substitute.
3. The UI publishes the latest requested generation/index and queues an update. The render thread consumes an owned ready frame and retains it until upload/use completes; it never waits for decode.
4. Subtitle snapshots are tagged with both video time and subtitle revision, so an old overlay cannot be paired with a new seek. Generation checks also cover slow font/overlay jobs.
5. Hide/show, device loss, docking to a new window and shutdown invalidate GPU resources on the correct thread without destroying CPU frame ownership prematurely.

These are design recommendations based on the existing queue and Qt lifetime rules. Queue depth, prefetch policy and responsiveness targets require measurement. VSync/compositor delivery means displaying the requested source frame exactly does not imply deterministic physical presentation time.

### 7. RHI backends and Linux windowing

| Environment | Portable QSG/QRhi path | Caveat to test |
|---|---|---|
| Windows D3D11 | Supported RHI backend | Texture formats, upload cost, resize/device loss |
| Windows D3D12 | Supported in current Qt | Pin supported Qt version; do not reuse D3D11 handles |
| Vulkan | Supported RHI backend | Resource layouts/fences if importing native textures |
| OpenGL | Supported; also FBO/libmpv option | Context sharing/state, driver differences |
| Linux Wayland/X11 | QPA windowing and graphics backend are distinct | Native embedding, compositor pacing, floating-window/DPI behavior |
| Software Quick | CPU reference can be designed | QQuickRhiItem is unavailable; custom materials need a fallback |

QRhi abstracts common rendering commands, not every external decoder interoperability path. Explicitly log chosen QPA, RHI backend, renderer and Qt version in prototype results. Do not extrapolate D3D11 success to D3D12 or one Linux compositor to all others. [QRhi backend overview](https://doc.qt.io/qt-6/qrhi.html), [QQuickRhiItem restrictions](https://doc.qt.io/qt-6/qquickrhiitem.html)

### 8. How other Qt apps do it

The public mpv render API inspected at `a1bf4b6559d6e644b967c22d87232dc77c7dd442` exposes OpenGL and software rendering. Its QML example uses `QQuickFramebufferObject`. Thus the example demonstrates Qt integration but not arbitrary Qt RHI support; native mpv video-output capabilities must not be confused with its embeddable render API. [render API](https://github.com/mpv-player/mpv/blob/a1bf4b6559d6e644b967c22d87232dc77c7dd442/include/mpv/render.h), [QML example](https://github.com/mpv-player/mpv-examples/blob/master/libmpv/qml/main.cpp)

Current Kdenlive source (`e4d5b25d2aa5af7095fc1623353c39204fd946de`) has a QQuickWidget-based video monitor fed by MLT, with separate OpenGL/D3D/Metal implementations; the D3D implementation explicitly asserts D3D11. Shotcut (`c594b9a61e1aae798db4e94f727e1e621e3384c3`) similarly uses QQuickWidget/MLT and backend-specific video widgets. They demonstrate the amount of lifecycle/interop engineering required, not a single ready-made FFMS2/libass presenter to copy. [Kdenlive monitor](https://github.com/KDE/kdenlive/blob/e4d5b25d2aa5af7095fc1623353c39204fd946de/src/monitor/videowidget.h), [Kdenlive D3D](https://github.com/KDE/kdenlive/blob/e4d5b25d2aa5af7095fc1623353c39204fd946de/src/monitor/d3dvideowidget.cpp), [Shotcut monitor](https://github.com/mltframework/shotcut/blob/c594b9a61e1aae798db4e94f727e1e621e3384c3/src/videowidget.cpp), [Shotcut D3D](https://github.com/mltframework/shotcut/blob/c594b9a61e1aae798db4e94f727e1e621e3384c3/src/widgets/d3dvideowidget.cpp)

Qt's custom-material and RHI texture-item examples are the smallest primary starting points for a controlled presenter spike. Use them to establish lifecycle correctness, then replace the synthetic texture with an owned decoded frame; do not infer video throughput from a triangle demo. [RHI texture item](https://doc.qt.io/qt-6/qtquick-scenegraph-rhitextureitem-example.html)

## Open questions

Research is complete; #25 and a native QML/C++ spike must decide the presenter after testing exact forward/backward frame requests on VFR/B-frame files, rapid seeks, paused subtitle changes, colour/alpha reference images, 1080p/4K upload latency, queue memory, mixed DPI, dock/window recreation and shutdown. Measure decode-to-ready and ready-to-render separately. No hardware-decoder zero-copy, HDR correctness, frame-rate or latency claim is established here.
