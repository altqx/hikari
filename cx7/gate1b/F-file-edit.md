# CX7 #810 gate 1b: menu bar, File and Edit

Read-only, at `origin/main` 8dbf88f41. `src/` and `tests/ui/menu_bar.txt` in this worktree match it. The menus are defined in `src/ui/Main.qml:2961-3323`, with the actions at `Main.qml:2258-2438` (File, Edit) and `Main.qml:10425-10505` (Review). Default keys are from `kMainDefaults` (`src/application/hotkeys.cpp:843-931`), and shortcut names from `kNames` (`hotkeys.cpp:443-836`, mirrored in `src/ui/command_names.cpp`).

The notation follows CX1 (`docs/ux/context-menus.md:1-16`). `{key}` is the default binding shown in the key column; "—" means unbound. `&x` is the mnemonic letter. Icon exemptions follow IC1 (`docs/ux/menu-icon-gaps.md:146-150`).

## File

`Main.qml:2963-3184`, `lazyContent`.

| # | Item | Command | Enabled when / notes | Icon |
|---|---|---|---|---|
| 1 | &Open… | GLOBAL_OPEN_SUBS {Ctrl+O} | always | open-subtitles |
| 2 | Open &folder… | GLOBAL_OPEN_FOLDER — | always (PJ2) | open-project |
| 3 | Recently opened &subtitles ▸ | — | always | recent-subtitles |
| 3.1 | {dyn} recent subtitle files | — | opens or reveals the file; status help `"Open" + " " + path` | — (IC1: named by data) |
| 3.2 | None | — | disabled; shown only when the list is empty | — (IC1) |
| 4 | Remove subtitles from the &editor | GLOBAL_REMOVE_SUBS — | always | close-subtitles |
| 5 | &Close | GLOBAL_CLOSE_PAGE {Ctrl+W} | `shell.hasEditingTarget`; has no status help | tab-close |
| 6 | Close pro&ject | GLOBAL_CLOSE_PROJECT — | `projectName` set (PJ2) | close-project |
| 7 | Open &video… | GLOBAL_OPEN_VIDEO {Ctrl+Shift+O} | always; also in Video › (approved 28) | open-video |
| 8 | Import subtitles from MKV… | GRID_SUBS_FROM_MKV — | `matroska.available` | extract-subtitles |
| 9 | Import lyrics… | GLOBAL_IMPORT_LYRICS — | always (AU23) | import-lyrics |
| 10 | &Save | GLOBAL_SAVE_SUBS {Ctrl+S} | `editor.editable` | save |
| 11 | Save &all | GLOBAL_SAVE_ALL_SUBS — | `hasEditingTarget` | save-all |
| 12 | Save &as… | GLOBAL_SAVE_SUBS_AS {Ctrl+Shift+S} | `hasEditingTarget` | save-as |
| 13 | &Export as… | GLOBAL_EXPORT_SUBS_AS — | `hasEditingTarget` (EX1) | export |
| 14 | Save &translation | GLOBAL_SAVE_TRANSLATION — | `hasEditingTarget && translationMode`; opens the save dialog | save-translation |
| 15 | [x] Save subtitles using the video name | GLOBAL_SAVE_WITH_VIDEO_NAME — | checkable Action, the setting `subtitles.saveWithVideoName` | save-with-video-name (kept per IC1) |
| 16 | Open autosave… | GLOBAL_OPEN_AUTO_SAVE — | always; opens Autosaves | autosave |
| 17 | Remove temporary files | GLOBAL_DELETE_TEMPORARY_FILES — | always; opens Autosaves at "Delete autosaves saved before" | remove-temporary |
| 18 | Last session ▸ | — | always | last-session |
| 18.1 | Load last session | GLOBAL_LOAD_LAST_SESSION — | | last-session |
| 18.2 | Load session from file | GLOBAL_LOAD_EXTERNAL_SESSION — | opens a file picker | session-open |
| 18.3 | Save session to file | GLOBAL_SAVE_EXTERNAL_SESSION — | opens a file picker | session-save |
| 18.4 | [x] Ask whether to load the last session at program startup | — | `sessionRestore === 1`; clearing it sets 0 | — (IC1: check item) |
| 18.5 | [x] Load last session after program start | — | `sessionRestore === 2`; clearing it sets 0 | — (IC1: check item) |
| 19 | &Settings | GLOBAL_SETTINGS — | always | settings |
| 20 | Close &window | GLOBAL_CLOSE_WINDOW {Alt+F4} | always (MW6) | window-close |
| 21 | E&xit | GLOBAL_QUIT — | always | exit |

