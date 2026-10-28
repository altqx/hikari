# MuseScore 4 docks and panels

This is a reference for how MuseScore Studio 4 builds its docked panels, toolbars and status bar on
KDDockWidgets, and which parts HikariSub could take.
Source code was read at tag **v4.7.5** (commit `3654226c2e99289916916953a98e585a3d3b315a`). This is
the same tag as [musescore-appearance.md](musescore-appearance.md), which gives the theme-role values
cited below. The handbook pages were fetched on 2026-10-05.
Short links such as [DockFrame.qml] point to the v4.7.5 sources in the list at the end. Line numbers
refer to that tag.

## 0. MuseScore's engine and how it differs from ours

- MuseScore **vendors its own fork of KDDockWidgets 1.x** in the source tree, at
  `src/framework/dockwindow/thirdparty/KDDockWidgets`. Its `CMakeLists.txt` declares version
  **1.4.95**, lines 87–89. The changelog heads "v1.5.0 (unreleased)". MuseScore's code uses the 1.x API
  (`DockWidgetBase`, `Frame`, `FrameworkWidgetFactory`, `MultiSplitter`), which 2.x renamed or
  removed. It is **not** KDDockWidgets 2.4.1. Ideas carry over to our engine. Code does not.
- MuseScore replaces almost all of the engine's chrome through `DockWidgetFactory`
  ([dockmodule.cpp] 43–96). It supplies its own drop-indicator overlay (`DropController`), its own
  separator (`DockSeparator`), title bar and tab bar, and its own QML for the dock widget, frame and
  floating window (`DockFrame.qml`, `DockFloatingWindow.qml`).
- Engine configuration ([dockmodule.cpp] 124–140):
  - `Flag_HideTitleBarWhenTabsVisible` and `Flag_TitleBarNoFloatButton` are on.
  - Floating windows use `Qt::Tool | Qt::NoDropShadowWindowHint | Qt::FramelessWindowHint`.
  - `InternalFlag_UseTransparentFloatingWindow` is set.
  - `setAbsoluteWidgetMinSize(10×10)` and `setSeparatorThickness(1)`.
- The engine's own title bar is reduced to zero height ([docktitlebar.cpp] 28–42), so it does not take
  the mouse. All chrome is drawn in `DockFrame.qml` from MuseScore's own properties.
- The main window is created with `MainWindowOption_None` ([dockwindow.cpp] 143–145). The score area
  is **not** the engine's central frame. It is an ordinary dock (`DockCentralView`) with
  `floatable = false` and `closable = false` ([dockcentralview.cpp] 26–31). It is added first, and side
  panels are placed relative to it ([dockwindow.cpp] 361–375, 408–428).

## 1. Panel chrome

### What sits at the top of a docked panel

`DockFrame.qml` chooses among three headers from the number of tabs, the panel's orientation and its
flags ([DockFrame.qml] 40–42). `DockFrameModel` sets `titleBarAllowed` only when the dock is a
**Panel** that is `floatable || closable` ([dockframemodel.cpp] 152–160). `isHorizontalPanel` means a
Panel whose `location` is Top or Bottom (153–155).

| Situation | Header shown | Example |
| --- | --- | --- |
| One vertical panel (left or right) in its frame | **Title bar**: bold title on the left, a "⋯" menu button on the right. No tab bar. | History alone on the right |
| Two or more panels tabbed in one frame | **Tab bar only**. The title bar is hidden ([DockFrame.qml] 40, 91). | Palettes, Layout and Properties on the left |
| A horizontal panel (top or bottom), **even when it is alone** | **Tab bar with a single tab** (`hasSingleTab`, 41). Tab dragging is turned off for that single tab (127). | Mixer, Piano keyboard, Timeline |
| Panel that is neither floatable nor closable | **No header** | Drumset tools ([NotationPage.qml] 512–536) |

So there is **exactly one header row**. A frame never shows a title bar and a tab bar together, and no
frame shows a duplicate title.

**The title bar** comes from [DockTitleBar.qml], default component 72–127. It is a `RowLayout`
with margins of 2 at top and bottom and 12 at left and right, and a spacing of 4 (95–100). It holds a
`StyledTextLabel` in `bodyBoldFont`, left-aligned and filling the width (102–109), and a `MenuButton`
(111–124). The whole bar shows a move (SizeAll) cursor (86–90). A panel can supply its own title-bar
component through `DockPanel.titleBar` ([dockpanelview.cpp] 252–260), but no Notation-page panel
does so at v4.7.5.

**The "⋯" menu** is `DockPanelMenuModel` ([dockpanelview.cpp] 44–154). Its items, in order:

1. The panel's own items, if the panel set a `contextMenuModel`, followed by a separator (59–62).
   - Palettes: "Single-click to open a palette", "Open only one palette at a time", "Allow
     reordering palettes" and "Expand/Collapse all palettes" ([palettespanelcontextmenumodel.cpp]
     67–165).
   - Layout: "Instrument ordering" and "Expand/Collapse all instruments"
     ([layoutpanelcontextmenumodel.cpp] 182–193).
   - Mixer: "Playback setup" and a **View** submenu of section toggles
     ([mixerpanelcontextmenumodel.cpp] 41–51, 169–170).
