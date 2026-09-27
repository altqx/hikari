# Timing tools study

For [Prototype Time shift, timing postprocessor and keyframe workflows](https://github.com/altqx/hikari/issues/59). **Throwaway, memory-only, awaiting human reaction.** No media, subtitle file, configuration, keyframe file or external service is read/written. This does not settle the [draft and transaction policy](https://github.com/altqx/hikari/issues/43), C07 rejection behavior or an unlisted parser departure.

Open [timing-tools.html](timing-tools.html) directly, or run from the checkout:

```powershell
python -m http.server 8765 --directory docs/prototypes
```

Then visit `http://localhost:8765/timing-tools.html?variant=dock` or `?variant=bench`. No installation, build or persistence is required. Root owns browser QA, publication and tracker updates; this author performed source/model checks only.

## Concrete review

- **A / dock:** a compact right-hand persistent tool with mode tabs, scope/settings in a scrollable panel and a full-width preview below the Grid.
- **B / bench:** a bottom persistent workbench with target/scope, mode/settings and result columns together. The video-left, audio-above-editor-right and full-width Grid geometry remains the accepted Classic shell.
- Both retain settings/preview when switched. Both can hide/restore Timing, follow editing or pin a named inactive Document, inspect a protected reference and explicitly swap editing/reference roles. Floating/native docking is linked to the accepted workspace study; it is not reimplemented or qualified here.

Ask which presentation is easier during repeated shifts, whether every bulk operation should require Preview → Apply, and whether overlapping draft reconciliation is clear. The only runnable Apply route is previewed. Explicit draft commit/discard and one named multi-Line undo entry are **proposals**, not accepted transaction behavior. Direct Apply and automatic draft reconciliation are deliberately not simulated as defaults. There is no Enter-to-apply shortcut that could conflict with the accepted native editor/IME contract.

Tab reaches controls; F6 / Shift+F6 cycles core panels and Timing. Alt+P previews; Escape cancels a preview or dismisses the import dialog. Arrow keys switch variants outside interactive controls. Selection, current Line and marked Line are independent, labelled states. Keyboard shortcuts in this study are review aids, not an accepted shortcut registry.

## Reusable source audit and coverage

Read against baseline [20d647c4](https://github.com/altqx/hikari/tree/20d647c4c769ab7f5d383cf3c1c33f03876a94e9), the corrected [core inventory](https://github.com/altqx/hikari/blob/5d392a10c98b97c9b2b92f4b3f9dc49cd429752c/docs/research/core-inventory.md#3-timing-frames-keyframes-and-post-processing) and accepted time semantics. Static source observations are not runtime parity evidence.

| Current capability | Source-backed distinction | Study coverage / explicit limit |
| --- | --- | --- |
| Time shift scopes | All, selected, from **first selected**, start ≥ / ≤ first selected, named styles; non-dialogue rows excluded; hidden eligibility follows the filter override. Active and marked rows are different. [Algorithm][shift] | All six run. Added Current / From current are labelled convenience scope proposals, not renamed legacy scopes. Inspect-hidden changes presentation only. Non-ASS start-threshold/style gates are retained for sampled formats. Empty selection is diagnosed instead of reproducing the source's unsafe index path; that safer rejection needs disposition. |
| Offset, units, direction, endpoint | Milliseconds/frames, forward/back, both/start/end; start/end-only has confirmation; TMPlayer forces start-only. Exact timebase required for frames. [Controls][controls], [algorithm][shift] | Runs in bounded integer-ms fixtures, with visible confirmation through captured Preview → Apply. That confirmation design is proposed. ASS/SRT/MPL2/TMPlayer serialization projections run separately from stored times. MicroDVD original-frame/rate storage, plain text and full format conversion are gaps, not dropped capabilities. |
| Alignment plus offset | Marked Line start/end aligns to video or audio marker; the signed offset is additional. Video uses displayed frame and start/end representative. Millisecond negative alignment subtracts another 10 ms before truncation. [Algorithm][shift], [video conversion][video] | Runs with synthetic video/audio positions. Audio frame alignment subtracts target/anchor indices under approved T42-A. VFR starts include 0,40,81,120 ms. No decoder, audible alignment, accurate transport clock or sample-frame conversion is claimed. |
| Frame representatives and clamp | Frame shift maps endpoint with FrameAt, adds offset, applies StartTimeFor and centisecond truncation; both endpoint types use that start representative. Negative endpoints clamp independently. [Timebase][timebase], [algorithm][shift] | Model runs those representatives for bounded 25 fps, integer-projected 24000/1001 and authored VFR fixtures. A zero effective offset is a no-op. Production signed-64-bit microseconds, rational overflow, invalid PTS and full legacy extrapolation adapters are gaps. |
| Relative tag times | Characterized correction targets move/t/fad using frame-relative delays; translation can be preferred, values clamp at zero; not karaoke or fade. [Tag code][tags] | Checkbox remains discoverable but blocks Apply with an explicit gap. No fake tag rewrite. Scanner corner cases, duration adjustment, malformed/nested tags and translated-text precedence need native/core fixtures. |
| End correction | Leave, trim overlap, synthesize new times. New duration uses raw Original text length × configured ms/character, minimum 1000 ms, with first/overlap/last exceptions; path requires exact timebase. [Algorithm][shift] | Bounded sample logic preserves those exceptions, including first-row skip in New times. Synthetic Original text is the input; JS UTF-16 length does not qualify legacy wx Unicode counting on all platforms. |
| Postprocessor | Mode resets ordinary shift/alignment/end-correction. Affected rows sort by start/end. Order: signed lead-in/out → keys → continuity. [Algorithm][post] | All toggles and numeric values run on the affected subset; unaffected neighbors are not automatically added. Comments/translation marker semantics and every tie/equal-time case remain characterization gaps. |
| Keyframe snap | Four before/after start/end windows, centisecond-adjusted frame-start representatives, traversal/old-alignment/previous-end protection, strict resulting duration >600 ms. [Algorithm][post] | Bounded transcription, with preview endpoint reasons. The traversal is intentionally not replaced with generic nearest-keyframe behavior. It has not been compared against a running wx application or a complete tolerance/tie corpus. |
| Continuity | Positive gap ≤ sum of start/end thresholds, end-threshold-weighted share truncated to centiseconds; keyframe-found flags protect endpoints. [Algorithm][post] | Runs, including 100-ms gap with 40/60 split. No assertion of complete postprocessor parity follows from that case. |
| Shift profiles | Create/edit-overwrite/delete and select profiles; packed fields are scoped to ordinary shift mode. [Profiles][profiles] | Memory CRUD and overwrite confirmation; captures shift settings/scope, not targets, selection or postprocessing. No file import, legacy packed-string decoding, global option synchronization or profile-serialization parity. |
| Import/navigation | Aegisub v1, XviD, FFmpeg/avconv XviD logs, DivX, x264; missing media can defer; audio-only source uses 24000/1001; invalid input retains existing keys; next/previous strictly before/after and wrap. [Readers][keys], [integration][keymedia], [navigation][timebase] | Paste-only Aegisub integer fixture, preview/apply, missing-media deferral and strict wrapped navigation run. Other source families remain visible parser gaps. Audio-only fallback is a gap. Strict malformed-row rejection differs from old numeric coercion and is explicitly proposed. There is no claim of a generic external VFR-timecode importer. |

## Safety cases to drive

The six guided buttons reset to known cases and explain the next action: hidden scope/clamp; pending draft; inactive pinned target/protected inspection; VFR audio anchor; continuity/keyframes; and missing media/deferred import. Preview records target, revision, exact Line IDs and relevant parameters/context. Configuration, scope or media changes invalidate it. Apply checks captured target/revision/context again; protected content cannot receive Apply or Undo. Changing a media fixture never reinterprets authored times.

An overlapping draft blocks preview and remains pending. Commit draft explicitly creates a separate proposed history entry; Discard removes only the draft. Bulk Apply adds one proposed named entry; Undo changes only its target and refuses unresolved drafts. The HTML history is capped at 20 for study memory; **this is not a proposal to change the legacy 500-state product capacity**. History persistence, saved-point semantics, redo, a complete draft store and native editor input are outside scope.

Missing exact timebase, unsupported correction or reversed output blocks the whole sample operation before mutation. **C07 atomic rejection is still unapproved**; the old code can reject after earlier shifts. The model provides a concrete safe alternative for reaction, not evidence that the old algorithm was atomic.

## Bounded author checks

Both inline scripts compiled with Node. Direct calls to the pure model checked selected-hidden exclusion/inclusion, `[10,30) −20 → [0,10)`, T42-A's one-frame difference, unchanged inputs during preview, overlapping-draft blocking, missing-timebase blocking, format projections at 1009 ms, valid deferred import, malformed-import rejection, and continuity's 40/60 split.

After fixing zero-frame-offset quantization, ten further checks passed: zero-frame no-op; inactive frame controls ignored in postprocessor mode; non-ASS style-scope gate; pinned inactive Apply; target-only Undo; protected Apply rejection; stale revision rejection; changed format-context rejection; pending draft/content separation; and Cancel without history/content mutation. The state-handler checks used the actual JavaScript functions in a Node VM with rendering disabled, **not browser automation**. These are disposable model observations, not a formal test suite, browser interaction pass, native timing pass or compatibility certification.

No human preference is recorded yet. Full parity, native media/keyframe readers, settings persistence, accessibility and real screen-reader/IME behavior remain future implementation/verification work.

[controls]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L265-L476
[shift]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridBase.cpp#L420-L651
[post]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridBase.cpp#L653-L782
[timebase]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Timebase.cpp#L77-L165
[video]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoBox.cpp#L1688-L1694
[tags]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsDialogue.cpp#L1166-L1215
[profiles]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L751-L945
[keys]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/KeyframesLoader.cpp#L33-L97
[keymedia]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoBox.cpp#L1711-L1746

## Root browser observations

Reviewed both layouts at the default 1280-pixel viewport. Hidden-scope preview expanded from nine visible eligible Lines to eleven with two explicitly hidden Lines. A pending timing draft disabled Apply. The VFR audio-anchor fixture produced the approved one-frame offset, with legacy representative values displayed separately; Apply and target Undo worked. Explicitly pinning to Reference disabled Apply, while the combined pin/inspection walkthrough correctly retained its independent editable target. Missing timebase produced the explicit proposed whole-operation rejection. Postprocessor rows displayed keyframe/continuity reasons; valid pasted Aegisub indices replaced only the target resource context, without subtitle history. This is bounded browser evidence, not native timing/regex/parser parity. One off-viewport click after full-page capture needed keyboard activation; keyboard mode switching worked.