Items: 21 top level, 28 in all; 2 submenus; 0 separators. The listing has 27 lines, because it leaves out the {dyn} row.

## Edit

`editMenu`, `Main.qml:3185-3323`.

| # | Item | Command | Enabled when / notes | Icon |
|---|---|---|---|---|
| 1 | &Undo | GLOBAL_UNDO {Ctrl+Z} | `editor.hasLine` | undo |
| 2 | &Redo | GLOBAL_REDO {Ctrl+Y} | `editor.hasLine` | redo |
| 3 | So&rt all lines ▸ | — | `editor.editable` | sort |
| 3.1–3.8 | Start time, End time, Style, Actor, Effect, Layer, Text, Text without tags | GLOBAL_SORT_ALL_BY_* — | fixed model `root.sortKeys` (`Main.qml:104-121`) | — (IC1: sort keys) |
| 4 | So&rt selected lines ▸ | — | `editor.editable` | sort-selected |
| 4.1–4.8 | as 3.1–3.8 | GLOBAL_SORT_SELECTED_BY_* — | | — (IC1) |
| 5 | Undo to last save | GLOBAL_UNDO_TO_LAST_SAVE — | `editor.canUndoToLastSave` | undo-to-last-save |
| 6 | &History | GLOBAL_HISTORY {Ctrl+Shift+H} | `editor.hasLine` | history |
| 7 | Re&view ▸ | — | **present only while `review.on`**, inserted after History (`Main.qml:3259-3278`); no status help | review-keep |
| 7.1 | Keep AI change | GLOBAL_KEEP_AI_CHANGE {Ctrl+Shift+Enter} | the current line has a change | review-keep |
| 7.2 | Undo AI change | GLOBAL_UNDO_AI_CHANGE {Ctrl+Shift+Backspace} | same | undo |
| 7.3 | Next AI change | GLOBAL_NEXT_AI_CHANGE {Ctrl+Alt+Down} | `review.waiting > 0` | chevron-down |
| 7.4 | Previous AI change | GLOBAL_PREVIOUS_AI_CHANGE {Ctrl+Alt+Up} | same | chevron-left |
| | `|` | | | |
| 7.5 | Keep all | GLOBAL_KEEP_ALL_AI_CHANGES — | same | review-keep |
| 7.6 | Undo all | GLOBAL_UNDO_ALL_AI_CHANGES — | same; may ask "Undo all anyway" (`ReviewPanel.qml:432-434`) | undo |
| | `|` | | | |
| 7.7 | Show panel | GLOBAL_SHOW_AI_REVIEW — | `review.on` | show |
| 8 | Select &lines | GLOBAL_OPEN_SELECT_LINES — | `editor.hasLine`; opens the Select lines dialog | select-lines |
| 9 | &Find… | GLOBAL_SEARCH {Ctrl+F} | `editor.hasLine` | search |
| 10 | Re&place… | GLOBAL_FIND_REPLACE {Ctrl+H} | `editor.hasLine` | find-replace |
| 11 | Find next | GLOBAL_FIND_NEXT {F3} | `hasLine && !findBusy` | match-next |
| 12 | Find previous | GLOBAL_FIND_PREVIOUS {Shift+F3} | same | match-previous |
| | `|` | | | |
| 13 | Insert hard space | GLOBAL_INSERT_HARD_SPACE {Ctrl+Shift+Space} | `editor.editable` | hard-space |

Items: 13 top level with Review on (12 without), 36 in all (29 without); 3 submenus (2 without); 1 top-level separator, plus 2 in Review.

**Both menus:** 64 items in all (57 with Review off), across 5 submenus.