2. **Close** (`dock-set-open`, with the panel name and `false`) (64–66).
3. **Undock**, which reads **Dock** while the panel floats (`dock-toggle-floating`) (68–70, 119–122).
   The label updates live when the floating state changes (124–139).

The menu has no Move or Tab-with item. Close and Undock are added whatever the panel's
`closable`/`floatable` values are. A panel with both flags false has no header, so its menu cannot
open. There is **no separate close (×) button** and **no float button** on title bars or tabs.
`Flag_TitleBarNoFloatButton` is set, and MuseScore's QML draws neither button.

Double-clicking toggles floating. On a title bar, `DockTitleBar::doubleClicked` calls `onFloatClicked()`
([docktitlebar.cpp] 54–58). On a tab, `DockTabBar::doubleClicked` floats or docks the tab under the
pointer ([docktabbar.cpp] 89–94).

**Content headers are separate and functional.** A panel's content does not repeat its title. The
Palettes panel, for example, starts with `PalettesPanelHeader` (an "Add palettes" button and a search
field), 12 px from the sides ([PalettesPanel.qml] 72–104). Below a tab bar, the content starts **12 px
lower** (`stackLayout.anchors.topMargin`, [DockFrame.qml] 177–179). Below a title bar the gap is 0.

**A toolbar slot sits in the tab bar.** Right of the tabs, a `Loader` shows the current panel's
`toolbarComponent` ([DockTabBar.qml] 184–191). The Mixer and Percussion panels use it
([NotationPage.qml] 425–432, 568–574). This gives horizontal panels a compact command area on their
header row.

### What tabbed groups look like

Tabs come from [DockPanelTab.qml] (a `StyledTabButton`) inside the `ListView` of [DockTabBar.qml].

- The tab strip is filled with `backgroundSecondaryColor` ([DockTabBar.qml] 36).
- **Unselected tab:** `backgroundSecondaryColor`, `bodyFont`, a 1 px vertical separator on its right
  and a 1 px separator along its bottom ([DockPanelTab.qml] 81–86, 137–147).
- **Hovered tab:** `backgroundPrimaryColor` at `buttonOpacityHover` (0.5) (89–98).
- **Selected tab:** `backgroundPrimaryColor`, the same colour as the panel body, in `bodyBoldFont`. It
  has **no bottom separator**, so it merges with the content below it (100–108, 146). This is the
  classic "open folder tab" look.
- **There is no accent underline on dock tabs.** `StyledTabButton` itself draws a 2 px `accentColor`
  underline ([StyledTabButton.qml] 96–107), but `DockPanelTab` replaces the whole `background` and sets
  `states: []`, so the underline never appears. The 2 px accent underline belongs only to tab bars
  *inside* content, such as Preferences.
- **The "⋯" button appears only on the selected tab** (20×20 px, [DockPanelTab.qml] 63–79). The tab
  is laid out as [10 px | label | 6 px | ⋯ | 6 px + 1 px separator]. Without the button the right
  padding is 10 px (41–47).
- **Overflow:** when the tabs do not fit, the selected tab keeps its full width. The other tabs shrink
  in proportion and get a 20 px fade to 70 % of their background on the right
  ([DockTabBar.qml] 108–130, [DockPanelTab.qml] 112–135). The tabs never scroll or wrap.
- The tab area is a drag handle with a move cursor ([DockTabBar.qml] 161–182). A click that changes
  the tab is eaten so it cannot also open the "⋯" menu (176–181, [docktabbar.cpp] 55–87).

### What a floating panel looks like

[DockFloatingWindow.qml] is a **frameless tool window** (no OS title bar, [dockmodule.cpp] 130–132).
It has:

- an 8 px transparent margin that holds a `StyledRectangularShadow` (41–48, kept in step with
  `DOCK_WINDOW_SHADOW = 8`, [docktypes.h] 37–38);
- a `backgroundPrimaryColor` rectangle with a 1 px `strokeColor` border and a **3 px radius** (50–58);
- content clipped to the rounded corners when effects are allowed (60–70);
- `titleBarHeight: 0` (36). The window adds no title bar of its own.

The floating frame inside uses **the same `DockFrame.qml` rules**. A floated vertical panel keeps its
bold-title + "⋯" title bar, which is the drag handle. Its "⋯" menu now reads "Dock". A floated
horizontal panel keeps its single-tab header. Double-clicking the header docks the panel again.

## 2. Layout of the score page

### Screenshot, described in words

The handbook's [Toolbars and windows] page shows a light-theme capture (800×538) with five numbered
areas. Some labels are older than v4.7.5: the screenshot reads "Instruments", where 4.7 says
"Layout".

1. The menu bar.
2. The toolbar area, on two rows:
   - top row: Home / Score / Publish page tabs; "Parts" and "Mixer" in the centre; on the right a
     six-dot grip with the playback controls, the time and tempo readout, and undo/redo;
   - second row: the note-input toolbar, starting with a six-dot grip and ending with a gear icon.
3. The left panel column, with tabs reading "Palettes ⋯ | Properties | Instruments":
   - the selected Palettes tab is lighter than the grey tab strip and has its "⋯" next to the label;
   - no underline marks the selected tab;
   - below the tabs is a full-width "Add Palettes" button with a search icon, then a list of
     collapsed palettes (Clefs, Key signatures and so on).
