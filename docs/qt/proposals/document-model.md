# Proposed subtitle document model

Draft for [Design the subtitle document model and format boundaries](https://github.com/altqx/hikari/issues/41). Uses accepted [vocabulary](../../../CONTEXT.md), [architecture](../architecture.md), [compatibility](../compatibility.md) and [isolated Lua boundary](../automation.md). **The three named product outcomes at the end are accepted; the broader record/interface design remains under review.** Evidence is static: [core](https://github.com/altqx/hikari/blob/5d392a10c98b97c9b2b92f4b3f9dc49cd429752c/docs/research/core-inventory.md), [corrected data](https://github.com/altqx/hikari/blob/1eea34cd5db8db276f2e71cdff3f43f345cefd5b/docs/research/data-inventory.md), [UI](https://github.com/altqx/hikari/blob/6f91fd28eab4c42d2c00f848606b47585c2b6bc8/docs/research/ui-inventory.md). No parser, fixture or round-trip result is claimed.

## Authoritative records

| Record | Proposed contents / invariant |
| --- | --- |
| Document | Identity, revision, ordered Sections containing ordered Records. Records distinguish Lines, Styles, Script properties and opaque/non-dialogue content. This order is authoritative; grid Line order is a projection, not a second store. |
| Line | Stable LineId; speech/sign/drawing/comment classification only where represented; original text, optional translated text, Unconfirmed, timing, style-name reference, actor, layer, margins and effect. Keep original versus absent/empty translated text distinct. Preserve raw ASS tags, commas and wraps; parsing caches never replace authored text. |
| Timing | Timed range, start-only or untimed representation, plus original frame/time lexemes and provenance. Consume the separate time/frame contract; do not invent end times, select FPS, round or clamp during structural modeling. |
| Style / Script property | Ordered records with stable identities, known typed fields and retained original spellings/unknown fields. Duplicate names/keys, missing style references and absent versus explicit values remain representable with diagnostics; never silently deduplicate or install defaults into authored data. |
| Line group / annotations | Description and contiguous member LineIds, without invented nesting. Explicit bookmark, Unconfirmed and imported visibility/group flags retain provenance. Collapse state is a view projection with a legacy persistence mapping. Group-breaking edits require defined command semantics, not implicit repair. |
| Source representation | Immutable input bytes, encoding/BOM/newlines, ordered raw spans, section/Format declarations, embedded Fonts/Graphics payload and source-to-record links. Untouched unknown content remains available even when no semantic decoder understands it. |

Speech/sign/drawing are user content, not a new mandatory classification field. ASS Dialogue/Comment is a format distinction. A raw non-dialogue record is not automatically an editable subtitle Line.

LineIds survive reorder, filtering and edits; duplication creates new IDs, deletion removes IDs, undo restores them. IDs are not row numbers or ASS serialization fields. Proposed IDs need only survive the open-document/undo lifetime; reload/recovery reconciliation remains explicit application design, not a legacy identity guarantee.

Application state owns selection/active/marker LineIds, Grid filters, edit drafts, targets, undo and view positions. Hidden Lines remain content; command scope explicitly supplies included IDs and hidden-line policy. Protected references cannot receive mutations. Commands change semantic records and invalidate affected raw spans/caches together; source spans never become competing mutable truth.

Application resource context owns media/keyframe/automation paths, provenance, resolved locations and missing-resource diagnostics. Preserve recognized ASS/session association fields on export; decoding handles/caches and Workspace layout are not document content. Styles belong to Documents; external Style/font catalogs retain separate readers. Existing session/autosave readers remain required; no new Project format or invented legacy autosave index.

## Interfaces and ownership

Conceptual interfaces, not production declarations:

- parse(bytes, formatHint, encodingChoice) → LoadResult(Document, source, associationHints, diagnostics).
- inspectConversion(snapshot, targetFormat, timePolicy, textRole) → SavePlan(output mapping, losses, diagnostics).
- encode(snapshot, approvedPlan) → bytes + report; application file service owns destination checks and atomic replacement.
- apply(documentId, expectedRevision, explicitScope, edits) → changed IDs / diagnostics through the application transaction boundary.

Diagnostics identify severity, byte/record/field position, raw excerpt, interpretation and recovery choice. Distinguish malformed syntax, unsupported input, ambiguous decoding and missing resources. Retain undecodable bytes; fallback text/default-document behavior must match characterized legacy rules until a named departure approves replacement. A save plan is revision-bound; changing data/options invalidates it.

Engineering recommendation: standard C++ value records, std::u8string for decoded UTF-8, byte vectors for original input, optional/variant for presence/kinds. No automatic Unicode normalization; invalid bytes stay in source storage. Qt Core conversion belongs at application adapters; QML/QObject pointers never enter core records. Container/ID encodings are internal choices, not new user behavior.

Lua IPC serializes versioned value snapshots with document/revision/run identities and explicit selection. The helper adapter exposes legacy row-indexed tables, fields and coercions; it never sees native pointers or raw C++/Qt layouts. Retain an index-to-ID mapping for each staged revision; validate returned edits against target/revision. Process isolation does not approve new script fields, marker interpretation or C06–C08 transaction semantics.

## Format boundary and review examples

Same-format saving and conversion are separate plans. Proposed unchanged same-format save can reuse exact bytes; changed records regenerate only affected spans where encoding/structure permits. Otherwise report normalization before writing. This is a proposed preservation contract, not an existing universal byte guarantee.

Adapters retain:

- ASS/SSA: ordered sections, embedded/unknown data, field declarations and legacy style distinctions; SSA→ASS is an explicit conversion, with losses reported.
- SRT: cue numbers, multiline payload, whitespace and malformed/raw records alongside decoded timing.
- MicroDVD: original frame integers and missing end; timing context is explicit.
- MPL2/TMPlayer: source precision/markup and start-only semantics; quantization/end synthesis belongs to approved format/time policy.
- Plain text: physical records and absent timing, without silently adding meaningful timestamps.

Recognize TLMode pairs, generated original styles, form-feed+D and Actor markers using characterized legacy precedence. Keep source spans/diagnostics for collisions or malformed pairs; do not assume markers combine independently.

Retaining Format declarations does not approve replacing legacy positional parsing. Changed interpretation, SSA normalization and other parser fixes require separately named outcomes; raw retention alone proves neither semantic nor byte-exact parity.

1. ASS contains an unknown section between Events sections. Editing one Line should retain that section and untouched payload under proposed **C03 preservation**. Existing loader can move colon records into Script Info.
2. One Line has original “Gate”, empty translation and Unconfirmed. Preserve all three; translated export states its fallback role. ASS→SRT preflight lists tag/style/group loss, row deletion/deduplication and synthesized timing separately (**C02**).
3. Session A has Audio; B omits it. Store each association's provenance; correcting B's inherited Audio requires **C05**, not a structural-model shortcut.

## Accepted product outcomes; model still under review

The user approved these three named outcomes on 2026-09-27. The [approved-departure ledger](../compatibility-decisions.md) records their scope. Old/new fixtures remain required; the surrounding record/interface design is still a proposal.

1. **C03 preservation:** retain unknown sections/unchanged raw content on save, replacing legacy structural loss.
2. **C02 loss handling:** use revision-bound conversion preview and explicit acceptance of listed losses, retaining the source Document. Individual conversion/parser fixes still need named outcomes.
3. **C05 omitted Audio:** resolve B from its own media context or none, never a preceding tab. Catalog/recovery behaviors remain unchanged pending their own dispositions.

The named outcome approvals do not approve every parser change or demonstrate byte/semantic round-trip correctness.
