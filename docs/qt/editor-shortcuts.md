# Line editor actions and shortcuts (E28-shortcuts)

Coverage of the legacy `EDITBOX_*` actions in the [UI inventory](https://github.com/altqx/hikari/blob/6f91fd28eab4c42d2c00f848606b47585c2b6bc8/docs/research/ui-inventory.md) by the Qt Line editor. Default keys come from `Hotkeys.cpp` at `20d647c4`. The keys are the Editor's bindings, remapped in the shortcut editor (O2: the Options dialog's Hotkeys page and Shift+click on the mapped buttons); the action IDs stay as the importer's aliases. The text field's own keys (plain Enter, numpad Enter, editing and selection keys) stay fixed, as in legacy.

| ID | Action | Legacy default | Rewrite | Evidence |
| --- | --- | --- | --- | --- |
| 3011 | `EDITBOX_COMMIT_GO_NEXT_LINE` Apply and go to the next line | Enter | Enter (outside composition); on the last Line it appends one, as `SubsGrid::NextLine` does | `editor_workflow`, `shell_tests`, legacy capture 37024532554 |
| 3010 | `EDITBOX_COMMIT` Apply changes | Ctrl+Enter | Ctrl+Enter | `LineEditorController::commit` |
| 3012 | `EDITBOX_INSERT_BOLD` Add bold | Ctrl+B | Ctrl+B and the B button: ASS override tags with legacy placement and restoration; SRT `<b>`, MicroDVD `{Y:b}` (`PutinNonass`) | `tag_commands_tests`, `shell_tests`, legacy captures 37024532554 and 37034136343 |
| 3013 | `EDITBOX_INSERT_ITALIC` Add italic | Ctrl+I | Ctrl+I and the I button | `tag_commands_tests` |
| 3001 | `EDITBOX_CHANGE_UNDERLINE` Underline | none | U button | `tag_commands_tests` |
| 3002 | `EDITBOX_CHANGE_STRIKEOUT` Strikethrough | none | S button | `tag_commands_tests` |
| 3014 | `EDITBOX_SPLIT_LINE` Add line wrap | Shift+Enter | Shift+Enter: `\N` (ASS, SRT) or `\|` (line formats) replaces the selection and an adjacent space on each side | `shell_tests` |
| 3017 | `EDITBOX_FIND_NEXT_DOUBTFUL` Next unconfirmed line | Ctrl+D | Ctrl+D, translation mode, visible Lines, wrapping once | `shell_tests` |
| 3018 | `EDITBOX_FIND_NEXT_UNTRANSLATED` Next untranslated line | Ctrl+R | Ctrl+R, same rules | `shell_tests` |
| 3019 | `EDITBOX_SET_DOUBTFUL` Mark as unconfirmed and go to the next line | Alt+Down | Alt+Down: toggles Unconfirmed (its own step), then Enter's behaviour | `shell_tests` |
| — | Undo / Redo in the field | Ctrl+Z / Ctrl+Y | Draft snapshots first, then Document history (accepted transaction policy) | `editor_workflow`, `shell_tests` |
| — | Discard the draft | — | Esc (accepted transaction policy) | `LineEditorController::discard` |
| 3000 | `EDITBOX_CHANGE_FONT` Font selection | none | Fn button: the "Select a font" dialog; each change is tagged at once, Cancel takes it back (E1) | `editor_font_colour_tests`, `shell_tests` |
| 3006–3009 | `EDITBOX_CHANGE_COLOR_*` Primary, secondary, outline, shadow colour | none | 1c–4c buttons: the "Choose color" picker (spectrum, RGB, alpha, ASS/HTML text, recent colours); each change is tagged at once, Cancel takes it back (E1) | `editor_font_colour_tests`, `shell_tests` |
| 3015, 3016 | `EDITBOX_START_DIFFERENCE` / `END_DIFFERENCE` | Ctrl+, / Ctrl+. | Ctrl+, / Ctrl+.: the shown frame's time minus Start, or its distance from End, in ms (both truncated to 10 ms), replaces the edited field's selection; refused without video or outside the Line | `shell_tests` |
| 3003, 3004 | `EDITBOX_PASTE_ALL_TO_TRANSLATION` / `PASTE_SELECTION_TO_TRANSLATION` | none | Paste all and Paste the selected buttons (translation mode): the Original's raw text replaces the Translated text; the Original's selection is inserted at the Translated caret | `shell_tests` |
| 3005 | `EDITBOX_HIDE_ORIGINAL` (renamed **Comment out original**, E63-comment-original) | none | Comment out original button: wraps the raw Original in braces | `shell_tests` |
| 3100–3119 | `EDITBOX_TAG_BUTTON1`…`20` | none | Not yet: custom tag buttons and their preferences | — |

A "Not yet" row is unfinished, not dropped: each is assigned to the card or surface named.
