# Qt media transport and silent PortAudio continuation

Ticket: [Prototype native media presentation and transport handoff](https://github.com/altqx/hikari/issues/50).

This bounded functional continuation used the newly installed matching Qt Multimedia and PortAudio runtime. The [earlier missing-prerequisite report](../media-handoff/README.md) remains valid as of its historical run. This does not change that artifact or qualify the complete shipping media pipeline.

Open [report.html](report.html) for actual software-rendered frame captures and readable results. [observations.json](observations.json) contains track/buffer/frame events, fixture and native binary hashes, wheel URLs/hashes, loaded native module paths/hashes, callback times, generation transitions and remaining gates. [native-stderr.log](native-stderr.log) is the final run's Qt/FFmpeg diagnostic output. Probe-source hashes were not captured at run time; the publishing commit identifies the reviewed source, including the later description-only correction.

## Run and scope

From this checkout:

```powershell
./HikariSub/prototypes/media-transport/run.ps1
```

The launcher uses the already configured LOCALAPPDATA/HikariSub/prototype-runtime Python. It installs nothing. It runs offscreen, mutes every QMediaPlayer QAudioOutput and sets its application volume to zero, then opens **one output-only PortAudio stream containing zero-filled buffers** for roughly 0.45 seconds of scheduled device frames. It never opens a microphone, input or loopback stream and never changes OS volume.

Optional -Source points to the existing repository binaries; -Fixtures points to the earlier original CFR/VFR fixture directory. Otherwise the newest prior fixture pair is reused. On a clean checkout without those ignored fixtures, the original generator in media-handoff/run.py regenerates them inside this prototype's ignored _run directory, using the explicitly located repository FFmpeg executable. No PATH-selected decoder, download or external media is used.

The original 320×180, 100-frame CFR/VFR pixels, frame-ID stripes, tone/impulse PCM, vector ASS, SRT and chapter data are CC0-1.0 fixture content from the prior probe. Only generated media/raw frames/child logs go under ignored _run/UTC. Intended published files are the two Python probes, Presenter.qml, launcher, README, observations.json, report.html, native-stderr.log and .gitignore. No third-party binary is copied.

## Final observed run

Final run: **20260926T231007Z**, Windows 11 build 26200, CPython 3.12.14, PySide6 Essentials/Addons/shiboken6 6.11.2; Qt FFmpeg backend requested, Qt Quick software/offscreen presentation. The existing host remains outside the accepted calibrated release-performance reference machine.

| Responsibility | Observed result | Boundary |
| --- | --- | --- |
| Audio tracks | Qt enumerated and selected both tracks in each fixture. Six decoded buffers per selection yielded approximate zero-crossing identities 439.45 and 659.18 Hz, corresponding to authored 440/660-Hz tones. | Decoded PCM inspection with muted output, not acoustic measurement. |
| Embedded subtitle tracks | Both ASS and SRT tracks enumerated/selected; QVideoSink emitted the authored vector ASS payload and the distinct SRT sentence. | Packet/text selection proof; no Qt ASS drawing fidelity claim. Hikari-owned libass composition remains required. |
| Disable internal subtitles | Active subtitle track became -1; sink text cleared; no nonempty subtitle events appeared during the sampled caption interval, in either fixture. | Event-level suppression, not a complete visual duplicate-subtitle regression matrix. |
| CFR time seeks | First frames matched containing fixture identities for 1035, 1980, 20 and 3500 ms. The 1000-ms request first delivered frame 24 at 960 ms, followed by frame 25 at 1000 ms. | First arrival cannot simply acknowledge a seek; no generic exact-seek claim. |
| VFR time seeks | 1035 ms delivered frame 23 at 1040 ms, while the containing fixture frame is 22. Requests 1980, 20, 3500 and 1000 ms produced frame identities 43, 1, 75 and 22 at those starts. | QVideoFrame endTime equaled startTime for these VFR deliveries, so its interval cannot establish containment. |
| Indexed child | Separate FFMS2 5.1.0 processes again returned 100/100 sequential barcode identities and 10/10 reordered hash matches per fixture. Frame-25 owned BGRA bytes matched the child record. | Restricted existing x64 ABI/fixture evidence, not a full codec or platform certification. |
| Owned presentation | Qt-owned frame 25 survived source clearing and the child run. Its barcode agreed with FFMS2's frame 25. Actual QQuickWindow software captures showed requested frame 25, with a frameSwapped event. | CPU-owned image and software-scene acknowledgement; no GPU fence, scanout, color/alpha equivalence or visible desktop-window claim. |
| PortAudio | Default output “22E1W (NVIDIA High Definition A”, host API MME, stereo float32 at 44100 Hz; 78 callbacks × 256 frames = 19968 device frames. Every filled callback buffer compared equal to zero bytes. No reported output underflow. | One short output-only stream. This is not a WASAPI test, stress pass or audible playback proof. |
| PortAudio stop | CallbackStop, finished callback, stop return, inactive/stopped state and close were observed. | Physical device tail remains unmeasured. API-reported output latency was 0.203174603 s, not calibrated latency. |
| Chapters | FFprobe recovered authored Departure/Arrival chapters; QMediaPlayer has no chapter-named method in this runtime. | Chapter workflow still needs an adapter/metadata path. |

The small near-request sampling gate accepts a known-duration frame when end_us > target_us and start_us <= target_us + 100000; a wholly later interval can therefore pass. When duration is unknown it requires target_us <= start_us <= target_us + 100000. This is a near-window criterion, not a containment check. The emitted policy description was corrected after review; recorded measurements are unchanged. This is instrumentation, **not an approved product seek policy or timeout**. The report preserves first-arrival and subsequent-frame evidence separately.

## Actual handoff boundary exercised

For the **CFR 1-second / frame-25 case**:

1. Request QMediaPlayer stop, observe StoppedState, clear its source.
2. Invalidate the source generation and clock owner.
3. Release an intentionally delayed worker that hashes actual previously delivered owned Qt frame bytes; reject its completion by old generation. This controlled late-completion test is labeled, not presented as a naturally occurring decoder race.
4. Request FFMS2 in a separate process. Observe owned CPU decode completion, then separately present frame 25 through a QQuick image provider. Read the actual offscreen window capture's barcode before acknowledging software presentation.
5. Open the output-only silent PortAudio stream. Record callback currentTime/outputBufferDacTime, device frames and status. A logical one-second media anchor maps scheduled silence to callback times; no source PCM or resampling is played.
6. Observe PortAudio stop/close, invalidate its clock, create a new Qt source generation, receive/render frame 25, then resume the general player's clock while muted.

The VFR frame-25 operation is a separate ownership comparison at that frame's 1140-ms start, **not a same-position handoff from the last 1000-ms request**.

There is no explicit Qt device-buffer flush acknowledgement in this probe. StoppedState/source clearing are logical observations, not proof of a drained acoustic tail. Therefore the complete end-to-end stop/flush/presentation/acoustic handoff remains unqualified.

## Dependency identities and collision boundary

The parent-installed official PyPI wheels are recorded with exact URLs and SHA-256 values in observations.json:

- PySide6_Addons 6.11.2: f449ea4431da20e7b86752cca8d166f93434516fe417f981c27e5f8e1b554407
- sounddevice 0.5.6: 7f4162f514f007b0bf25a3ccfed3f1705bc2ec311888a90232729eec4f57a4f4
- cffi 2.1.1: f53e442b08449d42821fa4a4fba000095af9f62742a500f978a9f557ec44339a
- pycparser 3.0: b727414169a36b7d524c1c3e31839a521725078d7b2ff038656844266160a992

Loaded PortAudio reports “V19.7.0-devel, revision unknown”; its binary hash is ec080194f01e4095c7fb43dbd7ed05af922c5b34295056a9ff56782741d65481. Do not turn that version text into an invented upstream commit pin.

GetModuleFileNameExW records Qt's ffmpegmediaplugin and its actual PySide DLL origins, including the Windows app-virtualized LocalCache path. FFMS2's separate child records C:/Work/Kainote/x64/Debug origins. Both use names such as avcodec-61.dll, but their hashes differ: Qt avcodec begins 3823070d38dc; FFMS2's begins 0209f3620970. This experiment avoids their shared-process collision; it does **not** qualify coexistence or select production media process boundaries.

The default output route identifies MME and the NVIDIA-named endpoint. A contemporaneous PnP inventory includes NVIDIA audio driver 1.4.5.7, Realtek 6.0.8703.1 and USB audio 10.0.26100.9457. That inventory is not a traced endpoint-to-driver relationship.

## Candidate conclusion and remaining gates

Qt Multimedia is viable enough to continue as the general-player candidate for these track/seek/suppression operations. Its time seeks must remain distinct from FFMS2 indexed stepping. PortAudio opened a real default output device and supplied usable callback-clock fields in this short silent probe.

Still open: real OS-visible/GPU presentation, libass alpha/color/QSG integration, arbitrary media/long GOPs, exact decoder release/flush races, acoustically measured latency/tails/drift, resampling, source PCM playback, device loss/default changes/sleep-wake, hard-real-time callback design, Linux, accessibility, packaging/license qualification and the accepted performance workload. Callback logging/byte inspection allocates Python objects and is intentionally measurement scaffolding, not a production callback implementation.

API basis: [QAudioBufferOutput](https://doc.qt.io/qt-6/qaudiobufferoutput.html), [QVideoSink](https://doc.qt.io/qt-6/qvideosink.html), and [sounddevice raw output streams](https://python-sounddevice.readthedocs.io/en/0.5.6/api/raw-streams.html). Functional completion here does not close the broader native media prototype ticket.