## Changed since 1d122c203

`git log --format='%h %cs %s' 1d122c203..origin/main -- tests/ui/menu_bar.txt` lists 105 commits, most of them merges. The File and Edit changes below come from `git log -S` on each line, mapped to the PR that merged it.

| Change | Row | Commit | PR |
|---|---|---|---|
| removed | Edit › Fix minor errors (experimental) | 003f6fa18 | #634 (#608) |
| removed | File › Import legacy settings... (moved to Settings › General) | 5e84f0ca4 | #646 (#609) |
| added | Edit › Sort all/selected › Text, Text without tags | 655c54dde | #654 (FA11) |
| renamed, moved | Edit › "Find and replace" + "Find" → "Find…", "Replace…" (Find first) | e2253ea5d | #660 (#597) |
| added | Edit › Find previous | e2253ea5d | #660 (#597) |
| added | File › Export as… | cb6f45415 | #622 (EX1 #448) |
| added | File › Import subtitles from MKV… (from the Grid menu, approved 28) | 57027a885 | #680 (CX4 #580) |
| added | Edit › separator, Insert hard space | e906b0a7a, 8c7bd6634 | #681 (FA7) |
| renamed | File › "Open auto save" → "Open autosave…"; Sort › "The starting time" → "Start time", "Styles" → "Style" | a8f80546b | #709 (#596) |
| added | Edit › Review ▸ (7 rows, 2 separators) | e3ceae9f4 | #798 (AI2 #430, pin train 2) |
| added | File › Open folder…, Close project | 551cc10b5 | #817 (PJ2) |
| added | File › Close window; Exit gains GLOBAL_QUIT | 1adf03da2 | #876 (MW6 #866) |
| added | File › Import lyrics… | 562b4cf84 | #955 (AU23 #857) |

Nothing in File or Edit moved between menus. No row was renamed after #709.

## Findings

### FE1. name-differs-from-context-menu: File › "Open…" is "Open subtitles…" everywhere else

- **Tag:** name-differs-from-context-menu, inconsistent-name.
- **Evidence:**
  - `Main.qml:2262` `qsTr("&Open…")`.
  - The video and full-screen menus call GLOBAL_OPEN_SUBS "Open subtitles…" (`tests/ui/context_menus.txt:201, 236`).
  - The dialog it opens is titled "Open subtitles" (`Main.qml:9153`).
  - The shortcut name is "Open subtitles" (`hotkeys.cpp:613`).
  - text-style's one-name table gives "Open subtitles…" as the File command (`docs/ux/text-style.md`, "What the user opens and edits"); text-inventory.md:42 has the same row.
- **Recommend:** "Open &subtitles…". The current &s on "Recently opened &subtitles" then moves to another letter (see FE10).
- **Visible:** yes.

### FE2. flat-list: File has 21 rows and no separator, with the closes in the middle of the opens

- **Tag:** flat-list, destructive-next-to-common.
- **Evidence:**
  - `Main.qml:2969-3181` has no `MenuSeparator`, and `menu_bar.txt:2-28` has no `File > -`.
  - Remove subtitles from the editor, Close and Close project (rows 4–6) sit between Open folder… and Open video….
  - Settings sits between Last session ▸ and Close window.
- **Recommend:** keep the commands, in 7 groups:
  1. Open subtitles…, Open video…, Open folder…, Recently opened subtitles ▸
  2. Import subtitles from MKV…, Import lyrics…
  3. Save, Save as…, Save all, Save translation…, Export as…, [x] Save subtitles using the video name
  4. Open autosave…, Delete autosaves… (FE4), Last session ▸
  5. Remove subtitles from the editor, Close tab (FE8), Close project
  6. Settings
  7. Close window, Exit

  This gives 6 separators and still 21 rows. Regenerate `menu_bar.txt` and the docs export.
- **Visible:** yes.

### FE3. flat-list: Edit's Sort submenus split the undo group

- **Tag:** flat-list.
- **Evidence:**
  - `Main.qml:3190-3313`: Undo, Redo, Sort all ▸, Sort selected ▸, Undo to last save, History, (Review ▸), Select lines, Find…, Replace…, Find next, Find previous.
  - The only separator is before Insert hard space (`:3314`).
