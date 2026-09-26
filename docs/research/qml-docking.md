# Docking and workspace layouts in Qt Quick

Research ticket: altqx/hikari#16 (`qml-docking`). Completed desk research, 2026-09-27. Repository inventory is at `20d647c4`; upstream documentation is current on the access date. No docking runtime or accessibility benchmark was run.

## Summary

KDDockWidgets 2.x is a credible Qt Quick docking candidate, while nested Qt Quick `SplitView`s are sufficient for a deliberately constrained workspace. Qt's own `QDockWidget` is a Widgets API, not a native QML docking solution. The choice depends on whether users actually need arbitrary floating/tabbed panels, and on the document-to-panel model. Those questions should be answered with QML/HTML prototype tickets before adopting a framework.

## Implications for the decision

Recommendation, not an architecture decision: prototype a predictable workspace first, with panel visibility, saved split sizes and named presets. In parallel, use a small KDDockWidgets QtQuick spike to test detach/redock, focus, mixed-DPI monitors and Wayland. Keep workspace layout separate from document/session data and application preferences. The user has selected Figma Starter, with agent-built QML/HTML mockups for reaction and Figma only for occasional handoff; no paid Figma workflow is a prerequisite.

## Detailed findings

### 1. Current HikariSub layout model

Today's layout is fixed and hand-built. Nothing is dockable. The only rearrangement a user can make is to drag splitters, move the toolbar to another edge, hide the editor, and split the document area in two.

**Frame.** `HikariSubFrame` owns a `Notebook* Tabs`, a `HikariToolbar`, a `HikariStatusBar` and a custom `MenuBar` (`HikariSub/HikariSubFrame.h:51`, `:67-70`; created at `HikariSub/HikariSubFrame.cpp:147-150`). There is no sizer. `OnSize` positions the menu bar, toolbar, notebook and status bar with absolute coordinates (`HikariSub/HikariSubFrame.cpp:1660-1697`). The toolbar can sit on any of four edges, selected by the `TOOLBAR_ALIGNMENT` option (0 = left, 1 = top, 2 = right, 3 = bottom) (`HikariSub/HikariSubFrame.cpp:1663-1693`).

**Documents are tabs.** `Notebook` is a custom-painted `wxWindow`, not `wxNotebook`. It holds `std::vector<TabPanel*> Pages`, one per open subtitle/video document, and draws its own tab strip with scroll arrows and a "new tab" button (`HikariSub/Notebook.h:30-130`, `Pages` at `:119`). It can show **two documents side by side**: `Notebook::Split(page)` toggles `split`, puts the active page on the left and `splititer` on the right at `w / 2`, and a draggable `splitline` separates them (`HikariSub/Notebook.cpp:1061-1083`; dragging clamps with `MID(200, x, w - 200)` at `:450`). That is the only multi-view feature. It is limited to two panes and one orientation.

**Each tab is a fixed composite.** `TabPanel` owns `SubsGrid* grid`, `EditBox* edit`, `VideoBox* video` and `ShiftTimes* shiftTimes` (`HikariSub/TabPanel.h:39-42`). The constructor builds three nested box sizers (`HikariSub/TabPanel.cpp:38-86`):

```
MainSizer (vertical)
├── VideoEditboxSizer (horizontal): [ video | edit ]
├── HikariWindowResizer (a drag bar that sets the video height)
└── GridShiftTimesSizer (horizontal): [ grid | shiftTimes ]
```

**Audio is inside the edit box, not in the tab.** `EditBox::LoadAudio` creates the `AudioBox` lazily. It prepends the box and a second `HikariWindowResizer` to the edit box's own `BoxSizer1`, and it persists the height as `AUDIO_BOX_HEIGHT` (`HikariSub/EditBox.cpp:2121-2168`). `CloseAudio` removes them again (`:2170-2182`). So audio can only appear above the text editor, in the top-right quadrant.

**"Hide editor" is the one mode switch.** `HikariSubFrame::HideEditor` hides the grid, edit box and resizer. It moves `video` out of `VideoEditboxSizer` into `MainSizer` with proportion 1, so video fills the tab. Showing the editor again reverses the move (`HikariSub/HikariSubFrame.cpp:2009-2080`). This state is per tab (`TabPanel::editor`). It is also a global option `EDITOR_ON` (`HikariSub/HikariSubFrame.cpp:427`, `:530`, `:2090`).

**Floating windows.** Fullscreen video is a separate top-level `Fullscreen` window, placed on a monitor chosen by `GetMonitorRect1` (`HikariSub/VideoBox.cpp:783-795`). The Style Manager (`stylestore.cpp`) is a separate window whose position is saved as `STYLE_MANAGER_POSITION`. It has a "detached edit window" option, `STYLE_MANAGER_DETACH_EDIT_WINDOW` (`HikariSub/hikarisubApp.cpp:392`, `:469`; `HikariSub/stylestore.cpp:45`). Find/Replace is a dialog that uses `HikariTabBar`, a small custom-painted tab strip. Nothing else uses it (`HikariSub/HikariTabBar.h:37-70`; used at `HikariSub/FindReplaceDialog.cpp:426`).

