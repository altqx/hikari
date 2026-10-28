# Audio timing and karaoke study

Throwaway prototype for [#53](https://github.com/altqx/hikari/issues/53), prepared 2026-09-27. Open [audio-timing.html](audio-timing.html) directly in a browser; no server, dependency installation, account or network access is needed. All edits live in memory and disappear on reload. Root handles publication and browser QA; this note does not record a human verdict.

## Question and alternatives

How should Audio expose audition, draft timing and karaoke without losing the familiar Editing workspace? All alternatives retain the accepted Compact Studio palette and Classic composition: video left at approximately 44% of the upper workspace, Audio upper right, Editor below Audio, full-width grid at the bottom. Narrow screens use a single column. These are Audio hierarchy choices, not another theme or workspace-layout vote.

| URL option | Arrangement | Tradeoff to review |
| --- | --- | --- |
| `?variant=A` (default) | Local strips: transport, plot, draft timing, view controls | Most familiar adjacency; several horizontal control groups. |
| `?variant=B` | Dedicated audition rail beside the plot and timing | Stable playback controls cost waveform width. |
| `?variant=C` | Line / Syllable task buttons and editable values before the plot | Editing intent leads; waveform becomes feedback below the values. |

The floating switcher updates the URL and preserves sample state. Arrow keys switch variants only on the page background or switcher, avoiding timing and field-editing shortcuts. Dark, light and contrast tokens are included; this is not a native high-contrast or accessibility verification.

## Walkthrough

1. Start on Editing Line 2, `We sail away.`, committed at `[1800,3800)` ms. Drag the plot background to make an **audition selection**. Audition animates a silent cursor through that requested window; the grid does not change.
2. Drag the yellow S / E handle or enter `1785` in Start ms. The yellow draft and dashed committed boundary differ. Commit updates just that Line in the grid; Undo restores its committed state and pending draft. Revert discards the active Line's draft. Commit + next commits and advances. Navigating away preserves drafts by stable sample Line ID.
3. Expand **Selection, markers, gain and timing options**. Edit both selection endpoints numerically, then use Selection → draft or Draft → selection explicitly. Try the 500-ms and marker windows. An audition request is clipped to the synthetic media's `[0,12000)` ms interval without rewriting Document timing; a reversed or empty request does not play. Try a negative numeric selection to see requested versus playable ranges in Live timing state.
4. Enable keyframe or other-Line snapping and choose the active edge. Snap finds the nearest eligible visible candidate; dragging snaps within the proposed radius. Numeric entry stays exact. At synthetic 25 fps, K45 is drawn at 1800 ms and its Audio boundary representative is 1785 ms. The resulting draft value and snap reason are visible.
5. Change horizontal zoom and scroll; Go to selection recenters it. Switch between waveform and illustrative spectrum; adjust amplitude and the linked volume state. Move the synthetic video marker and copy it to the Audio marker. These controls manipulate drawn/sample state, not decoded media.
6. Enable Karaoke. Select `sail `, split after character 2, adjust Duration cs, and Join next. The original sample exercises `k`, `K`, `kf` and `ko`. Split retains the left tag and inserts `k` on the right; join retains the left tag, concatenates text and preserves the later boundary. Dragging an interior syllable boundary redistributes its two adjacent durations. Numeric duration editing shifts subsequent boundaries. Neither operation silently changes the Line end.
7. Move the Line start and inspect the ASS preview after Commit: centisecond durations remain relative to that start. A karaoke total extending beyond the Line end produces a visible note rather than automatic rescaling. On an untagged Line, Rebuild from words supplies a whitespace-only example; it deliberately does not claim the legacy syllabifier.
8. Change Audio's Tool target to the protected Reference. Its own Line position and audition selection are available, but boundary, lead, commit, tag and split/join writes are disabled and guarded. The Editor, preview and grid still name the Editing document. Return to Editing: its retained draft reappears.

F6 / Shift+F6 traverses Video → Audio → Editor → Grid. Ordinary Tab follows DOM order; in B and C the visual rearrangement makes that order a specific review concern. Numeric fields replace timing drags. In Audio outside inputs, the documented legacy-style letters cover playback, Line/syllable navigation, commit, scrolling and lead values. Focused timeline arrows nudge the active draft edge by 10 ms (Shift: 1 ms); inputs keep browser text/number behavior. Ctrl/Alt/Meta combinations are not intercepted. This is a small prototype shortcut map, not the accepted production command router.

## Capability coverage

| Existing capability | This artifact | Still required; no removal proposed |
| --- | --- | --- |
| Waveform / spectrum, ruler, selection, current/other Lines, keyframes, video/Audio markers | Deterministic invented waveform and spectrum pattern; ruler and separate overlays; visibility toggles | Real PCM, FFT/window/hop behavior, source channels, speech-frequency transform and cache/QSG rendering. |
| Selected range / syllable, whole Line, first/last/before/after 500 ms, before/after marker, to end, stop | Silent range animation; 500-ms windows use current selection/syllable; configurable marker-window length; clipped half-open ranges | Audible output, source/device sample conversion, clock estimation, tails, latency, drift, buffering and reverse/variable-rate gestures. Native transport #50 is separate. |
| Timing selection, numeric endpoints, lead-in/out, snapping, commit and advance | Working draft operations, committed grid, one-step undo, per-Line retained drafts | Production undo groups, multi-Line commands, selected/hidden row policy, full frame/time entry and malformed/import recovery. |
| Horizontal zoom/scroll, vertical stretch, gain/volume link, speech emphasis | Zoom and scrolling affect the drawing; amplitude affects waveform; link updates volume state; spectrum emphasis changes the invented pattern | Real gain behavior, clipping/limiting, channel layout, display precision at sample zoom, live playback cursor-follow policy. |
| Auto-scroll, auto-commit, advance after commit, audition on change, auto-focus | Explicit toggles and functional sample rules | Exact legacy gesture/configuration parity and accepted defaults. “Follow active Line” is not continuous playback auto-scroll. |
| Karaoke parsing/timing/split/join and automatic creation | Typed sample tags, syllable selection, numeric duration, draggable shared boundaries, split/join, simple whitespace rebuild | Full ASS parsing, override-block/escape preservation, grapheme/RTL editing, legacy vowel/punctuation heuristics, merge-every-N, split-mode gestures and empty/tag-only behavior. |
| Open/close audio, from video, dummy audio, tracks and resource context | Fixed labelled synthetic source only | All actual media-loading/resource actions, cancellation, error recovery, delay, indexing and device policy remain in scope. |

The fixed sample Editor is read-only; the grid selects a Line but does not implement full painted-grid selection or translation mode. Absence from this small study is not approval to merge or drop a capability.

## Deliberate proposals and boundaries

- **Selection versus draft:** brushing creates an audition selection, while S/E handles edit the draft. Explicit copy buttons connect them. That separation, retaining per-Line drafts on navigation, and restoring a draft after Undo are review proposals, not claims that every old gesture behaves this way.
- **Commit events:** Auto-commit applies only after a completed valid mutation, never each pointer movement, and never auto-advances. Explicit Commit follows its Advance toggle; Commit + next always advances. A failed split does not commit unrelated pending work. Current defaults are conservative sample defaults, not a migration-settings decision.
- **Snapping:** the sample uses a 10-screen-pixel drag radius, earlier-time tie choice, Shift bypass, visible source scope and a separate nearest-candidate Snap command. These rules need review. Legacy Audio toggles both snap settings with Shift and considers its configured visible-Line shading scope. Its `GetBoundarySnap` uses `StartTimeFor` for **both** Audio edges; this is distinct from `EndTimeFor` in an inclusive end-frame entry control. The sample therefore uses `max(0, frame*40ms−15ms)` for both Audio edges at 25 fps. It is not a general VFR or no-video implementation.
- **Karaoke navigation and values:** previous/next traverses syllables and crosses Line boundaries while Karaoke is enabled, independently of the audition-on-change toggle. That regularized interaction is proposed; the old previous-navigation branch depends on its play flag. Numeric duration shifts later boundaries; dragging redistributes adjacent durations; neither rescales to the Line. Character positions use JavaScript code points, not the legacy override-aware parser or a native grapheme editor. Rebuild from words intentionally replaces only this sample's karaoke draft and is clearly labelled.
- **Time:** bounded integer microseconds are safe in these short JavaScript fixtures; production requires checked signed 64-bit typed values. Numeric timing permits ±60 seconds in this study, preserves negative draft values, and rejects reversed ranges at commit. This is not an importer and cannot establish lossless handling of existing malformed content. No ASS time serialization or quantization runs here. Karaoke durations are integer centiseconds.
- **Transport:** `requestAnimationFrame` provides a visual simulation only. There is no audio element, Web Audio, PortAudio, decoder, real spectrum or native scene graph. “Volume” is intentionally state-only. The accepted FFMS2/PortAudio/audio-derived clock/QSG direction remains unchanged. No measurement or sandbox claim follows from this page.

## Evidence and checks

Sources used:

- [Completed UI inventory](https://github.com/altqx/hikari/blob/6f91fd28eab4c42d2c00f848606b47585c2b6bc8/docs/research/ui-inventory.md), especially AudioBox, AudioDisplay and audio command/default-shortcut tables.
- [Completed audio research](https://github.com/altqx/hikari/blob/7e1aad3ba4f7ac217b9bb467a297035bc0639abd/docs/research/audio.md) and [core inventory](https://github.com/altqx/hikari/blob/5d392a10c98b97c9b2b92f4b3f9dc49cd429752c/docs/research/core-inventory.md).
- Pinned baseline [audition windows and marker setting](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L384-L454), [Audio snapping](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioDisplay.cpp#L2489-L2568), [karaoke navigation](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioDisplay.cpp#L2759-L2824), and [split/join](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/KaraokeSplitting.cpp#L221-L257).
- Accepted 2026-09-27 [media contract](https://github.com/altqx/hikari/blob/qt/docs/qt/media.md), [typed time contract](https://github.com/altqx/hikari/blob/qt/docs/qt/time-semantics.md) and [visual language](https://github.com/altqx/hikari/blob/qt/docs/qt/ux/visual-language.md). These branch links identify the canonical living specification, not an immutable implementation certificate.

Executed during preparation: inline JavaScript syntax compilation, duplicate-ID inspection, and an ad hoc Node VM run of the actual sample model with minimal DOM stubs. Observed current-Line commit, one-step undo/draft restoration, protected-reference write guards, split/join recovery, preserved relative karaoke duration on Line movement, K45's 1785-ms representative for both Audio edges, negative-range audition clipping without rewrite, selection-bounded first-500-ms playback, reversed-range rejection, syllable navigation, variant state retention and rejected-split auto-commit guard. These checks are not a test suite and do not verify browser geometry, actual pointer capture/focus, screen readers or native behavior. Browser review at 1366×900 / 1536×768 and narrow layout, plus human reaction, remains open.

Root browser checks subsequently exercised numeric timing commit and undo/draft restoration, karaoke split/join, protected-reference write guards, variant state retention and F6 from Audio to Editor. All three arrangements were visually inspected and captured at the default 1280-wide viewport; an 800-wide check stacked the layout without horizontal document overflow. Pointer-drag behavior, every shortcut, native input/accessibility and the specified target-desktop sizes remain unverified. Human reaction remains open.

All media illustration, sample text and generated patterns were written for this prototype. No media/font binary or external icon pack is embedded; text glyph controls are placeholders. No file, network or persistent-storage writes occur.

## Human review

1. Which hierarchy is easiest to time repeatedly: A's strips, B's audition rail or C's task-first controls? Which exact controls should move?
2. Keep the distinct audition selection and draft Line boundaries, or couple them more closely? Judge this together with the explicit protected target state.
3. Accept or change the proposed auto-commit/advance rules and karaoke duration interactions? Native transport accuracy and performance are separate evidence gates, not answered by liking this mockup.
