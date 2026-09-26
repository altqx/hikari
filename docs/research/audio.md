# Audio output, scrubbing and waveform/spectrum rendering in Qt Quick

Research for altqx/hikari#13 (slug `audio`). Draft in progress.

## Summary

_TBD_

## Implications for the decision

_TBD_

## Detailed findings

### 1. Today's behaviour in HikariSub

All paths below are relative to `HikariSub/` at commit `20d647c4`.

**Decode and caches.** Audio is decoded by FFMS2 and resampled to interleaved
signed 16-bit (`FFMS_FMT_S16`), stereo kept for playback
(`ProviderFFMS2.cpp:465-469`); the waveform and spectrum read a mono downmix
(`ProviderFFMS2.cpp:751-756`). The decoded PCM lives in RAM or in a disk cache
file under `AudioCache` (`ProviderFFMS2.cpp:87`, `ProviderFFMS2.cpp:812-864`).
Because the whole track is decoded up front, any sample can be fetched at random
with `GetBuffer`/`GetPlaybackBuffer` (`Provider.h:63-67`). That property is what
makes sample-accurate play ranges and scrubbing cheap in any output backend: the
backend only has to pull PCM from a callback.

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

**Position clock.** `AudioPosition::Frame()` computes the audible frame as
"frames written minus bytes the play cursor has not reached", from
`IDirectSoundBuffer::GetCurrentPosition`
(`AudioPlayerDSound.cpp:800-815`). It is shared (via `shared_ptr`) with the video
renderer, which slaves its clock to it: `RendererVideo::PlaybackClock()` returns
the audio frame in ms when audio is playing and falls back to `timeGetTime()`
otherwise (`RendererVideo.cpp:1153-1163`); seeks call
`AudioPosition::Restart()` so the clock holds until new data is written
(`RendererFFMS2.cpp:607-610`, `AudioPlayerDSound.h:37-43`). **So audio is already
the master clock for A/V sync on Windows**, and any replacement must expose an
equivalent "frame audible now" query.

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

**Scrubbing.** There is no scrubbing: a search for "scrub" across `HikariSub/`
finds nothing. Playback is always "play a range" (`AudioDisplay::Play`,
`AudioDisplay.cpp:1551-1590`).

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

**What the rewrite must keep:** random-access PCM from a decoded cache;
`Play(start, count)` in frames with a sample-accurate end; an audio-derived
"frame audible now" clock that the video renderer uses as master; a 60 Hz
cursor that does not repaint the waveform; waveform peaks and a cached
spectrum. **What it must add:** device selection and hot-plug, a real clock on
Linux and macOS, scrubbing, and a window function on the FFT.

### 2. Audio output backends

_TBD_

### 3. Waveform and spectrum rendering in Qt Quick

_TBD_

### 4. How Audacity 4 draws clips and waveforms

_TBD_

## Open questions

_TBD_
