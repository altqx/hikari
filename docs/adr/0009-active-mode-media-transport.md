---
status: accepted
---

# Let the active media mode own audio and clock

In the live review on 2026-09-27, altqx accepted general-player ownership of its audio and A/V clock while active, with editor output stopped and explicit stop/flush/generation invalidation during handoff. This retains FFMS2 indexed editing and cross-platform general playback without requiring both modes to share an external PCM sink; Qt Multimedia's FFmpeg player is the first general-player candidate, with capability and native timing gates still required. The [media contract](../qt/media.md) defines the ports, CPU-reference-to-scene-graph presentation direction, separate tools and optional Windows adapters without claiming a tested pipeline or deciding the pending core time semantics.
