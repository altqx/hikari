---
status: accepted
---

# Use PortAudio for editor output and QSG audio views

In the live review on 2026-09-27, altqx accepted PortAudio as the sole editor-output library, an audio-derived editor playback clock, QSG waveform geometry and cached texture tiles for the spectrogram. PortAudio's documented estimated DAC/stream timing supports the chosen clock role, while actual latency, synchronization, host routing and device behavior remain native verification obligations rather than proven benefits. The [media contract](../qt/media.md) separates PCM preparation, callbacks, analysis caches and presentation, and preserves exclusive active-mode ownership without silently adding fallback audio engines or choosing unresolved device policies.
