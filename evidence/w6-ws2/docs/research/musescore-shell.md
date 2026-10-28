# MuseScore 4 shell: title bar, Preferences, Home and dialogs

This is a reference for how MuseScore Studio 4 builds its main-window title bar, its Preferences window,
its Home screen and its dialogs, and for what HikariSub should take from each. It is the design source
for the wave-6 redesign cards (#213): SH1 title bar (#220), SH2 Home page (#221), DG1 dialog frame (#222),
PF1 Settings (#225), TM1 Translation (#232), LE1 Line editor (#233), SY1 Style Manager/Editor (#247),
AM1 Automation panel (#248) and TS1 Shift Times (#249).

Source code was read at tag **v4.7.5** (commit `3654226c2e99289916916953a98e585a3d3b315a`). On
2026-10-18 this is still the newest release, so it is the same tag as
[musescore-appearance.md](musescore-appearance.md) and [musescore-docking.md](musescore-docking.md). The
theme-role values and the dock chrome are in those two documents and are not repeated here. Short links
such as [AppTitleBar.qml] point to the v4.7.5 sources in the list at the end. Line numbers refer to that
tag. The handbook pages were fetched on 2026-10-18.

Sizes are logical pixels at MuseScore's default body font of 12 px, unless a row says otherwise.
"Derived" means the value comes from reading the layout code, not from measuring a running build.

## 0. Summary

- **The custom title bar is Windows only.** Linux and macOS use the system's title bar. On Linux,
  MuseScore also forces X11 (XWayland under a Wayland session), so it has no Wayland-specific title-bar
  code at all. HikariSub's owner decision (custom on Windows, KDE and GNOME) therefore goes beyond the
  reference on Linux.
- **The Windows title bar is small and plain:** 32 px high, menus inline on the left, the window title
  centred, and minimise / maximise / close on the right. There is no app icon. The close button does not
  turn red. Only the title text area drags the window. Windows 11 snap layouts do not appear on the
  maximise button.
- **Preferences** is a resizable 880 × 640 dialog. A 220 px page list sits on the left. Each page holds
  sections with a bold title, separated by lines. The window has **OK, Cancel and Reset preferences**, and
  no Apply. Changes preview live inside a settings transaction: OK writes them, while Cancel, Esc or the
  window's close button roll them back. There is no global search.
- **Home** is a full page (Home / Score / Publish). It has a 260 px menu on the left (Scores, Plugins,
  MuseSounds, Learn, account) and a Scores page with recent files as a grid or a list. Missing files are
  dropped silently.
- **Dialogs** are separate top-level windows with the **system's** title bar, on every platform. They
  are application-modal by default and centred on the main window each time they open. QML dialogs do
  not remember their size or position, but the older QWidget dialogs (Style included) do.

## 1. Main window and title bar

### Which platforms get a custom title bar

The build picks one `Main.qml` per operating system ([appshell CMakeLists.txt] 111–131):

| Platform | Main window | Title bar | Menus |
| --- | --- | --- | --- |
| Windows | [win/Main.qml]: `AppTitleBar` above `WindowContent` | **Custom**, drawn in QML; the system frame is removed natively ([winwindowscontroller.cpp]) | Inline in the custom title bar |
| Linux / FreeBSD | [linux/Main.qml] | **System** (window manager) | In-window QML menu bar under the system title bar; or the desktop's global menu when `com.canonical.AppMenu.Registrar` is on the session bus ([linux/Main.qml] 43–51; [linuxplatformtheme.cpp] 79–86) |
| macOS | [mac/Main.qml] | **System**, made transparent and filled with the theme's window colour ([macosplatformtheme.mm] 90–106) | Native global menu (`Qt.labs.platform` `MenuBar`, [PlatformMenuBar.qml]) |

On Linux, `main.cpp` sets `QT_QPA_PLATFORM=xcb` unless `MU_QT_QPA_PLATFORM` is set. The source comment
reads "Forced X11, with Wayland there are a number of problems now" ([main.cpp] 100–103). It also sets
`QT_QPA_PLATFORMTHEME=gtk3` (96–98). The result on each Linux desktop:

| Desktop | What MuseScore 4.7.5 shows |
| --- | --- |
| KDE Plasma (Wayland session) | An XWayland window with KWin's server-side title bar. The global menu is used if Plasma's global-menu registrar is running. |
| GNOME (Wayland session) | An XWayland window with mutter's server-side title bar and the in-window menu bar. |
| Tiling Wayland (sway, Hyprland, …) | An XWayland window with whatever the compositor draws for X11 clients (sway: its title border; Hyprland: a border only), plus the in-window menu bar. |
| X11 (any window manager) | The window manager's frame, plus the in-window menu bar. |

MuseScore has no "use system title bar" setting, because on Linux and macOS the system title bar is the
only one, and on Windows the custom one is the only one.

### Windows title bar: contents and layout

[win/Main.qml] 51–76 anchors `AppTitleBar` to the top with **height 32** and puts `WindowContent` under it.
[AppTitleBar.qml] is one `RowLayout` with spacing 8, on `backgroundPrimaryColor` (35, 50–56):

```
[ File  Edit  View  Add  Format  Tools  Plugins  Help ]   <title, centred in the window>   [ – ] [ □ ] [ × ]
```

| Element | Behaviour | Source |
| --- | --- | --- |
| App icon | **None.** The bar starts with the File menu. | [AppTitleBar.qml] 50–64 |
| Menus | `AppMenuBar`, a horizontal `ListView` of text `FlatButton`s (see "Menu bar" below) | [AppTitleBar.qml] 58–64 |
| Title | `StyledTextLabel` in `defaultFont`. It fills the remaining width, is left-aligned, and gets left padding so that the text centres on the **window**, not on the free space, when there is room (`leftPadding`). It elides at the right. Rich text. Hidden in full screen. | [AppTitleBar.qml] 66–98 |
| Window buttons | `AppSystemButtons`: a `Row` with spacing 8 of three icon-only `FlatButton`s (`APP_MINIMIZE`, `APP_MAXIMIZE`/`APP_UNMAXIMIZE`, `APP_CLOSE`). Hidden in full screen. | [AppTitleBar.qml] 100–122; [AppSystemButtons.qml] 29–87 |
| Drag area | **Only the title label's rectangle** (`titleMoveArea`). The gaps between menus and the space beside the buttons do not drag. | [AppTitleBar.qml] 125–132; [win/Main.qml] 41–45 |

**Button look** ([AppButtonBackground.qml] 27–75). The buttons are transparent at rest. On hover they show
`buttonColor` at 0.5 opacity, and on press `buttonColor` at 1.0. **The close button has no red hover**; all
three buttons look alike. Keyboard focus draws the usual `fontPrimaryColor` border, `navCtrlBorderWidth`
(2) wide, inside the button. In high contrast the theme's `borderWidth` (1) outlines each button in
`strokeColor`.

**Title text** ([mainwindowtitleprovider.cpp] 98–115). The title reads "MuseScore Studio" with no score
open, and otherwise the score name plus the part name (`projectNameAndPartName`). There is no app-name
suffix. The unsaved-changes marker is shown only on macOS: `MainWindowBridge::setFileModified` is empty
on the other platforms ([mainwindowbridge.cpp] 102–109). On Windows and Linux, unsaved changes show
on the score tab instead.

### Windows title bar: frame behaviour

`WinWindowsController` handles the native side ([winwindowscontroller.cpp]):

- **Style.** When the main window registers, the controller sets `WS_POPUP | WS_CAPTION | WS_SYSMENU |
  WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME` and calls `DwmExtendFrameIntoClientArea` with 1 px
  margins on all sides. Windows therefore keeps the DWM **drop shadow** and the snap and animation
  behaviour of a normal captioned window (50–65).
- **Frame removal.** `WM_NCCALCSIZE` returns `WVR_REDRAW` without insetting the client rectangle, so the
  whole window is client area. When the window is maximised, the client rectangle becomes the monitor's
  work area. With an auto-hidden taskbar, one pixel is left free on the taskbar's edge so the taskbar
  can still be revealed (158–196).
- **Hit testing** (`WM_NCHITTEST`, 256–342):
  - inside the title move area → `HTCAPTION`, which gives native drag, **double-click to maximise and
    restore**, and **Aero Snap** (drag to the top or the sides, and shake);
  - a resize band of `iBorderWidth + iPaddedBorderWidth + 2` px (`borderWidth()`, 479–487) **inside** the
    window's edges → `HTLEFT`, `HTTOPRIGHT` and so on; no resize band while maximised;
  - everything else goes to Qt.
- **No snap layouts.** The controller never returns `HTMAXBUTTON` for the maximise button, so Windows 11
  does not show the snap-layout flyout when the pointer hovers over maximise. The button is an ordinary
  QML button.
- **System menu.** A right click on the caption (`WM_NCRBUTTONDOWN`) opens the window's system menu with
  `TrackPopupMenu`. Before it opens, Restore, Move, Size, Minimise and Maximise are enabled or greyed to
  match the window's state (344–405). Win+arrow keys work because the window keeps `WS_MAXIMIZEBOX` and
  `WS_THICKFRAME`. Alt+Space was not traced. The menu bar's Alt handling (below) consumes a lone Alt
  release, so this needs a native check before anyone relies on it.
- **Max size.** `WM_GETMINMAXINFO` fixes the maximised size and position to the work area of the
  window's monitor (198–229).
- **No white flash.** `WM_ERASEBKGND` paints with the theme's `backgroundPrimaryColor`, for the main
  window and for every registered dialog (124–126, 140–156, 231–254).
- **Screen changes.** When the window moves to another screen, it is sent `SWP_FRAMECHANGED` so the
  frame is recalculated (67–90). Hit testing scales the QML move area by `guiScaling()` (271–276).
- **Size.** The default is 1150 × 800 and the minimum is **1050 × 500** ([AppWindow.qml] 41–45). On first
  start, or after quitting in full screen, the window opens maximised (see the docking research).

### Menu bar

The same `AppMenuBar` serves the Windows title bar and the Linux in-window menu row ([AppMenuBar.qml]).

- **Items.** Each top-level menu is a `FlatButton` of type `TextOnly`, `isNarrow`, with **margins 8**, so
  it is the text width + 16 px wide and `defaultButtonSize` (30) high (121–140; [FlatButton.qml] 84–88,
  302–312). The text is in `defaultFont`. While its menu is open, the button is an `accentButton`.
- **Pointer.** Click opens and closes. While a menu is open, hovering another title switches to its menu
  (189–197). Clicking outside both the bar and the open menu closes it ([navigableappmenumodel.cpp]
  211–223).
- **Keyboard** ([navigableappmenumodel.cpp] 260–345):
  - tapping **Alt** alone highlights the first menu title and shows the **mnemonic underlines**, which
    are hidden until then (`titleWithMnemonicUnderline`, [AppMenuBar.qml] 176);
  - **Alt+letter** opens that menu directly;
  - while titles are highlighted: Left and Right move between titles, Down, Space, Return or Enter
    open the menu, Esc leaves, and a plain letter jumps by mnemonic;
  - a second Alt tap leaves and restores the previous keyboard focus.

  **There is no F10 binding**, and [shortcuts.xml] has none either.
- **Accessibility.** The bar is a Panel named "Application menu". Each title is a Button named by its
  title text (72–78, 144–171).
- **Menus.** The top level reads File, Edit, View, Add, Format, Tools, Plugins, Help, and Diagnostics in
  developer builds ([appmenumodel.cpp] 199–580). Preferences is the **last item of Edit**, with the macOS
  `PreferencesRole` (263).

### Under the title bar

- **Main toolbar** ([WindowContent.qml] 62–96). It is a non-floatable, non-closable dock toolbar row with
  the page tabs **Home, Score, Publish** ([maintoolbarmodel.cpp] 85–97). These are `PageTabButton`s:
  36 high, in `largeBodyFont` (14), with the selected tab in bold and marked by a **2 px accent underline**
  and an accent fill at 10 % ([MainToolBar.qml] 79–101; [PageTabButton.qml] 43, 89–125, 171–215). They
  switch whole pages, each with its own dock layout. HikariSub's presets are the closest analogue.
- **Workspace switcher.** It is not in the title bar. It is a text button with the `WORKSPACE` icon in
  the **status bar** that opens the Workspaces menu ([NotationStatusBar.qml] 115–142), plus
  View → Workspaces.
- **Score tabs.** A 26 px strip on `backgroundSecondaryColor` above the score view, with a rounded close
  "×" on each tab. The selected tab takes `popupBackgroundColor` ([NotationSwitchPanel.qml] 36–38;
  [NotationSwitchButton.qml] 42–110, 147–172).

## 2. Preferences

### Window

[PreferencesDialog.qml] is a `StyledDialogView` (see §4), so it is a separate top-level window with the
system's title bar:

- title "Preferences";
- content **880 × 640**, `resizable: true`, so 880 × 640 is also its minimum (34–38; §4 "Sizing");
- application-modal (the `DialogView` default).

It opens from Edit → Preferences… (`preference-dialog`, [applicationuiactions.cpp] 226–231). The handbook
says Ctrl+, opens it on Windows and Linux ([Preferences handbook]), but v4.7.5's [shortcuts.xml] has no
binding for `preference-dialog`. Only the macOS menu role brings Cmd+, there.

Layout ([PreferencesDialog.qml] 126–193):

```
┌──────────────┬──────────────────────────────────────────────┐
│ ⓘ General    │  Page (scrolls)                              │
│ ⓘ Appearance │    Section title (bold)                      │
│ …            │    label (208)    [control 208]              │
│ (220 wide)   │    ──────────── separator ──────────────     │
│              │    Section title …                           │
├──────────────┴──────────────────────────────────────────────┤
│ [Reset preferences]                       [Cancel] [  OK  ] │  70 high
└─────────────────────────────────────────────────────────────┘
```

### Page list

[PreferencesMenu.qml] is a `LegacyTreeView` on `backgroundPrimaryColor`. It is 220 wide and starts 12 px
from the top (57–87, [PreferencesDialog.qml] 137–147). A 1 px vertical `SeparatorLine` parts it from the
page.

- **Rows** are 36 high (`rowDelegate`). Each is a horizontal `PageTabButton`: a 24 × 24 icon from the
  icon font, a 16 px gap and the title, with left padding 16 × (depth + 1). The title is in `bodyFont`,
  or `bodyBoldFont` when selected (89–149).
- **Selected row**: accent at 10 % opacity, a **2 px accent bar on the left edge** and a bold title.
  **Hover**: `buttonColor` at 0.5 ([PageTabButton.qml] 89–125, 171–215).
- **Keyboard**: one NavigationPanel with arrow keys. Moving the keyboard highlight selects the page at
  once (`navigation.onActiveChanged` → `selectRow`, 112–116). The accessible role is ListItem.
- **Tree.** The model supports child pages that expand when their parent is selected
  ([preferencesmodel.cpp] 257–279, 317–337), but **no page has children at v4.7.5**. The list is flat.
- **Last page.** The dialog opens on the last page viewed, or "general" the first time
  ([preferencesmodel.cpp] 137–152). The page id is held **in memory only**
  (`m_preferencesDialogCurrentPageId`, [appshellconfiguration.cpp] 271–279), so a restart starts on
  General again. Callers can open a given page with parameters (`currentPageId`, plus page properties such
  as `shortcutCodeKey`).

The pages, in order ([preferencesmodel.cpp] 156–203):

| Id | Title | Icon |
| --- | --- | --- |
| general | General | `SETTINGS_COG` |
| appearance | Appearance | `EYE_OPEN` |
| canvas | Canvas | `NEW_FILE` |
| cloud | Save & publish | `CLOUD_FILE` |
| note-input | Note input | `EDIT` |
| score | Score | `SCORE` |
| audio-midi | Audio & MIDI | `AUDIO` |
| midi-device-mapping | MIDI mappings (only with the MIDI-remote module) | `MIDI_INPUT` |
| video | Video | `VIDEO` |
| percussion | Percussion | `PERCUSSION` |
| import | Import | `IMPORT` |
| shortcuts | Shortcuts | `SHORTCUTS` |
| general-folders | Folders | `OPEN_FILE` |
| advanced | Advanced | `CONFIGURE` |
| braille | Braille | `BRAILLE` |

The handbook's list ([Preferences handbook]) is older: it has an "Update" page and no Video or
Percussion. The update check now sits on General.

### Page layout

- **Page** ([PreferencesPage.qml]):
  - the background is **`backgroundSecondaryColor`**, against the list's `backgroundPrimaryColor` (31);
  - the content sits in a `StyledFlickable` with **30 px margins on all four sides**, and a vertical
    scroll bar appears when needed (40–41, 63–86);
  - when the page scrolls, 30 px gradient fades at the top and bottom blend the content into the
    background (88–116).
- **Sections.** A page is a `Column` of sections with spacing 24 (`sectionsSpacing`), separated by
  full-width `SeparatorLine`s (for example [GeneralPreferencesPage.qml] 45–137: Language | Program
  start | Welcome dialog | Automatic update).
- **Section** ([BaseSection.qml]):
  - a title in `bodyBoldFont`, then the content 18 below it;
  - rows in the content 12 apart;
  - `columnWidth` 208 and `columnSpacing` 12.
- **Label + control row** ([ComboBoxWithTitle.qml]): a label 208 wide, wrapping to at most 2 lines and
  vertically centred, 12 px gap, then the control 208 wide. Check boxes, toggles and radio groups take
  their text on the control itself and span the section ([ProgramStartSection.qml] 42–103).
- Each section is its own NavigationPanel, named by its title, with arrow-key movement inside
  ([BaseSection.qml] 40–52).

Program start, as an example section ([generalpreferencesmodel.cpp] 188–211): radio buttons for **Start
empty** (the default, [appshellconfiguration.cpp] 46, 84), **Continue last session**, **Start with new
score** and **Start with score** (followed by a file picker).

### Search

There is **no Preferences-wide search or filter**. Two pages have a search field of their own:

- **Advanced** filters its key/value table ("Search advanced", [AdvancedTopSection.qml] 60–69);
- **Shortcuts** filters its list ("Search shortcut", 160 wide, [ShortcutsTopPanel.qml] 93–103).

### OK, Cancel and Reset

The dialog edits settings inside one **transaction** ([settings.cpp] 296–362):

- **Opening** calls `beginTransaction()` (`PreferencesModel::load` → `startEditSettings`). From then on,
  writes change values in memory and fire their change channels, so the app **previews changes live**
  (theme, fonts and so on). Nothing is written to disk.
- **OK** ([PreferencesDialog.qml] 176–191) calls `commitTransaction()`, which writes every changed key,
  then calls each page's `apply()`. The Shortcuts page saves its bindings in its `apply()`. If any
  `apply()` fails, the dialog stays open.
- **Cancel** calls `rollbackTransaction()`, which sends every changed key's old value back through its
  channel and so undoes the preview (171–174). **Esc and the window's close button also roll back**:
  `PreferencesModel`'s destructor calls `cancel()` ([preferencesmodel.cpp] 39–45).
- **There is no Apply button.** A change shows at once, but is only kept with OK.
- **Reset preferences** sits on the left of the button row ([PreferencesButtonsPanel.qml] 49–58). It asks
  first, in a warning with **Cancel as the accent (default) button**:
  - title: "Are you sure you want to reset preferences?";
  - text: "This action will reset all your app preferences and delete all custom shortcuts. It will not
    delete any of your scores. This action cannot be undone." ([preferencesmodel.cpp] 212–231).

  On Reset, every page's `reset()` runs, then `Settings::reset` **clears the settings file immediately and
  ends the transaction**, and a new transaction starts ([preferencesmodel.cpp] 233–245;
  [settings.cpp] 95–120). **Cancel cannot undo a reset.** It resets everything, not just the current
  page.
- Button order follows the platform (§4): OK before Cancel on Windows, Cancel before OK on Linux and
  macOS. OK is the accent button.

### Shortcuts page

This is the model for AM1's Settings → Hotkeys group and for the KB cards.

- **Layout** ([ShortcutsPage.qml] 76–145, spacing 20):
  - top row: **Define…** and **Clear**, then **Search shortcut** on the right;
  - a `ValueList` with columns "action" and "shortcut", each row showing the action's icon;
  - bottom row: **Import**, **Export** on the left and **Reset to default** on the right.

  Buttons are at least 104 wide. The list is **flat and sorted, with no groups**. Search matches a
  `searchKey` role ([ShortcutsList.qml] 45–61).
- **Editing.** Double-clicking a row or pressing Define… opens "Enter shortcut sequence", a 538 × 200
  dialog with 20 px margins ([StandardEditShortcutDialog.qml] 28–86). It contains:
  - the header "Define keyboard shortcut" in `headerBoldFont` (22);
  - "Old shortcut:" and "New shortcut:" fields, the new one with an accent border, which records keys as
    they are pressed;
  - a conflict line;
  - a button row of **Clear** on the left, then Cancel and Save.

  All keys are captured except Esc and Tab ([EditShortcutDialogContent.qml] 61–172).
- **Conflicts.** While a sequence is typed, the conflict line reads "This shortcut is already assigned
  to: **<action>**". Saving then asks "Reassign shortcut": "… Are you sure you want to assign it to <b>%1</b>
  instead?", with Cancel and OK ([editshortcutmodel.cpp] 216–251).
- **Deep link from plugins.** On Home → Plugins, a plugin's "Edit shortcut" opens Preferences on the
  Shortcuts page with the search pre-filled with the plugin's action code (`currentPageId=shortcuts`,
  `shortcutCodeKey`; [extensionslistmodel.cpp] 206–220; [ShortcutsPage.qml] 58–61).

## 3. Home screen

### Structure

Home is a **page**, `musescore://home`, chosen by the Home tab in the main toolbar. It is not a document
tab and not an empty-state view ([HomePage.qml]).

- **Left menu** ([HomePage.qml] 79–106; [HomeMenu.qml]). A fixed-width, non-floatable, non-closable dock
  panel, **260** wide. It becomes **icons-only at 76** when the window is narrower than its minimum
  width + 184 (`iconsOnly`, 84–93). From the top:
  - an account button, 60 high and 20 below the top (64–79);
  - **Scores**, **Plugins**, **MuseSounds** and **Learn** as vertical `PageTabButton`s with a 30 px gap
    and 30 px left padding (90–130).
- **Central area**: the chosen section's page. Scores is the default.

### Scores page

[ScoresPage.qml], on `backgroundSecondaryColor`, with **46 px side margins** (`sideMargin`):

```
Scores (titleBoldFont, 32 DemiBold)                        [⌕ Search      220]
[ New & recent | My online scores ]          [⟳ Refresh] [▦][≣]  (grid / list)
┌─────────────────────────────────────────────────────────────────────────────┐
│ ＋ New score   ▢ score   ▢ score   …   (grid)    or    rows (list)          │
└─────────────────────────────────────────────────────────────────────────────┘
[Score manager (online)]                                  [New] [Open other…]   ← 100 high
```

| Part | Detail | Source |
| --- | --- | --- |
| Header row | The title "Scores" in `titleBoldFont` (32), 46 from the top; a search field 220 wide on the right ("Search recent scores") | [ScoresPage.qml] 70–110 |
| Tabs | `StyledTabBar` with **New & recent** and **My online scores**, 46 below the header. The tab position is remembered (`scoresPageModel.tabIndex`). | 112–168 |
| View switch | Grid / List icon radio buttons, 30 × 30; **Refresh** only on the online tab. The view type is remembered. | 180–224 |
| Content | Starts 24 below the tabs. Grid or list of recent scores. | 227–243 |
| Bottom bar | 100 high: **Score manager (online)** on the left (min width 216); **New** and **Open other…** on the right, 12 apart | 304–373 |

Recent scores ([RecentScoresView.qml], [recentscoresmodel.cpp] 60–104):

- The **first item is always "New score"**; a "no results" item is added for empty searches.
- Each score shows its **name** (with the suffix when it is not `.mscz`), **path**, file **size**, a
  **cloud mark**, and the **time since the file was modified** ("Modified", from the file's mtime, upper
  case). It is not the time it was last opened.
- **Grid**: cells 172 × 294 with a 172 × 224 thumbnail and 60 / 40 px gaps between columns and rows
  ([ScoresGridView.qml] 111–121; [ScoreGridItem.qml] 79–92).
- **List**: rows 64 high under a 44 high header, columns 44 apart (Name, Modified 25 %, Size 15 %)
  ([ScoresListView.qml] 141, 191–192; [ScoreListItem.qml] 42).
- **Missing files are removed silently.** Each read of the list drops entries whose file no longer
  exists (`removeNonexistentFiles`, [recentfilescontroller.cpp] 59–70, 180–195), and so does
  `prependRecentFile` (77–98).
- **There is no context menu and no per-item remove.** File → Open recent → Clear recent files empties the
  whole list ([appmenumodel.cpp] 102–116, [recentfilescontroller.cpp] 118–123).
- Opening a score: a click (grid) or activating the row (list). Double-click per the handbook
  ([Opening and saving]).

### Startup, sessions and "What's new"

- **Which page opens at startup** ([startupscenario.cpp] 64–75, 140–184):
  - **Start empty**, **Start with new score** and crash **Recovery** open Home;
  - **Continue last session** and **Start with score** open the Score page.
- **Session restore is not offered on Home.**
  - The session is the list of open project paths ([sessionsmanager.cpp] 56–123). It is cleared on a
    normal quit unless the startup mode is Continue last session (44–53).
  - If paths are left over at the next start, the app crashed, and a question appears: "The previous
    session quit unexpectedly. Do you want to restore the session?" (No / Yes,
    [startupscenario.cpp] 155–158, 330–345).
- **What's new** comes in two forms, both dialogs:
  - the **Welcome dialog** ([WelcomeDialog.qml]) is an 812 wide carousel:
    - a title in `headerBoldFont`, a 572 × 322 image with 8 px corners and 48 px chevron buttons;
    - a description in `largeBodyFont`, an accent call-to-action button and page dots;
    - a 60 px footer on `backgroundSecondaryColor` with "Don't show welcome dialog on startup" and OK.

    Its content comes from MuseScore's server, not from the release notes. Preferences → General has a
    Welcome dialog section to turn it back on.
  - the **update notice** ([AppReleaseInfoDialog.qml]) is 644 × 474 with 22 px margins:
    - "A new version of MuseScore Studio is available!" in `headerBoldFont`, then "Release notes";
    - full-width separators and the notes as **`Text.MarkdownText`** ([ReleaseNotesView.qml] 59);
    - older releases' notes in expandable sections;
    - the buttons Remind me later, Skip and Install.

    The notes are the `bodyMarkdown` of the release feed ([appupdateservice.cpp] 335).

## 4. Dialogs

### Frame

Every QML dialog is a `StyledDialogView` → `DialogView` → `WindowView` ([StyledDialogView.qml];
[dialogview.cpp]; [windowview.cpp]):

- A **separate top-level `QQuickView`** with `Qt::Dialog` flags and the app icon ([dialogview.cpp]
  55–56). It is not an overlay inside the main window. Its **transient parent** is the window that opened
  it, or the top window ([windowview.cpp] 192, 413–426).
- The **system title bar**, on **every platform**, Windows included. The custom title bar is for the main
  window only. On Windows, `applyPlatformStyleOnWindowForTheme` is empty ([windowsplatformtheme.cpp]
  95–97), so a dialog's title bar does not follow MuseScore's dark theme.
- The window colour is `backgroundPrimaryColor` and repaints on theme change ([dialogview.cpp] 61–72).
  The title defaults to the application title when the dialog sets none (82–85).
- `frameless: true` instead draws a 4 px rounded, 1 px `strokeColor` bordered card with a shadow
  ([StyledDialogView.qml] 94–107). Only the two progress dialogs use it.
- It **closes when its parent hides** ([windowview.cpp] 194–198).

### Sizing

- Size is `contentWidth × contentHeight` plus `margins` on each side ([StyledDialogView.qml] 65–117).
- **Not resizable** (the default): the window is fixed at its content's implicit size. **Resizable**: the
  implicit size is the minimum and the window can only grow ([windowview.cpp] 458–472).
- If the content's implicit size changes while open, the window follows.

| Dialog | Size | Resizable | Source |
| --- | --- | --- | --- |
| Preferences | 880 × 640 | yes | [PreferencesDialog.qml] 36–38 |
| About | 480 × 424 | no | [AboutDialog.qml] 36–37 |
| Welcome | 812 × content | no | [WelcomeDialog.qml] 36–37 |
| Update available | 644 × 474, margins 22 | no | [AppReleaseInfoDialog.qml] 36–39 |
| Enter shortcut sequence | 538 × 200, margins 20 | no | [StandardEditShortcutDialog.qml] 33–36 |
| Standard message (`StandardDialog`) | text column up to **420** wide, or wider when the buttons need it; margins 16 | no | [StandardDialog.qml] 58–61; [StandardDialogPanel.qml] 159–171 |

16 of the 52 `StyledDialogView`s at v4.7.5 set `resizable: true`.

### Placement

Placement is computed on every open; `DialogView::updateGeometry` ([dialogview.cpp] 118–170):

1. **Centre on the main window**, then move **20 px down** (`DIALOG_WINDOW_FRAME_HEIGHT`). If the main
   window is hidden, centre on the screen instead.
2. Move it inside the parent screen's available geometry, keeping a **20 px margin**. At the top the
   margin also includes the style's title-bar height.
3. If it still does not fit, shrink it to the available area minus those margins.

The reference screen is the parent window's screen. In full screen it is the whole screen rather than the
available area ([windowview.cpp] 437–450).

**QML dialogs remember nothing.** They recentre on each open, and a user's resize is lost. **QWidget
dialogs do remember**: `WidgetStateStore` saves and restores `saveGeometry()` in `QSettings` under
`Geometries/<objectName>`, on hide and show ([widgetstatestore.cpp] 29–47). These are Style
([editstyle.cpp] 1267–1286), Page settings, Staff/part properties, Measure properties, Transpose,
Tuplet, Select, Master palette, Time signature properties, Special characters and a few more.

### Modality

- `DialogView.modal` defaults to **true**, which means **`Qt::ApplicationModal`** ([dialogview.h] 105;
  [dialogview.cpp] 90–94). A build flag can turn all modality off (`MUSE_MODULE_UI_DISABLE_MODALITY`).
- No user-facing QML dialog sets `modal: false`. Modeless windows are opened with `?modal=false` in the
  URI, which only the **diagnostics** windows and **plugin/extension** dialogs use. Extensions declare
  `modal` in their manifest ([interactiveprovider.cpp] 455–477; [diagnosticsactionscontroller.cpp] 35–40).
- QWidget dialogs opened through the interactive layer are application-modal unless the URI says
  otherwise ([interactiveprovider.cpp] 720–721). Style and Page settings call `setModal(true)`
  ([editstyle.cpp] 241).

### Keys and focus

- **Esc closes** the dialog (`closeOnEscape`, default true) as a reject ([StyledDialogView.qml] 42,
  87–91). Text fields and the shortcut recorder take Esc first.
- The dialog's content is an **Exclusive** navigation section, so F6, Tab and the arrow keys stay
  inside it ([StyledDialogView.qml] 74–92).
- On open, keyboard focus goes to the dialog. A `ButtonBox` puts it on its **accent button**, or failing
  that its first button ([ButtonBox.qml] 61–68). Screen readers get the whole message read once, 500 ms
  after opening ([StyledDialogView.qml] 56–63; [StandardDialogPanel.qml] 123–142).
- **Enter, Return and Space trigger the focused control** (`nav-trigger-control`, [shortcuts.xml]
  79–83). So **Enter accepts only because focus starts on the accent button**. There is no separate
  default-button binding: once focus moves to Cancel, Enter cancels.
- **Focus returns to the opener.** The control that was active when the dialog opened is reactivated on
  close (`activateNavigationParentControl`, [windowview.cpp] 165, 535–555).

### Buttons

`ButtonBox` ([ButtonBox.qml]; [buttonboxmodel.h] 137–176; [buttonboxmodel.cpp] 111–129):

- Buttons are 12 apart. Custom buttons marked `isLeftSide` go to the far left, then a stretch, then the
  standard buttons.
- **Order by platform**:
  - Windows: accept (OK, Save) **before** reject (Cancel);
  - Linux and macOS: reject **before** accept.

  MuseScore does not tell KDE from GNOME; all of Linux gets the macOS-like order.
- **OK and Save are accent buttons.** Cancel, Apply, Reset and Restore defaults are plain.
- Message dialogs ([StandardDialogPanel.qml] 144–231):
  - an optional 48 px type icon, 27 from the text;
  - the title in `largeBodyBoldFont` (14), 18 above the body text;
  - an optional "Don't show this message again" check box;
  - "Show details" as a left-side button that expands an error-details view.

### The Style dialog (for SY1)

Score style is a QWidget dialog ([editstyle.cpp]), not QML:

- **Page list on the left**, a stacked page on the right. The last page and Text-styles sub-page are
  remembered in the configuration ([editstyle.cpp] 1236–1240).
- **Live editing**:
  - opening starts an undo transaction (`prepareChanges("Edit style")`), and every field change applies
    to the score at once;
  - **OK commits** the transaction, **Cancel rolls it back** (1267–1272, 1583–1616);
  - "Apply to all parts" is an extra `ApplyRole` button (241–243).
- **A reset icon button (`UNDO`) beside every field** returns that one value to its default, and only
  that one (1026, 1064, 1087–1227).
- **Deep links.** `edit-style` takes `currentPageCode` and `currentSubPageCode`, so other UI can open the
  dialog on one text style ([notationactioncontroller.cpp] 1789–1793; used from the staff-type dialog,
  [editstafftype.cpp] 164–165).
- The window's geometry is remembered (above).

### Popups

Menus, dropdowns and popovers are `PopupView`s ([popupview.cpp]; [PopupContent.qml]):

- each is a separate frameless window: `Qt::Tool` on X11, `Qt::Popup` on Wayland, and
  `BypassWindowManagerHint` outside KDE (34–57);
- **padding 12** (64–65), a `popupBackgroundColor` fill, **radius 4**, a 1 px `strokeColor` border, a drop
  shadow blurred by the padding, and an optional arrow towards the anchor
  ([PopupContent.qml] 100–160);
- they close on a press outside the parent (`CloseOnPressOutsideParent`, [popupview.h] 156) and on Esc.

## 5. Panel content (for LE1, AM1, TS1)

The Properties panel ([InspectorForm.qml]) is MuseScore's densest editing surface and the closest
analogue to the Line editor, Shift Times and Automation panels. The panel header and chrome are in the
docking research. Inside the panel:

| Rule | Value | Source |
| --- | --- | --- |
| Side margins | 12 | [InspectorForm.qml] 118–120 |
| Top / bottom margin, gap between sections | 12 / 12 / 12 | [InspectorForm.qml] 64–67 |
| Section header | `ExpandableBlank`: a small down-arrow icon, rotated −90° when collapsed, 4 px gap, title in `bodyBoldFont`; content 12 below | [ExpandableBlankSection.qml] 82–112; [ExpandableBlank.qml] 52 |
| Property | **Label above the control**, 8 apart. The label row wraps to 2 lines and has a **20 × 20 reset button (`UNDO`) at its right end**, enabled only when the value differs from the default. When the value comes from the score style, the button is instead a "⋯" menu: Reset, Save as default style. | [InspectorPropertyView.qml] 65–183; [PropertyResetButton.qml] 28–37 |
| Two controls on one row | each half the width, 8 apart (X/Y offsets and similar) | [OffsetSection.qml] 47–52 |
| Controls | spin boxes, dropdowns and text fields 30 high; segmented icon groups (`FlatRadioButton`) fill the width evenly | [FlatRadioButton.qml] 55–66 |

## 6. Type scale and sizes

The default UI font is Segoe UI on Windows and the system's general font elsewhere, at **12 px** (pixel
size, not points). Other roles are derived from it ([uiconfiguration.cpp] 555–575, 645–661;
[themeapi.cpp] 400–424):