- **Recommend:** keep the commands, in these groups:
  1. Undo, Redo, Undo to last save, History
  2. Review ▸ (while on)
  3. Find…, Replace…, Find next, Find previous, Select lines…
  4. Sort all lines ▸, Sort selected lines ▸
  5. Insert hard space

  The separator around Review must hide with it. `Main.qml:3276` already places Review by index; place it after History, then a separator that is visible only while `review.on`.
- **Visible:** yes.

### FE4. legacy-name, text-style: "Remove temporary files" opens the autosave delete prompt

- **Tag:** legacy-name, inconsistent-name (text-style "…" and Delete/Remove).
- **Evidence:**
  - `Main.qml:2350-2351` calls `autosaves.show(true)`, which opens the Autosaves window and then its "Delete autosaves saved before" prompt (`Autosaves.qml:69-74, 314`).
  - The help still says "Opens the temporary file removal window" (`Main.qml:3105`).
  - text-style says "Delete" is for autosaves, and that a command needing a value takes "…". The window's own button is "Delete older than…" (`Autosaves.qml:295`).
- **Recommend:** "Delete old autosaves…". Name the shortcut the same; it has no shortcut name now (FE7). The help should read "Deletes autosaves older than a date you choose". The command id stays.
- **Visible:** yes.

### FE5. text-style "…": four File/Edit rows open a picker or a dialog but show no "…"

- **Tag:** inconsistent-name (text-style ellipsis rule).
- **Evidence:**
  - Select lines: `Main.qml:2399`. Its action opens `selectLinesDialogLoader`, and text-style uses "Select lines…" as its own example.
  - Load session from file: `Main.qml:2360`. It opens a file picker (`SessionWindows.qml:33-38`).
  - Save session to file: `Main.qml:2365`. Same picker code.
  - Save translation: `Main.qml:2322, 2327-2328`. It always ends in `openSaveDialog()`.
- **Recommend:** "Select &lines…", "Load session from file…", "Save session to file…" and "Save &translation…". Give the shortcut names the same "…".
- **Visible:** yes.

### FE6. toggle-hides-state: Last session's two startup check items are one three-way choice

- **Tag:** toggle-hides-state, inconsistent-name.
- **Evidence:**
  - `Main.qml:3136-3153`: two check items write one value, `sessionRestore` 0/1/2. Ticking one clears the other, and clearing either sets 0.
  - The wording differs between the two ("at program startup" and "after program start"). The longer label has 57 characters.
  - The two items follow the three commands with no separator.
  - CX5 made exclusive choices into radio rows (`MenuRadioItem.qml`, `MenuHeading.qml`).
- **Recommend:** keep Load last session, Load session from file… and Save session to file…, then a separator. Below it, a `MenuHeading` "At startup" with three (•) rows: "Don't load the last session", "Ask", "Load the last session". The setting values stay 0, 1 and 2.
- **Visible:** yes.

### FE7. missing-expected: three File commands can't be found or bound in Settings › Shortcuts

- **Tag:** missing-expected.
- **Evidence:**
  - GLOBAL_LOAD_EXTERNAL_SESSION (5053), GLOBAL_SAVE_EXTERNAL_SESSION (5054) and GLOBAL_DELETE_TEMPORARY_FILES (5106) are in `kActions` (`hotkeys.cpp:207-208, 216`), but they have no `kNames` row.
  - `HotkeyList::build` lists only named ids, plus ids already bound in the user's map (`hotkeys.cpp:1482-1518`).
  - `docs_export.cpp:80-82` skips unnamed ids.
  - So the three rows never show a key, and the docs export leaves them out.
  - **Uncertain:** whether legacy also left these unnamed on purpose. I didn't check the legacy tree.
- **Recommend:** add `kNames` and `command_names.cpp` rows: "Load session from file…", "Save session to file…" and "Delete old autosaves…" (FE4, FE5).
- **Visible:** yes (Settings › Shortcuts and the docs export).

### FE8. inconsistent-name: shortcut names differ from the File/Edit rows

