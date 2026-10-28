# ASS-aware text editing in QML: highlighting, spellcheck, RTL, IME

Research for altqx/hikari#15 (map: altqx/hikari#1). Feeds: "Prove ASS-aware editing in a QML text area".

Status: completed desk research, 2026-09-27. Qt docs resolve to 6.11.2; repository inventory is at `20d647c4`. This establishes API feasibility and prototype gates, not tested IME/accessibility or latency results.

## Summary

QML `TextArea` plus `QQuickTextDocument` and a C++ `QSyntaxHighlighter` is a viable first prototype for the active ASS line. Syntax colours, token-aware bracket matching, spelling annotations and a separate original field are achievable through supported APIs. True editable tag hiding, document-wide undo semantics and precise decoration rendering require additional design and validation; they are not supplied automatically by adding a highlighter.

## Implications for the decision

Recommendation: prototype raw ASS editing first, preserving native text input behavior and an authoritative logical source string. Keep formatting separate from document edits. Add a read-only clean-text projection before attempting editable hidden tags. Use agent-built QML for runtime checks and HTML only for interaction/layout reaction; Figma remains Starter and occasional handoff. No custom text engine or Widgets embedding is justified before a concrete failure is measured.

## Detailed findings

### Today's behaviour in the wx editor

The current `TextEditor` is a custom `wxWindow`, with its own key handling, selection, rendering and tag completion. Its header exposes source replacement and BiDi conversion; implementation references `SpellChecker`, `BidiConversion`, parsed text and context menus. The Enter accelerator commits and advances a line. `EditBox` tracks original and translated text separately and commits multi-line edits through the grid. These behaviors are requirements to test, not reasons to retain manual Unicode reordering in a Qt control. [TextEditor](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/DialogueTextEditor.cpp), [interface](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/DialogueTextEditor.h), [EditBox](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp)

### QML TextArea + QQuickTextDocument + QSyntaxHighlighter

