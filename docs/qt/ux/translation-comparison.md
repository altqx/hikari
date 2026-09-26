# Translation and protected comparison

**Partially accepted, 2026-09-27.** The live review of [Prototype translation mode and protected comparison navigation](https://github.com/altqx/hikari/issues/49) selected **stacked Original/Translated fields with a bottom reference tray**, and **independent reference navigation by default**. Other interaction choices below remain proposals. This does not reopen the accepted [workspace ownership](workspaces.md) or [document text roles](../document-model.md).

## Accepted arrangement and navigation

Translation mode presents Original above Translated in the editor. When optional protected comparison is open, its reference tray appears below the full-width editing Grid. Video stays upper-left; Audio remains above the Editor on the right. The comparison reference is another Document; Original/Translated remain roles within one Line. Translation mode does not automatically open comparison or change the Editing preset.

Reference navigation starts independently of the editing Line. A user can explicitly enable one-way linked matching from Editing. Keep explicit candidate counts/navigation when several references match and an empty no-match state without silently substituting the nearest Line. A reference never becomes writable merely because it receives focus. Explicit role changes preserve per-document content and selection; pinned tools retain their named target and obey protection.

The [reviewed source](https://github.com/altqx/hikari/blob/8f6c3b1ee861c2a75a2acc57fee5da44fc9dfc34/docs/prototypes/translation-comparison.html), [run guide](https://github.com/altqx/hikari/blob/8f6c3b1ee861c2a75a2acc57fee5da44fc9dfc34/docs/prototypes/translation-comparison-notes.md) and [stacked capture](https://github.com/altqx/hikari/blob/8f6c3b1ee861c2a75a2acc57fee5da44fc9dfc34/docs/prototypes/translation-stacked-preview.png) record both alternatives. Paired fields/right reference remain a comparison reference, not the default. Movable/floating placement remains available under the shared workspace contract.

## Decisions still required

- Drafts when leaving a Line: retain per-Line drafts, automatically commit, or require a commit/discard choice; relation to document commands and Undo.
- Separate Commit and Commit + confirm; Unconfirmed changes/traversal, wrap/filter behavior and original-unlock/copy/tag-transfer details.
- Normal-mode text role and save/export fallback for an empty translation, preserving existing TLMode serialization semantics unless a named departure is approved.
- Exact linked-match criteria/combination and legacy comparison parity beyond this prototype's simplified interval example.
- Bulk original copying, full tag transfer and translation paste/shift task flows.

The browser sample preserves drafts in memory, uses a sample clipboard and previews committed role text rather than subtitle bytes. Checks covered draft navigation/undo, no-match output, role previews, reference copy and pinned History after Swap. Those observations do not approve all demonstrated behavior, establish native IME/accessibility or prove file/format compatibility.
