# Audio output and waveform proposal

**Status: proposal for [Choose the audio output backend and waveform renderer](https://github.com/altqx/hikari/issues/26); no audio engine or renderer has been accepted.** This draws on [Audio output, scrubbing and waveform/spectrum rendering in Qt Quick](https://github.com/altqx/hikari/blob/7e1aad3ba4f7ac217b9bb467a297035bc0639abd/docs/research/audio.md), not new benchmarks.

## Decision requested

Recommend **PortAudio as the sole editor-output library**, an audio-derived presentation clock for indexed editing playback, **QSG geometry for the visible waveform**, and **cached texture tiles for the spectrogram**. HikariSub owns transport, cache and presentation policy behind its application ports. FFMS2 remains the decoder; libass remains the subtitle renderer. Target Windows 10/11 and Linux first, preserving a possible later macOS adapter.

The minimum human choice is whether to accept this bundle or prefer Qt-only output despite its weaker documented presentation-clock contract. Buffer targets, scrub gestures and analysis settings can then be settled against explicit acceptance criteria; none is silently fixed by this proposal.

## Output choice and tradeoffs

PortAudio documents estimated callback-buffer DAC time and stream time in the same clock domain. That is the strongest timing contract identified by the research for locating the source frame expected to be audible. It is **not a measured accuracy or latency guarantee**.

| Alternative | Main benefit | Cost for this requirement |
| --- | --- | --- |
| Qt `QAudioSink` | Qt device notifications and integration | `processedUSecs()` is not a documented DAC timestamp; the callback path requires Qt 6.11, so cannot be assumed under an older baseline. |
| miniaudio / RtAudio | Compact PCM integration and broad host support | Respective engine/stream clocks do not establish an equivalent portable per-buffer DAC timestamp; timing uncertainty still needs a solution. |
| SDL3 | Device events and desktop audio-server support | Additional event integration and a clock strategy; queued bytes do not identify audible frames. |

For the first PortAudio verification, use WASAPI on Windows and an explicitly configured ALSA desktop-compatibility route on Linux. Confirm that route in supported Linux packages: native PipeWire, compatibility routing and direct hardware ALSA are different support claims. The researched PortAudio release lacks master's PulseAudio backend. Pin the chosen revision/host APIs in the build decision; do not promise master-only features or silently add Qt/SDL fallback output engines. Device loss stops playback with an explanation. Initial policy should retain an explicitly selected device and require an explicit reopen after loss/default-device change.

## Clock and transport ownership

One application transport owns the active mode, playback generation, source range and clock validity. Under the [video proposal](video-pipeline.md), the general player owns its audio/clock only while that mode is active; the editor PortAudio output is stopped. Switching modes transfers ownership explicitly. If one output sink is required for both modes instead, revise both proposals together before acceptance. The output adapter supplies timestamp-backed estimates and their uncertainty; video scheduling and the audio cursor consume that shared clock. Coordinate this contract with [Choose the video decode, playback and presentation pipeline](https://github.com/altqx/hikari/issues/25), including who outputs sound during general playback, so two pipelines never independently own playback timing.

Keep source sample frames, resampled device frames and monotonic time distinct. Invalidate old queued generations/clock estimates on seek, underrun or device replacement; hold or stop presentation rather than extrapolate stale audio indefinitely. Use a monotonic clock when playback intentionally has no audio. Preserve exact source-frame audition ranges and zero-fill after their end, while measuring unavoidable device-buffer tails. Reverse/variable-speed scrubbing and click-suppression fades require separate explicit semantics.

## Cache and view responsibilities

FFMS2 decode/cache workers prepare PCM and prefetch disk data. Audio callbacks consume ready buffers only: no disk access, blocking work, allocation or QML calls. Worker-built multiresolution min/max peaks serve overview zoom; close zoom uses appropriate raw samples. Spectrum workers compute bounded magnitude tiles keyed by media identity, channel/rate and FFT/window/hop settings; palette changes should reuse magnitudes where possible.

The presenter draws only the visible waveform geometry and spectrum tiles. Separate cursor, selection, keyframes and karaoke handles share one time-to-x transform; cursor motion does not rebuild static analysis. Bound CPU/GPU caches and reject stale worker results. Supply numerical/keyboard marker editing and accessible range/state controls. FFT window/hop choices remain reviewable analysis changes, not presumed legacy parity.

## Evidence required after a choice

Under the still-open [Set measurable performance and resource budgets for the rewrite](https://github.com/altqx/hikari/issues/36) and [Choose the test strategy for the rewrite](https://github.com/altqx/hikari/issues/34), verify loopback onset/stop tails/drift, resampling, repeated audition, underrun, device loss and sleep/wake; then measure long-track zoom/scroll, cache growth and drag response on both platforms. No latency, A/V sync, rendering performance, memory or native accessibility pass is claimed.
