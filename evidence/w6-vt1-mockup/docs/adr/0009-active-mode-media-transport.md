---
status: accepted
---

# Let the active media mode own audio and clock

In the live review on 2026-09-27, altqx accepted general-player ownership of its audio and A/V clock while active, with editor output stopped and explicit stop/flush/generation invalidation during handoff. This retains FFMS2 indexed editing and cross-platform general playback without requiring both modes to share an external PCM sink; Qt Multimedia's FFmpeg player is the first general-player candidate, with capability and native timing gates still required. The [media contract](../qt/media.md) defines the ports, CPU-reference-to-scene-graph presentation direction, separate tools and optional Windows adapters without claiming a tested pipeline or deciding the pending core time semantics.

## Addendum — later time acceptance on 2026-09-27

Altqx subsequently accepted the full [typed-time and frame contract](../qt/time-semantics.md), recorded in [ADR 0014](0014-typed-time-and-frame-semantics.md), including distinct Document/media/sample values, interval and frame lookup operations, retained conversion rules and the named timing departures. Core timing is therefore no longer pending, while transport clock ownership remains this ADR's separate responsibility: player positions cannot redefine authored Document times. Exact native seeks, presentation acknowledgements, output flushing/device tails and calibrated timing still require evidence; later bounded Windows observations in the [media contract](../qt/media.md#lifecycle-and-verification-obligations) do not qualify the complete pipeline or select a production media-process boundary.

## Addendum — backend acceptance on 2026-09-27

The subsequent [backend decision](0016-backend-values-and-isolated-ffms2.md) selects qualifying isolated FFMS2 first, with Qt general playback in the application process. This selects the architectural direction that the earlier experiment left open; transfer, recovery, packaging, performance and output-quiescence evidence still must pass.

## Addendum — full pipeline proposal accepted on 2026-09-27

[Altqx approved the current video proposal](https://github.com/altqx/hikari/issues/25#issuecomment-5853349062): retain FFMS2 indexed decoding, qualify Qt Multimedia first for general playback, start from owned CPU frames and a CPU-BGRA/libass reference, then verify the Qt Quick scene-graph presenter with separate visual overlays. Optional DirectShow and xy-VSFilter/CSRI adapters stay behind the cross-platform ports. This settles the pipeline architecture; the existing native media follow-up retains seek acknowledgement, chapter/track parity, acoustic handoff, GPU/color and supported-platform qualification gates.