| Role | Size at body 12 | Weight | Used for |
| --- | --- | --- | --- |
| `bodyFont` / `bodyBoldFont` | 12 | Normal / DemiBold | everything ordinary; section titles; selected page in the Preferences list |
| `largeBodyFont` / `largeBodyBoldFont` | 14 (body + body/6) | Normal / DemiBold | page tabs, Home menu, recent-score names, message-dialog titles, `StyledTabButton` |
| `tabFont` / `tabBoldFont` | 16 (body + body/3) | | Welcome call-to-action |
| `headerFont` / `headerBoldFont` | 22 (body + body/1.2) | | dialog headings (Define keyboard shortcut, A new version is available, Welcome) |
| `titleBoldFont` | 32 (body + body/0.6) | DemiBold | page titles ("Scores") |
| `defaultFont` | system default family at 12 | | title-bar title and menu titles |
| icon font | body + 4 = 16; toolbar body + 6 = 18 | | `MusescoreIcon` glyphs |

`defaultButtonSize` is max(30, max(body line height, icon line height) + 8), so **30** at defaults
([themeapi.cpp] 449–458).

| Element | Size | Source |
| --- | --- | --- |
| Windows title bar | **32** high; item spacing 8 | [win/Main.qml] 58; [AppTitleBar.qml] 56 |
| Window buttons | 30 × 30 icon-only, 8 apart | [AppSystemButtons.qml] 34; [FlatButton.qml] 291–300 |
| Menu title | text + 2 × 8 wide, 30 high | [AppMenuBar.qml] 134–137 |
| Native resize band (Windows) | `iBorderWidth + iPaddedBorderWidth + 2`: 1 + 4 + 2 = 7 px with Windows' default metrics at 100 % (derived, not measured) | [winwindowscontroller.cpp] 479–487 |
| Main window | default 1150 × 800, minimum 1050 × 500 | [AppWindow.qml] 41–45 |
| Page tab (Home/Score/Publish) | 36 high, 2 px accent underline | [PageTabButton.qml] 43, 89–125 |
| Score tab strip | 26 high | [NotationSwitchPanel.qml] 36 |
| Preferences | 880 × 640; list 220; rows 36; icons 24; button row 70 with 20 margins | §2 |
| Preferences page | margins 30; sections 24 apart; title → content 18; rows 12; label and control columns 208, 12 apart | §2 |
| Text field / dropdown | 30 high; dropdown default 126 wide; field radius 3 | [TextInputField.qml] 98, 141; [StyledDropdown.qml] 63–64, 155 |
| Check box / radio / toggle | 20 / 20 / 36 × 20; label 6 from the indicator | [CheckBox.qml] 75–81; [RoundedRadioButton.qml] 40, 104; [ToggleButton.qml] 75–81 |
| Content tabs (`StyledTabBar`) | 32 high, 36 apart, 2 px accent underline, `largeBodyFont` | [StyledTabBar.qml] 31–34; [StyledTabButton.qml] 53, 92–107 |
| Search field | 220 by default | [SearchField.qml] 35 |
| Button box | 12 between buttons; text buttons at least 132 wide (24 when narrow) | [ButtonBox.qml] 41–42; [FlatButton.qml] 84–86 |
| Home menu | 260, or 76 icons-only; rows 36 | §3 |
| Scores page | margins 46; grid cell 172 × 294; list row 64; bottom bar 100 | §3 |
| Dialog placement | 20 px below centre; 20 px screen margin | [dialogview.cpp] 40, 118–170 |
| Message dialog | margins 16; icon 48; icon gap 27; text up to 420 | §4 |
| Popup | padding 12; radius 4; 1 px border | §4 |
| Panel content | margins 12; sections 12; label → control 8; reset button 20 × 20 | §5 |