**Supported bridge and version boundary.** `TextArea` extends `TextEdit` with styling, padding and control behavior; it can participate in a `ScrollView`. `QQuickTextDocument::textDocument()` exposes the underlying `QTextDocument`, explicitly including syntax highlighting. Qt 6.7 adds replacing that document, with caller-retained ownership, and `textDocumentChanged`; source/load/save-related APIs are still marked preliminary in current docs. HikariSub should retain its own ASS load/save path and attach the highlighter to the existing editor document rather than delegate ASS persistence to these APIs. [TextArea](https://doc.qt.io/qt-6/qml-qtquick-controls-textarea.html), [QQuickTextDocument](https://doc.qt.io/qt-6/qquicktextdocument.html)

**ASS and karaoke syntax.** `QSyntaxHighlighter::highlightBlock` applies format ranges without changing the source and offers block state/user data plus selective rehighlighting. Use one lexer with source offsets for override blocks, escaped line breaks, drawing mode, tag names/arguments and karaoke durations; preserve incomplete input instead of treating it as fatal. Store token/bracket metadata in block user data. Highlight matched/unmatched braces/parentheses on cursor movement and rehighlight only affected blocks. A chain of naive regexes is insufficient for nested tag arguments and malformed input. [QSyntaxHighlighter](https://doc.qt.io/qt-6/qsyntaxhighlighter.html)

**Spelling and suggestions.** Keep Hunspell behind a language/dictionary service. Pass dialogue text with ASS control sequences excluded, retaining a mapping from spelling spans to source UTF-16 offsets. Debounce background work with document/line revision tokens; discard stale results and apply visual annotations on the UI thread. Suggestions replace a source span through the same edit transaction as typing. Dictionary encoding, unavailable dictionaries, ignore/add-word persistence and per-field language need explicit behavior. Hunspell provides spell checking and suggestions; it does not know ASS syntax or Qt cursor positions. [Hunspell upstream](https://github.com/hunspell/hunspell), [current integration](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SpellChecker.cpp)

**Underline limitation.** `QTextCharFormat` defines wave and platform-dependent spellcheck underlines and colour. That API surface is not proof of identical drawing in Qt Quick and Widgets. The inspected Qt 6.11 Quick text-node engine derives underline flags from glyph runs and constructs geometric decoration lines; custom wave/dash fidelity therefore needs a visual test. Use distinct spelling colour plus an accessible message as the baseline; if waves are required and fail, use a separate decoration layer driven by text-layout ranges, without altering text. Avoid promising `WaveUnderline` just because setting it compiles. [Char formats](https://doc.qt.io/qt-6/qtextcharformat.html#UnderlineStyle-enum), [inspected Quick engine](https://github.com/qt/qtdeclarative/blob/7d0a433a9d318fdf4d018710d3561abdb21a21e3/src/quick/items/qquicktextnodeengine.cpp)

**Cursor and selections.** `TextEdit` exposes `cursorPosition`, selection endpoints, `select`, `positionAt`, `positionToRectangle`, insertion/removal and undo/redo. These can drive completion placement and bracket feedback. Offsets are character positions in Qt's text representation, not UTF-8 bytes; test surrogate pairs and combining sequences at every source-map boundary. Preserve selection and scroll when changing a visual annotation. Do not reassign the entire `text` property after every key, which would fight the native edit lifecycle. [TextEdit](https://doc.qt.io/qt-6/qml-qtquick-textedit.html), [QTextCursor](https://doc.qt.io/qt-6/qtextcursor.html)

**RTL/BiDi.** Keep source in logical order and let Qt shape and display mixed-script text. `TextEdit` exposes direction/alignment queries and native selection behavior. The old `BidiConversion` code should not pre-reorder the same string before feeding Qt; that would risk double reordering. Test literal ASS tags amid Arabic/Hebrew, punctuation and numbers, visual/logical arrow movement, clipboard round trips and partial selections. Editing display correctness is distinct from libass subtitle-rendering parity. [TextEdit](https://doc.qt.io/qt-6/qml-qtquick-textedit.html), [text layout cursor semantics](https://doc.qt.io/qt-6.8/qtextlayout.html)

**IME.** `inputMethodComposing` and `preeditText` expose composition state. `QInputMethodEvent` distinguishes provisional preedit text from committed/replacement text. Leave native input events with the control; global Enter/shortcut actions must not steal composition confirmation. Do not commit, normalize tags, replace the document or switch lines mid-preedit. Test Japanese, Chinese and Korean input on the actual Windows and Linux input systems, including cancellation, candidates, focus changes and undo. No cross-platform IME test has been performed in this research. [TextEdit](https://doc.qt.io/qt-6/qml-qtquick-textedit.html), [QInputMethodEvent](https://doc.qt.io/qt-6/qinputmethodevent.html)

**Tag hiding and line-break visualization are projections.** A highlighter changes appearance; it does not supply lossless removal/folding of arbitrary inline source spans. Painting tags transparent still leaves their width and editable cursor positions. Recommended progression: raw source with styled tags; read-only clean preview; only then editable projection with explicit source/display offset maps. Decide how deletion across a hidden tag, paste, selection, IME replacement and undo work. Keep literal `\\N`, `\\n` and `\\h` distinct. If displaying `\\N` as a real break, round-trip it through the mapping, not a lossy global replacement. Soft visual wrapping should not insert subtitle breaks. These are application design obligations inferred from the formatting/editing APIs above.

**Translation mode.** Use a labeled read-only original TextArea and an independently editable translation TextArea, each with its own cursor/selection and language. Both refer to the same stable line ID. If editing originals is allowed, expose an explicit action and document transaction, rather than silently sharing mutable text documents or undo buffers.

**Undo integration.** `QTextDocument` has its own undo/redo history and emits `undoCommandAdded`; `QTextCursor` edit blocks can group changes. There is no automatic bridge to HikariSub's document history. Choose one explicit policy: (A) local typing undo within an active line, with a grouped document command on commit, or (B) document-owned live edit transactions, with reentrancy guards and native history reconciliation. Prototype A first because its boundary is clear, then test whether users expect one continuous timeline. Background highlighting/spelling must not create document undo entries; suggestions, completion and tag operations should each be a single coherent edit. Define line switching, cancellation, redo invalidation and external automation behavior. [QTextDocument](https://doc.qt.io/qt-6/qtextdocument.html#undoCommandAdded), [edit blocks](https://doc.qt.io/qt-6/qtextcursor.html#beginEditBlock)

**Performance.** Work on the active line, not a TextArea containing the entire subtitle file. A single very long line is still a potentially expensive text block: incremental block highlighting does not make a megabyte ASS drawing free. Cache tokenization, avoid synchronous dictionary calls per keystroke, debounce expensive validation and bound pathological parsing. Measure input-to-frame p95/p99 with normal and oversized lines; no latency figures are asserted.

### Alternatives

| Option | Verified capability | Remaining application work / tradeoff |
|---|---|---|
| Custom QSyntaxHighlighter | QTextDocument formatting and block metadata | ASS lexer, spell service, bracket state and edit semantics |
| KSyntaxHighlighting | QTextDocument highlighter and QML binding, definitions/themes, partial rehighlighting | Validate/provide ASS definitions; karaoke semantics, Hunspell, undo and hidden tags remain ours |
| Custom Quick text item | QTextLayout/QSGTextNode can render shaped text | IME, text queries, selection, clipboard, navigation and accessibility become a large editor project |
| Widgets editor in native window | Mature QTextEdit/QPlainTextEdit APIs; native window can be embedded | Separate native surface, stacking/clipping/focus/docking/IME problems to test |

KSyntaxHighlighting provides a QSyntaxHighlighter implementation and a QML `SyntaxHighlighter` tied to a text field. It is useful if shared syntax definitions/themes are wanted; it is not an ASS editing or spelling engine. Do not assume an existing subtitle definition covers HikariSub's ASS dialect without a fixture corpus. [C++ API](https://api.kde.org/ksyntaxhighlighting-syntaxhighlighter.html), [QML API](https://api.kde.org/qml-org-kde-syntaxhighlighting-syntaxhighlighter.html)

A custom item can use public `QSGTextNode` from Qt 6.7, but rendering shaped text is only a small part of an editor. A custom input item must implement IME queries/events and accessible text semantics too. Prefer customizing decorations around the native TextArea before replacing its input engine. [QSGTextNode](https://doc.qt.io/qt-6/qsgtextnode.html), [IME requirements](https://doc.qt.io/qt-6/qinputmethodevent.html)

Be precise about embedding direction: `QWidget::createWindowContainer` embeds a QWindow in a Widgets hierarchy, not a QWidget in QML. Qt Quick `WindowContainer` (since 6.8) embeds QWindows; a native Widgets window is therefore an interop experiment, not a regular scene-graph item. The embedded window sits above ordinary Quick siblings and does not interoperate with QQuickWidget/QuickRenderControl-style composition. That conflicts with arbitrary popovers, clipping and movable docks unless handled deliberately. Reserve it for a proven text-editor blocker. [QWidget container](https://doc.qt.io/qt-6/qwidget.html#createWindowContainer), [Quick WindowContainer](https://doc.qt.io/qt-6/qml-qtquick-windowcontainer.html)

## Open questions

Research is complete; the downstream ASS editor prototype should record these results separately:

1. Raw-source preservation and styling for malformed/nested tags, karaoke and drawing strings; bracket matching; literal break markers.
2. Hunspell misspelling spans and one-step suggestion undo; underline styles visually compared on the chosen render type/backend.
3. Arabic/Hebrew mixed tags, emoji and combining characters; logical source survives selection, copy and paste exactly.
4. Native CJK composition, candidate confirmation/cancellation, Enter-to-commit guarding and focus transitions. Human/platform testing remains necessary.
5. One documented undo policy through typing, completion, line switch, document undo and automation changes.
6. User reactions to original/translation fields, raw versus clean view, and timing of line commit. Editable hidden tags require their own follow-on gate; a clean read-only preview does not prove them.