4. The score, with a document tab ("*Daylight ×") above it.
5. A thin status bar holding "Workspace: Default", Concert pitch, Page view and zoom.

A 1 px line separates the panel column from the score.

### Default composition (from [NotationPage.qml] and [dockwindow.cpp])

- **Top-level toolbars** across the top, left to right ([dockwindow.cpp] 377–391, 430–482):
  - the window-wide Main toolbar (Home / Score / Publish, [WindowContent.qml] 62–96);
  - then the page's Notation toolbar (centre-aligned);
  - then Playback controls, Extensions toolbar and Undo/redo (right-aligned) ([NotationPage.qml]
    116–215).

  `alignTopLevelToolBars` pads the last left-aligned toolbar and the last centre toolbar so that the
  centre group is centred.
- **Note input toolbar** below them ([NotationPage.qml] 217–247).
- **Left column:** Palettes, Layout and Properties, **tabbed together**, with Palettes current
  ([dockwindow.cpp] 500–508). They are tabbed because they share `groupName: "VERTICAL_PANELS"` and
  location Left ([dockpageview.cpp] 195–205, [dockpanelview.cpp] 272–295).
- **Centre:** the notation view, as the non-closable, non-floatable central dock ([DockPage.qml] 48–59).
- **Bottom:** the status bar, as its own dock ([dockwindow.cpp] 370–372).

### The panels

| Panel (`title`) | Group | Default location | Visible by default | Size |
| --- | --- | --- | --- | --- |
| Palettes | VERTICAL_PANELS | Left | yes (current tab) | width **300**, min **300**, max **300**; height 10–7500 |
| Layout | VERTICAL_PANELS | Left (tab) | yes | same |
| Properties | VERTICAL_PANELS | Left (tab) | yes | same |
| Selection filter | VERTICAL_PANELS | Left | no | same |
| History | VERTICAL_PANELS | **Right** | no | same |
| Mixer | HORIZONTAL_PANELS | Bottom | no | height **368**, 100–520; width 10–7500 |
| Piano keyboard | HORIZONTAL_PANELS | Bottom | no | height 200, 100–520 |
| Timeline | HORIZONTAL_PANELS | Bottom | no | height 200, 100–520 |
| Percussion | HORIZONTAL_PANELS | Bottom | no | height 200, 100–520 |
| Drumset tools | none | Bottom | no | height fixed at **64**; not floatable or closable |

Sources: [NotationPage.qml] 93–99 (constants), 249–588 (panels).

