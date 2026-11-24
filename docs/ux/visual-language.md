# Quiet visual language

## Status and evidence

**Accepted design baseline: Quiet** ([#1205](https://github.com/altqx/hikari/issues/1205), [ADR 0033](../adr/0033-quiet-visual-language.md)). On 2026-11-17 the owner chose direction A, Quiet, from the three Gate B boards ("yes to all and i like the A", Q581 on tracking [#1209](https://github.com/altqx/hikari/issues/1209)). Two parts come from direction C, Studio: the 20-unit transport timecode and the full-screen strip. Quiet replaces Compact Studio, the D study that the owner approved on 26 September 2026 ([#32](https://github.com/altqx/hikari/issues/32#issuecomment-5849386836)). Compact Studio's density rule stays: Quiet is as dense or denser everywhere.

Quiet follows the chrome of VS Code and Zed:

- one flat surface, with panels split by 1-pixel lines and no gutters;
- radius 4 on controls;
- 24-unit controls and 30-unit panel headers;
- the Document tabs in the title bar.

The chrome steps back so the Document carries the screen.

**What the evidence is.** The Gate A inventory is real screenshots of main 344efa574 in all four themes at 1920 × 1080 and 1366 × 768, with 25 findings. The Gate B boards are HTML mockups drawn with the real icon set. Neither is committed; both use Sprite Fright (© Blender Foundation | studio.blender.org, CC BY 4.0). [Fixes by area](#fixes-by-area) carries the 25 findings, so code cards can be cut from it.

**Where the app is today.** The four built-in themes are theme files in `src/ui/themes`, read by the same loader, schema and contrast check as plugin themes, and they carry Quiet's roles and accents. The shared controls in HikariStyle draw Quiet's state table. The windows and panels outside the shared controls still keep Compact Studio's layout until their #1205 area lands. `hikari_themes_tests` checks the built-in files against the schema and the floors, and `hikari_ui_theme_tests` measures the roles the theme layer resolves, so a change to a role updates both tests' expected values and keeps its contrast floors.

HikariSub owns the Qt application layer under [ADR 0001](../adr/0001-hikari-owned-qt-layer.md). The vocabulary of **workspace**, **editing target** and **protected reference** comes from [CONTEXT.md](../../CONTEXT.md). Panel membership, workspace ownership and document behaviour belong to the [workspace specification](workspaces.md); this document defines their visual and interaction presentation.

**Restyle only.** These take Quiet's tokens (surfaces, radii, heights, type) and are not redesigned:

- the menus and the menu bar (CX7, [#810](https://github.com/altqx/hikari/issues/810));
- the Agent panel ([#1189](https://github.com/altqx/hikari/issues/1189));
- the icon set ([icons.md](icons.md)).

## Composition

Quiet has square-cornered panels on one flat surface, 1-pixel `line` splits between them, rounded controls and monospaced tabular data. The default editing scene is unchanged: Video at the left, Audio above the Line editor at the right, and the Grid across the full width below both, with the side bars of [ADR 0032](../adr/0032-side-bars.md) outside it. Use about 44 % / 56 % for the upper split.

**One row of chrome at the top.** The main window's title bar holds the menus, the layout switcher and the Document tabs in one 36-unit row on `bg`. The Document tab row below it goes. Each panel then has one 30-unit header row, and the Grid's header carries its filter field, so the empty strip above the column headings goes. A side bar's icon strip is the panel header of the panel it shows: the strip's icons on the left, that panel's ⋯ at the trailing end, with no second title row under it.

**Panels.** A panel is `panel` with no frame and no radius. Neighbouring panels are split by a 1-pixel `line`, with no gutter (Compact Studio had 3). The application background shows only in the title bar and the status bar. In the high-contrast themes the splits are the border pick, as today.

**Panel header.** The dock header row stays the only panel header (D3, [#207](https://github.com/altqx/hikari/issues/207), [docking.md](../docking.md#chrome)), restyled:

- **A lone panel:** a 30-unit row on `panel`, with the title 12 from the leading edge in 12-unit semibold sentence case (no uppercase) and the ⋯ button at the trailing end.
- **A group of tabs:** the same row with tabs in place of the title. The selected tab is `text` in semibold over a 2-unit `accent` underline; the others are `muted` and regular, with no separators. A hovered tab takes `hover`.
- **The panel's toolbar** sits after the tabs, as today.
- **The focused panel** keeps the 8-unit accent triangle in its top trailing corner (see Keyboard focus).
- **A floating panel** is a borderless window in `panel`, with a 1-unit `line` boundary, 6-unit corners and the popup shadow.
- **Drag feedback** is the only accent in the dock chrome: a 1-unit border and a 30 % fill over the area a dragged panel would take.
## Semantic appearance tokens

All components share the same semantic roles, and switching theme switches the whole palette. Light and Dark are Quiet's own values. The high-contrast themes keep K2's fixed values, the same in every direction. The default accent is green; the [accent presets](#hikari-theme-layer-k2) set `accent`, `onaccent` and `select`.

| Role | Variable | Dark | Light | High contrast white | High contrast black |
| --- | --- | --- | --- | --- | --- |
| Application background: title bar, status bar | `bg` | `#181B20` | `#EEF0F3` | `#FFFFFF` | `#000000` |
| Panel, dialog and popup surface | `panel` | `#1E2127` | `#FFFFFF` | `#FFFFFF` | `#080808` |
| Raised surface: button, table header, hovered menu title | `raised` | `#2A2E35` | `#EEF0F3` | `#EBEBEB` | `#151515` |
| Input / recessed surface | `field` | `#15171B` | `#FFFFFF` | `#FFFFFF` | `#000000` |
| Primary text | `text` | `#E4E7EB` | `#1E2329` | `#000000` | `#FFFFFF` |
| Secondary text | `muted` | `#A0A8B2` | `#56616C` | `#1A1A1A` | `#EEEEEE` |
| Split and separator | `line` | `#2C3037` | `#DCE0E5` | `#000000` | `#FFFFFF` |
| Operated control's boundary | `control` | `#757E89` | `#7A838D` | `#000000` | `#FFFFFF` |
| Disabled text, icons and boundaries | `disabled` | `#737B85` | `#7A838D` | `#6E6E6E` | `#8C8C8C` |
| Action / active accent (green) | `accent` | `#9CDBC9` | `#145C4C` | `#0037B3` | `#FFFF00` |
| Text on accent | `onaccent` | `#102C24` | `#FFFFFF` | `#FFFFFF` | `#000000` |
| Selected background (green) | `select` | `#304C47` | `#D6EBE4` | `#C9DAF8` | `#253F60` |
| Hover overlay | `hover` | `text` at 6 % | `text` at 5 % | `text` at 8 % | `text` at 12 % |
| Pressed overlay | `pressed` | `text` at 12 % | `text` at 10 % | `text` at 16 % | `text` at 24 % |
| Keyboard focus | `focus` | `text` | `text` | `text` | `text` |
| Error | `danger` | `#FFADAD` | `#A51F31` | `#A00000` | `#FFADAD` |
| Warning | `warning` | `#E0A030` | `#8A5A00` | `#6B4500` | `#FFD54A` |
| Success | `success` | `#4DBA4D` | `#008000` | `#005A00` | `#7CFC7C` |

In theme files ([ADR 0034](../adr/0034-plugins-grow-into-an-extension-system.md)) `bg` is spelled `background` and `onaccent` is `onAccent`; the roles are the same.

Changes from Compact Studio:

- **Surfaces.** The four Light and Dark surfaces change. In Light, `panel` and `field` become white on a cooler `bg`. In Dark, `field` sits below `panel`.
- **`line`** is softer (Dark `#414B57` → `#2C3037`, Light `#AAB5BE` → `#DCE0E5`). It now only splits panels and rows; operated controls keep their 3:1 boundary in `control`.
- **`hover` and `pressed`** are new roles: overlays of `text` over whatever surface they sit on. Hover never uses the accent.
- **Text, `muted`, `control` and `disabled`** move with the surfaces. Their contrast is in the role table below.
- **Unchanged:** the accent presets and their `select` values, `danger`, `warning` and `success`. The high-contrast themes don't change at all.

## Hikari theme layer (K2)

The user decided on 2026-10-05 to follow MuseScore 4's appearance model ([research at v4.7.5](../research/musescore-appearance.md)) with no per-colour editing. [K2](https://github.com/altqx/hikari/issues/206) implements it as the Hikari theme layer, `src/ui/theme.h`: one set of the roles above, resolved from the chosen appearance and exposed once. QML reads the `Theme` singleton (`Theme.panel`, `Theme.accent`, ...); the Qt Quick Controls draw with the application palette the layer sets, in the `HikariStyle` controls style (`src/ui/style`: Fusion on every platform, since the native Windows style draws with the system's colours, with every operated control's outline in `control`, keyboard focus drawn as the 1-wide focus ring below (`FocusFrame`, in `focus` from `StyleColours`, which the layer sets with the palette), and a focused field's border from the accent; Fusion's own outline, its window colour darkened 140%, measures about 1.1:1 against the Dark and High contrast black panels, so fields and buttons lost their boundaries there; `hikari_ui_theme_tests` controlsOutlineInTheBoundaryColour, keyboardFocusRingInTheTextColour); controls the shell draws itself take `FocusRing`, the same ring (the Appearance page's cards and swatches); the icons take `IconTheme`'s colours, which are the layer's; owner-drawn items (the Grid, the audio display, the spelling marks) read the layer's roles and content colours and repaint when it changes.

**Themes.** Light, Dark, High contrast white and High contrast black (`appearance.theme`: `light`, `dark`, `highContrastWhite`, `highContrastBlack`; Dark by default, legacy's default theme). High contrast black is the spec's high-contrast column; High contrast white is drawn to match it on white. The four are theme files in the app's resources carrying Quiet's tokens, read by the same loader and held to the same contrast check as plugin themes ([ADR 0034](../adr/0034-plugins-grow-into-an-extension-system.md), Q672). A plugin theme is `light`, `dark`, `highContrastLight` or `highContrastDark`; plugins may ship high-contrast themes that pass the high-contrast floors (Q673). The theme file sets the roles and content colours below; the ones worked out from others (`focus`, `hover`, `pressed`, `control` in high contrast, `onaccent`, the syntax default and the selection colours) may be left out ([plugins.md](../plugins.md#extensions), Q676). The tables here are the built-in files' values.

**Follow system theme** (`appearance.followSystem`, on by default). While on, Light or Dark comes from the platform's colour scheme (`QStyleHints::colorScheme`, live: the Windows setting, the XDG desktop portal on Linux); it never turns high contrast on or off, it only moves a high-contrast theme to its matching side (white for a light scheme, black for a dark one). An unknown scheme keeps the chosen theme. Which theme each side shows is `appearance.lightTheme`, `appearance.darkTheme`, `appearance.highContrastLightTheme` and `appearance.highContrastDarkTheme` (the built-ins by default), so a plugin theme can be one side of a pair; choosing a theme makes it its type's member of the pair, and choosing one of the side the system isn't on turns following off.

**Window title bars.** On Windows the title bar Windows draws on a framed window (dialogs and tools in their own windows, and the main window with "Use system title bar"; floating panels and the main window's own title bar are frameless) follows the shown theme: dark under Dark and High contrast black, light under Light and High contrast white, whether the theme follows the system or is chosen in Options, and live on every switch (`src/ui/title_bars.h`, `DWMWA_USE_IMMERSIVE_DARK_MODE`). While the theme does not follow the system, Qt is asked for the theme's side (`QStyleHints::setColorScheme`, on Windows only), so that what Qt sets on a frame itself agrees. Other window systems draw their decorations from the desktop's own theme, and Qt's colour scheme is left to the system there.

**The main window's title bar (SH1, [#220](https://github.com/altqx/hikari/issues/220)), restyled.** After MuseScore 4's (MS1), in Quiet's form: one 36-unit row on `bg`, left to right the application icon (16 px in a 36-unit square), the menu titles (26 high, 8 on each side), the layout switcher 8 after them (transparent, `raised` under the pointer and while open), the Document tabs (each `muted` at rest, the active one `panel` with `text` and a 2-unit `accent` bar along its top, its dirty mark and close button inside it; the new-tab button after them), then the two side-bar toggles as 24-unit tool buttons and the window buttons (46 × 36: Minimise, Maximise or Restore, Close; IC1's 16-unit glyphs). The window's title is the window system's and the tabs', so the row draws none. A menu title is `raised` under the pointer and while its menu is open; the window buttons are `raised` under the pointer and take `pressed` over it when pressed (the high-contrast themes: `select`, with a 1-unit `line` ring), Close included (no red). While the window is not active the titles, tabs and glyphs take `disabled` and the icon 55 % opacity. Tabs that do not fit scroll, keeping the active one whole, and a narrower window folds the last menus into More (⋯). The keyboard on the menus (Alt alone, F10) rings the current title or the switcher in `focus`, 1 wide, and underlines the menus' letters, which also show while Alt is held and never otherwise. In the high-contrast themes a 1-unit `line` parts the row from the panels. On Wayland and X11 the frameless window has a 1-unit `line` boundary while not maximised and no drawn shadow; Windows draws its own shadow and boundary. With "Use system title bar" the window system's frame stays and the row, 30 high, holds the menus, the switcher and the Document tabs.

**Dialogs (DG1, [#222](https://github.com/altqx/hikari/issues/222)).** One frame for every dialog, `src/ui/DialogWindow.qml`, after MuseScore 4's (MS1, docs/research/musescore-shell.md §4 and §7 DG1): a top-level window of its own, transient for the window it was opened from (the main window, a floating panel, the Style manager) and closed with it. Its title bar is SH1's style in one 32-pixel row on `panel` (`DialogTitleBar.qml`): the title (the command that opens the dialog) 12 from the leading edge, elided before the buttons, and at the trailing edge Close and, only while the dialog can be resized, Maximise (Restore while maximised), 46 × 32 with SH1's hover and press, Close's too; no Minimise and no application icon. While the dialog is not active the title and glyphs take `disabled`; in the high-contrast themes a 1-pixel `line` parts the row from the body. The frame is SH1's (`DialogFrame`, window_frame.h): on Windows the native one, which drops the minimise box and, on a fixed-size dialog, the sizing frame and maximise box, so Aero Snap and the system menu leave it its size; on Wayland and X11 frameless with the 1-pixel `line` boundary, and the resize band only on a resizable dialog. With "Use system title bar" the window system's frame stays and no row is drawn. The body sits 24 inside the window's sides (12 under the drawn row, 24 under the system's) on `panel`; the footer, 24 below it and 24 from the edges, holds extra actions at the leading edge and the closing buttons at the trailing one in the platform's order (WindowFooter), 8 apart, the default button in the accent. A fixed-size dialog is its content's size; a resizable one starts there and can only grow. The first open is centred on the opener, 20 lower, kept 20 inside the screen's free area and shrunk to fit; later opens are where it was left and as big, per dialog (`workspace.dialogGeometry`, dialog_placement.h; a fixed-size dialog keeps its position only, a place off every screen falls back to the first rule, and on Wayland only the size is kept). Esc closes (a control that takes Esc takes it first); Enter presses the focused button, else the default one (a multiline field keeps its Enter); the keyboard starts on `initialFocus`, else the default button with its ring, and goes back to the opener's control when the dialog closes. Modality is the dialog's: modeless by default (opening a shown dialog again raises it), or modal to its opener alone for a question that blocks an operation. About, Credits and the update offers are modeless. A wait the dialog cannot end (PU7's Install update, while the installer runs) is not closable: no Close on the drawn title bar, and Esc and the window system's close do nothing. A task of steps (PU7's Import legacy settings) is a wizard in one dialog: a stepper under the title bar (the current step in the accent, those done ticked), Back at the footer's leading edge and the step's action at its trailing one, the default. In Quiet the dialog's corners are 6 on Wayland and X11, its footer buttons 24 high like every button, and its body's sections follow the spacing scale (12 between rows, 24 between sections).

**Popups (PK1, [#301](https://github.com/altqx/hikari/issues/301)).** Five kinds, each for one job; nothing new is drawn as an in-window Controls `Dialog` or a `MessageDialog` (shell_tests popupsAreOnTheKit holds the ones left, tests/ui/in_window_popups.txt, until the PU cards move them):
- **Window**: a dialog on DG1's frame (above), for a task with its own content.
- **Message box** (`MessageBox.qml`): a question, or a notice that needs an answer (an error that stopped an action, a choice that can lose data); modal to the window it came from. The title names the command (never "Prompt", "Confirmation" or "Info"); a severity icon (information, the warning triangle, the error octagon; a question takes the severity of what it leads to, never a "?"), one main line, an optional detail, the technical text under a Details disclosure with a copy button, and "Don't ask again" where the question can be hidden (Settings → Notices → Ask again brings them back). The buttons are named for what they do; the safe default is filled in the accent, a destructive one outlined in `danger` (`DangerButton.qml`) and never the default.
- **Popover** (`Popover.qml`, ColourPopover's surface `PopoverBackground.qml`): edits a value in place, under the control that opened it (above it without room below; in a window of its own when the host window is too small). Enter, a press outside or the control again keeps the change; Esc puts the value back. Small forms follow one rule: a popover when opened from a control on screen, a compact window when opened from a menu or a hotkey (the hotkey capture is one).
- **Quick pick**: a field over a list, hung from the top centre of the window, for one choice, several, or one line of text (the input box) asked for from a menu, a hotkey or a flow. The command palette, Quick Open, the track choosers and plugins use it ([command-palette.md](command-palette.md), #1236).
- **Notice**: a toast (`ToastStack.qml`) for what needs no answer, at the window's bottom right, the newest on top, three at most (a fourth drops the oldest information toast, or folds the oldest warning into "N more notices", which opens the Log); information fades after 5 seconds unless the pointer is on it, warnings and errors stay until ×; every notice goes to the Log too, and with Settings → Notices → "Show notices as toasts" off only there. An inline bar (`InlineBar.qml`) for a state that lasts and has its own actions (the update-ready bar), under the menus; it never takes the keyboard.

The forms built on them: the clash review (PK1-clash-review), Choose styles (PK1-choose-styles-live), the hotkey capture (PK1-hotkey-capture), the list chooser (PK1-list-chooser, which becomes the quick pick) and the date picker (PK1-date-picker).

File and session popups (PU1, [#303](https://github.com/altqx/hikari/issues/303)): a state that belongs to one Document is an inline bar under the Document tabs, shown only while that Document is the editing target (a file changed on disk, each file a restore could not find, an MKV being read); the startup offer is a card on the Home page, or the same offer as a bar when files were opened from the command line, and never holds the start back. A question about several Documents (unsaved work) is a window with a row per Document; about one, a message box. The file pickers are the native ones, titled for what they do or the file they are for ("Save “name” as"), each kind starting where it was last left.
**Notices and the Log (NT1, [#302](https://github.com/altqx/hikari/issues/302)).** Which kind a notice takes: one that reports an outcome, or a failure the user can read later (nothing was changed, the file did not open), is a toast, errors included; one that refuses what was just asked inside a window, or an error that stopped a command the user waits on (a script that did not load), is a message box with OK, modal to that window. The PU cards follow the same split; docs/ux/notices-inventory.md lists every notice and its kind. The calls, and nothing else:
- `notices.post(severity, title, text, {details, actions})` in QML: a toast and its Log entry. `severity` is `"info"`, `"warning"` or `"error"`; Main's stack is `root.notices`, handed to a component as its `notices: ToastStack` property; `post` returns the toast's id (0 when none shows).
- `LogController::notice(severity, title, text, details)` in C++ (any thread): the same, through the Log, which Main's stack shows (those given before the window is made, a profile's retired hotkeys, show when it is: the last ten); `LogController::log(text, severity, title)` puts a line in the Log only (legacy's silent log; an error with a title when a message box says it).
- `messages.ask(options, done)`: a message box (PK1's `ask` options; `done(id)` also on Esc). Each window that asks keeps its own `MessageHost { id: messages }`, which makes the box at the first question and queues the next; Main's is `root.messages`.

The title names the command that gave the notice (Open subtitles, Save, Find), never "Warning", "Info" or "Error"; the text is legacy's message on one line, and a technical or long part (a list, a script's traceback, the file's name for a player that refused it) goes under Details. A notice's title, text and details show as plain text, never markup: they may quote a file (a lyrics excerpt). A toast's Details opens the Log at its entry (out of fullscreen first). Toasts show above an open in-window dialog, modal or not, so a dialog's own warnings are seen while it is open.

The Log is a panel ("Log", `LogPanel.qml`, `LogController`), along the bottom under the other panels and the whole width, closed by default; nothing opens it by itself. View → Log and View → Panels → Log show and hide it; a toast's Details and "N more notices" → "Open the Log" open it (Details at its entry, marked with `select` and a 2-pixel `accent` bar, shown whatever the filters). Its strip holds three severity toggles with their counts (information, warnings, errors), a filter field that matches the title, text and details (Esc clears it) and Copy, which copies the entries shown; Ctrl+C copies the current entry. Each entry is one row: the time ("HH:MM:SS", `muted`), the severity icon, the title in bold and the text, selectable, with the details below in `muted`. The panel header names it; it has no heading of its own.

The Spellchecker is a panel too (PU6, "Spellchecker", `SpellCheckerPanel.qml`), along the bottom, closed by default: a tool that walks the document while the editor and the Grid stay in reach is a panel, not a window. Showing it (Subtitles → Check spelling, View → Panels) starts at the active Line; a layout that shows it does not. Its options are its "⋯" items, its outcomes toasts, and with nothing to show it says why in one muted line with the one command that helps.

**A running task in the toast corner (AD3, [#312](https://github.com/altqx/hikari/issues/312)).** A long task the user started and may cancel (a macro run) shows as a pinned item at the bottom of the toast stack, under the toasts (`ToastStack`'s default slot, `AutomationProgress.qml`): a one-line notice (its name and a slim bar, busy until it knows the progress) as it starts, growing in place into a card (PK1's Notice kind) once it has run about a second or has something to show. The card has the task, the bar, its log folded or open, Cancel, and a danger-outlined Force stop only after the grace; when the task ends it says how in its title (the outcome's icon and colour: done in the accent, cancelled and problems in `warning`, failed in `danger`) and stays until × only when there is something to read. It is the task's control, not a notice, so it shows with "Show notices as toasts" off too. It never takes the keyboard by itself; F6 reaches it as a region and Esc gives the keyboard back without cancelling. A log line (`RunLogLine.qml`) is the script's plain text in the monospace font after its level's name (error and fatal in `danger`, warning in `warning`, the others muted) and, when times are on, its time since the start; the application's own lines carry an icon in the level's place. The panel that keeps the runs uses the Log panel's filter field and a level threshold in one combo box, which fits a narrow panel where a toggle per level does not.

**Accent.** The accent choices live in each theme file's `accents` (Q674). Built-in Light and Dark carry the seven presets below, in hue order, the spec's green the default; the user's pick is kept per theme in `appearance.accents` (`<theme id>=<choice id>`), which replaces `appearance.lightAccent` and `appearance.darkAccent`; a pick whose choice is gone falls back to the default. A theme without accent choices hides the picker. Each preset sets three roles: `accent`, `onaccent` and `select` (its hue at the green's selected lightness); `focus` does not change with the accent. The accent drives selection, primary buttons, toggles, the selected tab's underline, the focused field border, text selection, progress, the icons' accent layer and the selection-type marks of owner-drawn content.

| Key | Light accent / text on it | Dark accent / text on it |
| --- | --- | --- |
| `red` | `#B3261E` / `#FFFFFF` | `#F4A9A3` / `#2C1210` |
| `orange` | `#9C4600` / `#FFFFFF` | `#F5BB8A` / `#2C1D10` |
| `gold` | `#7A5E00` / `#FFFFFF` | `#E3CF7E` / `#2C2610` |
| `green` (default) | `#145C4C` / `#FFFFFF` | `#9CDBC9` / `#102C24` |
| `blue` | `#1D5BA8` / `#FFFFFF` | `#A6C8F2` / `#101D2C` |
| `purple` | `#5B3FB0` / `#FFFFFF` | `#C6B5F4` / `#18102C` |
| `pink` | `#9E2A73` / `#FFFFFF` | `#EFAAD3` / `#2C1021` |

Measured WCAG 2.x contrast (`hikari_ui_theme_tests` accentPresetsMeetContrast; the swatch sheet `k2-accents.png` and `k2-contrast.txt` are written with `HIKARI_THEME_SHEET_DIR`): the lowest of each accent against its mode's bg, panel, raised and field, text on the accent, and primary text on the preset's selected background.

| Key | Light: on surfaces / text on it / text on selected | Dark: on surfaces / text on it / text on selected |
| --- | --- | --- |
| `red` | 5.73 / 6.54 / 11.47 | 7.17 / 9.20 / 9.37 |
| `orange` | 5.59 / 6.38 / 12.10 | 8.02 / 9.58 / 8.39 |
| `gold` | 5.36 / 6.12 / 12.67 | 8.76 / 9.69 / 7.60 |
| `green` | 6.90 / 7.88 / 12.70 | 8.69 / 9.51 / 7.52 |
| `blue` | 5.91 / 6.74 / 11.76 | 7.90 / 9.86 / 8.92 |
| `purple` | 6.61 / 7.55 / 11.17 | 7.35 / 9.85 / 9.99 |
| `pink` | 6.07 / 6.92 / 11.49 | 7.37 / 9.44 / 9.37 |

**High contrast and UI colours.** The colours a user may change are the ones a theme lists in `userColours`, at most six interface roles (Q675). Built-in High contrast white and High contrast black list `accent`, `text` (text and icons) and `line` (the old border picker), as before; MuseScore's low-vision exception, its non-working "Disabled text" picker left out. The user's colours are kept in `appearance.colours` (`<theme id>/<role>=#RRGGBB`), which replaces `appearance.highContrastWhite.*` and `appearance.highContrastBlack.*`; "Reset to default" puts the theme's back. Every other role is fixed by the theme. Text on a picked accent turns black or white to stay readable. No colour outside a theme's `userColours` is a setting, and there is no general per-colour editing.

**Role tables.** Every theme with its default accent, and each role's lowest contrast against the theme's four surfaces (`hikari_ui_theme_tests` rolesMeetContrast; text roles at least 4.5:1, 7:1 in high contrast; accent, icons, disabled and `control` at least 3:1; `success` at least 4.5:1 outside Light (7:1 in high contrast); `focus` is `text`, focusIsTheTextColour). `warning` is the rewrite's warning emphasis (WINDOW_WARNING_ELEMENTS' role), `success` the Font collector's success text (legacy's fixed `#008000`, FontCollector.cpp:872, 978), `disabled` disabled text and icons.

| Role | Light | Dark | High contrast white | High contrast black |
| --- | --- | --- | --- | --- |
| `bg` / `panel` / `raised` / `field` | `#EEF0F3` / `#FFFFFF` / `#EEF0F3` / `#FFFFFF` | `#181B20` / `#1E2127` / `#2A2E35` / `#15171B` | `#FFFFFF` / `#FFFFFF` / `#EBEBEB` / `#FFFFFF` | `#000000` / `#080808` / `#151515` / `#000000` |
| `text` | `#1E2329` 13.85 | `#E4E7EB` 10.99 | `#000000` 17.62 | `#FFFFFF` 18.26 |
| `muted` | `#56616C` 5.54 | `#A0A8B2` 5.67 | `#1A1A1A` 14.60 | `#EEEEEE` 15.74 |
| `line` | `#DCE0E5` 1.16 | `#2C3037` 1.03 | `#000000` 17.62 | `#FFFFFF` 18.26 |
| `control` | `#7A838D` 3.37 | `#757E89` 3.31 | `#000000` 17.62 | `#FFFFFF` 18.26 |
| `accent` | `#145C4C` 6.90 | `#9CDBC9` 8.69 | `#0037B3` 8.02 | `#FFFF00` 17.01 |
| `onaccent` (on `accent`) | `#FFFFFF` 7.88 | `#102C24` 9.51 | `#FFFFFF` 9.56 | `#000000` 19.56 |
| `select` (`text` on it) | `#D6EBE4` 12.70 | `#304C47` 7.52 | `#C9DAF8` 14.87 | `#253F60` 10.72 |
| `focus` (`text`) | `#1E2329` 13.85 | `#E4E7EB` 10.99 | `#000000` 17.62 | `#FFFFFF` 18.26 |
| `danger` | `#A51F31` 6.48 | `#FFADAD` 7.68 | `#A00000` 7.06 | `#FFADAD` 10.29 |
| `warning` | `#8A5A00` 5.19 | `#E0A030` 6.00 | `#6B4500` 7.11 | `#FFD54A` 12.93 |
| `success` | `#008000` 4.50 | `#4DBA4D` 5.48 | `#005A00` 7.15 | `#7CFC7C` 13.98 |
| `disabled` | `#7A838D` 3.37 | `#737B85` 3.18 | `#6E6E6E` 4.28 | `#8C8C8C` 5.43 |

`success` in Light is legacy's fixed green. It now measures 4.50:1 at its lowest, where Compact Studio's darker `bg` gave 4.21. Dark keeps its readable green, `#4DBA4D` (K2-dark-success, user decision 52, 2026-10-15).

In Light and Dark, `line` (1.16:1 and 1.03:1 at its lowest, against `raised`) is a split, not a boundary. It parts panels, rows, menus, tab strips and group boxes, and it is never the only edge of something the user operates. Against `panel`, the surface it most often splits, it measures 1.33:1 in Light and 1.22:1 in Dark.

Operated controls draw their outline in `control` while enabled. These are the button, text field and area, spin box, combo box, check box, radio button, and the slider's groove and handle. `control` measures at least 3:1 on all four surfaces (K2-control-boundary, the same decision; HikariStyle, `StyleColours.control`).

A tool button has no outline at rest (see [Controls and their states](#controls-and-their-states)). In high contrast, `line` and `control` are both the border pick, the control outlines included.

**Keyboard focus.** A HikariSub design decision, not a legacy departure: keyboard focus follows MuseScore 4's convention ([research](../research/musescore-appearance.md): its `focusColor` is vestigial; the real ring, `NavigationFocusBorder`, is `fontPrimaryColor`), a ring in the theme's text colour (user, 2026-10-05, with the wave-5 batch-2 review). The user's 2026-10-13 decision (wave-5 visual review: the 2-wide rings looked too big and cluttered focused elements) replaces the 2026-10-05 ring's size and the panel's ring: a control's ring is 1 wide and drawn tight, the Grid's current row keeps a 1-wide ring ("grid row should still use the ring too"), and the panel holding the focus loses its ring for a small accent triangle in its top trailing corner. The `focus` role is `text` in every theme and with every accent, and follows a high-contrast text pick; the spec's focus tokens and the presets' own focus colours are withdrawn. The accent stays for selection, the default button and primary actions, the focused field's border, the tab underline, the current Line's outline and the focused panel's corner mark, so a focused control (a text-coloured ring) and selection (an accent fill, marker or outline) are told apart by colour and by shape; on a selected background the ring reads as text does (at least 4.5:1, 7:1 in high contrast).

| Where | Keyboard focus |
| --- | --- |
| HikariStyle controls (`src/ui/style`, `FocusFrame.qml`): Button, ToolButton (and the icon buttons on them), ComboBox, CheckBox, RadioButton, TextField, TextArea, SpinBox, Slider | A ring 1 wide just outside the control, touching its edge (a check box's or radio button's whole control, a slider's handle), while it has keyboard focus: `visualFocus`, or for a field focus by Tab, Backtab or a shortcut. A click's focus draws none. The control's own outline stays `control`; Fusion's accent tint for keyboard focus (combo box, spin box buttons, slider handle) is gone. A focused field keeps the accent's border, with the ring beside it for keyboard focus. |
| TabButton (the dialogs' tab bars, the panel tabs) | A ring 1 wide, 1 inside the tab's edge, where the tab bar would clip one outside it; on a panel's selected tab it stops short of the "⋯" button, which rings itself. |
| Shell-drawn controls (`FocusRing.qml`: the Appearance page's theme cards and accent swatches) | A ring 1 wide just outside the control, while it has keyboard focus. |
| The Grid's focused cell (`line_grid.cpp`) | While the Grid has keyboard focus, the current Line's row: a ring 1 wide just inside its 1-wide accent outline; a selected row's 3-wide accent marker stays over the ring's left side. |
| The panel holding the focus (`PanelFocusMark.qml` on its dock header, `DockTabBar.qml`) | No ring. A filled right triangle in the accent, its legs 8 logical pixels (whole device pixels at any scale), flush with the panel's top and trailing edges: the top right corner, the top left in a right-to-left layout. It is drawn over the header's toolbar and takes no input. The group holding the focus (the engine's `isFocused`; a floating window while active) shows it, the Grid's group included, and it moves with F6, Tab and a click. A floating window of several groups shows it on the focused group's header only; its title bar (`DockTitleBar.qml`) draws none. A header control with keyboard focus (the selected tab, the "⋯" button) rings itself as well. Panels have no accent border (D1 drew a focused panel's border 2 wide in the accent). |

The mark's accent is the high-contrast accent pick in those themes; against the header's surfaces it sits on (`field` under a lone panel's title bar and a selected tab, also the panel's body; `raised` under the tab strip) and the surfaces around a panel (`panel`, the windows' and dialogs' surface; `background`, the application's) it measures at least 3:1 in every theme with every preset (`hikari_ui_theme_tests` panelFocusMarkContrast):

| Theme | Accent | On `field` | On `raised` | On `panel` | On `background` |
| --- | --- | --- | --- | --- | --- |
| Light | Red `#B3261E` | 6.54 | 5.73 | 6.54 | 5.73 |
| Light | Orange `#9C4600` | 6.38 | 5.59 | 6.38 | 5.59 |
| Light | Gold `#7A5E00` | 6.12 | 5.36 | 6.12 | 5.36 |
| Light | Green `#145C4C` | 7.88 | 6.90 | 7.88 | 6.90 |
| Light | Blue `#1D5BA8` | 6.74 | 5.91 | 6.74 | 5.91 |
| Light | Purple `#5B3FB0` | 7.55 | 6.61 | 7.55 | 6.61 |
| Light | Pink `#9E2A73` | 6.92 | 6.07 | 6.92 | 6.07 |
| Dark | Red `#F4A9A3` | 9.44 | 7.17 | 8.49 | 9.08 |
| Dark | Orange `#F5BB8A` | 10.56 | 8.02 | 9.49 | 10.16 |
| Dark | Gold `#E3CF7E` | 11.53 | 8.76 | 10.36 | 11.09 |
| Dark | Green `#9CDBC9` | 11.44 | 8.69 | 10.28 | 11.01 |
| Dark | Blue `#A6C8F2` | 10.40 | 7.90 | 9.34 | 10.00 |
| Dark | Purple `#C6B5F4` | 9.67 | 7.35 | 8.69 | 9.30 |
| Dark | Pink `#EFAAD3` | 9.71 | 7.37 | 8.73 | 9.34 |
| High contrast white | Accent pick (default) `#0037B3` | 9.56 | 8.02 | 9.56 | 9.56 |
| High contrast black | Accent pick (default) `#FFFF00` | 19.56 | 17.01 | 18.65 | 19.56 |

Spacing for the ring: a control's ring needs 1 pixel beyond it. The Line editor's scroll brings a focused control into view with 1 pixel to spare (it kept 5 for the 2026-10-05 ring). Views that clip a control at their edge keep it 1 inside: the visual tool rail's buttons (its frame's padding 2 became 1, the buttons 1 inside its scrolled area, where they were), the Shift times form (1 inside its scrolled area), and the recovery, auto save, unresolved restore and Misspelling replacer result lists (1-pixel margins). The panels' 4-pixel body margin and the controls' 8-pixel spacing are the layout's rhythm, older than the ring, and stay.

Validation: `hikari_ui_theme_tests` focusIsTheTextColour (every theme, every preset, the high-contrast text picks; focus never the accent or the selected background), keyboardFocusRingInTheTextColour (each control above in all four themes: the ring shown for keyboard focus only, 1 wide, touching the control's edge or 1 inside a tab, drawn in the text colour, the control's outline in `line`, the default button and a focused field in the accent; a high-contrast text pick; without a profile the palette's text colour), panelFocusMarkContrast; `hikari_ui_line_grid_tests` focusRingOnTheCurrentRow; `hikari_ui_shell_tests` focusedPanelShowsTheAccentCornerMark (the mark on the focused panel alone, the Grid's and the Line editor's in turn, hidden on the others, in the accent and following an accent or theme change live, no header or panel ring; the Grid's current row ringed 1 wide while it has keyboard focus and not after), floatingPanelShowsTheCornerMark, panelFocusMarkMirrorsRightToLeft, appearancePagePreviewsSavesAndRestores (the cards' and swatches' 1-wide ring). Not covered: the controls the style leaves to Fusion (lists' delegates, menu items, scroll bars) keep Fusion's focus drawing; the panel's mark follows the engine's focus tracking, so a click into a panel marks it as keyboard focus does.

**Content colours** are fixed per theme, not settings: the audio display (waveform, spectrum, cursor, boundaries, keyframe and second marks, the timescale), the Grid's comparison colours (GRID_COMPARISON_*), the spelling marks (GRID_SPELLCHECKER / EDITOR_SPELLCHECKER), the Line editor's text colours (EDITOR_CURLY_BRACES, EDITOR_TAG_OPERATORS, EDITOR_TAG_NAMES, EDITOR_TAG_VALUES, EDITOR_SPLIT_LINES_AND_DRAWINGS and the five EDITOR_TEMPLATE_* colours; plain text takes the text role) and the Grid's alternate-row and warning cells. Dark keeps legacy's dark theme (config.cpp:405-472, `LoadDefaultColors(true)`; the spectrum's `#000000`, `#674FD7`, `#F4F4F4` among them); Light keeps legacy's light comparison and editor text colours and gets matched audio colours (legacy's light theme kept the dark audio display); both high-contrast themes get their own (the editor's text colours at 4.5:1 on the field). Spelling marks are a red wavy underline under the word in the Line editor and the Grid, where legacy filled the word's background (owner decision 8 on [#531](https://github.com/altqx/hikari/issues/531)); each theme's red keeps 3:1 against the field, the panel and the Grid's alternate rows. Selection-type marks take the accent: the audio selection (AUDIO_SELECTION_BACKGROUND and _MODIFIED at legacy's 0x37 alpha, AUDIO_WAVEFORM_SELECTED) and GRID_SELECTION's blend over a compared row (at legacy's alpha 75). Document colours (ASS styles, rendered subtitles, the video) never change with the appearance.

**Appearance page.** Settings › Appearance (in legacy's Themes page's place) shows the themes as sample cards by type, the built-ins first and plugin themes after them with their plugin's name; Follow system theme; the shown theme's accent choices as swatches (each with its contrast in the tooltip); and, for a theme with `userColours`, its pickers with their reset. Every choice applies at once, as the whole Settings window does ([settings.md](settings.md)); "Set default" leaves the appearance.

Panel bodies are `panel`; fields, the Line editor's text and the Grid's cells are `field` in Dark (a step below the panel) and white in Light. Secondary text remains meaningful content, not a licence to reduce essential text to unreadable contrast. The Grid's row divider is `line` at 48 % alpha; splits are solid `line`.

Interface appearance must not rewrite a document's ASS styles or recolor rendered subtitle output. The preview's white sample subtitle and illustrated scene are media content, not UI tokens. Likewise, an application appearance does not select a document, change a workspace's ownership, or import legacy Kainote themes.

These are accepted color references; the theme layer above measures their contrast, which is not assistive-technology certification. Native disabled states, translucent separators, selection/hover combinations and operating-system contrast integration still require verification. A contrast-mode implementation must preserve meaningful boundaries even when shadows or transparency are suppressed.

## Density, typography and geometry

All measurements in this section are **reference logical units**, not physical pixels or literal `font.pixelSize` values. The Qt build keeps these compact relationships while respecting operating-system scaling, the user's text size, font metrics and fallback. Control sizes come from text plus padding. Never clip larger text to fit a reference height.

**The density rule.** Quiet is as dense as Compact Studio or denser, everywhere. A change that costs rows of the Grid, lines of the Line editor or the video's size needs a finding that justifies it. Small references are for dense desktop controls; they are not permission to drop accessible names, keyboard operation or usable pointer targets.

| Element | Quiet | Compact Studio was |
| --- | --- | --- |
| Main window title bar (menus, switcher, Document tabs, window buttons) | 36 high; window buttons 46 × 36; menu titles 26 high, padding 8 | 32, plus a 34-unit Document tab row |
| Panel header (D3) | 30 high; title 12 from the leading edge; tabs padded 10 / 10; ⋯ 20 × 20, 4 from the trailing edge | 35 |
| Workspace gutter | 0; panels split by 1-unit `line` | 3 |
| Panel body padding | 8 (Line editor 12 at the sides) | 4 / 9–12 |
| Ordinary button, single-line field, combo box, spin box | 24 high; button padding 0 / 12; field padding 0 / 8 | 30 |
| Small control (toolbars, Grid filter, inspector fields) | 22 high | 22–27 |
| Tool button | 24 × 24; the visual tool rail's 28 × 28 in a 36-unit rail | 27 |
| Dialog footer button | 24 high, like every other button | 30 |
| Transport row | 34 high; transport buttons 24 × 24, gap 2 | 24 minimum |
| Grid row / column header | 22 / 24; cell padding 0 / 7 | content + 4 / 7 |
| Status bar | 24 high; padding 0 / 12; item gap 16 | 22 |
| Dialog title bar (DG1) | 32 high | 32 |
| Spacing scale | 4 · 8 · 12 · 16 · 24 | 2 · 3 · 6 · 8 · 12 · 24 (ad hoc) |
| Ordinary boundary | 1 (the dock separators too, with an 11-wide handle) | 1 |
| Keyboard focus ring | 1 wide, 1 outside the control (1 inside a tab and the Grid's row); the text colour | the same |
| Focused panel mark | A right triangle with 8-wide legs in the accent, flush with the panel's top trailing corner | the same |
| Selected row marker | 3-wide inset leading-edge accent | the same |

Typography roles:

| Role | Reference size / treatment |
| --- | --- |
| Body, ordinary controls, the Line editor's text | 13; platform sans serif |
| Field label, Grid data | 12; labels `muted` |
| Panel title, selected panel tab | 12; weight 600; sentence case |
| Table header, status bar, badge | 11; table header weight 500, `muted` |
| Monospace (Grid times, time fields, inspector values) | 12; tabular figures |
| Transport timecode | 20 monospace, tabular figures, `text` (direction C's) |
| Dialog title | 13; weight 600 |
| Application name | 14; weight 600 |

Panel titles are no longer uppercase. Never transform user-authored subtitle text, file names or script text. Use the platform's sans serif and fixed-width families, preserving shaping and fallback for Thai, Arabic, CJK and mixed scripts. The Line editor stays in body text; the Grid's times and numbers are monospace. Secondary roles scale with the user's base font.

**Radii.** Controls 4 (buttons, fields, combo boxes, spin boxes, tool buttons, menu titles, the layout switcher, segmented groups); check box 3; radio button and slider handle round; popups, menus, dialogs and floating windows 6; panels, the title bar, the status bar and the Grid 0. A badge is 3 and a pill-shaped status is fully round.

**Elevation.** Panels are flat. Menus, popovers, toasts, dialogs and floating windows have a 1-unit `line` boundary and a `0 8 28` shadow (black at 45 % in Dark, `#141E28` at 18 % in Light). In the high-contrast themes there's no shadow, only the boundary. A modal's backdrop is `bg` at 72 % (Dark) or 78 % (Light).

No motion system is set. Hover and press states change at once.

## Components and state presentation

### Controls and their states

Every shared control is drawn by HikariStyle (`src/ui/style`) or a shared shell component from the roles above. These include the check box, the radio button, the spin box and the combo box's indicator, which Fusion drew until now. The tables give each state in roles; the role values per theme are in [Semantic appearance tokens](#semantic-appearance-tokens), so each cell holds in all four themes.

The general rules:

- **Hover and pressed** lay `hover` or `pressed` over the control's rest surface.
- **Focus** adds the 1-wide ring in `focus`, 1 outside the control, for keyboard focus only.
- **Disabled** drops the overlays and draws text, icon and boundary in `disabled`.
- **Checked** is shown by fill and by shape, never by colour alone.
- **Text** on a hovered or pressed control stays `text`; `muted` text never sits on a pressed surface.

| Control | Rest | Hover | Pressed | Focus | Disabled | Checked / on |
| --- | --- | --- | --- | --- | --- | --- |
| Button | `raised`, 1-unit `control` boundary, `text` | + `hover` | + `pressed` | ring | no fill; `disabled` text and boundary | — |
| Primary (default) button | `accent` fill and boundary, `onaccent` semibold | `accent` + `text` at 12 % | `accent` + `text` at 20 % | ring | as Button disabled | — |
| Danger button | as Button, boundary and text in `danger` | + `hover` | + `pressed` | ring | as Button disabled | never the default |
| Quiet button (text only) | transparent, no boundary | `hover` | `pressed` | ring | `disabled` text | — |
| Tool button (icon) | transparent, no boundary; icon in `text`, accent layer in `accent` | `hover` | `pressed` | ring | `disabled` icon | `select` fill, accent layer in `accent`; HC adds a 1-unit `accent` boundary |
| Segmented group | `field`, one `control` boundary round the group, `line` between items | item + `hover` | item + `pressed` | ring round the item | `disabled` | item `select`, semibold, 2-unit `accent` underline |
| Text field, spin box | `field`, `control` boundary, `text`; placeholder `muted` | boundary `text` | — | ring; editing: 1-unit inset `accent` border | `disabled` text and boundary | — |
| Spin box steps | two 12 × 10 chevrons in `muted` inside the field's trailing end | chevron `text` on `hover` | chevron on `pressed` | (the field's) | hidden | — |
| Combo box | as a field, one chevron (`chevron-down`) in `muted` at the trailing end | boundary `text` | `pressed` | ring | `disabled` | open: boundary `accent` |
| Check box | 14 × 14 box, radius 3, `field` with `control` boundary; label `text` | box + `hover` | box + `pressed` | ring round the box | `disabled` box and label | `accent` fill and boundary, `onaccent` tick; mixed: `onaccent` bar |
| Radio button | 14 round, as the check box | as check box | as check box | ring round the circle | as check box | `accent` boundary and a 6-unit `accent` dot on `field` |
| Slider | 3-unit groove in `line` (HC `muted`), `accent` fill to the value; 12-unit round handle in `text` with a 2-unit `panel` ring and a 1-unit `control` edge | handle + `hover` halo | handle grows to 14 | ring round the handle | groove and handle `disabled` | — |
| Panel tab | `muted`, regular | `hover` | `pressed` | ring 1 inside | `disabled` | `text`, semibold, 2-unit `accent` underline |
| Document tab (title bar) | `muted` on `bg` | `hover` | `pressed` | ring 1 inside | — | `panel` fill, `text`, 2-unit `accent` bar along its top; HC adds a `line` boundary |
| Menu title, layout switcher | `text`, transparent | `raised` | `raised` + `pressed` | ring | `disabled` | open: `raised` |
| Window button (title bars) | glyph `text` | `raised` | `raised` + `pressed` | ring | inactive window: glyph `disabled` | — |
| Menu item | `text` on `panel`; shortcut `muted` | `select` | `select` + `pressed` | `select` + ring | `disabled` | check mark in `accent` in the check gutter |
| List and Grid row | `text`; divider `line` at 48 % | `hover` | — | ring 1 inside (Grid's current row) | `disabled` | `select` + 3-unit leading `accent` marker; current Line: 1-unit `accent` outline |
| Filter chip | rounded `raised`, `control` boundary, icon, text, chevron | + `hover` | + `pressed` | ring | `disabled` | narrowing: `select` fill, `accent` boundary, semibold text saying the filter |

Overlapping states compose in this order: selection first, then hover or pressed over it, then the focus ring on top. A hovered selected row keeps its `select` fill and marker under the `hover` overlay. A hovered primary button stays in the accent. Disabled beats every other state.

The Gate B boards show the button, tool button, field and check box in all six states in each theme. The theme tests check the values each role resolves to. A card that draws a control checks its states in all four themes in its screenshots.

### Other components

| Component / state | Presentation |
| --- | --- |
| Time field | A masked clock in the document's form (ASS `0:00:00.00`, SRT `00:00:00,000`, TMPlayer `00:00:00`) up to 99:59:59.99 (SRT 99:59:59,999; FA7 [#564](https://github.com/altqx/hikari/issues/564)). ASS shows one hour digit below 10 h and two from 10 h; a paste, the wheel or a right-drag over the hours reaches them. A Line timed past the cap loads and saves unchanged: the field shows 99:59:59.99 and writes it only when the user edits that field. |
| Line editor syntax colours | One colour per role from the theme's palette. A drawing (`\p1` and up) and a vector `\clip`/`\iclip` alternate two colours, X then Y, per coordinate, restarting at each command letter, in every theme. A `mixin` Effect is a template, as `template` is. |
| Invalid value | Danger boundary and explanatory text; associate the explanation with the field and disable an action whose prerequisite is invalid. Do not communicate failure by color alone. |
| Mixed value | Muted italic “Multiple values” in the gallery. Preserve mixed/indeterminate semantics; do not replace an actual value with this string in the model. |
| Row warning / confirmation | Warning emphasis and textual context; confirmation appears as text/check/state badge. These are presentation examples, not a decision to persist new document fields. |
| Script dialog (AD1) | A DG1 window (`src/ui/AutomationDialog.qml`): the script's grid with 6-unit rows and 8-unit columns, scrolling inside the window with the 6-unit themed scroll bar when capped to the screen; a hint strip above the footer (muted text, top `line`, fixed at the tallest hint up to three lines) showing the hovered else the focused control's hint; the script's buttons in the footer in its order with the Enter button as the primary action. A number field out of range takes the danger boundary and the strip explains the clamp; a colour is a swatch (checkerboard under alpha) opening the shared colour popover, beside a monospace hex field. |
| Document tab | In the title bar (SH1): explicit editing target, document name and dirty indication. The dirty dot is accompanied by an accessible textual state. Selection (the top accent bar) and focus (the ring) are separate. |

The sample validates centisecond-formatted timestamps and end-after-start, highlights CPS above 18, and bounds a sample shift dialog to 0–60 seconds. Those are demonstration rules, not approved production timing, reading-speed or validation policy. Display actual domain validation and state from the application model. Similarly, per-keystroke snapshot undo, illustrative layer storage and regex-based tag hiding are not production editing algorithms.

## Keyboard, focus and tabs

The accepted navigation direction provides a route into every workspace region and keeps focus visible. HikariSub owns its native realization; HTML event handlers are not the implementation contract.

* **F6 / Shift+F6:** cycle forward/backward through the visible workspace regions. In the default arrangement the order is video, audio, Line editor, Grid. Start from the currently focused region and maintain a meaningful order when layout or visibility changes. The revised study follows actual focus; native Qt must implement the same explicit navigation responsibility.
* **Tab / Shift+Tab:** navigate controls within their normal scope. A focused region must provide a useful next entry into its controls. Keep the focus ring visible at panel edges and in scrolled content.
* **Grid:** provide a clear keyboard entry and visible focus, retained selection when filtering hides a row, and an explicit empty state. The [painted-grid contract](subtitle-grid.md) governs native displayed-order navigation and the distinct selected set, current line and keyboard focus. The HTML study has a single-selection example in which Enter selects its focused row and enters the editor; this is illustrative, not an accepted production activation rule. Define activation and its effects on multi-selection in the native follow-up rather than copying the twelve-row HTML array.
* **Text inputs:** cursor movement, selection, IME composition and field-specific keys take precedence over surrounding navigation. The study's global Left/Right A–D comparison shortcut is harness-only and must not become an application shortcut.
* **Document tabs:** click selects the editing target. Left/Right navigate and activate neighboring tabs; Home/End select the first/last. The study wraps its two tabs. Use one active tab entry in the tab sequence, retain visible focus, and name the associated content. Tab activation changes document context, not interface appearance. Closing, reordering, overflow, protected-reference placement and dirty-document prompts need their document/workspace contracts; the gallery does not demonstrate them.
* **Menus:** opening the example menu enters its first item; Escape dismisses it and restores focus to its opener. Use complete native menu navigation rather than copying the sample's incomplete menu key handling.
* **Dialogs:** a modal operation contains focus, has an explicit title and cancellation path, and returns focus sensibly on dismissal. Cancel must not apply its pending edit. The HTML relies on browser dialog behavior; verify focus containment, Escape and restoration in Qt.

Focus is a ring in the text colour (Hikari theme layer, "Keyboard focus"), distinct from selection (the accent) and error (`danger`). Do not reduce focus to a subtle text-color change. Confirm that status updates can be announced without stealing focus or flooding assistive technology during typing.

## Icons, content and adaptive layout

The H mark, Unicode transport symbols, arrows, checkmarks and other glyphs are placeholders. They neither select a third-party icon pack nor establish redistribution rights for brand assets. Implement a coherent vector-icon set with consistent optical size, stroke weight and alignment. Record provenance and licence for any adopted assets; an icon-only action needs an accessible name and appropriate tooltip. Use directional mirroring where it conveys navigation, while retaining the meaning of media controls and data symbols. Font glyph availability must not decide whether an essential action is understandable. The set is specified in [icons.md](icons.md) (K1: drawn in house, UI icons only, following the appearance live in the theme layer's colours, K2).

The study stacks preview, editor, audio and grid below a 900-unit width and simplifies some content below 590. Those browser breakpoints and fixed stacked heights are exploratory references, not supported minimum-window sizes. In particular, hiding the layer field or an action at a narrow width is not permission to make that capability unreachable. Use resizing, overflow, scrolling and appropriate panel arrangements while preserving access to fields and commands. Check long translations, larger system text and mixed-direction text rather than assuming English widths.

## Tool strips, option rows and labels

The user's rule of 2026-10-05: HikariSub looks like professional software, not "vibe coded". Its presentation rules, applied by the UI polish; legacy behaviour (HikariSub/, baseline 20d647c4) is unchanged by them.

**Say it once.** A label never repeats what its container says: a panel's name appears once, in its dock header (the Automation panel has no heading of its own); the document name in the tab and the window title only; a tool has one name in its dock, panel and menu ("Shift times", where the dock said "Timing"); a dialog is titled with the menu command that opens it ("Select lines", "ASS file properties", "Resample subtitles"). A group of docked tabs names its panels in the tabs only (D3: one header row, no title bar above them). The Find panel's results pane has no heading: its summary ("12 matches in 9 lines") leads it, and "Results" is the pane's accessible name. The status bar does not say "Modified" (the tab's mark does) and drops "Saved" once the Document is edited again.

**Tool strips are icon-only.** A tool strip shows icons of the K1 set ([icons.md](icons.md#where-icons-go)) with the name as the tooltip and the accessible name. The visual tool rail is a 32-pixel strip of eleven 28-pixel tool buttons, one per family: the on family is the style's checked button, the buttons take focus by Tab (a click leaves it with the video), Up and Down move between them, and a family whose tool has not landed says so in its tooltip rather than being drawn faded. A Video panel too short for the twelve families shows the ones that fit and ends in More tools, a menu of the others; the on family takes the last slot, so it stays in sight and no button is ever cut off (VB1).

**One inspector bar for the active tool** (VT1, [#269](https://github.com/altqx/hikari/issues/269)). Below the video, one bar carries the active tool's options (icon toggles and actions; a choice in a group, the drawing's shape, the all-tags tool's tag and change option, is a dropdown showing the chosen entry as text) and its values (compact fields sized for a coordinate, in tabular figures, the unit inside the field), each group after a short muted label (Scale's Rectangle and Tags, Move's Start and End, the rotations' Origin) and a thin rule, and at the trailing end the batch picker. The bar is one line (VB1, [#481](https://github.com/altqx/hikari/issues/481)), so the video keeps its size across the tools; the tool's name is the rail's, not repeated. The crosshair has no line: its position, read-only, and Copy video position sit at the end of the video times line. A field is typed and applied by Enter, or scrubbed by dragging its label sideways (the label is underlined with dots and shows the sideways pointer; Shift for a tenth of the step); Enter and each drag's release are one undo step, Esc during a drag drops it and Esc while typing puts the value back, as does another active Line or another set of picked Lines. From a field with nothing typed, Esc is the tool's, as over the video (a pending point goes). Groups that don't fit its width fold, whole and last first, under More options before the batch picker, which opens a popover with them one per line, and the batch picker keeps its trailing slot. The batch picker is an icon button whose checked look is its state; while Lines are picked their number shows beside it ("2 lines") and Clear appears. Fullscreen's bar has the same bar as its top row while a tool other than the crosshair is on (FS1's inspector row; the families are in its Visual tools menu and the rail).

**No debug text, no empty rows, nothing cut off.** No readout that reads like a log ("Targets: active line"); an empty list or result pane says so in one muted line ("No results yet", "No scripts loaded", "No auto save files", "No suggestions"), its explanation in the tooltip; a choice with nothing to choose says so ("No dictionaries found") rather than show a blank box; a footer of actions appears once there is something to act on. Controls are as wide as their content (menus size to their widest item up to 480 pixels; a combo box to its widest choice; a fixed Grid column to its header), panels clip what does not fit rather than draw over their neighbours, and a scrolling form shows its scroll bar while there is more. The Line editor scrolls its rows in a dock shorter than them (translation mode in the default 1280 × 800 window) beside a thin bar in the theme's colours, as the visual tool rail's; keyboard focus brings the focused control into view, and the dock keeps room for the tag buttons and a line of each text field. A window with nothing in it offers the command that fills it (the Automation panel's empty state has "Load script…"). Dialog buttons say "OK", translated, everywhere (legacy's literal "Ok" and "OK" alike). Integers are plain digits (no thousands separator in a pixel or time field); the resolution separator is "×"; legacy labels' hard line breaks become wrapping text; an ellipsis is "…".

**Colours come from the Theme roles.** No per-colour settings (the high-contrast themes' pickers are the approved exception, [Hikari theme layer](#hikari-theme-layer-k2)); text on the selection takes the palette's highlighted-text colour; text on a colour of its own (the Grid's number on its label colour) takes whichever of `text` and `field` contrasts more, at 4.5:1 or better; errors take `danger`, secondary text `muted`. The visual tools' "Line is not visible on video" warning keeps legacy's text, size and centring in `danger` on a scrim of the panel colour.

**Footers close at the trailing edge.** A window's or dialog's closing row is a `DialogButtonBox` (`WindowFooter.qml` inside a window's layout, a dialog's `footer:` otherwise) at the trailing edge, never centred under the content, its buttons in the platform's order by their roles: OK (accept) before Cancel (reject) on Windows and KDE, after it on GNOME and macOS; Apply, Discard and the window's other actions where the platform puts them. A button that hides (Force stop, Remove group) is a `FooterButton`, so the row closes up rather than keep its slot. The Style manager has one footer that never changes (SY1): Revert at the leading edge, then OK, Cancel and Apply. A question's answers (the find questions, the Style replace question) are a dialog footer with roles too: Yes, OK, No, Cancel on Windows. Rows of commands on a list (the Hotkeys page's, the spelling replacer's, Remove in Temporary files) are not footers and stay with their list; Edit shapes and Edit tags keep each change as it is made, with Undo and Redo at the leading edge and Close at the trailing one (PU5); a script's dialog keeps its own button order, at the trailing edge.

**Sentence case.** HikariSub's own copy is in sentence case: "1 line selected", "Follow the editing line", "Open &video…". [text-style.md](text-style.md) has the full rules for the interface's English. Line, Document and the other [glossary](../../CONTEXT.md) terms are capitalized in the documentation, not in the interface. Names keep their capitals (the "Editing" and "Classic" layouts, the Light and Dark themes, Automation, Insert). `hikari_i18n_contexts_tests` and `hikari_i18n_text_style_tests` check the catalogs.

**Abbreviations name themselves on hover.** A Grid heading that abbreviates its column shows the full name below it while the pointer is over it: "Layer" over "L.", "Left margin", "Right margin" and "Vertical margin" over "Left", "Right" and "Vert." (legacy's column chooser, SubsGrid.cpp:501-508), "Characters per second" and "Line wraps" over "CPS" and "Wraps" (its Hide columns names, SubsGrid.cpp:199-200); a heading that is a word has none, in the editing Grid and the reference tray's.

**The Grid scrolls and its columns resize (GS1, [#544](https://github.com/altqx/hikari/issues/544)).** A 6-unit themed scroll bar (`field` track in `line`, `muted` handle, `text` while pressed) runs down the Grid's right edge below the headings while the rows don't fit, and one along the bottom while the columns don't; dragging it, clicking its track and the wheel move the same view. A 1-pixel `line` divider sits between two headings: dragging it resizes the column on its left (down to 24 pixels, the pointer a split cursor over it), and double-clicking it fits that column to its widest cell. Text keeps the room that is left down to 120 pixels, so widening Original text narrows Translation; past that the columns scroll sideways. The widths are kept in `grid.columnWidths`, shared by the editing Grid and the reference tray's, and the Grid's Hide columns menu has Reset column widths.

**The Style manager is a sidebar and an editor (SY1, [#247](https://github.com/altqx/hikari/issues/247)).** One window on DG1's frame, resizable and modeless, its size and place kept; the body is flush with the frame. The sidebar (`background`, 300 wide, 12 inside) holds the filter, the Catalog heading with its box, New and Delete, then the catalog's Styles and this file's, each under a strip of 16-pixel icon tools with their names on hover and a ⋯ menu for the rarer ones; the copies between the lists sit between them, only the direction with a selection on. A list row is 24 high: a 14-pixel swatch (the primary colour inside a 3-pixel border in the outline colour), the name, a 6-pixel accent dot while its edit is not applied, and in the file's list the Lines using it in `muted` (none in `disabled`); the selection is `select` with a 3-pixel accent bar at the leading edge. The editor is always beside the lists, a 1-pixel `line` between: the preview (84 high) and its text pinned at the top under 12, then the form scrolling under a `line`, 24 inside, in MuseScore 4's sections (a 13-pixel semibold title, 12 above): each field under its 12-pixel `muted` label, a reset icon at the label's end while the field differs from the value it opened with, units inside the field (%, °). The fields are one four-column grid of even columns: Name across; Family over three columns and Size; Font catalog over two, Emphasis (four toggles in one outline) and Text encoding; the four colours, each a swatch with its hex and a 46-wide transparency; Border, Shadow, Scale X, Scale Y; Angle, Spacing, Border type and the 3 × 3 Alignment grid over two rows; the three margins. A field where the selected Styles differ reads "Multiple values" in muted italic (a colour hatched). The name's problem is a `danger` line under it and a `danger` border; the rename check box sits there too. On a 1280 × 800 screen at 150 % (853 × 533) every control stays whole: the lists share the height and scroll, and the form scrolls under its pinned preview with its bar shown, sideways only on a smaller screen.

**Menus.** Every menu item keeps a check gutter and an icon gutter, so labels line up whether or not an item has an icon or a check, and shows its key binding right-aligned in the muted colour, in one notation ("Ctrl+Shift+O"): the menu bar's from the Global bindings, the context menus' from legacy's tab-separated text.

Dock chrome (headers, tab bars) keeps MuseScore 4's form under D3 ([#207](https://github.com/altqx/hikari/issues/207), [musescore-docking.md](../research/musescore-docking.md), [docking.md](../docking.md#chrome)) in Quiet's metrics (see Composition). Its "⋯" button (K1's panel-menu, three dots) is the one menu icon in a header; the panel's name is its tooltip and accessible name ("<panel> options"). A panel reports the smallest size its content takes unclipped, and the docking engine keeps it at least that big; a row too wide for a narrow panel wraps (the Line inspector's fields) rather than be cut off.

## Video transport and full screen

**One time readout.** Under the video there's a single transport row, 34 high. The extra line above it goes: it was a log-like accent readout ("00:01:02,750; 1506; 0; 0 ms, -3580 ms") and broke the no-debug-text rule. Left to right, the row holds:

- the transport buttons (previous frame, play/pause, next frame, play line, stop), 24 × 24 tool buttons, 2 apart;
- the timecode in the 20-unit monospace role, in `text` (direction C's);
- the frame number in `muted` monospace ("frame 5146 / 15116");
- at the trailing end: the zoom readout ("Fit", "200 %"), then volume and its slider.

The time is also the Jump to field, and the row's content and behaviour are in [video-controls.md](video-controls.md) (#1206).

The seek slider spans the panel above the row. The crosshair's position and Copy video position keep their place at the end of the row (VT1). The visual tool's inspector bar sits below it when a tool other than the crosshair is on.

**The full-screen strip** (direction C's, [FS1](https://github.com/altqx/hikari/issues/275)'s bar, restyled). Full screen draws its controls as one strip across the bottom of the screen:

- **Letterboxed video:** the strip fills the bottom letterbox, its controls centred in it, so nothing sits on the picture.
- **Video fills the screen:** the strip lies over the bottom of the picture on a gradient from transparent to black at 82 %.

The strip is always on black, in every theme. Its text and icons are `#EEF0F2` (at least 15:1 on the black, 7:1 or more on the gradient's dark half). In high contrast its top edge is a 1-unit white line. The strip has two rows:

1. The start time, the seek bar across the full width, and the duration, in `muted` monospace.
2. Three groups:
   - leading: the active Line's number in `muted` and its text, elided;
   - centred: the transport buttons and, after them, the timecode in 20-unit monospace;
   - trailing: the zoom readout, Typeset, volume and Leave full screen.

Buttons are 34 × 34 with 20-unit glyphs, unboxed, the same glyphs as the panel's. Hover is white at 12 %. This strip is Simple mode's ([#1207](https://github.com/altqx/hikari/issues/1207)): the visual tools and their inspector move to the typesetting mode. The strip shows and hides as #1207 says. Full-screen typesetting ([#1208](https://github.com/altqx/hikari/issues/1208)) has its own layout, in [full-screen-typesetting.md](full-screen-typesetting.md).

## The resolution question

When a video opens with a size different from the subtitles' script resolution (`Application::checkResolution`, `askResolutionMismatch` in `src/ui/Main.qml`), the question is a window on DG1's frame titled "Open video", modal to the main window. It replaces the four-button message box. The behaviour of each choice is unchanged (`matchVideoResolution`, `src/application/resample.cpp`) apart from the two decisions below.

**Layout.**

- **The lead:** the information icon, then the case in one bold line and the numbers in `muted` under it, then "Choose how the subtitles fit it." At the trailing side, a small frame titled "As made, on a W × H video".
- **The choices:** one card per choice in a two-column grid. Each card holds a preview frame, a radio button with the name, and one line saying what the choice does to the subtitles. The selected card has a 2-unit `accent` boundary on `select`.
- **The footer:** "Don’t ask again" at the leading edge, and the default button at the trailing edge, named after the selected choice ("Resample", "Keep as is", …). Esc is Keep as is.

**The case line** names the case plainly. Two sizes are the same shape when their ratios differ by less than 1 % (Q583, below).

| Case | Bold line | Detail line |
| --- | --- | --- |
| Same shape, bigger | The video is the same shape as the subtitles, N % bigger. | Video W × H. The subtitles were laid out for w × h. |
| Same shape, smaller | The video is the same shape as the subtitles, N % smaller. | the same |
| Different shape, wider | The video is wider than the picture the subtitles were made for. | the same |
| Different shape, taller | The video is taller than the picture the subtitles were made for. | the same |

**The choices.** They appear in this order. Resample is recommended and selected when the window opens.

| Choice | Same shape | Different shape | What the code does |
| --- | --- | --- | --- |
| **Resample** (badge "Recommended") | Fit the subtitles to the video. Everything keeps its size and place on the picture, and new positions use the video’s pixels. | Fit the subtitles to the video. Signs stay on what they mark; text keeps its proportions. | Positions, margins and clips scale per axis; font size, border, shadow and spacing by the smaller ratio; 3D rotations keep their shapes; a set layout resolution follows. |
| **Keep as is** | Change nothing. They still fit this video when it plays, but positions you add are in the old size. | Change nothing. The picture is wider, so the player stretches the subtitles sideways. ("taller … upwards" for a taller picture) | Nothing. |
| **Resample and stretch** (different shape only) | — | Fit them and stretch the text with the picture. For a video that is the same picture, stretched. | As Resample, and Scale X or Y × the larger ratio ÷ the smaller. |
| **Change the size only** (warning mark) | Only the file’s size changes. Text gets smaller and signs slide up and left, off what they mark. | Only the file’s size changes. Text gets bigger and signs slide off what they mark. | Only `PlayResX` and `PlayResY` change. |

The preview text of "Change the size only" says which way the text goes: "smaller" when the video is bigger, "bigger" when it is smaller.

**The previews.** Each card shows a frame from the video, or a neutral frame when none is decoded yet, with one dialogue line and one positioned sign as the choice would render them. They are drawn from the same math as `resizeSubtitles`, not from the Document's own Lines. On "Change the size only" a dashed white box marks where the sign was. The "As made" frame shows the same line and sign on the subtitles' own canvas.

**Q582: one question, not two** (owner, 2026-11-17). When the file has no layout resolution, the window adds a check box under the cards: "Also set the layout resolution to the video’s (W × H)", ticked by default. It applies whichever choice is taken. The separate `missingLayoutRes` question no longer follows this one. When `video.missingLayoutRes` is set to answer without asking, its saved action applies and the check box isn't shown. The `layoutResMismatch` question (a layout resolution that differs from the video's) is unchanged.

**Q583: shapes within 1 % are the same** (owner, 2026-11-17). Two sizes are the same shape when `|(videoWidth ÷ subsWidth) ÷ (videoHeight ÷ subsHeight) − 1| < 0.01`. Then the case line says "same shape" and "Resample and stretch" isn't offered (1920 × 1080 → 1920 × 1088, or the 0.05 % of 1920 × 804 → 2048 × 858). Today any difference at all counts.

"Don’t ask again" keeps today's meaning: it sets `video.dontAskForBadResolution`, and Settings → Notices → Ask again brings the question back. Subtitles › Resample subtitles stays the place to do it later. The strings live in the `HikariSub.Conversion` context.

Subtitles › Resample subtitles, with its letterbox and crop edges and its matrix choice, is specified in [resample.md](resample.md).

## Fixes by area

The Gate A inventory of main 344efa574 found the items below. Each is cut into a code card by area after the beta.4 tag. Numbers are the inventory's findings.

**Theme layer and HikariStyle (do first; everything else reads it)**

- The Quiet palette, the `hover` and `pressed` roles, and typography, spacing and radius tokens in the theme layer, exposed once (the metrics half that K2 left open). `hikari_ui_theme_tests` takes the new expected values and keeps its floors.
- Check box, radio button, spin box and combo box drawn by HikariStyle, with one chevron for every dropdown (11, 12).
- One button height (24) for footer and inline buttons; primary, secondary, danger and quiet as in [Controls and their states](#controls-and-their-states) (14).
- One checked look for toggles: `select` fill and the icon's accent layer. It replaces the tool rail's outlined square and the Audio toolbar's accent outline plus fill (9).

**Main window chrome**

- The Document tabs move into the title bar. The tab row goes, and the Document tab loses its accent frame for the top bar (1, 3).
- Side-bar toggles become 24-unit tool buttons, left of the window buttons, in the title bar's style (4).
- The status bar labels its numbers ("Zoom 37 %", "Video 2048 × 858", "Subtitles 1920 × 804" with the warning mark when they differ) and lines its items up from the leading edge.

**Panels and docking**

- Panel headers are 30 high, in sentence case, with the accent underline on the selected tab and no separators. No gutter (1).
- The Grid's header holds the filter field, and the empty strip and the gap before the headings go (2).
- In a side bar, the icon strip is the header: the second title row goes (1). The Styles panel's toolbar no longer cuts its title to "Styles: …", and the Explorer's empty state says so in one muted line (5).
- The focused-panel mark stays the 8-unit triangle; with the 30-unit header and no empty strip it sits on the header's corner in every panel (6).

**Video and audio**

- One time readout and the 20-unit timecode (7).
- Transport buttons are the same unboxed tool buttons in the panel and in full screen (8).
- The Audio toolbar takes one idiom: icon tool buttons, one segmented group per choice, the dropdown's chevron as a separate 16-unit part of its button, and the zoom readout in `muted` (10).

**Line editor**

- Every field in a row is 24 high, labels in the 12-unit `muted` role. At 1366 × 768 the rows wrap in two at most, with labels the width of their text (13, 24).

**Settings and dialogs**

- Settings rows lose their icons (15).
- The page area takes the height: the footer is one 24-unit row 12 from the bottom, so the page grows from about 340 to about 500 units at 1366 × 768 (16).
- Every dialog body sits 24 inside its sides under DG1, including the Style manager's form, Find fade and Track colour. A window with nothing in it is no bigger than its empty state, as the Plugins window is today (17).
- Home's empty Recent files is one muted line and Open…, not a framed empty box, and Kanji timer's empty lists say what goes in them (18).
- "Delete temporary files" is titled for its command, and Esc closes it (19).
- The resolution question as specified above, which also fixes finding 20.
- Style manager and Remove tags fit 1366 × 768 with room (25).

**Themes**

- Light separates panels by surface steps (`bg` against white `panel`) with soft `line` splits, so regions stop looking boxed (21).
- In high contrast, tool buttons and tag buttons have no boundary at rest and gain the border pick on hover, focus and check. Fields and buttons keep theirs. This thins the lattice without losing a boundary on anything operated (22).
- The accent is kept for selection, the default button, the checked mark, the editing field's border, the current tab's underline, the playhead and the focused-panel mark. Hover is never the accent (23).

**Menus, the Agent panel, icons:** tokens only (radius 6 popups, 24-unit items, `select` hover, `muted` shortcuts). No layout change.

## Qt Quick responsibilities and remaining verification

| Owner | Responsibility |
| --- | --- |
| Hikari theme and metrics layer | Expose the semantic palette, typography roles, spacing and radii once; resolve appearance and system/user scaling; keep subtitle-rendering styles separate. The palette half is K2's theme layer (`src/ui/theme.h`, above); typography, spacing and radii join it with Quiet (see Fixes by area). |
| Shared Qt Quick Controls styling | Apply tokens to ordinary buttons, fields, selectors, toggles, sliders, menus, dialogs and tabs, including overlapping states and accessible labels/values. Avoid divergent per-screen colors. |
| Workspace and action layer | Own region order, focus restoration, shortcut scope, editing-target indication and modal command routing. Styling must not infer the editing target from the focused panel. |
| Grid model and presentation | Keep stable row identity, selection and editing state independent of delegates; render compact columns, current/selected/focused states and filtered-selection feedback. |
| Media surfaces | Present actual video, subtitle overlays, waveform and time state through their adapters. UI palette changes must not alter document rendering. |

These responsibilities implement the [architecture contract](../architecture.md); they do not select a docking dependency or a third-party component library. Figma remains on Starter for occasional handoff. Agent-built QML/HTML prototype tickets remain the mechanism for exploring unresolved interactions; acceptance of D is already recorded.

The HTML approval establishes visual direction and the demonstrated interaction vocabulary. It does **not** establish that the native application has passed any of the following:

* Native rendering and text readability in all four themes, including combined states and operating-system contrast behavior (the role contrast is measured above).
* System font changes, fallback/shaping, IME input, mixed-direction content, text enlargement, high-DPI scaling and movement between monitors with different scales.
* Complete keyboard reachability, visible focus, tab/menu/dialog semantics, screen-reader names/roles/values and announcements on the supported platforms.
* Large-document grid behavior, latency, memory, scrolling or frame-time targets; real audio/video synchronization and rendering performance.
* Production persistence, undo, docking, window restoration or the full document/tab lifecycle.

Those are implementation and verification obligations, not reasons to reopen the approved visual direction. Native findings should produce specific fixes or focused prototype evidence while preserving the accepted Quiet baseline.
