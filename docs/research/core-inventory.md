# Core behaviour inventory

Research for [altqx/hikari#5](https://github.com/altqx/hikari/issues/5), completed 2026-09-27. Source baseline: [`20d647c4c769ab7f5d383cf3c1c33f03876a94e9`](https://github.com/altqx/hikari/tree/20d647c4c769ab7f5d383cf3c1c33f03876a94e9). The existing research checkout has identical `HikariSub/` and `tests/` contents to this baseline. Source links below are immutable.

## Summary and evidence boundary

The compatibility surface includes formats and timing, translation pairs, comments/non-dialogue records, style/header metadata, hidden/tree states, selection-aware commands, history/save boundaries, exact versus estimated frame timing, renderer-specific output and recoverable media caches. These interacting behaviours do not prescribe preserving the old class structure. [Loader][loader] [Dialogue][dialogue] [History][history] [Timing][timebase] [Providers][manager]

This is a **source inventory and characterization plan**. It does not establish a new format specification, select architecture, accept parity exclusions, or claim an executed compatibility suite. The 63 existing headless cases were inventoried, not rerun for this documentation-only task. No wx application, real subtitle corpus, font installation, MKV extraction or libass/CSRI image comparison was executed.

- **Observed implementation:** directly supported by pinned code; unusual behaviour may still require a runtime fixture to establish its impact.
- **Existing test contract:** an assertion present in `tests/`, within that test's inputs.
- **Proposed characterization/classification:** suggested fixtures and candidate defect treatment for [parity decision #38](https://github.com/altqx/hikari/issues/38). None of the proposed exclusions is accepted by this report.

The [completed data inventory](https://github.com/altqx/hikari/blob/1eea34cd5db8db276f2e71cdff3f43f345cefd5b/docs/research/data-inventory.md) owns the full persisted field/option registries. This report owns algorithms and observable outcomes. Related integration research: [Qt video #10](https://github.com/altqx/hikari/issues/10), [general playback #11](https://github.com/altqx/hikari/issues/11), [visual overlays #12](https://github.com/altqx/hikari/issues/12), [audio #13](https://github.com/altqx/hikari/issues/13).

## 1. Subtitle formats, encoding and round trips

### Dispatch and encoding

`SubsLoader` clears the grid and tries extension-dependent parsers. ASS/SSA tries ASS→SRT→text; SRT tries SRT→ASS→text; other extensions initially load linewise and may be reparsed. Success is largely “at least one row”, not complete syntax validity. Failure creates default ASS content and an error message, rather than an empty document. Original format is tracked separately from working format. [Loader][loader]

`OpenWrite::FileOpen(test=true)` checks a UTF-8 BOM, validates non-ASCII UTF-8, then uses uchardet and `wxCSConv`, falling back to the local converter if detection fails. The validator rejects overlong/surrogate/out-of-range/truncated UTF-8 but returns false for ASCII-only content, which follows detection instead. `test=false` bypasses this detection. Empty decoded content counts as failure. `FileWrite(utf=true)` explicitly writes UTF-8 with BOM; false uses local conversion. Subtitle saving uses `PartFileWrite`, which prepends a BOM but leaves the converter implicit. Exact output bytes need fixtures on each supported wx build; there is no blanket original-encoding preservation contract. [Encoding][encoding]

### Format matrix

| Format | Source-observed load/save rules | Important round-trip boundary |
| --- | --- | --- |
| ASS | LF-tokenized, left-trimmed lines; fixed positional comma fields; remaining commas belong to text. Actor/effect/text are trimmed. `Format:` declarations ignored. `Marked=` accepted in first field. Serialization emits canonical layer/start/end/style/actor/margins/effect/text and CRLF, with Dialogue/Comment prefix. | Byte whitespace, field order and section structure are not retained. ASS times truncate to centiseconds. Event-area semicolon records can be hidden non-dialogue rows which emit their raw text. [Loader][loader] [Dialogue][dialogue] [Times][substime] |
| SSA→ASS | V4 style section selects legacy parser; no SSA working format. Tertiary colour discarded; back colour supplies outline/back; underline/strikeout false, scales 100, spacing/angle 0, AlphaLevel skipped. Alignment 9/10/11→4/5/6 and 5/6/7→7/8/9. | Writes ASS representation, not SSA byte round trips or all legacy SSA semantics. [Styles][styles] [Loader][loader] |
| SRT | Exact ` --> ` delimiter, multiline aggregation, end trimming, heuristic removal of a trailing numeric line as the next cue number. Newlines become `\N` internally; timestamps are milliseconds. Save generates row-based numbers, CRLF text lines and blank separators. | Original numbering, blank lines and whitespace are lost; numeric-only text, missing numbers, malformed separators and timestamp settings need fixtures. [SRT][srt] [Dialogue][dialogue] [Save][save] |
| MicroDVD | Regex `{signed-digits}{optional-signed-digits}text`; empty end becomes zero. Retains original frame integers and milliseconds; pipes remain until conversion. Emits original frame fields. | Parse initially uses 23.976 fps; later recalculation uses supplied fps; editing uses shared static FPS; serialization has a 25 fps fallback when frame is zero but time is not. Frame round trip and time-based edit differ. [Regexes][formatregex] [Times][substime] |
| MPL2 | `[signed-digits][optional-signed-digits]text`; tenths stored as milliseconds. Emits `ceil(ms/100)`. | Non-grid times round upward. Slash italics/pipe wraps belong to conversion helpers, not a general markup interpreter. [Dialogue][dialogue] [Times][substime] [Conversion][convert] |
| TMPlayer | Start-only hours/minutes/seconds, accepting colon/semicolon time separators and colon/semicolon/comma/space before text. | Emits `HH:MM:SS:text`, losing subseconds. Missing end is synthesized during conversion, not an authored zero-duration cue. [Regexes][formatregex] [Times][substime] [Conversion][convert] |
| Plain/unrecognized text | Nonempty physical lines become rows; a single brace-delimited line may become a hidden non-dialogue comment. | May open under default ASS metadata; diagnostics/raw-content treatment require an explicit parity choice. [Loader][loader] [Dialogue][dialogue] |

ASS Fonts/Graphics sections are preserved as embedded text and appended on save. Other unknown sections are not structurally preserved: non-comment colon records can become ScriptInfo while section headings disappear. There is no general section AST or dedicated Aegisub Extradata/Project Garbage writer in this path. Container `FFMS_GetSubtitleExtradata` means codec-private header data, not that Aegisub section. Missing/None YCbCr Matrix normalizes to TV.601 on ASS load. [Loader][loader] [Save][save] [Demux][demux]

Translation mode consumes original/translated event pairs when TLMode is Yes and the original uses TLMode Style. Resulting rows retain Text/TextTl; original effect form-feed+D marks doubtful translation. Saving can emit pairs, comment the original, or export translated text; several services prefer nonempty translation and otherwise original. Bookmark/hidden/visible-block/tree states use Actor prefixes. The parser's marker chain is `if/else-if`, so combinations need fixtures rather than assumed independent round trips. [Loader][loader] [Dialogue][dialogue] [Save][save]

Conversion is lossy: ASS→SRT retains limited bold/italic/underline toggles, changes hard spaces, strips blocks and can empty drawing rows. Text→ASS maps selected MicroDVD font/size/colour/style tags, slash italics and pipe wraps; SRT conversion handles lowercase i/b/u/br markup. Grid conversion can delete comments, synthesize duration as `max(1000, configured_ms_per_character * raw_text_length)`, trim the preceding end to next start, sort start/end/locale text, delete duplicate timing+text rows and empty converted rows. These command side effects need classification alongside syntax. [Helpers][convert] [Grid conversion][gridconvert]

**Proposed F01–F07:** reordered ASS Format fields/comma text/whitespace/malformed records; SSA alignment/colour table; each format-pair conversion with an explicit loss set; SRT numeric-only payload/missing numbers/empty lines/settings; MicroDVD missing end and fps changes across two documents; UTF-8 BOM/no-BOM, ASCII, UTF-16, legacy/invalid encodings and empty file; translation EOF/malformed pairs, combined Actor markers, embedded/unknown sections. Record decoded model, diagnostics and serialized bytes separately.

## 2. ASS editing semantics, LineParse and karaoke

### Editing scanner versus renderer interpreter

`Dialogue::ParseTags` scans brace blocks for caller-requested tag-name prefixes, records source positions and optionally emits plain/pvector spans. It accepts numeric sign/decimal prefixes and textual fn; parenthesized values end at the first closing parenthesis. Drawing is tracked when p is in the requested set: p0 disables, other accepted values enable. This is not a full nested transform grammar or a libass-equivalent evaluator. Raw text remains the serialization source; ordinary text getters prefer nonempty translation. [Tag scanner][tags] [Text helpers][texthelpers]

Tag stripping uses a brace regex; first-block stripping uses another scanner. Merging special-cases c/1c and fr/frz then uses regex/name matching. Tagged extent measurement applies a selected tag set to a style, not the renderer's complete shaping pipeline. `ChangeTimes` adjusts relative move/t/fad arguments, clamps changed numbers at zero, and edits the preferred text field; fade and karaoke tags are not in its set. Nested transforms, partial argument forms and replacement offsets therefore need targeted fixtures. Preserving unknown segments is a recommendation, not an accepted core design. [Helpers][texthelpers] [Style application][styleapply] [Relative times][tagtimes]

`LineParse.cpp` wraps TextData; real bracket/text analysis is in `SpellChecker::CheckTextAndBrackets`. It skips override/HTML-like blocks and drawing spans, recognizes ASS N/n wraps and h hard spaces or text-format pipes, and maps plain text back to source offsets (including compressed grid tag placeholders). Bracket/doubled-slash diagnostics are heuristic. Boost.Locale boundaries determine letter/number spans. Options include/exclude spaces and punctuation for CPS/wrap lengths. Warnings flag a wrap over 43 characters or more than two wraps. Results cache until cleared. CPS divides character count by duration in seconds, converts to integer and afterwards clamps negative/over-999 results to 999. Zero duration is not prevalidated. [LineParse][lineparse] [Analysis][spellanalysis]

### Karaoke

Existing k/K/kf/ko tags are read as centisecond durations accumulated from line start, preserving surrounding text via a limited regex/scanner. Translation is preferred. Untagged text uses whitespace splitting or a Latin-vowel/n/punctuation heuristic with “merge every n”; this is not universal linguistic syllabification. Initial boundaries share duration; intermediate boundaries truncate to centiseconds, final boundary uses the line end. Join combines text and takes the later boundary. Split inserts a centisecond midpoint and a new k tag. Serialization computes durations from boundaries relative to current audio start and merges into an existing leading override block where possible. [Karaoke][karaoke]

**Proposed A01–A05:** source offsets across tags/supplementary Unicode/combining marks/RTL; p→p0→text and nested t/rStyle/colour/alpha/aliases/repeated/unknown/malformed tags; relative-time supported/unsupported forms; zero/negative/long-duration CPS and long text, wrap options and a word interrupted by tags; tagged/untagged karaoke, empty/tag-only text, punctuation, zero/negative durations, leading text, join/split serialization. Keep source preservation and diagnostics separate from rendered pixels.

## 3. Timing, frames, keyframes and post-processing

### Timing models

`SubsTime` holds integer milliseconds, format and original frame number. ASS truncates to 10 ms; SRT to 1 ms; TMPlayer to 1 s; MPL2 rounds upward to tenths; MicroDVD serializes frame integers. NewTime/Change clamp negative time and update MicroDVD frames with ceil; frame operations clamp independently. Subtraction clamps negative components instead of preserving a signed delta. ASS conversion uses `ZEROIT(a)=(a/10)*10`. Shared static MicroDVD fps and inconsistent fallback values are legacy observations, not a recommended new model. [SubsTime][substime] [Macro][zerodef]

`Timebase` models integer frame-start timestamps or CFR and distinguishes exact/estimated timing. **FrameAt(t)** means first frame starting at/after t; **FrameShownAt(t)** means displayed frame at t. MsAt truncates CFR float time to integer ms and extrapolates beyond known frames; ClampFrame is separate. StartTimeFor uses previous/current midpoint+5 ms capped at current start; EndTimeFor uses current/next midpoint+5 capped at next start. Common-fps tests assert centisecond truncation retains the intended frame. VFR/high-fps tests check raw timing and do not prove every sub-10-ms interval survives ASS serialization. PlayEndBefore yields the last frame-start before an end boundary; huge calculations saturate at INT_MAX. [Timebase][timebase] [Tests][timetests]

FFMS2 supplies integer timestamps from indexed PTS and keyframes in ms; missing frame info is skipped. SetKeyframes sorts/deduplicates; next/previous are strictly after/before and wrap, or -1 when empty. CFR dummy video uses FromFps. “Exact” identifies the declared provider model, not independent decoder certification. FromTimecodes assumes ordered usable input and is not a validated external-file parser. No independent VFR timecode-file importer was found in the inspected source; Automation reports an empty timecodes_file. [Indexed timing][ffmstiming] [Dummy][dummy] [Timebase][timebase] [Automation][automationpaths]

Keyframe import recognizes Aegisub v1, XviD/FFmpeg/avconv XviD logs, DivX and x264 headers, mapping frame positions through active timebase. Without video/audio the path is deferred; audio-only assumes 24000/1001 fps. Invalid imports keep existing keyframes. Header normalization, nonnumeric lines, DivX scanning and reordered x264 frames need fixtures; header recognition is not proof of all producer compatibility. [Importer][keyloader] [Integration][videotiming]

Playback helpers distinguish start/end seeking, clamp actual seeks, retain only the latest pending request with its flags, stop at end/last frame and skip when sufficiently late. Queue tests use a synthetic decoder; they do not certify FFMS2 seek accuracy or AV sync. [Playback tests][playtests] [Queue tests][queuetests]

### Shift and post-processor order

`SubsGrid::ChangeTimes` owns the algorithm; ShiftTimes mostly loads controls/settings. Scope: all, selected, from first-selected row, start-time at/after or at/before first selection, or named styles. Hidden rows are skipped unless ignoreFiltered; non-dialogue records skipped. It shifts both/start/end, forward/backward, ms/frames, or aligns marked-row start/end to video/audio. Frames require exact timebase. Millisecond alignment uses centisecond truncation with an extra negative 10 ms adjustment; SubsTime clamps below zero. TMPlayer forces start-only. Optional tag correction compares frame-relative delays before/after and uses the limited ChangeTimes helper. [Shift][shift]

Post-processing requires enable bit 16; features are lead-in (1), lead-out (2), continuity (4), keyframes (8). It resets ordinary shifts/alignment and processes affected rows sorted by start/end, without automatically including unaffected neighbours. Lead-in/out apply first. Keyframe candidates use centisecond-adjusted frame-start boundaries and before/after windows; chosen boundaries require resulting duration >600 ms. Already aligned boundaries and previous-line overlaps get special handling. Continuity then shares a positive gap no larger than the sum of thresholds: previous end receives a threshold-weighted centisecond-truncated extension, current start may follow, subject to keyframe protection. [Post-processor][postprocess]

End correction trims overlaps to the next affected cue or synthesizes raw-text-length duration with a 1000 ms minimum and first/overlap/last exceptions. The shared correction/post-processing path requires exact video even for lead-in/out or duration correction, and earlier per-row shifts can happen before rejection. This warrants transactional characterization, not acceptance of the constraint. [Shift][shift] [Correction][postprocess]

**Proposed T01–T06:** preserve approved existing helper assertions; cross format×fps/VFR×start/end/both×positive/negative offsets; equal/zero times, overflow, unknown fps, missing/duplicate/nonmonotonic timestamps; every selection/style/hidden scope; marked versus first-selected and video/audio anchors; post-processor ordering, threshold equality, 600 ms edge, overlap/equal-start and selected-only neighbours. No-video/unsupported operations need a defined whole-document result and undo boundary. Include external keyframe BOM/CRLF/malformed inputs.

## 4. Undo/history

UndoHistory owns snapshots and current/saved indices. Recording after undo drops redo and can forget saved state; rewind permanently drops future; pruning retains first state and shifts current/saved, possibly forgetting saved. Amend requires the latest, noninitial, unsaved step. These have existing direct tests. [Helper][undoh] [Tests][undotests]

File snapshots contain ordered dialogue/style/ScriptInfo pointer vectors, selection source indices, active/marker/scroll and filter flag. Unchanged objects are shared; edits copy rows/styles/metadata and retain ownership lists. Dialogue copies include times/fields/translation/comment/state/tree/visibility, not parse cache. Embedded ASS sections live outside File snapshots on SubsFile. Decoder state, waveform cache, settings, clipboard and user dictionary are not in the snapshot. [Shape][fileh] [Copy/ownership][history] [Dialogue copy][tags]

SetModified records when changes are marked and saves selection/view state. Consecutive automatic typing commits merge only for line-edition commands on the same active row at the latest unsaved step. Explicit commands, save, undo/redo and ended typing runs interrupt that eligibility. The cap is 500 states, retaining first plus recent states. This is neither per-keystroke history nor universal time-window coalescing. Named commands cover multirow edits, sort/conversion, audio/visual edits, scripts, search, filters and trees. EndLoad(initialSave=true) forgets saved state for extracted/backup-like content despite its name. [SaveUndo][history] [Commit][modified] [Typing][typing] [Load][endload]

Selection/view state can update the existing historical step without a new document command. OpenCloseTree directly mutates rows; tree creation/filter commands use copies/history. “Undo snapshots every visible state” is consequently too broad. SubsFile Undo/Redo return false on success, unlike the helper; that API accident is not a user-visible requirement. [View state][saveselections] [Trees][trees] [History][history]

**Proposed U01–U04:** typing→save→typing; typing→row change/explicit command→undo; one bulk versus many edits; saved-marker loss after branch; >500 states with correct rows/styles/header; translation/filter/tree undo and active/marker/scroll; direct expansion across history; cancellation/dummy edit without dirty/redo corruption. Test old ownership separately from user-visible semantics; no need to reproduce raw-pointer design.

## 5. Indexing, caches and subtitle rendering

FFMS2 indexes video/chosen audio, selecting among track names/languages. Index path is executable-local Indices/basename_audioTrack.ffindex. Reuse checks file existence, newer source mtime, readability and IndexBelongsToFile; failure reindexes with progress/cancel. Index write failure can leave a usable memory index. Source creation uses configured seek mode, so “FFMS2” alone does not guarantee every seek mode is exact. [Setup][ffmssetup] [Index][ffmsindex]

Video normally becomes pitch-aware BGRA; Windows GPU conversion may use NV12; explicit frame retrieval temporarily uses BGRA. Colour matrix/range and unspecified-metadata fallback matter. Audio is S16 mono/stereo aligned to first video track, with stereo playback and downmixed visualization. Configured delay is applied during RAM/disk cache building. The .w64 file is raw PCM, not Wave64. Its filename includes basename, audio track, channel count and delay in sample frames: `basename_trackN_Cch_D.w64`. Ordinary channel/delay changes therefore use different keys. It omits full source-path/content identity, and reuse of an existing cache when no new index was built does not verify expected length. New cache uses .part, promoted only if completed, otherwise removed on destruction. [Decode][ffmsdecode] [Timing/audio][ffmstiming] [Cache][ffmscache] [Completion][ffmsdestroy]

Manager chooses libass for literal configured libass, otherwise CSRI/VSFilter; empty selection defaults libass off Windows. Manager instances are separate but renderer/library handles are shared/static. Supports whole/dummy scripts, style strings, parameters and preparation. [Manager][manager]

libass reads UTF-8 ASS/SSA memory, sets frame size and renders integer ms. Ordered monochrome masks blend into BGRA using inverted ASS transparency and mask coverage. Existing tests permit at most two byte values versus reference blend and cover zero/opaque masks, stride and varied widths. Overlay change detection clears old bounds, unions dirty regions and invalidates across renderer/tab/size changes; disappearance/animation must survive optimization. Adapter accepts RGB32/ARGB32; its “only works with FFMS2” message describes current integration, not libass generally. [libass][libass] [Blend tests][blendtests]

CSRI opens UTF-8 scripts, selects configured/default renderer, negotiates format and renders seconds. Negative stride handles inverted buffers. Generic overlay clears/redraws whole surface without libass change detection. Format fallback and shared-thread/tab access need real integration fixtures; no source oracle proves providers pixel-identical. [CSRI][csri]

Both adapters cache parsed script objects by text hash in an LRU with default capacity 3, protect the active object, and prepare latest pending text on a worker joined at shutdown. Existing tests cover lookup/LRU/protection, not hash collision, native parser lifetime or reload races; hash hits do not compare original text. [Cache][parsed] [Workers][csri] [Reload][libass] [Tests][parsedtests]

**Proposed R01–R05:** CFR/VFR/B-frame/nonzero-PTS media and track delays; missing/corrupt/stale/cancelled/unwritable index; same basenames and changed audio settings, partial/truncated cache, RAM/disk equivalence; representative ASS rendered with pinned fonts/provider version/resolution/matrix/exact time, including no-subtitle/drawing/transforms/clipping/karaoke/RTL/fallback; tab/script/provider/size switches and stop/seek/reload during preparation. Pixel tolerance and renderer-specific baselines need decision. No performance or AV equivalence was measured here.

## 6. Other core services

| Service | Observed implementation | Proposed fixtures |
| --- | --- | --- |
| Spellchecker | Hunspell .aff/.dic, default en_US, dictionary encoding, missing-file disable; user additions/removals clear cached results. Boost.Locale letter words checked, numbers contribute metrics; Arabic/Hebrew may reverse previously converted RTL words. Cache clears beyond100k entries. Tags inside words retain source-offset ranges. [Dictionary][spell] [Analysis][spellanalysis] | S01: unmatched files, encoding/Unicode paths, user add/remove/reload, tag-interrupted and RTL/combining words; inspect highlighted ranges and replacement, not only spell result. |
| Misspell rules | Enabled ordered wx advanced regex with optional case-insensitivity; current/all tabs and all/selected/from-selected/style scopes; prefers translation. Force lower/upper/unchanged or source-case inference. Text/tag restrictions inspect brace context at match start. Invalid regex skipped; checked results reject changed text. Form-feed rule fields persist separately from enabled flags. [Rules][rules] | S02: dependent length-changing rules, captures/overlap/zero-width, boundary-crossing matches, Unicode case, stale results, every scope, translations. Source inconsistencies below need reproduction. |
| Find/replace | Text/style/actor/effect; literals or wx advanced regex/captures; anchors, case, selected/all/from-selected/style, comments, hidden policy, tabs/files/folder filters. Brace context is not complete ASS parsing. Translation TXT may concatenate original+newline+translation and split back. Find-all advances one source position (overlap possible); replace-all uses a different advancement. [Options][searchoptions] [Engine][search] [Bulk][bulkscope] [Columns][textcolumns] | S03: overlaps, empty/zero-width, anchors, tags crossed, length-changing case fold, stale results, original/translation separator, hidden selection, file backup/error/cancel. |
| Font collector | Collects style fonts and fn/b/i/p runs, skips comments/vector spans; records chars after wrap/hard-space conversion, reports missing/unused faces/glyphs, matches actual font bytes/size incl TTC. Windows system/user/external paths; Linux fontconfig paths and compatibility font APIs. Check/copy one/all tabs, UTF-8 ZIP. Reset r absent from requested tags, so not full renderer font resolution. [Runs][fontparse] [Files/glyphs][fontfiles] | S04: style reset, inline weight/italic, unused/translated text, fallback/missing glyphs, CJK/RTL/emoji, collections/duplicate names, system/user fonts, Unicode ZIP, partial failures; compare collection to actual renderer use. |
| MKV subtitles/fonts | Project FFMS2 extensions; text codecs ass/ssa/subrip/srt/text offered, bitmap tracks omitted. Header/styles from codec-private data. ASS/SSA packet Start/Duration +5ms then centisecond truncation; ReadOrder removed, callback order retained. SRT ms preserved. Cancel clears gathered packets. Four explicit font MIME types; names strip path components, invalid UTF-8 falls back Latin-1, empty names synthesized. [Demux][demux] [Caller][extract] | S05: one/multiple/cancel tracks, no-text media, packet ordering/commas/large times, private headers, Unicode/invalid-byte/traversal/duplicate names, short writes/disk full/ZIP. |
| Grid filtering/trees | Key=source vector index; id=visible-row position, not immutable identity. Mapping invalidates on document/visibility. Default selection read omits hidden; stable sorting with selected-only slot permutation. Filters use style/selection short circuits plus remaining dialogue/doubtful/untranslated bits, invert/add modes. VISIBLE_BLOCK exposes hidden blocks. Tree=description comment+contiguous child rows; selected runs get descriptors; toggle affects command/render visibility. [Mapping][rowmap] [Selection/sort][selections] [Filter][filters] [Trees][trees] | S06: filter truth tables/inversion/add/remove/reveal, zero visible rows/hidden selections/sort-delete-insert identity, tree at EOF/single child/adjacent trees/filter inside tree/save-load-undo. Do not confuse old visible id with prototype stable IDs. |

Muxing launches an external muxer asynchronously with argument vectors, a fixed track-order template and attachments. Process start is not mux completion, and the template does not model every container layout. Treat argument construction and completion/error reporting as orchestration fixtures. [Mux][mux]

## 7. Existing tests and gaps

Standalone C++20 target defines one CTest executable without wxWidgets/window. There are **63 TEST declarations** across eight files; the table is source inventory, not a run result. [CMake][testbuild]

| File / count | Assertions covered | Boundary |
| --- | --- | --- |
| [TimebaseTests][timetests] /15 | Empty, ceil/display frame, CFR/timecode, round trip/extrapolation/clamp, centisecond common-fps, VFR/short-frame, first/play-end/keyframes/saturation/derived fps/exactness | No SubsTime/parser/grid shift integration |
| [UndoHistoryTests][undotests] /13 | Record/undo/redo/rewind, save distance/forgotten marker, prune/clear/free/amend/ownership merge | Synthetic values, not dialogue sharing or UI typing |
| [PlaybackTests][playtests] /11 | Seek start/end/clamp, latest request flags, end, due/late/skip bounds | No AV drift/device/decoder measurement |
| [FrameQueueTests][queuetests] /7 | Order/stale/reset/failure/EOF/stop/restart | Synthetic decoder, not concurrency soak or FFMS fidelity |
| [WaveformPeaksTests][peaktests] /5 | Block extrema, chunk equivalence, touched blocks, partial tail/outside | Conservative whole-block query, not arbitrary exact-window peak/FFT/device |
| [SemVerTests][semvertests] /5 | Parse prefix/prerelease, rejection, ordering, build metadata, declared version | No transport/installer/config migration |
| [AssBlendTests][blendtests] /4 | Reference tolerance, zero/opaque mask, padded stride and widths | No shaping/fonts/rasterizer/full overlay |
| [ParsedScriptsTests][parsedtests] /3 | Hash lookup, LRU and active object protection | No collision/worker/native reload |

For a later implementation task: `cmake -S tests -B <build-dir>`, `cmake --build <build-dir> --config Release`, `ctest --test-dir <build-dir> -C Release --output-on-failure`. CMake/compiler were not found on this research shell's PATH; no dependency installation was needed for the source inventory.

## 8. Candidate defects: proposed classification for #38

Source contradictions below are evidence; runtime consequences need fixtures where stated. No item is an accepted parity exclusion. First preserve old-result evidence, then encode a separately approved desired result. Do not automatically make crashes, loss or platform-dependent accidents the golden oracle.

| Evidence | Trigger/contradiction | Proposed investigation/classification |
| --- | --- | --- |
| Source-proven [SubsTime][substime] | >= uses >, <= uses <; equality false | Equal-time fixture; propose correct comparisons and audit dependent callers |
| Source-proven [conversion][convert] | Within type==SRT, type==MDVD/MPL2 branches are unreachable | Markup fixtures; propose source-format checks |
| Source-proven broad match [conversion][convert] | Any p+digits match empties text, including p0 or mixed text/drawing | Preserve non-drawing runs/report loss, pending decision |
| Source-proven assignment [grid conversion][gridconvert] | Empty Y resolution writes resX=720 and leaves Y empty | Empty/partial settings fixture; propose corrected Y default |
| Source-proven order [karaoke][karaoke] | Division by syllable count precedes empty fallback | Empty/tag-only fixture/sanitizer; runtime result not reproduced |
| Source-proven bounds assumption [dictionary][spell] | .dic loop indexes .aff at same index without count/pair validation | Unequal/reordered lists; propose basename pairing |
| Source-proven mismatch [text helpers][texthelpers] | First-block fallback selects txt but later substrings empty text parameter | Default-argument fixture; propose consistent selected source |
| Source-proven mismatch [rules][rules] | Next rule matches original lineText but writes stringChanged offsets; from-selected value2 absent in replace eligibility | Two length-changing rules/scope2 fixtures; propose sequential semantics/full predicate |
| Source-proven scanner [keyframes][keyloader] | DivX loop searches literal IPB repeatedly, not each character; Aegisub converts every subsequent line numerically | Producer-shaped/fps/malformed fixtures; propose validated records |
| Source-proven unchecked input [CPS][lineparse] | Divide before zero-duration check; count stored short | Zero/negative/long text fixtures; define bounded/unavailable result |
| Source-derived risk [shift][shift] | No-selection path still uses first-selection row; late exact-timebase rejection after mutation; audio frame alignment converts a time delta rather than subtracting frame indices | No-selection/no-video/VFR fixtures; propose validated transactional outcome |
| Source-derived loss [loader][loader] [dialogue][dialogue] | Unknown sections, combined markers, malformed translation and SRT number heuristic | Corpus evidence; decide preserve/warn/normalize |
| Source-derived risk [audio cache][ffmscache] [parsed cache][parsed] | Audio key includes basename/track/channels/delay but lacks full source identity and length validation; script hash without content check | Same-basename/stale/truncated fixtures; propose identity validation |
| Source-derived state risk [trees][trees] [history][history] | Shared row direct mutations; unsigned EOF sentinel compared <0 | EOF/undo/sanitizer fixtures; define transient versus document ownership |
| Source-derived capability gap [tags][tags] [fonts][fontparse] | Partial scanners used for measurements/font discovery/edits, omitting reset/nested forms | Renderer-backed examples; explicit parser capability boundaries |

This is a targeted inventory, not a comprehensive bug hunt. None of these runtime failures was reproduced during this task. Research closure does not imply fixes or accepted behavioural changes.

## 9. Follow-on decisions and vocabulary

Proposed glossary terms: document; dialogue/event versus non-dialogue record; original/translated text; comment/style/ScriptInfo/embedded section; source key/visible position/stable identity; selected/active/marked row; tree/visibility; history step/saved state; milliseconds/frame index/frame-start time; exact/estimated timebase; keyframe; subtitle overlay; source index versus decoded audio cache. “Id”, “frame time”, “extradata” and “save” are overloaded today and should be defined before crossing new module boundaries. [Shape][fileh] [Timebase][timebase] [Demux][demux]

Recommendations, not decisions:

1. #38 should classify format losses, metadata extensions, rounding and each candidate defect: preserve, deliberately change, or defer. Attach raw fixtures and user examples.
2. Separate pure document/time/rule command fixtures from font/renderer/device integration. Existing helpers are useful tested seams, not mandatory implementation.
3. Define atomic edit/cancel/save outcomes; compute/validate a batch before commit. This is proposed design work.
4. Pin renderer versions/fonts/media hashes/timestamps/platform for parity comparisons; keep source, visual, interaction and performance evidence distinct.
5. Use QML/HTML prototype tickets for human interaction choices. User policy remains Figma Starter and occasional handoff. This research adds no Figma dependency and accepts no prototype without reaction.

The inventory question is answered. Remaining work is implementing/characterizing the fixture matrix, accepting parity choices and choosing interfaces from them—not treating unmeasured source behaviour as a passed compatibility result.

## Pinned primary sources

[loader]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsLoader.cpp#L27-L170
[srt]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsLoader.cpp#L172-L243
[encoding]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OpennWrite.cpp#L57-L220
[dialogue]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsDialogue.cpp#L698-L877
[formatregex]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsDialogue.cpp#L664-L673
[styles]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/styles.cpp#L285-L434
[styleapply]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/styles.cpp#L197-L246
[substime]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsTime.cpp#L21-L230
[zerodef]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsDialogue.h#L19-L21
[save]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridBase.cpp#L308-L405
[convert]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsDialogue.cpp#L935-L1053
[gridconvert]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridBase.cpp#L187-L306
[texthelpers]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsDialogue.cpp#L216-L421
[textcolumns]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsDialogue.cpp#L636-L662
[tags]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsDialogue.cpp#L1055-L1164
[tagtimes]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsDialogue.cpp#L1166-L1211
[lineparse]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/LineParse.cpp#L24-L53
[spell]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SpellChecker.cpp#L86-L264
[spellanalysis]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SpellChecker.cpp#L266-L514
[karaoke]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/KaraokeSplitting.cpp#L41-L286
[timebase]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Timebase.cpp#L23-L165
[dummy]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ProviderDummy.cpp#L95-L98
[automationpaths]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Automation.cpp#L483-L496
[keyloader]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/KeyframesLoader.cpp#L33-L97
[videotiming]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoBox.cpp#L1663-L1742
[shift]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridBase.cpp#L417-L607
[postprocess]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridBase.cpp#L608-L783
[undoh]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/UndoHistory.h
[fileh]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsFile.h#L27-L164
[history]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsFile.cpp#L62-L337
[modified]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridBase.cpp#L1125-L1165
[typing]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1562-L1576
[endload]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsFile.cpp#L615-L633
[saveselections]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsFile.cpp#L952-L966
[rowmap]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsFile.cpp#L354-L392
[selections]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsFile.cpp#L470-L591
[trees]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsFile.cpp#L699-L746
[filters]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridFiltering.cpp#L35-L264
[manager]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubtitlesProviderManager.cpp
[libass]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubtitlesLibass.cpp
[csri]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubtitlesVSFilter.cpp
[parsed]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ParsedScripts.h
[ffmssetup]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ProviderFFMS2.cpp#L162-L269
[ffmsindex]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ProviderFFMS2.cpp#L270-L345
[ffmsdecode]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ProviderFFMS2.cpp#L363-L423
[ffmstiming]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ProviderFFMS2.cpp#L424-L492
[ffmsdestroy]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ProviderFFMS2.cpp#L495-L547
[ffmscache]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ProviderFFMS2.cpp#L550-L869
[rules]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/MisspellReplacer.cpp#L477-L769
[search]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/findreplace.cpp#L825-L997
[bulkscope]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/findreplace.cpp#L1062-L1167
[searchoptions]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/findreplace.cpp#L1273-L1479
[fontparse]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCollector.cpp#L572-L695
[fontfiles]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCollector.cpp#L706-L1244
[extract]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCollector.cpp#L913-L1038
[mux]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCollector.cpp#L1246-L1283
[demux]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Demux.cpp#L34-L315
[testbuild]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/tests/CMakeLists.txt
[timetests]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/tests/TimebaseTests.cpp
[undotests]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/tests/UndoHistoryTests.cpp
[playtests]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/tests/PlaybackTests.cpp
[queuetests]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/tests/FrameQueueTests.cpp
[peaktests]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/tests/WaveformPeaksTests.cpp
[semvertests]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/tests/SemVerTests.cpp
[blendtests]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/tests/AssBlendTests.cpp
[parsedtests]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/tests/ParsedScriptsTests.cpp