- **Vertical panels cannot be resized horizontally.** They have `minimumWidth == maximumWidth == 300`.
  The splitter between the column and the score therefore shows no resize cursor
  (`showResizeCursor` compares the separator's min and max positions, [dockseparator.cpp] 107–112).
- Hidden panels open as tabs of a visible panel in the same group and location
  ([dockpageview.cpp] 218–242).
- When a horizontal panel opens, it can resize itself to fit its content (`resizeRequested`,
  `panelShown`, [NotationPage.qml] 435–444).

## 3. Toolbars and the status bar

**Toolbars are docks.** `DockToolBar` is a `DockToolBarView` with `DockType::ToolBar` ([DockToolBar.qml]
31; [docktoolbarview.cpp] 111–117).

- A toolbar is 36 px thick by default (`thickness`, [DockToolBar.qml] 40). The note input bar is 40 px
  thick horizontally and 76 px vertically ([NotationPage.qml] 231).
- A floatable toolbar has a **24 px grip** (`IconCode.TOOLBAR_GRIP`, the "six dots") with 2 px padding
  and a move cursor ([DockToolBar.qml] 78–104). The grip is the only drag handle.
- Double-clicking the grip toggles floating ([docktoolbarview.cpp] 201–229).

Which toolbars can move:

- **Main toolbar, Notation toolbar and Undo/redo** are `floatable: false` and `closable: false`. They
  are fixed.
- **Playback controls** and the **Extensions toolbar** can float, and can be docked again only to the
  right of the Notation toolbar (`dropDestinations`, [NotationPage.qml] 152–154, 179–182).
- **Note input** can dock at the top, bottom, left or right holders. It turns vertical when docked left
  or right ([dropcontroller.cpp] 179–207; [docktoolbarview.cpp] 166–192).

**The status bar is a dock but stays fixed.** `DockStatusBar` is 28 px high (minimum = maximum) with
`floatable = false` ([dockstatusbar.cpp] 26–36). It can be hidden through View → Toolbars → Status bar
(`toggle-statusbar`, [applicationuiactions.cpp] 217–224).

The handbook agrees: the note input and playback toolbars can be dragged by "the six dots", and the
playback toolbar "can only be redocked in its default position" ([Toolbars and windows]).

## 4. Behaviour

### Opening and closing

- The **View menu** ([appmenumodel.cpp] 292–329) lists checkable toggles:
  - Palettes, Master palette, Layout, Properties, Selection filter, History, Navigator, Braille,
    Timeline, Mixer, Piano keyboard, Percussion, Playback setup;
  - a **Toolbars** submenu (Playback controls, Note input, Status bar, 783–792);
  - **Workspaces**;
  - **Show**;
  - **"Restore the default layout"**.

  There are no Float or Dock commands in the menus. Floating is reached only from the panel's "⋯"
  menu, by dragging, or by double-clicking.
- Each toggle maps an action to a dock name ([applicationuiactions.cpp] 388–412). It goes through
  `dock-toggle` / `dock-set-open` ([dockwindowactionscontroller.cpp] 35–65). Its check state follows
  `docksOpenStatusChanged`.
- Default shortcuts ([shortcuts.xml]): F7 Layout (746–747), F8 Properties (925–926), F9 Palettes
  (751–752), F10 Mixer (756–757), F12 Timeline (766–767), Alt+F11 Braille, P Piano keyboard (915–916)
  and O Percussion.
- "Close" in the "⋯" menu calls `dock->close()`. Opening a panel uses `open()` or, if a same-group
  panel is open at its location, adds it as a tab there ([dockpageview.cpp] 218–242).

### Dragging and drop indicators

MuseScore turns off the engine's classic drop-indicator arrows. `DropController` replaces the
indicator overlay, and `posForIndicator` returns nothing ([dropcontroller.cpp] 153–156). While a drag is
over a valid destination, the destination frame draws a **highlight rectangle**: a 1 px `accentColor`
border over an `accentColor` fill at **30 % opacity** ([DockFrame.qml] 197–216). The handbook calls it
"a blue rectangle". Drops are strictly limited:

- **Panel onto panel.** Allowed only for open, docked panels with the **same `groupName`**
  (`isTabAllowed`). The hovered frame is cut into thirds along its long axis: the first third docks
  before it, the middle third adds a tab, and the last third docks after it
  ([dropcontroller.cpp] 309–356). The highlight covers half the frame, or all of it for a tab
  (358–401). For a vertical column this is the handbook's "top left / centre left / bottom left of the
  sidebar".
- **Panel beside the score.** Vertical panels list the central dock, Left and Right, with
  `dropDistance: 300` ([NotationPage.qml] 104–107). The drop counts only within 300 px of the score's
  edge ([dropcontroller.cpp] 62–87). The highlight is the dragged panel's width at that edge (373–383).
- **Holders.** Each page has invisible 36 px `DockingHolder` docks at the top, bottom, left and right,
  one set for toolbars and one for panels ([DockPage.qml] 61–113, [dockingholderview.cpp] 27–56). A
  holder opens and highlights when the pointer comes within **50 px** of that edge of the score
  (`MAX_DISTANCE_TO_HOLDER`, [dropcontroller.cpp] 46, 281–307). Horizontal panels may drop only into
  the top or bottom panel holders ([NotationPage.qml] 109–112).
- Anywhere else, the dragged panel stays floating.

### Reset layout

"Restore the default layout" (`dock-restore-default-layout`) asks for no confirmation. It calls
`resetToDefault()` on every dock, which restores the visibility the dock had at `componentComplete`
([dockbase.cpp] 543–546, 672). It then clears the saved state of **every page** and the window
geometry, and reloads the page ([dockwindow.cpp] 335–359).

### Workspaces and persistence

- **Layout is stored per page.** `DockWindow::windowState()` is `KDDockWidgets::LayoutSaver::serializeLayout()`,
  which is the engine's JSON ([dockwindow.cpp] 695–701). It is saved under the page's `objectName`
  ("Notation", "Home", "Publish") when the user leaves a page, when the workspace is about to change,
  and on quit ([dockwindow.cpp] 175–193, 215–231, 233–253, 608–615). The window geometry is saved
  under the key `"window"` (`WINDOW_GEOMETRY_KEY`, [uiconfiguration.cpp] 64, 722–744). MuseScore's own
  comment says that this, too, is a full layout dump, because the library cannot save geometry alone
  ([dockwindow.cpp] 189–192).
- **Both live in the current workspace.** `UiArrangement` keeps them in a JSON object under the
  workspace data key `ui_states`, next to `ui_settings` and `ui_toolconfigs` ([uiarrangement.cpp]
  102–116; [uitypes.h] 180–182).
  - A workspace is a **`.mws` zip** of named data files with a meta entry ([workspacefile.cpp] 60–132;
    [workspacemanager.cpp] 31).
  - The built-in workspace is "Default" (`Default.mws` in the app data folder). User workspaces live
    in `<user app data>/workspaces` ([workspaces.cfg]; [workspaceconfiguration.cpp] 53–75).
  - Editing a built-in workspace writes a user copy. "Reset" deletes that copy and goes back to the
    built-in file ([workspace.cpp] 63–74, 101–122).
  - A new workspace is a **clone of the current one** ([workspacemanager.cpp] 109–111, 155). At v4.7.5
    the New Workspace dialog only asks for a name ([NewWorkspaceDialog.qml]). The handbook's
    "choose the options it contains" step describes older builds.
  - Switching workspace saves the page state and re-applies the incoming one: `workspaceChanged` updates
    the JSON and notifies every key that changed, and `restorePageState` reloads the current page
    ([uiarrangement.cpp] 36–73; [dockwindow.cpp] 617–656).
  - The handbook: changes "are automatically saved" and cover "the content, docked/undocked status and
    positions of palettes, toolbars and assorted open panels". Score view settings are saved in the
    score instead ([Workspaces]).
- **Restoring a page** uses `RestoreOption_RelativeToMainWindow`. This skips the main window's
  geometry and scales floating windows relative to it ([dockwindow.cpp] 636–671; [LayoutSaver.cpp]
  64–75).
  - Docks missing from the saved JSON, such as panels added in a newer version, go to the matching
    same-group tab. Failing that, they go into that location's holder and stay closed
    (`handleUnknownDock`, [dockwindow.cpp] 526–557, 626–646).
  - The only corruption check is "a non-floatable dock is floating". If it fails, the default layout is
    restored ([dockwindow.cpp] 260–263, 673–688).
- On first start, or if the app quit in fullscreen, the window opens maximised ([dockwindow.cpp] 271–281).

### Screen and monitor changes

MuseScore's dock code has **no QScreen handling of its own**. A grep of `src/framework/dockwindow`
outside the vendored engine finds only the fullscreen check. It relies on the engine's restore path:

- `ScalingInfo` scales saved floating geometry by the main window's size ratio. It leaves window
  *positions* alone when the main window has changed screen ([LayoutSaver.cpp] 1036–1101).
- `FloatingWindow::ensureRectIsOnScreen` moves a saved window onto the nearest screen if it does not
  intersect any screen ([LayoutSaver.cpp] 338–359; [FloatingWindow.cpp] 639–680).

Nothing reacts while the app is running when a monitor is unplugged. The window manager's handling of
frameless `Qt::Tool` windows applies.

## 5. Theming

| Chrome element | Theme role (MuseScore) | Source |
| --- | --- | --- |
| Panel body and frame background | `backgroundPrimaryColor` | [DockFrame.qml] 45 |
| Tab strip background | `backgroundSecondaryColor` | [DockTabBar.qml] 36 |
| Unselected tab | `backgroundSecondaryColor` | [DockPanelTab.qml] 85 |
| Hovered tab | `backgroundPrimaryColor` × `buttonOpacityHover` (0.5) | [DockPanelTab.qml] 89–98 |
| Selected tab | `backgroundPrimaryColor`, bold text, no bottom line. **No accent underline.** | [DockPanelTab.qml] 100–108, 146 |
| Tab and strip separators (1 px) | `strokeColor` (`SeparatorLine`) | [SeparatorLine.qml] 31–33 |
| Dock splitters (1 px) | `strokeColor`. The hit area is 5 px wider on each side. | [DockSeparator.qml] 32, 38–44 |
| Drop highlight | `accentColor` 1 px border plus `accentColor` fill at 30 % | [DockFrame.qml] 197–216 |
| "⋯" menu button | `FlatButton`, transparent; becomes an `accentButton` while its menu is open | [MenuButton.qml] 46–48 |
| Title text, tab text, icons | `fontPrimaryColor` (default of `StyledTextLabel` and `FlatButton`) | see musescore-appearance.md §6 |
| Keyboard focus ring | `fontPrimaryColor`, `navCtrlBorderWidth` (2 px), drawn inside the tab | [NavigationFocusBorder.qml] 46–47; [DockPanelTab.qml] 149–154 |
| Floating window | `backgroundPrimaryColor` fill, `strokeColor` 1 px border, radius 3, 8 px shadow | [DockFloatingWindow.qml] 41–58 |

Values per theme (from [musescore-appearance.md](musescore-appearance.md) §3), Light / Dark:

- `backgroundPrimaryColor`: #F5F5F6 / #2D2D30
- `backgroundSecondaryColor`: #E6E9ED / #363638
- `strokeColor`: #CED1D4 / #1E1E1E
- `accentColor` (default): #70AFEA / #2093FE

In the dark theme the separators are *darker* than both surfaces. In the light theme they are a
mid-grey line.

## 6. Accessibility and keyboard

- **Fixed navigation keys** ([shortcuts.xml] 22–83; handbook [Accessibility]):
  - F6 / Shift+F6 (or backtick) moves between **sections**;
  - Tab / Shift+Tab moves between **panels** (control groups);
  - the arrow keys move between controls inside a panel;
  - Ctrl+Tab / Ctrl+Shift+Tab are registered too, but act exactly like Tab
    ([navigationcontroller.cpp] 277–284);
  - Enter, Return and Space activate.

  There is **no Ctrl+F6**. The handbook states that "UI navigation shortcuts are fixed".
- **Sections follow dock location.** The Notation page defines the sections NavigationTopPanel (3),
  NavigationLeftPanel (4), NavigationRightPanel (6) and NavigationBottomPanel (7). Each panel joins the
  section of its location ([NotationPage.qml] 54–87). Two seconds after a dock moves or changes
  visibility, `reorderSections` sorts the docks in each section by screen position (left to right,
  then top to bottom; floating docks last). It gives each dock a block of 1000 panel orders
  ([dockpageview.cpp] 244–342).
- **Header navigation.** In each frame the header's controls form one `NavigationPanel` named
  `<frame>PanelTabs`. It is horizontal by default ([DockFrame.qml] 70–82; [navigationpanel.h] 110).
  - Tabs have orders 0, 2, 4…. The selected tab's "⋯" button has the tab's order + 1
    ([DockTabBar.qml] 139–141; [DockPanelTab.qml] 72–73).
  - **To reach the "⋯" menu by keyboard:** F6 to the section, Tab to the header, **Right arrow** from
    the selected tab, then Enter or Space.
  - In a title bar the "⋯" button is the header's only control (navigation order 1).
  - No shortcut opens a panel's menu directly. Shift+F10 / Menu is `notation-context-menu`, which opens
    the score's context menu, not the panel's ([shortcuts.xml] 1184–1188).
- **Accessible names.**
  - Each tab has role **RadioButton**. Its name is the tab title and its `checked` state is "selected"
    ([StyledTabButton.qml] 67–82, `navigation.name: text` in [DockTabBar.qml] 139).
  - The "⋯" button is named **"Menu"** ([MenuButton.qml] 50), without the panel's name.
  - The header `NavigationPanel` has role Panel ([navigationpanel.cpp] 33–37) and no accessible name
    set in DockFrame.
  - Title-bar text has no special role.
- When a tab becomes current, the frame switches its navigation section to that panel's section
  ([dockframemodel.cpp] 192–205).

## 7. Sizes

Sizes are logical pixels at the default body font of 12 px. `bodyBoldFont` is DemiBold at the same
size ([themeapi.cpp] 400–418; [uiconfiguration.cpp] 555–576).

| Element | Size | Font and roles | Source |
| --- | --- | --- | --- |
| Tab bar height | **35** (34 tab + 1 bottom separator) | strip `backgroundSecondaryColor` | [DockFrame.qml] 124; [DockPanelTab.qml] 37–38 |
| Tab padding | left 10; right 10, or 6 when "⋯" shows; +1 for the separator; label-to-button gap 6 | `bodyFont`; selected tab `bodyBoldFont` | [DockPanelTab.qml] 41–47, 52–58 |
| "⋯" button on a tab | 20 × 20 | icon font = body + 4 = 16 px | [DockPanelTab.qml] 63–67; appearance doc §6 |
| Gap from tab bar to content | 12 | none | [DockFrame.qml] 179 |
| Title bar | margins 2 / 12 / 2 / 12, spacing 4. Height is the `RowLayout`'s implicit height + 4. | `bodyBoldFont` | [DockTitleBar.qml] 80, 92–109 |
| "⋯" button in a title bar | declared 20 × 20, but inside a `RowLayout`. Layouts size from implicit height, which is `defaultButtonSize` (≥ 30), so the title bar comes out **about 34 px**. This is derived, not measured. | none | [DockTitleBar.qml] 111–124; [FlatButton.qml] 142–143; [themeapi.cpp] 450–460 |
| Dock splitter | **1 px** visible; 11 px hit area | `strokeColor` | [dockmodule.cpp] 140; [DockSeparator.qml] 38–40 |
| Cut-off tab fade | 20 px wide | 70 % of the tab colour | [DockPanelTab.qml] 112–135 |
| Floating window | 8 px shadow margin, 1 px border, radius 3 | `strokeColor` | [DockFloatingWindow.qml] 41–58 |
| Drop highlight | 1 px border, 30 % fill | `accentColor` | [DockFrame.qml] 197–216 |
| Toolbar | 36 thick by default; grip 24 + 2 × 2 padding | none | [DockToolBar.qml] 36–40, 78–95 |
| Status bar | 28 fixed | none | [dockstatusbar.cpp] 29–33 |
| Docking holder | 36 | none | [dockingholderview.cpp] 37–49 |
| Vertical panel width | 300 fixed | none | [NotationPage.qml] 93, 258–260 |
| Horizontal panel height | 100–520 (Mixer starts at 368, the others at 200) | none | [NotationPage.qml] 95–96, 401 |
| Absolute dock minimum | 10 × 10 | none | [dockmodule.cpp] 139 |

## 8. What HikariSub would take

Our panels are **Video, Audio, Line editor (`Editor`), Grid, Reference tray (`Reference`)**, and the
tools **Timing, Search** (Styles, History and Automation come later). Our engine is KDDockWidgets
**2.4.1**, QtQuick, behind `ui/docking.cpp`. Today our chrome is `DockTitleBar.qml` (engine-style image
buttons) and `DockTabBar.qml` (Float ↗ and Close × on every tab). Each dock body is a `Panel`
(`Main.qml` 1454–1479) that **draws its own bold heading** under the engine's title bar.

1. **One header row, no duplicate title.** Drop the `Panel` heading wherever the dock shows a title
   or tab, which today means Video, Audio, Line editor, Timing and Search. The title bar or tab already
   names the panel. Keep a content header only where it says something new.
   - The Grid's "Editing: *document*" names the editing target, not the panel. That matches MuseScore,
     whose content headers are functional (search, "Add palettes").
   - The Line editor's "Line editor: *document*" belongs to the same family. Show the document name
     either in the tab text or in that content header, not both.
   - `Panel` keeps its `Accessible.name`, so screen-reader naming does not depend on the visible
     heading.
2. **Header rules.** A lone panel gets a title bar (bold title + "⋯"), a tabbed group gets a tab bar
   only, and a wide horizontal panel gets a single-tab bar. In 2.4.1 the engine pieces are
   `Flag_HideTitleBarWhenTabsVisible`, plus `Flag_AlwaysShowTabs` or our own QML condition. We would
   apply the single-tab rule to the **Grid, Reference tray and Search**, our full-width horizontal
   panels. Their tab rows could then carry a **toolbar slot** right of the tab, as MuseScore's
   `toolbarComponent` does. The Grid's Insert / Filtering / Hide-columns commands and the Reference
   tray's navigation fit there without adding a row.
3. **Replace per-tab ↗/× and the image-button title bar with a "⋯" menu.**
   - Show the menu only on the selected tab, and on the title bar.
   - Items: the panel's own items, a separator, **Close**, and **Float / Dock** (the label follows the
     state).
   - Unlike MuseScore, also add **Move…** for our keyboard placement dialog, and **disable** Close or
     Float when a panel's flags forbid them. MuseScore shows them anyway.
   - Keep double-click on a header to float or dock.
   - Name the button "*Panel* options" rather than MuseScore's bare "Menu". docs/qt/docking.md asks
     tabs to expose close and float, and named menu items do that.
   - Keep `View → Panels → Show/Hide/Float/Dock` (`Main.qml` 1034–1060). MuseScore has no menu path to
     floating, and ours is better for keyboard users.