**What is persisted.** Layout state is scattered across flat options in `Config.h`. They include `WINDOW_POSITION`, `WINDOW_SIZE`, `WINDOW_MAXIMIZED`, `MONITOR_POSITION` and `MONITOR_SIZE`, which restore the frame onto the monitor it was on (`HikariSub/HikariSubFrame.cpp:424-425`, `:517-530`; `HikariSub/HikariFrame.cpp:436-477`). They also include `VIDEO_WINDOW_SIZE`, `AUDIO_BOX_HEIGHT`, `TOOLBAR_ALIGNMENT`, `EDITOR_ON` and `SHIFT_TIMES_ON` (`HikariSub/Config.h:103`, `:146`, `:183`, `:232`, `:243-251`). The session file `LastSession.txt` stores each tab's files and view state: video path and position, subtitle path, active line, scroll, `Editor` flag, audio and keyframes (`HikariSub/Notebook.cpp:1323-1361`). There is no "workspace" or named-layout concept.

**Implications for a docking model (design inference).** The video, audio, grid and edit panes are plausible dock units, but today they belong to individual documents. Prototypes must distinguish active-document panels, document-owned panels, and pinned comparison views. Docking libraries do not determine this product model. KDDockWidgets supports multiple main windows and affinities, so the earlier draft's assertion that docking can only be app-level was too strong. A per-document workspace is possible application engineering, not a proven built-in feature. [KDDockWidgets features](https://docs.kdab.com/kddockwidgets-manual/latest/)

### 2. KDDockWidgets QtQuick frontend