- **Tag:** inconsistent-name. This is the CX2 1.3 precedent: menu, shortcut list and command search use one name.
- **Evidence:** each pair is shortcut name (`hotkeys.cpp` / `command_names.cpp`) → menu row.
  - "Load subtitles from an MKV file" (`hotkeys.cpp:548`) → "Import subtitles from MKV…". CX4 renamed only the menu.
  - "Close current tab" (`:605`) → "Close".
  - "Save all subtitles" (`:624`) → "Save all", which is also the tab menu's name.
  - "Export as" (`:711`) → "Export as…".
  - "Find" (`:629`) → "Find…".
  - "Find and replace" (`:569`) → "Replace…".
  - "Sort all lines by styles" and "Sort selected lines by styles" (`:582, 590`) → "Style". #709 fixed the menu row but not these names.
  - "Review panel" (`:751`) is not verb-first.
  - B11 already covers "Open video" and "Open subtitles"; they are not repeated here.
- **Recommend:**
  - Rename the shortcut names to "Import subtitles from MKV…", "Close tab", "Save all", "Export as…", "Find…", "Replace…", "Sort all lines by style", "Sort selected lines by style" and "Show Review panel".
  - Rename File › "&Close" to "Close &tab", so it reads apart from Close project and Close window, which sit next to it. The tab menu keeps "Close", where the tab is implied.
  - Use TR1 for the four catalogues.
- **Visible:** yes.

### FE9. name-differs-from-context-menu: GLOBAL_SHOW_AI_REVIEW has three names, and the Review icons don't pair

- **Tag:** name-differs-from-context-menu, no-icon (wrong icon pairing).
- **Evidence:**
  - Names: Edit › Review › "Show panel" (`Main.qml:10500`), Grid row "Show in Review panel" (`Main.qml:7419`), and the shortcut name "Review panel" (`hotkeys.cpp:751`).
  - Icons:
    - Next AI change uses `chevron-down` and Previous AI change uses `chevron-left` (`Main.qml:3268-3269`). There is no `chevron-up`, while Find next and Find previous use the pair `match-next` and `match-previous` (`:3303, 3310`).
    - The Review ▸ submenu takes `review-keep`, the icon of its first row. The panel's own icon is `review-panel` (`Main.qml:672`).
- **Recommend:**
  - Use "Show in Review panel" in Edit › Review too, and as the shortcut name (FE8 then reads "Show in Review panel").
  - Give Previous AI change a new `chevron-up` icon, the partner of `chevron-down`, which the Grid row menu also uses. Alternatively, use `match-next` and `match-previous` in both menus.
  - Give the Review ▸ submenu the `review-panel` icon.
- **Visible:** yes.

### FE10. missing-expected: duplicate mnemonics in File and Edit

- **Tag:** missing-expected (keyboard).
- **Evidence:**
  - File: **S** on "Recently opened &subtitles", "&Save" and "&Settings" (`Main.qml:2987, 2294, 2370`).
  - File: **a** on "Save &all" and "Save &as…" (`:2300, 2310`).
  - File: **e** on "Remove subtitles from the &editor" and "&Export as…" (`:2272, 2316`).
  - Edit: **r** on "&Redo", "So&rt all lines" and "So&rt selected lines" (`:2381, 3206, 3227`).
  - Underlines show with Alt (`MainTitleBar.qml:50`).
  - **Uncertain:** whether Qt Quick's Menu cycles through duplicates or fires the first. Either way, one letter can't reach each row.
- **Recommend:** give each letter to one row per menu, and leave rarely used rows without one, as Find next already has none:
  - File: &Open subtitles…, Open &video…, Open &folder…, &Recently opened subtitles ▸, &Save, Save &as…, Save a&ll, Save &translation…, &Export as…, &Close tab, Close pro&ject, Setti&ngs, Close &window, E&xit. "Remove subtitles from the editor" loses its &e. The letters O V F R S A L T E C J N W X are each used once.
  - Edit: &Undo, &Redo, Sor&t all lines, Sort s&elected lines. History H, Review V, Select lines L, Find F and Replace P stay.
- **Visible:** yes (underlines).

