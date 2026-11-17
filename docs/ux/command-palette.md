# Command palette, Quick Open and the quick pick

One box to run any command and jump anywhere, as in VS Code ([#1236](https://github.com/altqx/hikari/issues/1236)). The palette and Quick Open are two faces of one popup, the **quick pick**: a field over a list. App flows and plugins use the same popup, and its input-only form, the **input box**. It is drawn in [Quiet](visual-language.md).

The owner asked for it on 2026-11-17. Command conditions come from [#1224](https://github.com/altqx/hikari/issues/1224) (Q629). The plugin API's names for these popups are fixed in [ADR 0034](../adr/0034-plugins-grow-into-an-extension-system.md) (#1221), not here. This document only says how the popups look and behave.

The app doesn't change until the code card lands after the beta.4 tag. Parts marked **Proposed** wait for the owner.

## Opening it

| Command | Default key | Menu row | Opens |
| --- | --- | --- | --- |
| Quick Open | Ctrl+P | File › Quick Open… | The quick pick with an empty field |
| Command palette | Ctrl+Shift+P | View › Command palette… | The quick pick with `>` in the field |

- Both are Global, so they work from every panel, the line editor's text included. Both keys are free on main in every context, and in Aegisub 3.5's defaults.
- Both are in the hotkey list, and the user can change or clear them like any other.
- Pressing either key while the quick pick is open switches it to that mode and keeps what was typed after the prefix.
- A dialog keeps its keys. Neither opens over a dialog.

## Placement and look

The quick pick hangs from the top centre of the active shell window, 8 under the title bar.

| Part | Size | Look |
| --- | --- | --- |
| Frame | 600 wide, or the window's width less 48, at least 360 | `panel`, radius 6, a 1-unit `line` boundary and the popup shadow (none in high contrast), as menus are |
| Title row | 30 high, only when a flow or plugin gives a title | The title in semibold `text`; the step ("2 / 3") and, for a plugin, its name in `muted` at the trailing end |
| Field | 28 high, 6 inside the frame | `field`, radius 4; the accent boundary while focused, as every focused field has |
| Rows | 24 high; 40 with a detail line | 12 rows show, then the list scrolls with the 6-unit themed scroll bar |
| Group heading | 20 high | 12-unit `muted` text, with a 1-unit `line` above it, except the first one; it can't be chosen |
| Foot hint | 24 high, only when the mode has one | 12-unit `muted` text on a `line`-topped strip, saying what Enter does |

It isn't modal. The window behind it keeps drawing and the video keeps playing. There's no backdrop.

**Rows.** From the leading edge:

1. a 16-unit icon in a 24-unit gutter, empty when the item has none, so names line up;
2. the name in `text`;
3. 12 after it, the description in `muted` (for a command, its menu path);
4. at the trailing end, the shortcut in `muted`, in the menus' notation ("Ctrl+Shift+O").

The description elides first, then the name. The current row is `select`. Under the pointer a row takes `hover`. In high contrast the current row also has a 1-unit `accent` boundary, as the rail's active tool does.

**Matched letters** are semibold, in the row's own colour. Colour alone never marks them, so they read the same in all four themes.

**Disabled rows** (#1224) keep their place and are drawn in `disabled`. The reason ("Needs a selection", one phrase per failing condition, as `commands.list` gives it) replaces the description, in `muted`. The arrows can land on a disabled row, so the reason can be read and announced. Enter on one does nothing; the reason stays shown.

**No matches** is one `muted` line in the list: "No matching commands", "No matching files", "No matches".

## Quick Open

With an empty field, Quick Open lists, in groups:

1. **Recently opened**: the recent files list (File › Open recent, at most 20), newest first. Files that are missing are left out, as Open recent leaves them out. Folders open as a Project, as Open recent does.
2. **Project files**: when the window has a Project, its files that the Explorer would show, by the Explorer's type filter and globs, sorted by path. A file that is also in the first group shows only there.

Each row has the file kind's icon, the file's name, and its folder in `muted`: relative to the Project inside it, else the full path with the home folder as `~`. A file open in a tab of this window has `open` in `muted` at the trailing end.

- **Typing** matches the file's name first, then its path. Letters match in order, not necessarily next to each other (`spfr` finds `sprite-fright.ass`). Matches at word starts and in the name rank higher. Among equal matches, the more recently opened file comes first.
- **Choosing** does what Enter does on the file in the Explorer. A subtitle file opens in a new tab, or goes to its tab. A video, audio, keyframes or timecodes file becomes the current tab's.
- **Without a Project**, the list shows only recent files. The foot says "Open a folder as a project to search its files."
- **Large Projects:** a list over 5000 files fills in while the field is used; the field shows the busy bar until it's done.

The placeholder says: "Search files by name. Type ? for the other searches."

### Prefixes

A prefix at the start of the field switches what is searched. A space after the prefix is allowed and ignored. Backspace on a field holding only the prefix removes it and goes back to files.

| Prefix | Searches | Enter | Status |
| --- | --- | --- | --- |
| `>` | Commands (see [The command palette](#the-command-palette)) | Runs the command | As VS Code |
| `:` | Lines by number: `:46` | Makes line 46 the active Line, selects it and scrolls the Grid to it. The video seeks to its start when it follows the active Line. | Proposed |
| `@` | A time or a frame, in Jump to's grammar ([video-controls.md](video-controls.md#jump-to)): `@3:40`, `@5200`, `@+5s`, `@-12f` | Seeks the video, as Jump to does | Proposed |
| `#` | The Document's styles | Applies the style to the selected Lines, as one undo step | Proposed |
| `view ` | Panels and layouts | Shows and focuses the panel, or switches to the layout | Proposed; the same word as VS Code |
| `?` | The prefixes themselves, one row each | Puts that prefix in the field | As VS Code |

**`:` Lines.** The row shows the Line's number, its start time in `muted` monospace and its text, elided. A number past the end is the validation line "Not a line: 500. This Document has 412 lines." With no number yet, the foot says "Type a line number, 1 to 412."

**`@` Times and frames.** One row says where Enter goes: "Go to 0:03:40.00 (frame 5280)". Input it can't read gives Jump to's validation line, with Jump to's wording. Without a video, a time selects the first Line showing at that time, or the next one after it, and a frame or step is refused: "Frames need a video."

**`#` Styles.** Rows show each style's name, its font and size in `muted`, and how many Lines use it at the trailing end. The foot says what Enter does: "Enter applies "Sign" to 3 lines · Shift+Enter selects its 12 lines". With no Lines selected, Enter's row is disabled with "Needs a selection", and Shift+Enter still works.

**`view ` Panels and layouts.** The groups are **Panels** (every panel the View menu lists, plugin panels with the plugin's name in `muted`) and **Layouts** (the layout switcher's entries). A closed panel opens where it was last. Plugins don't add prefixes.

## The command palette

`>` lists every command the user can run:

- HikariSub's own commands, from every menu and every hotkey context, including those that only have a key;
- the AI's commands;
- the Lua macros the user loaded, by their names;
- plugin commands, by their titles.

The description is the command's menu path ("Video › Zoom", "Automation › Line tools"). A command that has no menu row shows its group from the hotkey list instead. A plugin command's `description` is the row's tooltip and accessible description.

Menu-only entries that are lists of things (Open recent's files, the layout entries, the windows) aren't commands here; Quick Open and `view ` reach them.

- **Recently used first.** With nothing typed after `>`, the ten most recently run commands come first, under **Recently used**, then every other command, A to Z, under **All commands**. While typing, a recently used command ranks above an equal match. The list is kept across sessions. **Clear command history**, a command with no key, empties it.
- **Matching** works as it does for files, over the name, then the menu path. `>jump` and `>jt` both find Jump to; `>auto` also finds every command under Automation.
- **Disabled commands** show greyed with their reason (#1224). A Lua macro whose validation fails has the reason "Not available here", because Automation gives no reason.
- **Running** closes the quick pick first, then runs the command with `via: "palette"`, so a command that opens a dialog or its own quick pick opens it in the cleared place. Running records the command as recently used.

## The shared quick pick and input box

The palette and Quick Open are uses of one component. App flows and plugins get the same component, with these parts.

### Picking from a list

- **Items** have a name, an optional description, an optional detail line (a 40-high row), an optional icon from the K1 set and an optional disabled reason. They're drawn as above.
- **Placeholder**: the field's `muted` hint. The caller says it.
- **Groups**: optional headings, drawn as above.
- **Picking one**: Enter, or a click, answers with that item and closes.
- **Picking several**: each row gets a leading check box (in the icon gutter's place), and the title row gets an **OK** button (24 high, the accent) and "3 picked" in `muted`.
  - A click on a row or its box toggles it.
  - Tab moves from the field into the list; there Space toggles the current row and Ctrl+A picks every row shown. Typing a letter goes back to the field.
  - Enter, from the field or the list, answers with the picked set. Items can start picked.
- **Busy**: while the caller is still filling the list or checking input, a 2-unit `accent` bar runs along the field's bottom edge. The list keeps what it has.

### The input box

The same frame with the field and no list:

- **Value**: it can start with text in it, selected.
- **Prompt**: one `muted` line under the field saying what to type ("A name for the thread").
- **Validation** shows as one line under the field, in place of the prompt, at three levels:
  - **error**: the field's `danger` boundary and the line in `danger` with the warning icon, as Jump to's. Enter is refused, and the field stays open with the text kept.
  - **warning**: the line in `warning` with the warning icon. Enter is allowed.
  - **information**: the line in `muted` with the information icon.
- Checks run as the user types. Enter while a check is pending waits for it.
- **Secret input** shows dots, with Show at the field's trailing end, as a plugin's secret setting does.

### Steps

A flow of several questions (a name, then a place) keeps one frame. The title row shows the step ("2 / 3") and a Back tool button at its leading edge from the second step on. Alt+Left goes back too. Each step keeps its answer when the user goes back.

### Closing

- Esc, a press outside, or the window losing focus closes the quick pick and answers *cancelled*.
- Only one quick pick is open per window. Opening a new one closes the old one, which answers *cancelled*.
- Closing returns the keyboard to where it was before.

### When a plugin opens it

- **The plugin is named.** The title row is always shown, with the plugin's name in `muted` at its trailing end, even when the plugin gives no title. That way a plugin's question never looks like HikariSub's own.
- **Only when the user asked.** A plugin can open one only while a command, menu item, key, status item or panel click of its own that the user started is running, or from its job. Otherwise the call is refused. It can never open over the user's typing on its own.
- **Plain text.** Names, descriptions, details, the prompt and the validation message are plain text, never markup, as notices are.
- **Within the Quiet look.** Plugins choose the words, the icons (from the K1 set and the plugin's own icons, as its commands use), the groups and the validation level. They don't choose colours, sizes or placement.
- **Closed with the plugin.** When the plugin stops, is turned off or crashes, its quick pick closes.

How the plugin asks for these things (the operation, its parameters and its answers) is ADR 0034's.

## App flows that move to the quick pick

The PK1 rule changes. A single list or a single line of text asked for from a menu, a hotkey or a flow is now the quick pick or the input box, not a compact window. Asked from a control on screen, it stays a popover at that control.

| Today | Kind today | Becomes |
| --- | --- | --- |
| Choose the audio track (audio file open) | List chooser, compact window | Quick pick, titled "Choose the audio track". Enter opens; Esc cancels the open, as today |
| Choose the subtitle track (Matroska open) | List chooser, compact window | Quick pick, titled "Choose the subtitle track" |
| Rename thread (Agent panel's menu) | Modal dialog window | Input box with the name selected. An empty name is a validation error, and the same name closes it without a change, as today |
| Rename… (node mode bar's menu) | Popup with Rename and Cancel | Input box, the node's name checks as validation |
| Switch layout from the keyboard | — | `view ` in Quick Open |
| Go to a Line by number | — (no command today) | `:` in Quick Open |

These stay as they are:

- **Fix suggestions** stay a list at the misspelt word. They're opened from text on screen.
- **Rename font catalogue** stays a popover at its tool strip.
- **The visual tag picker** stays a popover at the all-tags tool.
- **Jump to** stays in place in the transport row ([video-controls.md](video-controls.md#jump-to)); `@` reuses its grammar.
- **Save layout as** and **Split every** stay dialogs, because each asks for two things at once.
- **Choose styles** stays, because it previews live.
- Automation macros' own dialogs (Aegisub's `aegisub.dialog`) stay windows, for compatibility.

## Full screen

The quick pick works in both full-screen modes. Ctrl+P and Ctrl+Shift+P are Global there too.

- **Where.** It hangs from the top centre of the screen: in full-screen typesetting ([full-screen-typesetting.md](full-screen-typesetting.md)), 8 under the top bar; in Simple mode ([#1207](https://github.com/altqx/hikari/issues/1207)), 48 from the top, over the picture. The video keeps playing.
- **Look.** It follows the app's theme, as typesetting mode's panels do, also over Simple mode's black.
- **Simple mode's strip** stays shown while the quick pick is open, so the time stays in sight.
- **Esc** closes the quick pick first. The next Esc leaves full screen, as before.
- **Commands that need the window** leave full screen first, then run. These are showing a panel through `view ` and opening a dialog, as a toast's Details does today. Everything else runs in full screen. Typesetting mode's own regions (the rail, the inspector, the line box) are under `view ` in typesetting mode only, and focusing one stays in full screen.
- **`:` and `#`** work in full-screen typesetting on the line box's Line: `:46` steps the line box to Line 46, and `#` applies to the active Line or the picked Lines, as the style picker does.

## Keyboard

| Key | In the field | In the list (picking several) |
| --- | --- | --- |
| Up, Down | Move the current row; wrap at the ends | Same |
| PgUp, PgDn | Move by a page | Same |
| Home, End | Move in the text | First, last row |
| Enter | Choose the current row, or answer (picking several) | Answer |
| Shift+Enter | The mode's second action, when its foot names one | — |
| Space | Types a space | Toggles the current row |
| Ctrl+A | Selects the text | Picks every row shown |
| Tab, Shift+Tab | Into the list when picking several, else nothing | Back to the field |
| Alt+Left | Back a step | Same |
| Backspace on a bare prefix | Back to files | — |
| Esc | Close, answering *cancelled* | Same |

The pointer works throughout: a click chooses, the wheel scrolls, and a press outside closes.

## Accessibility

- The field is an editable combo box. Its accessible name is the mode: "Quick Open", "Command palette", or the title the flow or plugin gave. Its description is the placeholder or the prompt.
- The list is a list. Each row's accessible name is its name. Its description joins the rest: the description or menu path, the shortcut, "unavailable: Needs a selection" when disabled, and "picked" or "not picked" when picking several.
- Focus stays in the field while the arrows move the current row. Each move announces the row, as the tag list popup does today.
- After typing settles (300 ms), the count is announced politely: "23 commands", "No matching files".
- A validation message is announced when it appears. An error is announced as an alert.
- Group headings are announced with the first row under them, never as rows.
- Everything above holds in all four themes. The current row in high contrast has the `accent` boundary as well as `select`, so it isn't told by fill alone. The keyboard focus ring stays on the field.

## Open questions for the owner

1. **Prefixes.** Are `:` (a Line by number), `@` (a time or frame), `#` (a style) and `view ` (panels and layouts) right, as well as VS Code's `>` and `?`?
2. **`#` Enter.** Should choosing a style apply it to the selected Lines (Shift+Enter selects every Line using it), or the other way round?
3. **`@` without a video.** Should a time select the Line showing at that time, or should `@` need a video?
4. **Plugins only when the user asked.** Can a plugin open a quick pick or input box only while a command or click the user started is running (or its job), and is it always titled with the plugin's name?
5. **Rename thread and the node Rename.** Should they move to the input box, and the two track choosers to the quick pick, as the table says?
6. **Menu rows.** Should Quick Open go under File and Command palette under View?