Upstream describes QtWidgets and QtQuick as complete frontends over the same C++ docking core. The latest release returned by GitHub during this research is `v2.4.1`. Its QtQuick guide requires C++17 and Qt >= 6.2.1, supports a QtQuick-only build via `KDDockWidgets_FRONTENDS=qtquick`, and records cross-window QtGraphicalEffects problems and occasional Nvidia/X11 docking lag. This is evidence of maintained capability, not proof that our video surface can move between windows without rebuilding GPU resources. [Architecture](https://docs.kdab.com/kddockwidgets-manual/latest/architecture_and_concepts.html), [QtQuick guide](https://github.com/KDAB/KDDockWidgets/blob/main/README-QtQuick.md), [release](https://github.com/KDAB/KDDockWidgets/releases/tag/v2.4.1)

Tabbed groups, nested floating groups, multiple windows, affinities and partial layout restoration are documented features. `LayoutSaver` provides serialization/restoration; stable dock identifiers and reconstruction of missing panels remain application responsibilities. Save a versioned envelope around the library state, with a recoverable default layout and validation when panels disappear. [Features](https://docs.kdab.com/kddockwidgets-manual/latest/), [LayoutSaver API source](https://github.com/KDAB/KDDockWidgets/blob/main/src/LayoutSaver.h)

The published licensing choices are GPL-2.0 or GPL-3.0, with commercial licensing available separately. HikariSub's GPLv3 distribution can use the GPLv3 option; preserve the exact dependency's license and notices. This does not require a commercial license merely because we use QtQuick. [KDAB licensing](https://docs.kdab.com/kddockwidgets-manual/latest/licensing.html)

Wayland is supported, with documented differences: floating windows have separate native move and client docking title bars; saved layouts cannot restore their exact desktop positions. Test KWin and a GNOME compositor separately; KDAB's stated testing covers KWin and Weston. Windows/X11 floating placement should still be validated after monitor removal and scale changes. Never promise identical multi-monitor restoration across platforms. [Wayland limitations](https://docs.kdab.com/kddockwidgets-manual/latest/qpa-wayland.html)

Keyboard access requires application work: focus commands for each panel, cycling panel/tab focus, menus for show/hide/float/redock, and recovery after closing a floating window. The docs examined do not establish a complete keyboard-only docking or screen-reader guarantee. Use accessible buttons and named tab controls rather than treating drag affordances as sufficient. Muse's extra navigation layer below is evidence that the dock engine alone is not the whole accessibility solution.

### 3. Qt's own offerings

`QDockWidget` provides dock/floating windows within `QMainWindow`; `QMainWindow` offers tabification, nested docks and versioned `saveState`/`restoreState`. They operate on QWidgets. A Widgets shell hosting Quick content is possible, but it changes the shell architecture and needs separate focus, scene-graph and window-composition testing. It is not a drop-in `ApplicationWindow` docking API. The official Quick controls reviewed provide split containers and tabs, not the same full docking engine. [QDockWidget](https://doc.qt.io/qt-6/qdockwidget.html), [QMainWindow state](https://doc.qt.io/qt-6/qmainwindow.html#saveState), [Quick container controls](https://doc.qt.io/qt-6/qtquickcontrols-containers.html)

### 4. Hand-rolled SplitView layouts

`SplitView` lays out horizontal/vertical panes with min/max/preferred sizes; its `saveState` serializes preferred sizes. Nested split views plus `TabBar`/`StackLayout` can express a small set of layouts. Floating windows, drag targets, tab transfers, panel identity, screen recovery and a layout-tree serializer would be ours to build. Its saved size bytes do not constitute a complete workspace or session. [SplitView](https://doc.qt.io/qt-6/qml-qtquick-controls-splitview.html), [StackLayout](https://doc.qt.io/qt-6/qml-qtquick-layouts-stacklayout.html)

Inference: this has the smallest dependency surface for fixed presets, but becomes a costly reimplementation once arbitrary docking is required. Provide explicit keyboard resize commands rather than relying on draggable splitter handles. A constrained layout can deliberately keep all editing panes in one window while allowing a separate fullscreen preview; that is a product option for reaction, not a settled limitation.

| Requirement | KDDockWidgets Quick | Qt Widgets shell | Constrained Quick splits |
|---|---|---|---|
| Tabbed/floating panels | Built-in engine | QDockWidget/QMainWindow | App implementation |
| Persistence | LayoutSaver plus app schema | saveState plus geometry/session | Split sizes plus app layout tree |
| Multi-monitor | Supported windows; platform caveats | Supported windows; platform caveats | Explicit Window management |
| Keyboard/accessibility | App focus/actions plus framework validation | Widget defaults plus app actions | App focus/actions/split controls |
| Best initial use | Arbitrary dock workflows | If choosing a Widgets shell | Presets and layout reaction prototypes |

### 5. How MuseScore 4 and Audacity 4 do docking and workspaces

The Muse framework inspected at `1d8529e782618391bf0187a2e34c85f499b0680e` contains both `dockwindow` and `dockwindow_v2`, a vendored KDDockWidgets tree, and application-specific QML wrappers. Do not infer that a given released MuseScore or Audacity binary uses upstream KDDockWidgets 2.4.1 from those names. The inspected `DockWindow` saves page state before workspace changes and delegates restoration to `KDDockWidgets::LayoutSaver`; `DockPanelTab.qml` adds Muse navigation and focus borders. [DockWindow](https://github.com/musescore/muse_framework/blob/1d8529e782618391bf0187a2e34c85f499b0680e/framework/dockwindow/qml/Muse/Dock/dockwindow.cpp), [panel tab](https://github.com/musescore/muse_framework/blob/1d8529e782618391bf0187a2e34c85f499b0680e/framework/dockwindow/qml/Muse/Dock/DockPanelTab.qml)

Muse's workspace data provider delegates named raw-data values to the current workspace and notifies subscribers on switching. This is an extensible persistence service, separate from the docking engine. The MuseScore handbook says workspaces store palette contents, toolbars, panels, docking and positions, automatically saving changes; it explicitly excludes score-specific zoom/view properties. It does not establish that all application preferences are workspace-local, so do not promise that. [Data provider](https://github.com/musescore/muse_framework/blob/1d8529e782618391bf0187a2e34c85f499b0680e/framework/workspace/internal/workspacesdataprovider.cpp), [handbook](https://handbook.musescore.org/customization/workspaces)

Audacity development source at `36146d838c934ae429cb164e8eb3d39af75a65ff` imports `Muse.Dock`; its `ProjectPage.qml` is a `DockPage` containing toolbar and panel declarations, including tracks, playback meters and history. `.gitmodules` points `muse` to the Muse framework; that checkout pins framework commit `b1b09fa0343a40f6c5a31e9663ed3ed1ab796c70`. This substantiates framework reuse and project-scoped presentation, not release maturity, parity or benchmark claims. [ProjectPage](https://github.com/audacity/audacity/blob/36146d838c934ae429cb164e8eb3d39af75a65ff/src/appshell/qml/Audacity/AppShell/ProjectPage/ProjectPage.qml), [submodules](https://github.com/audacity/audacity/blob/36146d838c934ae429cb164e8eb3d39af75a65ff/.gitmodules)

## Open questions

The research question is answered at the capability/tradeoff level. These are downstream decisions or prototype validations, not missing factual conclusions:

1. Compare active-document panels with document-owned layouts and a pinned reference view, using two open subtitle documents. User reactions determine the model.
2. Prove keyboard-only show/hide/float/redock, focus return, tab movement and accessible panel naming in the selected shell.
3. Exercise video reparenting, device-resource recreation, mixed DPI, screen removal, Wayland restoration and a corrupted saved layout. No measured performance or portability claim is made here.
4. Define exactly what a named workspace stores: layout and toolbar visibility are candidates; media positions, active lines and files belong to document/session state; preferences need explicit scope.
