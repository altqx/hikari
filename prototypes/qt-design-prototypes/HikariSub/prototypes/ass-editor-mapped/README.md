# Editable hidden ASS tags: native source-map prototype

Ticket: [Prove ASS-aware editing in a QML text area](https://github.com/altqx/hikari/issues/28).

The user requires **editable hidden tags** and **Enter commits and advances outside native composition**. This extends the [earlier native raw/read-only-clean prototype](https://github.com/altqx/hikari/blob/cd166c5ad34d641b51cbfdc392c49501ab3ab1a3/HikariSub/prototypes/ass-editor/README.md) without changing that checkout. It is a standalone, throwaway PySide6/QML experiment, not production code. All document changes are in memory; no subtitles, settings, dictionary or clipboard data are saved.

## Run

From this checkout:

```powershell
./HikariSub/prototypes/ass-editor-mapped/run.ps1
```

It uses the already installed shared PySide6-Essentials 6.11.2 environment under LOCALAPPDATA/HikariSub/prototype-runtime. It installs nothing. A separately configured PySide6 6.11.2 Python can run prototype.py directly.

Reproduce the observed offscreen checks and render:

```powershell
./HikariSub/prototypes/ass-editor-mapped/run.ps1 -Offscreen -SelfCheck -Capture "$env:TEMP/hikari-mapped.png"
```

Startup alternatives: -Sample 0 through 5, -Affinity before|after, -Crossing retain|block. Closing the window discards memory.

## What to try

1. Type, select, delete or paste plain dialogue in **Hidden tags**. Raw ASS changes in the adjacent native editor, retaining untouched override tokens. Edit raw tags explicitly in **Raw ASS**; the mapped view updates. Neither active editor is reassigned wholesale after each keystroke.
2. Choose **Before tags** or **After tags**, then press **Boundary demo**. Starting from A followed by an italic block then B, inserting X either puts X before the italic block or after it. Compare exact raw strings.
3. Choose **Retain exact tags** or **Block cross-tag edit**, then **Cross-tag demo**. Retain replaces ABC with X while preserving both italic tokens in order after the replacement; it does not infer new styling attachment. Block refuses that crossing. Both are proposals.
4. Select visible text, then **Replace hidden-view selection**. The text field supplies a memory-only paste sample. Use **Insert hard break** to insert a mapped newline; it writes an ASS hard-break token. Normal native clipboard operations can be reviewed manually, but the checks do not access the OS clipboard.
5. Use **Escapes + Unicode**. Untouched hard break, soft break and hard space keep their original source tokens. Soft break displays as a space and hard space as NBSP. New pasted newlines become hard breaks; inserted NBSP becomes a hard-space token. This mapping choice is provisional, not a save-format normalization rule.
6. **Undo / Redo** or Ctrl+Z / Ctrl+Y restore exact raw snapshots across both views. The spelling button replaces one sample word through the same mapped transaction; it is not Hunspell. Enter commits the draft into the two-Line memory document and advances. **Undo committed Line** is separate.
7. Use **Drawing / malformed**. A visible square protects each drawing payload or malformed span; selection that touches it is refused. The original source remains available in Raw. The attempted visible text is retained in the recovery tab.
8. Use an actual Japanese, Chinese or Korean IME. Compose, change candidates, confirm, cancel, select existing text, replace surrounding text, switch focus and undo. Enter must stay with native composition; these platform checks remain required.

The map tab exposes source/display UTF-16 spans and exact original tokens. State includes draft raw text, document Lines, operation ranges, undo and commit history. Native selection and composition status remain visible above it.

## Mapping contract demonstrated

mapping.py scans into source spans; it never strips tags and rebuilds ASS from clean text. It preserves unknown closed override contents without interpreting every parameter. Top-level literal drawing-mode tags are recognized; drawing payloads and malformed/nested braces remain opaque. The scanner is deliberately incomplete, especially transformed drawing state.

A hidden-view edit identifies the native UTF-16 replacement range, rejects partial grapheme/token boundaries, removes only selected visible source spans, and inserts at the chosen boundary affinity. Intervening hidden tokens remain byte-for-byte-equivalent **strings in this fixture**, in their original order. Reprojection must equal the requested visible edit, and hidden/opaque-token sequences must match before and after. Failure leaves authoritative raw unchanged.

Qt grapheme boundaries reject surrogate, combining-sequence and emoji-ZWJ splits. ASS-syntax paste (braces/backslashes), the reserved square marker, opaque-span changes and unqualified complex IME replacements are refused with retained attempted text and a Raw recovery path. These guards limit the prototype; they are not accepted capability removals.

## Observed on Windows / Qt 6.11.2 offscreen software

The requested boundary fixtures in fixtures.py are executable source-preservation checks, added specifically for this native mapping assignment despite the general prototype skill's usual no-tests default.

**26 mapping fixtures passed:** before/after affinity, retained/blocking cross-tag edits, deletion, leading tags, hard/soft/space escapes, pasted newlines/NBSP, syntax refusal, emoji UTF-16, surrogate/combining/ZWJ boundaries, karaoke tokens, drawing protection, malformed source, mixed RTL and all-hidden input. Successful fixtures compare exact expected raw strings and unchanged hidden/opaque token sequences; rejected fixtures leave input unchanged.

**11 native checks passed** in the actual loaded QML artifact: mapped QTextCursor insertion; exact-source undo/redo; NBSP/escape preservation; selection replacement across tags; ambiguous paste rejection; synthetic QInputMethodEvent preedit, committed insertion and surrounding replacement; selected-range composition replacement/cancellation; raw-view selected composition cancellation/commit; native key typing/Ctrl+Z; and Enter commit-and-advance outside composition.

These checks found and addressed three concrete seams:

- QTextDocument.toPlainText normalizes NBSP; the bridge reads toRawText and converts only paragraph separators.
- An IME commit can emit a whole-block contentsChange including Qt's final paragraph sentinel. The event filter observes exact native input-method replacement metadata instead of guessing an edit location from a stripped-string diff. It returns false, so native input still processes the event.
- Qt removes a selected range when preedit starts. The bridge holds that removal in the native composition transaction until commit, and restores the selected source on cancellation. Authoritative raw is not changed merely by provisional preedit.

The first U+FFFC opaque marker rendered invisibly in Quick. The captured version uses a visible reserved square instead. Captures were generated from this running artifact and visually inspected:

- [Editable mapped view](review-mapped.png)
- [Protected drawing and malformed source](review-protected.png)
- [Mixed RTL with strict crossing policy](review-rtl-strict.png)

## Limits and decisions still open

**No real platform IME, candidate-window session, screen reader, Linux, GPU backend, latency benchmark, libass render comparison or full ASS grammar test was performed.** Sending QInputMethodEvent directly is synthetic event coverage, not an OS input-method qualification. The mixed RTL capture is not fluent-language selection/clipboard acceptance.

The probe owns raw-snapshot history so two native text documents cannot diverge. Native document undo is disabled; explicit buttons and Ctrl+Z/Ctrl+Y use the probe history. Native context-menu undo integration, grouping, caret/selection restoration, draft cancellation, line-switch command policy and automation interactions remain implementation/design work. The separate draft/commit history is a proposal; Enter behavior itself is already accepted.

Review only the remaining choices: **before versus after hidden tags**, and **retain exact intervening tokens versus refuse cross-tag replacement**. The retained-token option preserves source but can change which replacement text inherits styling; review that consequence rather than equating token retention with rendered parity.

Technical basis: [QML text-editing research](https://github.com/altqx/hikari/tree/research/qml-text-editing/docs/research/qml-text-editing.md), [Qt TextEdit](https://doc.qt.io/qt-6/qml-qtquick-textedit.html), [QTextDocument](https://doc.qt.io/qt-6/qtextdocument.html), [QInputMethodEvent](https://doc.qt.io/qt-6/qinputmethodevent.html), and [QTextBoundaryFinder](https://doc.qt.io/qt-6/qtextboundaryfinder.html). These APIs support the experiment; passing the limited fixtures does not approve the final editor architecture.
