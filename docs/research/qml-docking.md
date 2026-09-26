# Docking and workspace layouts in Qt Quick

Research ticket: altqx/hikari#16 (`qml-docking`). Status: draft in progress.

## Summary

_TBD_

## Implications for the decision

_TBD_

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

**Implications for a docking model.** The natural dock units are the video, audio, grid and edit panes. Today all four live inside one per-document `TabPanel`. A docking framework docks *app-level* panels. So the redesign has to decide one of two models. In the first, the docks are singletons that show the *active* document (the MuseScore/Audacity model: one score or project, panels around it). In the second, each document tab carries its own dock layout (closer to today's code, but no reviewed framework does this out of the box). Section 5 shows that MuseScore and Audacity both use the first model.

### 2. KDDockWidgets QtQuick frontend

_TBD_

### 3. Qt's own offerings

_TBD_

### 4. Hand-rolled SplitView layouts

_TBD_

### 5. How MuseScore 4 and Audacity 4 do docking and workspaces

_TBD_

## Open questions

_TBD_
