# Translation and protected comparison study

Throwaway prototype for [Prototype translation mode and protected comparison navigation](https://github.com/altqx/hikari/issues/49), using the accepted Classic shell. Open [translation-comparison.html](translation-comparison.html) directly, or run `python -m http.server 8765 --directory docs/prototypes` from this checkout and visit `/translation-comparison.html`.

Use `?variant=stacked` or `?variant=parallel`. The bottom switcher updates the URL. No packages, network requests, persistence, real media, system clipboard or file writes are used.

## Fixed foundation

Compact Studio styling; Classic menus and local controls; video upper left, audio above editor on the right, full-width editing grid below; shared layout; protected comparison; optional Home; follow-editing with optional pin. Timing, Translation and Typesetting optional presets are accepted; this study does not reopen membership or change the Editing default. Movable/floating placement remains in the [workspace study](workspace-tools.html?variant=B).

## Alternatives for reaction

| Variant | Translation editor | Protected reference |
| --- | --- | --- |
| Stacked / tray | Original above translated text with local commit/transfer controls | Full-width tray below editing grid; independent reference list |
| Paired / side | Original and translated text in parallel desktop columns | Right-side reference reader/list above the full-width editing grid |

Narrow layouts stack fields for readability; this is not a third product decision.

## Working sample behavior

Three Documents contain 12, 10 and 13 synthetic Japanese, Arabic, Thai, French and English rows. References omit some rows; an overlapping interval gives multiple time matches. Each Document retains its active row, selection, visibility, mode, original protection/visibility, drafts and one-operation undo.

- Translation mode and comparison are independent controls. Normal mode shows Original text as a **proposal**, retaining translation. Legacy translation mode affects serialization as well as display; this sample does not implement TLMode metadata.
- Original starts read-only in Translation mode. Explicit unlocking affects only the Editing document. Copy the whole current Original field or its selected text into the translation draft. Hide/show retains it.
- Tag transfer copies leading brace override blocks to the draft prefix. This is not a complete ASS parser or the full legacy tag-movement workflow.
- Drafts remain per Line through navigation, layout/document changes and role swaps. Commit writes both pending text fields without changing Unconfirmed. Commit + confirm also clears Unconfirmed. Toggle Unconfirmed and Mark Unconfirmed + next line are separate operations. Previous/next Unconfirmed and next untranslated use committed values, respect the editing visibility filter and do not wrap in this proposal.
- Undo restores the last sample content/flag operation in Editing and its prior draft buffer. This is not a history engine; production coalescing/granularity remain unsettled.
- Independent reference navigation keeps its own active row. Linked navigation is one-way from Editing: simple half-open interval overlap; optional visible, selected, same-style and chosen-style criteria combine with AND. Time off exposes all eligible candidates. Arrows/list choose matches. No match shows an empty state without nearest-row substitution.
- Linked display retains the independent position. Reference selection/visibility change view state only. Copy stages the chosen reference text role in a sample clipboard; Paste changes only the Editing draft.
- Explicit Swap changes roles while retaining per-document state. The protected reference is disabled in the ordinary Editing selector; use Swap. Pinned History stays named through swaps and becomes inspect-only if protected. This mini-tool demonstrates targets without content actions.
- Save/export previews committed Original, Translated or paired text. Empty translation can remain empty or fall back to Original by explicit selection. Target and excluded-draft count are shown. Output is a role listing, not subtitle-file bytes.

## Review questions

1. Stacked fields/bottom reference, paired fields/right reference, or a specific combination?
2. Independent navigation by default or linked from Editing? Keep explicit multiple-match navigation and no-match state, or propose different handling?
3. Retain drafts with separate Commit / Commit + confirm, or require commit/discard before leaving a dirty Line? The latter is a question, not an implemented branch.
4. In normal mode, show Original or require explicit role choice? Default empty translated export to empty or Original fallback?

Enter, native ASS editing/IME and hidden-tag editing remain [Prove ASS-aware editing in a QML text area](https://github.com/altqx/hikari/issues/28). Browser Enter inserts text normally; it does not commit. F6/Shift+F6 cycle panels; Tab reaches controls; Escape closes the preview dialog/menus; left/right arrows outside inputs/tables switch variants.

## Coverage and walkthrough

`node --check` and whitespace checks passed. Root browser QA verified draft retention through navigation, commit/confirm and undo, explicit unmatched-reference state, committed-only export and original fallback, protected reference copying into the editing draft, and pinned History becoming inspect-only after Swap. A stacked-field overlap was repaired; fields retain their heights and excess editor controls scroll. Both layouts retain a full-width grid. No native accessibility, rendering, timing, byte-round-trip or legacy comparison parity result is claimed.

Try: edit a translation draft, navigate away/back, commit and undo; select/copy Original text; visit lines 4 and 9 with linked Time overlap to see unmatched rows; combine Selected-only with row checkboxes; return from linked to independent reference navigation; copy reference and paste into Editing draft; pin History, Swap and inspect its protected target; preview each text role with empty translations.

Unimplemented samples: bulk document/selected-row original copying, mid-line tag transfer, full comparison algorithm, translation paste/shift dialogs, format conversion/TLMode serialization, native docking, full grid operations, real media, comprehensive undo and file saving. These capabilities remain in scope. Matching, draft-leave behavior, normal-mode role and export fallback remain questions; this sample authorizes no compatibility departure.