## 7. What HikariSub takes

HikariSub's starting point: one `ApplicationWindow` (1280 × 800) with a Qt Quick `MenuBar` under the
system title bar. Its dialogs are mostly in-window `Dialog` overlays (Settings, About, Credits, the update
notices); a few are separate `Window`s (Style Manager, Automation, Hotkey mapping). Some of the shell's
pieces already exist:

- the Compact Studio roles and metrics ([visual-language.md](../qt/ux/visual-language.md));
- dark system title bars that follow the theme on Windows (`title_bars.cpp`);
- an xdg-decoration client, Hyprland detection, system move and resize, and frameless windows with a
  drawn shadow, all built for floating panels in D3 (`window_decorations.cpp`,
  [docking.md](../qt/docking.md#chrome)).

The mapping below names what to take, and where HikariSub should **deliberately differ** from the
reference.

### SH1: custom title bar (#220)

| Topic | MuseScore 4.7.5 | HikariSub |
| --- | --- | --- |
| Platforms | Custom on Windows only. Linux is forced onto XWayland with system decorations. | **Differ (owner decision):** custom on Windows, KDE and GNOME; system on every tiling compositor, whatever decorations it draws. HikariSub runs native Wayland, so the Linux custom bar is new work. MuseScore offers no Linux code to copy. |
| Contents | Menus · centred title · min/max/close. No icon. | **Take** the order and the 32 px height (scaled by text size per Compact Studio, not fixed). **Add** the app icon on the left (SH1 lists it) and the layout switcher after the menus. The Document tabs go under the bar (WS4). |
| Layout switcher | Not in the title bar. Pages are tabs in a toolbar row; workspaces are a status-bar text button with a menu. | **Take the workspace button's form:** one button (layout icon + current preset or layout name + chevron) that opens a menu of the presets, a separator, the saved layouts, then Save layout…, Reset this layout, Manage…. A tab row would not fit beside the menus once users add layouts. |
| Title | Centred on the window when there is room, elided, hidden in full screen; score name only; modified marker only on macOS. | **Take** the centring and eliding. Keep today's window-title string. The Document tab already shows unsaved state, so the bar adds no marker. Hide the title before anything else when space runs out. |
| Drag area | The title label only. | **Differ:** every empty part of the bar drags (between menus, switcher, title and buttons). SH1 asks for "drag to move", and a narrow window would otherwise leave almost nothing to grab. |
| Double-click, Aero Snap, system menu (Windows) | Native through `HTCAPTION`; right-click on the caption opens the system menu with correct item states. | **Take** the whole `WM_NCCALCSIZE` / `WM_NCHITTEST` / `WM_GETMINMAXINFO` / `WM_ERASEBKGND` approach, the 1 px `DwmExtendFrameIntoClientArea` shadow, the auto-hide-taskbar pixel and the per-state system menu. Also open the system menu from a click on the app icon and from Alt+Space; verify Alt+Space natively. |
| Snap layouts (Windows 11) | **Missing**: maximise is a plain QML button. | **Differ (SH1 acceptance):** return `HTMAXBUTTON` for the maximise button's rectangle and handle the `WM_NC*BUTTON*` messages it then receives, so hover shows the flyout and the click still maximises. Hover and press states must be driven from those messages, since Qt sees no mouse events there. |
| Resize | A native band of about 7 px inside the edges; none when maximised. | **Take** on Windows. On KDE and GNOME use `QWindow::startSystemResize` from an edge band, and the drawn-shadow margin where the compositor composites (the floating-panel rules in docking.md). |
| Button look | Transparent; hover `buttonColor` at 0.5, press at 1.0; **no red close**. | **Take** (SH1: "window buttons follow the theme"): `raised` on hover, a stronger state on press, focus ring in `focus`. A red close-button hover is the one Windows convention left out; the mockup can show both if the owner wants to choose. |
| Menu keyboard | Alt tap highlights, Alt+letter opens, arrows and Esc; mnemonics hidden until Alt; **no F10**. | **Take** the model and hidden mnemonics. **Add F10** (SH1 acceptance) as the same "highlight first menu". **Keep** HikariSub's existing rule that an open menu takes every key. |
| Accessible names | Bar = Panel "Application menu"; titles = Buttons named by their text; window buttons named by tooltip. | **Take**, and name every control explicitly ("Minimise", "Maximise"/"Restore", "Close", "Layout: <name>", "HikariSub" for the icon). |
| Full screen | Title and buttons hidden; menus stay. | **Take.** Video fullscreen keeps its own window. |

Linux, per environment. This is new design; the reference has nothing here.

| Environment | Default | How |
| --- | --- | --- |
| KDE Plasma, Wayland | custom | `Qt::FramelessWindowHint`, and `zxdg_toplevel_decoration_v1.set_mode(client_side)` through the existing `window_decorations.cpp` so KWin draws nothing. Moves use `startSystemMove`, which keeps KWin's edge tiling and quick tile. A double-click maximises in QML. The right-click window menu needs `xdg_toplevel.show_window_menu`, which Qt does not expose publicly. SH1 should prove it through Qt's Wayland private API or record the gap. |
| GNOME, Wayland | custom | mutter has no xdg-decoration. Without the frameless flag Qt would draw its own decoration, and frameless removes it. `startSystemMove` gives mutter's edge tiling. Follow GNOME's button convention only if the mockup says so: GNOME's default layout shows close only. |
| KDE or GNOME on X11 | custom | Frameless with `_NET_WM_MOVERESIZE` through `startSystemMove`/`startSystemResize`. Draw a shadow only while a compositing manager runs, as the floating panels do. |
| Tiling Wayland (sway, Hyprland, niri, river…) | **system** | No custom bar. Menus, switcher and title go into an in-window row, as MuseScore's Linux menu row does. The compositor tiles the window, so the min/max buttons and dragging have no use there. **Owner decision (2026-10-19):** system is the default on every tiling compositor, whether it draws title bars (sway), only borders (Hyprland) or nothing. Compositor decorations never switch HikariSub to the custom bar on their own, so the card's earlier rule ("tiling compositors that draw no decorations") is dropped. SH1 detects the compositor as tiling, not its decorations: compositor globals (`hyprland_*`, as `window_decorations.cpp` reads them) or `XDG_CURRENT_DESKTOP`. An xdg-decoration `server_side` or `client_side` reply does not count as a signal. Any exception, such as custom on one named compositor, is an explicit owner decision recorded here and exposed through the setting, never inferred at run time. |
| Tiling X11 (i3 and similar) | system | Same as tiling Wayland, detected by window-manager name. |

The "Use system title bar" setting (SH1) goes on the Appearance page (PF1), with the per-environment
default above and a restart-free switch. MuseScore has no such setting. On tiling compositors the
setting starts on and the user may turn it off; the application never turns it off for them. The
default changes only when the owner names an exception in this table.

### SH2: Home page (#221)

| Topic | MuseScore | HikariSub |
| --- | --- | --- |
| Where | A page with its own tab and left menu, always reachable. | **Differ (owner decision):** shown in the main area only when no Document is open; no Home tab and no left menu. One view, not sections. |
| Anatomy | Title + search; tabs; grid/list; bottom bar New / Open other…. | **Take** title, search, list and an actions row. Actions: **New**, **Open…**, **Open video…**, plus **Restore last Session** when one exists. **Drop** the grid, thumbnails, the online tab and the account area. Subtitle files have no thumbnails. |
| Recent rows | Name, path, size, cloud, time since modified; 64 high; "New score" as the first item. | **Take** name (body, emphasised) above path (`muted`, middle-elided), plus a time column. **Differ:** show **last opened** time (SH2), not file mtime. No "New" pseudo-row, since New is an action. Size the rows from text (two lines + padding), not 64. |
| Missing files | Removed silently. | **Differ (SH2):** keep the row, mark it "Missing" in `muted`/`warning`, disable open, and offer Remove (button on the row, Delete key, context menu). |
| Session | Only after a crash, as a question dialog; or a startup mode. | **Differ:** a visible Restore last Session action on Home whenever a Session exists. HikariSub's P6 startup prompts and its session-restore setting (`SessionWindows.qml`) stay as they are; Home adds the action for when the user declined or chose not to restore. |
| What's new | A Welcome carousel dialog, plus release notes in the update dialog (Markdown). | **Differ (owner decision):** a What's new **card** on Home from GitHub release notes (SH3). Render it the way `ReleaseNotesView` does (`Text.MarkdownText` in a scrolling area). No popup after updates. |
| Sizes | 46 margins; 32 title; 220 search; 100 bottom bar. | Scale to Compact Studio: margins of about 24, the title in the dialog-title role (18) or one step up, search ≈ 220, actions in a normal button row. Fix them in the mockup. |
| Keyboard | Nav sections for header, tabs, view buttons, list and bottom bar. | Tab order: actions → search → list → What's new. Arrows move in the list, Enter opens, Delete removes a missing entry. Every control named. |

### DG1: dialog frame (#222)

| Topic | MuseScore | HikariSub |
| --- | --- | --- |
| Window | Separate top-level window, transient for its parent, system title bar on every platform. | **Take** separate top-level windows (DG1). **Differ:** a dialog title bar in the **custom style** when the main window uses the custom bar: title, plus close (and maximise only for resizable dialogs), on the same native frame code. When "Use system title bar" is on, or on tiling compositors, use the system frame. On Windows, keep the system bar dark in dark themes (`title_bars.cpp`), which MuseScore does not do. |
| Size | Fixed, or resizable with the initial size as minimum. | **Take**, with sizes derived from content and text (Compact Studio), never fixed pixel sizes. |
| First placement | Centred on the main window, 20 px down, kept 20 px inside the available area, shrunk if needed. | **Take** for the first open (DG1: "centred on its parent the first time"). Centre on the **opening window** (a floating panel or the Style Manager), not always the main window. |
| Later opens | QML: recentred, size forgotten. QWidget: geometry remembered per object name. | **Take the QWidget rule for every dialog:** remember size and position per dialog id in the Session/settings, and validate against current screens on restore (reuse the floating-panel screen rules). On Wayland remember size only, since the compositor places windows. |
| Modality | Application-modal by default; modeless only for diagnostics and extensions. | **Differ (owner decision): modeless where safe.** Suggested rule for the cards: informational windows (About, Credits, update notices, What's new) are modeless. Questions that block an operation in progress (save changes?, overwrite?, Translation-mode confirm) are **window-modal to their parent**, not application-modal, so floating panels stay usable. Editors that hold an uncommitted transaction (Style editor) stay modal until their target can be pinned. |
| Esc | Closes (reject); fields take it first. | **Take.** |
| Enter | Triggers the focused control; works as "default" only because focus starts on the accent button. | **Differ:** a real default button. Enter accepts from any non-multiline field, as `DialogButtonBox` already does with an accept-role button marked default. Focus starts on the first input, or on the default button when there is none. |
| Focus return | Back to the opener. | **Take** (DG1 acceptance). |
| Buttons | Windows: OK Cancel; Linux and macOS: Cancel OK; OK is the accent; extra buttons on the left. | **Keep HikariSub's rule** (visual-language "Footers close at the trailing edge"): OK before Cancel on Windows **and KDE**, after it on GNOME and macOS. MuseScore puts KDE with GNOME, which is wrong for Plasma. Accent = default button. |
| Message dialogs | 48 px icon, title 14 bold, text up to 420, optional "Don't show again", Show details. | **Take** for DG2's notices: type icon, bold title, body text capped at about 420 reference units, optional "don't show again". |

### PF1: Settings (#225)

| Topic | MuseScore | HikariSub today | Recommendation |
| --- | --- | --- | --- |
| Window | Separate, resizable, 880 × 640 | In-window modal `Dialog` 640 × 600 | **Take:** separate window on the DG1 frame, resizable, about 880 × 640 reference, size remembered. |
| Page list | 220 wide; 36 rows; 24 px icons; selected = accent 10 % + 2 px left bar + bold | 150 wide; 16 px icons on top-level pages; sub-pages indented | **Take** width, row height and selected look, mapped to Compact Studio: `select` fill, 3-wide inset leading-edge accent bar (the existing "Selected row marker"), bold. |
| Sub-pages | Supported but unused (flat list) | Editor › Conversion, Advanced; Audio › Advanced | **Flatten:** turn sub-pages into sections on their parent page, or into one Advanced page with its own search, as MuseScore's Advanced page has. Every setting must stay reachable (PF1). |
| Page body | `backgroundSecondary`; 30 margins; sections 24 apart with separators; bold section titles; label 208 + control 208 | Group boxes | **Take** the section form (bold title, separators, label column + control column), on `panel` against the list's `bg`. Use column widths from text, not 208. |
| Search | None global; Advanced and Shortcuts filter locally | Hotkeys has a filter | **Take as-is:** no global search (the reference has none, and PF1 says "if the reference has it"); keep the Hotkeys filter. |
| Per-option icons | **None**; icons only in the page list | Page icons | **Differ (owner decision, PF1):** the primary options on each page get icons drawn in house to K1's rules, beside the option's label. |
| Live preview | Yes, for everything, through a settings transaction | Appearance previews (`Theme.preview`) | **Take** for every setting that can apply without a reload; mark the others "Applies after restart / reload". |
| OK / Cancel / Esc / × | OK commits; Cancel, Esc and × roll back | OK / Apply / Cancel | **Take** the rollback on Cancel, Esc and ×. **Keep Apply** (a legacy button, and useful for audio-device and FFMS2 changes while the window stays open). This is a deliberate difference from MuseScore. |
| Reset | One global "Reset preferences", left side, warning with Cancel as default; clears the file at once, Cancel cannot undo it | "Restore defaults" restaging values | **Differ:** keep the reset **staged** (Cancel still undoes it) and scope it to **the current page** by default, with "Reset all settings…" behind its confirmation. MuseScore's reset also deletes custom shortcuts; ours must say plainly whether hotkeys are included. |
| Last page | Remembered for the run only | – | **Take**, and also remember it across runs (cheap and useful). |
| Hotkeys page | Flat searchable list; Define…/Clear; Import/Export; Reset to default; record dialog; reassign question on conflict; deep link with search pre-filled | Context groups | **Keep** the context groups (legacy hotkey contexts). **Take** the record dialog's form, the "already assigned to" line and the reassign question, and the deep link (`currentPageId` + filter) for AM1. |
| Placement of wave-6 settings | – | – | Appearance: Use system title bar (SH1). Line editor or Editor: Show tags (LE3), Time/Frames (LE2). Grid: auto-hide empty columns (GR3). Audio/Video: cursor sync (AV2). Editor: save the tab layout into ASS files (WS2). |

### TM1: Translation mode (#232)

MuseScore has no editing modes. What carries over:

- the **page-tab idea**: a preset selects a whole layout, and the mode follows the preset;
- the **toolbar slot in a panel's header** (docking research, `toolbarComponent`): put the translation
  actions (Paste all, Paste selection, Comment out original) in the Original-text panel's header row,
  and the toggles (Not confirmed, Moving tags) there as checkable icon buttons with names;
- the **message-dialog form** for the one-time "Turn on Translation mode for this file?" question:
  window-modal, OK as default.

### LE1: Line editor (#233)

Take the Properties panel's content rules (§5), adapted to Compact Studio density:

- 12 side margins;
- sections 12 apart with bold expandable headers only where a group is optional;
- two controls per row with an 8 gap (Start/End, Layer/Style);
- per-field reset only where a field has a meaningful default (Margins → 0 = Style's).

**Differ:** keep labels **beside** short fields when the dock is wide. Label-above, MuseScore's rule for a
fixed 300 px column, applies at the minimum dock width. The no-overflow rule from wave 5 governs.

### SY1: Style Manager and Style Editor (#247)

| Topic | MuseScore Style dialog | HikariSub |
| --- | --- | --- |
| Editing | Live on the score inside one undo transaction; OK commits, Cancel rolls back | **Take:** the Style editor previews live on video (SY3's font preview included) and commits on OK, with Cancel restoring. |
| Per-field reset | `UNDO` icon beside every field | **Take** for the Style editor's fields: reset to the catalog's or the default Style's value. |
| Deep link | `currentPageCode`, `currentSubPageCode` | **Take** for LE6: Edit style opens the editor on the current Line's Style directly. |
| Geometry | Remembered (`Geometries/<objectName>`) | **Take** through DG1, together with the last selected catalog/Style. |
| Modality | Modal | Modal for the editor while it holds uncommitted changes; the manager can be modeless if DG1's rule allows. |
| Colour | Native colour dialog through `selectColor` | **Differ:** SY2's single popover, using the popup frame (§4 "Popups"). |

### AM1: Automation panel (#248) and TS1: Shift Times (#249)

Both are dock panels, so the docking research's header rules apply. MuseScore has no direct analogue of
either. The content rules come from §5:

- 12 margins;
- bold section titles;
- label-above controls in a narrow dock;
- the primary action as one accent button at the end of its section ("Run", "Shift");
- status and errors as text under the action, in `muted` or `danger`, not as dialogs.

For **AM1** take the Shortcuts page's pieces:

- each macro row has a shortcut field that records keys, as the record dialog's "New shortcut" field
  does;
- a conflict shows "Already assigned to: <command>" inline, and saving asks the reassign question;
- an "Edit in Settings" item deep-links to Settings → Hotkeys with the Automation group filtered, as the
  plugin "Edit shortcut" link does.

For **TS1**, its options (amount as time or frames, direction, which Lines, which times) become sections
in that form. The history list fills the remaining height under the action, with its own Clear.

## Sources

Code links are pinned to tag `v4.7.5` (commit `3654226c2e99289916916953a98e585a3d3b315a`). Path prefix:
`https://github.com/musescore/MuseScore/blob/v4.7.5/`.

- [appshell CMakeLists.txt]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/appshell/qml/MuseScore/AppShell/CMakeLists.txt
- [win/Main.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/appshell/qml/MuseScore/AppShell/platform/win/Main.qml
- [AppTitleBar.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/appshell/qml/MuseScore/AppShell/platform/win/AppTitleBar.qml
- [AppSystemButtons.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/appshell/qml/MuseScore/AppShell/platform/win/AppSystemButtons.qml
- [AppButtonBackground.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/appshell/qml/MuseScore/AppShell/platform/AppButtonBackground.qml
- [AppMenuBar.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/appshell/qml/MuseScore/AppShell/platform/AppMenuBar.qml
- [PlatformMenuBar.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/appshell/qml/MuseScore/AppShell/platform/PlatformMenuBar.qml
- [linux/Main.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/appshell/qml/MuseScore/AppShell/platform/linux/Main.qml
- [mac/Main.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/appshell/qml/MuseScore/AppShell/platform/mac/Main.qml
- [AppWindow.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/appshell/qml/MuseScore/AppShell/AppWindow.qml
- [WindowContent.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/appshell/qml/MuseScore/AppShell/WindowContent.qml
- [MainToolBar.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/appshell/qml/MuseScore/AppShell/MainToolBar.qml
- [maintoolbarmodel.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/appshell/qml/MuseScore/AppShell/maintoolbarmodel.cpp
- [mainwindowtitleprovider.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/appshell/qml/MuseScore/AppShell/mainwindowtitleprovider.cpp
- [navigableappmenumodel.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/appshell/qml/MuseScore/AppShell/navigableappmenumodel.cpp
- [appmenumodel.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/appshell/qml/MuseScore/AppShell/appmenumodel.cpp
- [NotationStatusBar.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/appshell/qml/MuseScore/AppShell/NotationPage/NotationStatusBar.qml
- [HomePage.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/appshell/qml/MuseScore/AppShell/HomePage/HomePage.qml
- [HomeMenu.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/appshell/qml/MuseScore/AppShell/HomePage/HomeMenu.qml
- [WelcomeDialog.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/appshell/qml/MuseScore/AppShell/WelcomeDialog.qml
- [AboutDialog.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/appshell/qml/MuseScore/AppShell/AboutDialog.qml
- [applicationuiactions.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/appshell/internal/applicationuiactions.cpp
- [appshellconfiguration.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/appshell/internal/appshellconfiguration.cpp
- [startupscenario.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/appshell/internal/startupscenario.cpp
- [sessionsmanager.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/appshell/internal/sessionsmanager.cpp
- [main.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/app/main.cpp
- [shortcuts.xml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/app/configs/data/shortcuts.xml
- [winwindowscontroller.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/ui/internal/platform/windows/winwindowscontroller.cpp
- [windowsplatformtheme.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/ui/internal/platform/windows/windowsplatformtheme.cpp
- [linuxplatformtheme.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/ui/internal/platform/linux/linuxplatformtheme.cpp
- [macosplatformtheme.mm]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/ui/internal/platform/macos/macosplatformtheme.mm
- [mainwindowbridge.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/ui/view/mainwindowbridge.cpp
- [interactiveprovider.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/ui/view/interactiveprovider.cpp
- [widgetstatestore.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/ui/view/widgetstatestore.cpp
- [uiconfiguration.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/ui/internal/uiconfiguration.cpp
- [themeapi.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/ui/api/themeapi.cpp
- [StandardDialog.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/ui/qml/Muse/Ui/Dialogs/StandardDialog.qml
- [StandardDialogPanel.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/ui/qml/Muse/Ui/Dialogs/StandardDialogPanel.qml
- [settings.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/global/settings.cpp
- [diagnosticsactionscontroller.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/diagnostics/internal/diagnosticsactionscontroller.cpp
- [StyledDialogView.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/uicomponents/qml/Muse/UiComponents/StyledDialogView.qml
- [dialogview.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/uicomponents/qml/Muse/UiComponents/dialogview.cpp
- [dialogview.h]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/uicomponents/qml/Muse/UiComponents/dialogview.h
- [windowview.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/uicomponents/qml/Muse/UiComponents/windowview.cpp
- [popupview.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/uicomponents/qml/Muse/UiComponents/popupview.cpp
- [popupview.h]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/uicomponents/qml/Muse/UiComponents/popupview.h
- [PopupContent.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/uicomponents/qml/Muse/UiComponents/internal/PopupContent.qml
- [ButtonBox.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/uicomponents/qml/Muse/UiComponents/ButtonBox.qml
- [buttonboxmodel.h]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/uicomponents/qml/Muse/UiComponents/buttonboxmodel.h
- [buttonboxmodel.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/uicomponents/qml/Muse/UiComponents/buttonboxmodel.cpp
- [FlatButton.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/uicomponents/qml/Muse/UiComponents/FlatButton.qml
- [FlatRadioButton.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/uicomponents/qml/Muse/UiComponents/FlatRadioButton.qml
- [PageTabButton.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/uicomponents/qml/Muse/UiComponents/PageTabButton.qml
- [StyledTabBar.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/uicomponents/qml/Muse/UiComponents/StyledTabBar.qml
- [StyledTabButton.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/uicomponents/qml/Muse/UiComponents/StyledTabButton.qml
- [SearchField.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/uicomponents/qml/Muse/UiComponents/SearchField.qml
- [TextInputField.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/uicomponents/qml/Muse/UiComponents/TextInputField.qml
- [StyledDropdown.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/uicomponents/qml/Muse/UiComponents/StyledDropdown.qml
- [CheckBox.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/uicomponents/qml/Muse/UiComponents/CheckBox.qml
- [RoundedRadioButton.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/uicomponents/qml/Muse/UiComponents/RoundedRadioButton.qml
- [ToggleButton.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/uicomponents/qml/Muse/UiComponents/ToggleButton.qml
- [ExpandableBlank.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/uicomponents/qml/Muse/UiComponents/ExpandableBlank.qml
- [ExpandableBlankSection.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/uicomponents/qml/Muse/UiComponents/ExpandableBlankSection.qml
- [EditShortcutDialogContent.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/shortcuts/qml/Muse/Shortcuts/EditShortcutDialogContent.qml
- [StandardEditShortcutDialog.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/shortcuts/qml/Muse/Shortcuts/StandardEditShortcutDialog.qml
- [ShortcutsPage.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/shortcuts/qml/Muse/Shortcuts/ShortcutsPage.qml
- [ShortcutsTopPanel.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/shortcuts/qml/Muse/Shortcuts/internal/ShortcutsTopPanel.qml
- [ShortcutsList.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/shortcuts/qml/Muse/Shortcuts/internal/ShortcutsList.qml
- [editshortcutmodel.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/shortcuts/qml/Muse/Shortcuts/editshortcutmodel.cpp
- [extensionslistmodel.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/extensions/qml/Muse/Extensions/extensionslistmodel.cpp
- [AppReleaseInfoDialog.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/update/qml/Muse/Update/AppReleaseInfoDialog.qml
- [ReleaseNotesView.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/update/qml/Muse/Update/internal/ReleaseNotesView.qml
- [appupdateservice.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/update/internal/appupdateservice.cpp
- [PreferencesDialog.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/preferences/qml/MuseScore/Preferences/PreferencesDialog.qml
- [PreferencesPage.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/preferences/qml/MuseScore/Preferences/PreferencesPage.qml
- [PreferencesMenu.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/preferences/qml/MuseScore/Preferences/internal/PreferencesMenu.qml
- [PreferencesButtonsPanel.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/preferences/qml/MuseScore/Preferences/internal/PreferencesButtonsPanel.qml
- [BaseSection.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/preferences/qml/MuseScore/Preferences/internal/BaseSection.qml
- [ComboBoxWithTitle.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/preferences/qml/MuseScore/Preferences/internal/ComboBoxWithTitle.qml
- [ProgramStartSection.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/preferences/qml/MuseScore/Preferences/internal/ProgramStartSection.qml
- [AdvancedTopSection.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/preferences/qml/MuseScore/Preferences/internal/AdvancedTopSection.qml
- [GeneralPreferencesPage.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/preferences/qml/MuseScore/Preferences/GeneralPreferencesPage.qml
- [preferencesmodel.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/preferences/qml/MuseScore/Preferences/preferencesmodel.cpp
- [generalpreferencesmodel.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/preferences/qml/MuseScore/Preferences/generalpreferencesmodel.cpp
- [ScoresPage.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/project/qml/MuseScore/Project/ScoresPage.qml
- [ScoresGridView.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/project/qml/MuseScore/Project/ScoresGridView.qml
- [ScoresListView.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/project/qml/MuseScore/Project/ScoresListView.qml
- [RecentScoresView.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/project/qml/MuseScore/Project/internal/ScoresPage/RecentScoresView.qml
- [ScoreGridItem.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/project/qml/MuseScore/Project/internal/ScoresPage/ScoreGridItem.qml
- [ScoreListItem.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/project/qml/MuseScore/Project/internal/ScoresPage/ScoreListItem.qml
- [recentscoresmodel.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/project/qml/MuseScore/Project/internal/ScoresPage/recentscoresmodel.cpp
- [recentfilescontroller.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/project/internal/recentfilescontroller.cpp
- [NotationSwitchPanel.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/notationscene/qml/MuseScore/NotationScene/internal/NotationSwitchPanel.qml
- [NotationSwitchButton.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/notationscene/qml/MuseScore/NotationScene/internal/NotationSwitchButton.qml
- [editstyle.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/notationscene/widgets/editstyle.cpp
- [editstafftype.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/notationscene/widgets/editstafftype.cpp
- [notationactioncontroller.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/notationscene/internal/notationactioncontroller.cpp
- [InspectorForm.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/inspector/qml/MuseScore/Inspector/InspectorForm.qml
- [InspectorPropertyView.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/inspector/qml/MuseScore/Inspector/common/InspectorPropertyView.qml
- [PropertyResetButton.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/inspector/qml/MuseScore/Inspector/common/PropertyResetButton.qml
- [OffsetSection.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/inspector/qml/MuseScore/Inspector/common/OffsetSection.qml
- [Preferences handbook]: https://handbook.musescore.org/customization/preferences
- [Opening and saving]: https://handbook.musescore.org/file-management/opening-and-saving-scores
