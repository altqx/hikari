# Legacy observations: E5 "Moving tags" (local probe, 2026-10-12)

Captured by the `legacy_tag_move_capture` probe ([`tag_move_capture.cpp`](../../../../tools/legacy-capture/tag_move_capture.cpp), target in [`tools/legacy-capture/CMakeLists.txt`](../../../../tools/legacy-capture/CMakeLists.txt)) on the cases in [`inputs/tag-move-cases.txt`](../../../../tools/legacy-capture/inputs/tag-move-cases.txt). [`extract_functions.py`](../../../../tools/legacy-capture/extract_functions.py) copies `EditBox::SetTextWithTags` (`HikariSub/EditBox.cpp:1809-1874` at `20d647c4`) and the Line's `StoreTextHelper` (`SubsDialogue.h:122-252`) out of their files unchanged. They run against the stand-ins in [`tools/legacy-capture/tag_move/standins.h`](../../../../tools/legacy-capture/tag_move/standins.h): the grid's `hasTLMode` and its file's Line (`GetDialogue(currentLine)->Copy()`), the "Moving tags" toggle, `Visual`, the two TextEditor fields (`SetTextS` keeping the text and the modified flag, with resetsel selecting 0..0; `SetSelection` clamping both ends to the text's length, `DialogueTextEditor.cpp:135-155, 2154-2164`; `SetFocus`; `IsShown`) and the video's `GetState`, `OpenSubs` and `Render`.

Built locally with GCC 16.2.1 and the distribution's wxWidgets 3.2.11 (wxGTK, `wx-config --cxxflags base,core`). Run:

```
git archive 20d647c4 HikariSub cmake | tar -x -C <legacy>
cmake -S tools/legacy-capture -B <dir> -DLEGACY_SOURCE=<legacy> -DWX_CONFIG=$(which wx-config)
cmake --build <dir> --target legacy_tag_move_capture
<dir>/legacy_tag_move_capture < tools/legacy-capture/inputs/tag-move-cases.txt > observations.jsonl
```

`observations.jsonl` holds one line per case (34 cases, 38 calls). Each case gives the file's Line (text and translation) and its calls, each under a state (hasTLMode, Moving tags, Visual, the Original field shown, the video's PlaybackState). For each call it records whether the text was split (`splittedTags`), both fields' text and how many times each was set, the Translated field's modified flag, its selection (`[Selend.x, Cursor.x]`) and whether it took the focus, and the video calls. Two runs give the same file. sha256 `d54f0e67…275df5`.

Every case is run twice by the same compiled code: `linux` gives the texts as wxGTK's wxString stores them (a wchar_t per code point, the legacy Linux build), `windows` as wxMSW's wxString stores them (a wchar_t per UTF-16 code unit, so a character outside the BMP is its two surrogates). `windows` is `"same"` when its calls are those of `linux`.

The cases:

- The seven texts of `MovingTags.SplitsTheOverrideBlocksFromTheText`: a leading block, inner blocks, "}{" joined first, a '}' with no '{', a '{' after the first '}' (SubString's size_t wrap), an unclosed block after the last '}', a leading block holding a character outside the BMP.
- Extras: an empty text, no '}', a block never closed, "}{" alone and inside the text (removed, then no '}' is left), "}{" before a later '}', '}' first, '}' alone, an empty block, a block alone, a block then '}', nested blocks at the start and inside, three adjacent blocks, alternating blocks, a trailing block, characters outside the BMP around blocks, before the first '}', inside an inner block, two in a leading block, and `\N` with a trailing block.
- The conditions (`EditBox.cpp:1811`): a translated Line, translation mode off, Moving tags off, a visual tool other than the crosshair (Position, then the all-tags tool, then the crosshair again); the toggle off and on again with the video open (OnAutoMoveTags runs `SetTextWithTags(true)`); a refresh without video.

## Compared with the rewrite

`MovingTagsCapture.ReplaysTheLegacyObservations` (`hikari_application_translation_mode_tests`) runs every call through `moveTagsFromOriginal` under the call's conditions (hasTLMode, Moving tags, Visual <= CROSS, no translation; `LineEditorController::evaluateSplit` checks them in the editor). For both builds: the text is split exactly when legacy split it, both fields hold legacy's text, the Translated field takes the focus, and the caret is legacy's: the character legacy's selection stands after, counted in UTF-16 units.

The capture confirms the hand-worked values of the seven texts and adds:

- "}{" is removed before anything else, so "x}{y" and "}{" have no '}' left and are shown whole; "a}{b}c" splits with nothing moved ("ab}c" stays in the Original field).
- A '{' after the first '}' is read twice: "}a{b}" gives "}aa" and "{b}{b}", as "a}b{c}" does.
- A block's end is the first '}' after its '{': "{a{b}c}d" moves "{a{b}" and leaves "c}d".
- The caret goes after a leading block only, empty blocks included ("{}" puts it at 2), and stays at 0 otherwise.
- When the conditions fail the Line is shown whole: the translation in the Translated field and the text in the Original field, or the text alone without the Original field. Turning Moving tags off after a split takes a fresh copy of the Line (`splittedTags`); the video is refreshed only when it is open.

### Where the two legacy builds differ

Only in the caret after a leading block holding a character outside the BMP (`leading-astral`, `leading-two-astral`). Both builds put the caret on the same character, after the block; wxGTK counts it in code points (6 for `{\fn𝐀}`), wxMSW in UTF-16 units (7). The fields' texts are the same in both, since the split falls only at '{' and '}'. The rewrite follows that place on every platform: its caret is the same character counted in UTF-16 units, the unit of Qt's text fields, so it is 7 on Linux and Windows alike (E5-tag-move-caret).

## Not observed here

The running legacy app (the fields' drawing, the caret on screen); the BIDI conversion (every call here passes `BIDIConversion` false, so `SetTextS` stores the text as given); `EditBox::SetLine`, which calls this function; and a Windows-built probe (the `windows` results come from the Linux-built legacy code given UTF-16 units, as wxMSW's wxString holds them).