4. **Tab look, mapped to Compact Studio roles** (visual-language.md):
   - tab strip and unselected tab = `raised`, or `field` to match D's panel-header colour;
   - selected tab = `panel`, the body colour, bold text, no bottom line;
   - hover = `panel` at reduced opacity;
   - separators = 1 px `line`;
   - focus ring = 2 px `focus`, drawn inside the tab.

   MuseScore uses **no accent underline** on dock tabs. Adding one would be our own choice; the
   selected/merged surface already marks the selection.
5. **Sizes.** Use a tab header of about 34 + 1 px. That matches our 32-minimum panel header and
   21-minimum header actions, so a 20 px "⋯" fits. Leave a gap of about 8–12 px between header and
   content. Use **1 px splitters with a wider invisible hit area**: `Config::setSeparatorThickness(1)`
   plus our own `separatorFilename()` QML, in `line`. Use text-derived heights rather than fixed
   ones, as Compact Studio requires.
6. **Do not copy MuseScore's fixed 300 px side panels or its central dock.**
   - Our Editing preset is proportional (44 / 56 upper split, full-width Grid), and every core panel
     must resize.
   - We have no permanent centre. Video, Audio, Editor and Grid are all closable docks, so a
     `DockCentralView`-style non-closable dock does not fit our contract.
