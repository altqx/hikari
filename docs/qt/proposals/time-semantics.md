# Time and frame semantics proposal

**Proposal for [Set time and frame semantics for editing and playback](https://github.com/altqx/hikari/issues/42), not accepted or implemented.** The [core inventory](https://github.com/altqx/hikari/blob/5d392a10c98b97c9b2b92f4b3f9dc49cd429752c/docs/research/core-inventory.md#3-timing-frames-keyframes-and-post-processing) and pinned source below establish legacy behavior; [compatibility policy](../compatibility.md) requires explicit approval of departures. [Video](video-pipeline.md) and [audio](audio-pipeline.md) remain proposals. Core owns deterministic time semantics; neither a renderer nor playback clock owns Document timing.

## Typed values and precision

Recommend signed 64-bit **microsecond DocumentTime and TimeDelta**, checked arithmetic across their full integer range, and distinct integral VideoFrameIndex and AudioSampleFrame types. An audio sample frame contains every channel at one sample position. No sentinel timestamp represents unknown/invalid data. Parsing and overflow return diagnostics; retain original malformed input for recovery. Wider integer storage does not promise microsecond source accuracy.

Preserve media PTS as integer ticks plus its declared rational timebase/unit and timeline origin; retain stream offsets and audio delay explicitly. Compare rational values before any rounding. Conversion to DocumentTime uses nearest microsecond, ties away from zero, reporting loss; display-only conversion never edits content. Keep source frame integers authoritative for frame-based formats. Fractions outside representable arithmetic bounds fail rather than wrap.

Each Document's media context owns a positive rational CFR rate or indexed VFR timing, with exact/estimated/unknown provenance. `24000/1001` remains that fraction, not `23.976` substituted silently. Attaching media does not reinterpret existing authored times without an explicit operation. MicroDVD rate choice is decision 2 below.

## Intervals and frame lookup

Use half-open intervals **[start,end)**: equal endpoints are empty, and an end belongs to the following interval. Preserve imported reversed/negative ranges with diagnostics; audition intersects the playable media range without editing the Document, while reversed ranges are invalid. Legacy editing's endpoint-at-zero clamp remains an explicit command policy: shifting `[10,30)` ms by −20 ms yields `[0,10)`, not an invented duration-preserving shift. Signed subtraction itself yields a signed delta.

For strictly increasing presentation starts `B[i]`, expose separate operations:

| Operation | Definition |
| --- | --- |
| `frameAtOrAfter(t)` | First index with `B[i] >= t`; first subtitle-visible sampled frame. |
| `frameContaining(t)` | Index with `B[i] <= t < B[i+1]`; seek/display frame. |
| Last subtitle-visible frame | Last index with `B[i] < end`; end is exclusive. |

CFR uses `B[i] = origin + i / fps` rationally. VFR preserves indexed presentation order and frame identity; never skip an invalid PTS and renumber subsequent frames. Missing, duplicate or decreasing timestamps require a diagnostic/estimated mapping, not an “exact” label. The last frame's end needs a known duration; extrapolation is explicitly estimated. Exact lookup outside known bounds returns out-of-range; navigation can separately clamp to playable frames. Legacy Lua extrapolation/conversion remains a compatibility adapter, not the exact decoder contract.

With starts `0,40,81,120` ms, `frameAtOrAfter(41)=2`, `frameContaining(41)=1`; `[40,81)` includes frame 1 only, and exactly 81 ms displays frame 2. At `24000/1001`, frame 1 starts at `1001/24` ms; retain that rational boundary.

## Editing, conversion and karaoke

Preserve the [legacy frame representatives](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Timebase.cpp#L109-L134) for GUI frame entry, snapping and unchanged Lua results: preceding/current midpoint +5 ms capped at current start; the inclusive end-frame control uses current/next midpoint +5 ms capped at next start. Characterize the legacy integer-millisecond projection separately from precise media PTS. At 25 fps, entering start frame 1 gives 25 ms, serialized as 20 ms in ASS, still first visible on frame 1. Replacing it with 40 ms would be an observable change.

Preserve format quantization: nonnegative ASS truncates to 10 ms, SRT to 1 ms, TMPlayer to seconds, MPL2 rounds upward to 100 ms; MicroDVD retains authored frame integers. Thus 1009 ms exports as ASS 1000, SRT 1009, TMPlayer 1000 and MPL2 1100 ms. Display formatting cannot silently quantize stored values. Report conversion loss and retain source data. If no representable timestamp preserves requested frame coverage, report the conflict rather than claim exactness: ASS cannot place a start inside `(0,8]` ms to first show frame 1 of an 8-ms-frame source.

Karaoke `k/K/kf/ko` values remain cumulative centisecond durations relative to Line start: at 1000 ms, `\k10\k20` yields boundaries 1100 and 1300 ms. Moving the entire Line preserves those durations. Optional relative-tag correction retains the characterized `move/t/fad` scope and zero clamp; it does not silently rewrite karaoke or `fade`.

## Commands and transport

Timing requests identify Document/revision, affected Line IDs/filter policy, start/end/both, signed milliseconds or frames, anchor and tag-correction mode. Frame shifts add an index offset to each endpoint's `frameAtOrAfter`, then apply legacy start representatives. Preserve millisecond alignment's centisecond truncation and negative adjustment pending separate approval.

Postprocessing retains affected-row ordering, lead-in/out → keyframes → continuity. Inputs include separate before/after windows and continuity thresholds in milliseconds; outputs identify changed endpoints and reasons. Preserve keyframe candidate traversal/protection and strict `duration >600 ms`. Continuity closes eligible positive gaps using the existing threshold-weighted, centisecond-truncated split: a 100-ms gap with end/start thresholds 40/60 extends the preceding end by 40 ms, absent keyframe protection. Equality/tie/selected-neighbor cases require fixtures; this does not approve C07's separate late-rejection fix.

FFMS2 receives a validated frame index and generation; exact requests acknowledge that same index or error. Recommend [FFMS_SEEK_NORMAL](https://github.com/altqx/ffms2/blob/45d5f72100d88c52acdd54bfedcc0315a44c735d/doc/ffms2-api.md#ffms_seekmode) for exact requests; unsafe mode can guess without error. Playback reports estimated position, clock domain, generation and uncertainty; it may drop frames. Source-audio `[a,b)` plays exactly `b-a` frames before silence; retain legacy nonnegative time-to-sample truncation. At 44.1 kHz, 1–2 ms maps to `[44,88)`. Device latency and resampling tails need separate measurement.

## Three explicit decisions

1. **C01 equality:** recommend correcting equal-time `>=`/`<=` from false to true; audit callers and retain old/new fixtures. This does not approve all C01 changes.
2. **C01 FPS isolation:** replace shared 23.976/default-25 fallback state with an explicit per-Document rational MicroDVD rate; preserve raw frames while unknown. Recommend this departure.
3. **T42-A audio frame alignment:** old code maps `target−anchor` as a timestamp; recommend subtracting their frame indices. For the VFR example, anchor 40 → target 81 ms changes offset from 2 frames to 1.

Required fixtures cover these decisions, serialization, negative/overflow boundaries, short/invalid VFR intervals, two Documents with different rates, karaoke, unchanged Lua conversions, exact seeks and source-sample ranges. No parity or timing-accuracy pass is claimed.