### FE11. text-style: File and Edit status-bar help breaks the help rule

- **Tag:** inconsistent-name (text-style "Status-bar help: one clause in the third person, starting with the verb").
- **Evidence:**
  - Imperative or bare-noun help:
    - "Open subtitle file" (`:2971`)
    - "Save current file" (`:3056`)
    - "Save all subtitles" (`:3063`)
    - "Save as" (`:3070`)
    - "Save translation" (`:3084`)
    - "Save subtitles using the video name" (`:3091`)
    - "Last session options" (`:3111`)
    - "Program settings" (`:3157`; text-style lists "program settings" under "Not")
    - "Close this window; …" (`:3164`)
    - "Exit the program" (`:3174`)
    - "Undo", "Redo", "Undo to last save", "History" (`:3192-3254`)
  - "Opens video file" (`:3032`) drops its article.
  - "Sorts all lines in ASS file" and "Sorts selected lines in ASS file" (`:3203, 3224`) say "ASS file", where text-style uses "document".
  - No help at all: Close (`:3017-3022`), the Review ▸ submenu and its 7 rows (`:3262-3274`; the actions at `:10430-10505` have none).
  - "Remove subtitles from the editor" (`:3012`) only repeats its label.
- **Recommend:** for example:
  - "Opens a subtitle file"
  - "Opens a video file"
  - "Saves the document"
  - "Saves every open document"
  - "Saves the document under a new name or format"
  - "Saves the translation as a new file"
  - "Names new files after the video"
  - "Opens Settings"
  - "Closes this window; closing the last window exits the program"
  - "Exits the program"
  - "Undoes the last change", "Redoes the last undone change"
  - "Undoes every change since the last save"
  - "Shows the list of changes"
  - "Sorts every line in the document" and "Sorts the selected lines"
  - "Closes the current tab"
  - "Keeps the AI's change to this line", and so on for the Review rows.
- **Visible:** yes (status bar on hover).

### FE12. code: File › Recently opened subtitles builds its help from translated pieces

- **Tag:** inconsistent-name (text-style: "Never build a sentence out of translated pieces").
- **Evidence:** `Main.qml:2996` `qsTr("Open") + " " + modelData.path`. C16 raised the same pattern for Home's `RecentList.qml`; this is a second copy, in the menu bar.
- **Recommend:** `qsTr("Opens %1").arg(modelData.path)`, fixed in the same PR as C16.
- **Visible:** no.

### FE13. toggle-hides-state, uncertain: "Save subtitles using the video name" reads as a command

- **Tag:** toggle-hides-state.
- **Evidence:**
  - `Main.qml:2331-2342`: a checkable Action for the setting `subtitles.saveWithVideoName` (legacy SUBS_AUTONAMING, `settings.cpp:214`).
  - It sits among the Save commands, and its label is imperative.
  - text-style asks check items to name the state when it is on.
  - **Uncertain:** whether its check is clear enough on screen. I didn't run the app.
- **Recommend:** "[x] Name new files after the video", the last row of the Save group (FE2). The shortcut name and the command id stay "Save subtitles using the video name" only if the owner wants the legacy name kept. Otherwise rename them too (FE8).
- **Visible:** yes.

## Checked and clean

- **Duplicates with context menus:**
  - Save, Save all and Close (document tab, `context_menus.txt:311-332`) have the same labels and icons, and show the same bound keys.
  - Open video… (video menus) matches.
  - Undo and Redo in the text menus are the field's own, with the same label.
  - File › Open video… duplicates Video › Open video…, approved in CX2 28.
- **Icons:** every non-exempt row has one. The only exempt rows are the check items, the {dyn} rows, "None" and the sort keys, as IC1 lists.
- **"…" on window names:** Settings, History and the Review ▸ rows correctly have none.
- **Destructive rows last:** Exit is last in File. Edit has no destructive row apart from Review › Undo all, which follows Keep all within its group, as the Review panel orders them (`ReviewPanel.qml:136-144`).
- **British English and sentence case:** no violations in any File/Edit label.
- **Find next and Find previous:** same order and icon pair as the Find panel.