7. **Drop feedback.** MuseScore's accent rectangle (1 px border, 30 % fill, half-frame or tab
   highlight) is the look to aim for. In 2.4.1 there is no public factory for a custom overlay like
   `DropController`. The choice is `ViewFactory::s_dropIndicatorType` (`Classic`, `Segmented` or
   `None`; [KDDW 2.4.1 KDDockWidgets.h] 262–267). The native gate should check `Segmented` with
   `accent` styling first. MuseScore's group restriction (only same-group panels tab together) is
   optional for us: docs/qt/docking.md allows core panels and tools to tab freely.
8. **Floating groups.**
   - Floating panels keep the same header, so the title bar or tab row is the drag handle.
   - The frameless `Qt::Tool` window with an 8 px drawn shadow depends on a compositor that supports
     translucency, and it conflicts with our Wayland note (separate move/dock bars, no absolute
     placement).
   - Keep native-decorated floating windows on Wayland, and treat frameless windows as a Windows/X11
     option to qualify.
9. **Persistence and screens.**
   - MuseScore saves raw `serializeLayout()` JSON per page inside the workspace zip. It restores relative
     to the main window, places unknown docks in their default group and leaves them closed, and resets
     every page without confirmation. That is weaker than our accepted contract (schema envelope,
     atomic writes, backup, recovery notice), so we take only the **unknown-dock reconciliation rule**.
   - MuseScore has no live monitor-removal handling. Our `keepFloatingPanelsOnScreen()` already goes
     further.
   - MuseScore's per-workspace layouts (a named `.mws` cloned from the current one) correspond to our
     presets plus the custom arrangement, which is already accepted.
