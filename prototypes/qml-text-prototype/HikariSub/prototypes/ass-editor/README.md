# Throwaway native ASS editor prototype

Issue: [#28 — Prove ASS-aware editing in a QML text area](https://github.com/altqx/hikari/issues/28).
Question: can native Qt Quick input support raw ASS highlighting, spelling annotations, clean projection, mixed scripts and explicit undo/commit boundaries without replacing its text engine?

This is an in-memory PySide6/Qt Quick spike, not production code. The technical question requires a native QML surface rather than the prototype skill's usual browser layout variants. It sits next to the existing editor code under HikariSub. No files are loaded or saved, no dictionary is modified, and no application settings are changed. Figma remains Starter and occasional handoff only.

## Run

On this workstation, from this checkout:

```powershell
./HikariSub/prototypes/ass-editor/run.ps1
```

The launcher uses the existing shared environment at `%LOCALAPPDATA%/HikariSub/prototype-runtime/Scripts/python.exe` (PySide6-Essentials 6.11.2). Elsewhere, install that package in a Python environment, then run:

```text
python HikariSub/prototypes/ass-editor/prototype.py
```

The window is a real Qt Quick ApplicationWindow with a TextArea/QTextDocument bridge, not HTML. Closing it discards the draft and commit history.

Optional CLI startup choices: `--sample 0` through `4`, `--underline single|wave|spellcheck`, and `--view raw|translation|map`. For a reproducible review image without opening a desktop window:

```powershell
./HikariSub/prototypes/ass-editor/run.ps1 -Offscreen -Capture "$env:TEMP/hikari-editor.png"
```

The offscreen Windows capture registers available local Segoe UI/CJK font files because that plugin did not populate a usable system-font catalog here. No fonts are copied into this branch. The ordinary launcher uses the native platform integration.

## What to try

1. In **ASS + spelling**, edit tags, select text, type and use native Ctrl+Z/Ctrl+Y. The raw string stays authoritative. The highlighter adds colors and cursor-adjacent brace emphasis without replacing the text.
2. Click **teh → the** or **subtitel → subtitle**. Each suggestion uses one QTextCursor edit block; one draft undo should restore it. These are two sample spellings, not a Hunspell integration or complete spelling service.
3. Switch **Single / Wave requested / SpellCheck requested**. Inspect the actual decoration, not just the enum label. Colors are always present as a fallback.
4. Select **This is teh subtitel** in the clean preview, then **Locate preview selection in raw**. The projected 0–20 selection maps to raw UTF-16 12–37 and includes the intervening hidden italic tag. A caret at a hidden-tag boundary uses right affinity. **Offset map** exposes every raw/clean span.
5. Switch to **Mixed RTL**, then **Translation**. The original field is independent and read only. Try logical/visual arrows, Shift selection, clipboard round trips and punctuation around Arabic/Hebrew and ASS tags. No manual BiDi reordering is applied.
6. Use **Emoji + combining** for surrogate pairs, a combining accent and an emoji sequence. The status strip deliberately shows both Python codepoint count and Qt UTF-16 length. The mapping aligns source codepoints, not grapheme clusters; a production editable projection needs stronger boundaries.
7. Use **Incomplete + drawing**. An unclosed override is kept in raw and reported; the clean view hides it. Simple drawing mode is hidden in the projection. This deliberately exposes what a simplified parser cannot settle.
8. Type a draft, then **Commit line** or **Ctrl+Enter**. This records an in-memory document boundary and clears the local typing undo stack. **Undo committed line** restores the preceding commit; it is separate from draft undo. Plain Enter is left to native text input in this spike.

The projection is intentionally read only. It does not prove editable hidden tags. Hard break, soft break and hard space remain distinct source spans; the simplified clean view displays them as newline, space and NBSP respectively. This preview is not libass layout.

## Native IME and accessibility review remains open

Use real Japanese, Chinese and Korean input systems on Windows and Linux. Start preedit, choose candidates, confirm with Enter, cancel, move focus and undo. Watch inputMethodComposing and preedit text in the status strip. Ctrl+Enter commits only when not composing; while composing, key events remain available to the native control, and mutation buttons/fixture switching are disabled.

The conditional guard was exercised programmatically, but no real IME composition session was performed. Input-method events, focus transitions and candidate confirmation cannot be accepted from an offscreen screenshot.

Use NVDA and Orca to verify editor names, text/selection announcements, button access and reading order. The QML exposes Accessible names/descriptions, but no screen reader was run. Investigate especially whether tags should be announced and whether the read-only clean view needs a different accessible representation.

## Observed on 2026-09-27

Windows, PySide6-Essentials 6.11.2, Qt Quick Basic controls, offscreen platform with Qt Quick software adaptation:

- The native QML window loaded without runtime QML errors and attached QSyntaxHighlighter to its QTextDocument.
- Baseline source remained exactly the fixture string: 73 UTF-16 units / 72 codepoints, six override blocks and two spelling spans. The clean projection showed dialogue plus the hard break.
- A direct runtime exercise changed only teh to the; one QTextDocument undo restored the exact original source. Rehighlighting/underline-format changes did not create an undo entry.
- The clean selection 0–20 mapped to raw 12–37. A composing=true commit request added no history entry; a normal commit added one and cleared local undo. This verifies only the handler logic, not a native IME.
- The single underline baseline was visible. **WaveUnderline did not yield a visible squiggle** in the captured software Quick view. Do not infer wave support from the QTextCharFormat enum, or generalize this observation to all backends without a native comparison.
- The Arabic/Hebrew sample rendered and preserved logical source. It has not received fluent-language review or selection/clipboard acceptance.

A PySide wrapper-lifetime problem found during the exercise was fixed by retaining the QQuickTextDocument wrapper as well as its QTextDocument. Without that reference, a later document action saw a deleted underlying object. This is why the host retains quick_document.

These are runtime observations and visual inspection, not a formal automated test suite. The prototype skill calls for no test suite; no production behavior was changed.

## Captures

[Raw editing](review-raw.png) · [Mixed RTL / translation](review-rtl.png) · [Wave requested](review-wave.png)

The images came from this running native QML artifact. They are review aids, not proof of native input, accessibility or libass rendering.

## Limits and requested reaction

The source scanner is intentionally small. It does not implement complete ASS grammar, drawing validation, multiline highlighter state, nested syntax diagnostics, real dictionary services, asynchronous revision handling or grapheme-safe editable projections. Highlighting rescans the active source and is not a performance benchmark. The translation original is fixed demo text. There is no full subtitle document, file I/O, playback, line switching or document-redo implementation.

The provisional conclusion is that raw TextArea plus a highlighter and separate clean view is feasible enough to evaluate. The issue stays open for altqx's reaction: Is this raw/clean arrangement useful? Is a separate commit boundary acceptable? Should Enter commit-and-advance or remain text input? Are tags too intrusive in mixed RTL? Would red spelling text plus simple underlines suffice, or are custom squiggles required? Editable hidden tags, native CJK input and screen-reader acceptance remain unresolved.

Research basis: [completed QML text-editing investigation](https://github.com/altqx/hikari/tree/research/qml-text-editing/docs/research/qml-text-editing.md).
