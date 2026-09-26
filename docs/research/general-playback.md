# Cross-platform general playback to replace DirectShow and GStreamer

Research for altqx/hikari#11 (`general-playback`). Status: draft in progress.

## Summary

TODO

## Implications for the decision

TODO

## Detailed findings

### 1. Today's behaviour in HikariSub

**Two renderers, picked per file.** `VideoBox::LoadVideo` builds the FFMS2 renderer when the *Video indexing* menu toggle is on, the file is not opened fullscreen, and it is not a dummy video. Otherwise it builds the general-playback renderer: `RendererDirectShow` on Windows and `RendererGStreamer` on Linux (`HikariSub/VideoBox.cpp:290-322`). Opening a file straight into fullscreen always takes the general path (`HikariSub/VideoBox.cpp:294`, `:301`). When the general path fails, the user is told that "codecs or a splitter may be missing" (`HikariSub/VideoBox.cpp:330`). Only the general path starts playing on load and shows a volume slider. The FFMS2 path renders one still frame and hides the slider (`HikariSub/VideoBox.cpp:370-393`).

**What the general path buys today:**
- It opens instantly, with no indexing. FFMS2 has to index the whole file first, with a progress dialog, then write the index (`HikariSub/ProviderFFMS2.cpp:162`, `:300-320`).
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
- Playback runs off the audio clock. `RendererVideo::PlaybackClock` reads the position of HikariSub's own audio player and falls back to `timeGetTime` (`HikariSub/RendererVideo.cpp:1154-1164`). `NextPlaybackFrame` then skips frames that are more than 20 ms late (`HikariSub/Playback.cpp:18-67`).
- On Linux, a playback thread polls about every 4 ms and calls `FFMS_GetFrame` for each frame it presents (`HikariSub/RendererFFMS2.cpp:66-100`).
- Output is converted to BGRA by FFMS2's swscale (`HikariSub/ProviderFFMS2.cpp:384-387`).

**libass settings.** Every path shares one `SubtitlesLibass` provider. It calls `ass_set_fonts(..., "Arial", ..., 1, nullptr, true)` and `ass_set_frame_size(video w, h)`, and does not call `ass_set_storage_size` (`HikariSub/SubtitlesLibass.cpp:60-63`, `:118-119`). Whatever replaces the general path must render with this provider, or reproduce exactly this configuration.

**Build and licence facts.**
- The Windows FFmpeg is a prebuilt shared GPL build, 7.1.1 from gyan.dev. It is used because "A GPL build is required: HikariSub is GPL-3" (`Thirdparty/dependencies.json:83-88`).
- Linux links the distribution's FFmpeg, FFMS2, libass and GStreamer (`CMakeLists.txt:47-61`).
- The map makes replacing FFMS2 or libass out of scope ([#1](https://github.com/altqx/hikari/issues/1)).

### 2. Qt Multimedia with the FFmpeg backend

TODO

### 3. libmpv (render API)

TODO

### 4. A player built on FFmpeg directly

TODO

### 5. FFMS2 for everything

TODO

### 6. Comparison matrix

TODO

### 7. Is a separate playback path still justified?

TODO

## Open questions

TODO