10. **Keyboard.**
    - MuseScore's section, panel and control model (F6, Tab, arrows) matches our F6 traversal. Its
      header model is the one to copy: the tabs and the "⋯" button form one horizontal group, and the
      arrow keys reach the menu.
    - Give each section a reading order sorted by position after moves, as `reorderSections` does, so
      that F6 follows the visible layout, including floating groups, which come last.
    - Expose tabs as page tabs with selected state. Ours already are, which is better than
      RadioButton.
11. **Toolbars and status bar.** Nothing to take. Our Classic shell keeps menus, panel-local controls
    and the bottom status outside the dock system. The visual tool rail lives inside Video. MuseScore's
    grip-and-float toolbars would add movable chrome that nobody has asked for.

## Sources

Code links are pinned to tag `v4.7.5` (commit `3654226c2e99289916916953a98e585a3d3b315a`).

- [dockmodule.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/dockwindow/dockmodule.cpp
- [docktypes.h]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/dockwindow/docktypes.h
- [dockbase.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/dockwindow/internal/dockbase.cpp
- [dockseparator.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/dockwindow/internal/dockseparator.cpp
- [docktabbar.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/dockwindow/internal/docktabbar.cpp
- [docktitlebar.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/dockwindow/internal/docktitlebar.cpp
- [dropcontroller.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/dockwindow/internal/dropcontroller.cpp
- [dockwindowactionscontroller.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/dockwindow/internal/dockwindowactionscontroller.cpp
- [DockFrame.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/dockwindow/qml/Muse/Dock/DockFrame.qml
- [DockTabBar.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/dockwindow/qml/Muse/Dock/DockTabBar.qml
- [DockPanelTab.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/dockwindow/qml/Muse/Dock/DockPanelTab.qml
- [DockTitleBar.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/dockwindow/qml/Muse/Dock/DockTitleBar.qml
- [DockFloatingWindow.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/dockwindow/qml/Muse/Dock/DockFloatingWindow.qml
- [DockSeparator.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/dockwindow/qml/Muse/Dock/DockSeparator.qml
- [DockToolBar.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/dockwindow/qml/Muse/Dock/DockToolBar.qml
- [DockPage.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/dockwindow/qml/Muse/Dock/DockPage.qml
- [dockframemodel.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/dockwindow/qml/Muse/Dock/dockframemodel.cpp
- [dockpanelview.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/dockwindow/qml/Muse/Dock/dockpanelview.cpp
- [dockpageview.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/dockwindow/qml/Muse/Dock/dockpageview.cpp
- [dockwindow.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/dockwindow/qml/Muse/Dock/dockwindow.cpp
- [dockcentralview.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/dockwindow/qml/Muse/Dock/dockcentralview.cpp
- [dockstatusbar.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/dockwindow/qml/Muse/Dock/dockstatusbar.cpp
- [dockingholderview.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/dockwindow/qml/Muse/Dock/dockingholderview.cpp
- [docktoolbarview.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/dockwindow/qml/Muse/Dock/docktoolbarview.cpp
- [LayoutSaver.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/dockwindow/thirdparty/KDDockWidgets/src/LayoutSaver.cpp
- [FloatingWindow.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/dockwindow/thirdparty/KDDockWidgets/src/private/FloatingWindow.cpp
- Vendored engine version: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/dockwindow/thirdparty/KDDockWidgets/CMakeLists.txt
- [NotationPage.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/appshell/qml/MuseScore/AppShell/NotationPage/NotationPage.qml
- [WindowContent.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/appshell/qml/MuseScore/AppShell/WindowContent.qml
- [appmenumodel.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/appshell/qml/MuseScore/AppShell/appmenumodel.cpp
- [applicationuiactions.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/appshell/internal/applicationuiactions.cpp
- [shortcuts.xml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/app/configs/data/shortcuts.xml
- [PalettesPanel.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/palette/qml/MuseScore/Palette/PalettesPanel.qml
- [palettespanelcontextmenumodel.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/palette/qml/MuseScore/Palette/internal/palettespanelcontextmenumodel.cpp
- [layoutpanelcontextmenumodel.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/instrumentsscene/qml/MuseScore/InstrumentsScene/internal/layoutpanelcontextmenumodel.cpp
- [mixerpanelcontextmenumodel.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/playback/qml/MuseScore/Playback/mixerpanelcontextmenumodel.cpp
- [StyledTabButton.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/uicomponents/qml/Muse/UiComponents/StyledTabButton.qml
- [MenuButton.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/uicomponents/qml/Muse/UiComponents/MenuButton.qml
- [SeparatorLine.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/uicomponents/qml/Muse/UiComponents/SeparatorLine.qml
- [NavigationFocusBorder.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/uicomponents/qml/Muse/UiComponents/NavigationFocusBorder.qml
- [FlatButton.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/uicomponents/qml/Muse/UiComponents/FlatButton.qml
- [themeapi.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/ui/api/themeapi.cpp
- [uiconfiguration.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/ui/internal/uiconfiguration.cpp
- [uiarrangement.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/ui/internal/uiarrangement.cpp
- [uitypes.h]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/ui/uitypes.h
- [navigationcontroller.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/ui/internal/navigationcontroller.cpp
- [navigationpanel.h]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/ui/qml/Muse/Ui/navigationpanel.h
- [navigationpanel.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/ui/qml/Muse/Ui/navigationpanel.cpp
- [workspacefile.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/workspace/internal/workspacefile.cpp
- [workspacemanager.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/workspace/internal/workspacemanager.cpp
- [workspaceconfiguration.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/workspace/internal/workspaceconfiguration.cpp
- [workspace.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/workspace/internal/workspace.cpp
- [NewWorkspaceDialog.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/workspace/qml/Muse/Workspace/NewWorkspaceDialog.qml
- [workspaces.cfg]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/app/configs/workspaces.cfg
- [Toolbars and windows] (handbook): https://handbook.musescore.org/customization/toolbars-and-windows
- [Workspaces] (handbook): https://handbook.musescore.org/customization/workspaces
- [Accessibility] (handbook): https://handbook.musescore.org/navigation/accessibility
- [KDDW 2.4.1 KDDockWidgets.h]: https://github.com/KDAB/KDDockWidgets/blob/c1d28d25ef5ba077915bcb2b6fa9e14df2a361f8/src/KDDockWidgets.h
