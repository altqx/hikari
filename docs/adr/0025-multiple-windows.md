---
status: accepted
amends: 0020
---

# Let a Document tab move into a window of its own

On 2026-11-02 altqx decided, in the wave-8 grilling session ([#410](https://github.com/altqx/hikari/issues/410), card [#601](https://github.com/altqx/hikari/issues/601)), that HikariSub can have several top-level windows, like a browser's. A Document tab dragged out of the tab bar opens in a window of its own, a window can hold several tabs, and tabs move between windows and back. This supersedes the [docking contract](../docking.md)'s rule that the shell has one main window and implies no multi-main-window model, and the WS2 rule that a docked place is valid only in that one main window. [ADR 0020](0020-per-document-tab-layouts.md) still holds: each tab keeps its own arrangement, now inside whichever window shows it. [ADR 0017](0017-owned-native-docking.md) is unchanged; KDDockWidgets stays the engine behind the WorkspaceLayout adapter.

## Every window is a full shell

Each window has everything the first one has: the menus, the title bar with the layout switcher, the mode switcher, Home, the Document tab bar, its own docking area with every panel, and its own floating panel groups. No window is lighter than another, so nothing is missing in a second window. The windows are equal; the first is not a main window that the others depend on.

A Document tab belongs to one window at a time. Its arrangement (ADR 0020) moves with it, so it looks the same in its new window. Each window has its own current tab. The **editing target** is the current tab of the window that was active last: menus, hotkeys, the focus and the tools act on that window only. Clicking or focusing another window makes its current tab the editing target, as selecting a tab does today. A tool panel acts on its own window's tab.

Panels don't move between windows. Each window docks and floats its own panels, and a panel dragged over another window shows no drop highlight there. The same panel can be open in two windows at once, each on its own window's tab.

## Moving tabs

- **Drag out:** dragging a tab off the tab bar and releasing it outside every HikariSub window opens a new window holding that tab. On Windows and X11 the window opens under the pointer, at the size of the window it came from. On Wayland the compositor places it.
- **Drag between windows:** releasing the tab over another window's tab bar moves it there, at the place it was dropped. Releasing it back over its own tab bar reorders it, as today.
- **Keyboard:** the tab's context menu (the Menu key or Shift+F10 on a focused tab) and two hotkey commands give the same moves without a mouse: **Move tab to new window** and **Move tab to window ▸**, which lists the other windows by their current tab's title. Both are in the Document tab bar's keyboard path and are spoken by screen readers like every other menu item.
- A window whose last tab moves away closes. Moving a tab never saves, reloads or reviews its Document; its undo history, selection, scroll and media position move with it.

## What is app-wide and what is per window

| App-wide (one for all windows) | Per window |
| --- | --- |
| Settings, hotkeys and the theme: a change applies to every window at once | Home and whether it shows |
| Automation: the host, its scripts and the Automation manager | The focus and the focused panel |
| The Log | Full screen video |
| Recent files | The floating panel groups |
| The spellchecker and its dictionaries | The current tab and the tab bar's order |
| Saved layouts (WS3) and presets | The window's size, position and maximised state |

A toast or a modal question about a task opens in the window of the tab it is about. A notice that belongs to no tab (an update, a crash report offer) opens in the active window. Usage analytics and crash reports stay one context for the whole application; a crash report names every window's tabs and which window was active.

## Video and audio

Each window's Video and Audio panels show its own current tab's video, audio and position. **Only one window plays at a time**: starting playback in one window pauses every other one. The editor's audio and the general player keep the active-mode rule of [ADR 0009](0009-active-mode-media-transport.md) inside the window that plays. Today one video and one audio controller serve every tab and reopen the media on a tab change; with several windows each window gets its own, so a paused window keeps its frame on screen while another plays.

## Restore and the Session

All windows and their tabs come back at the next start, each with its size and position and its tabs in order, the current one current. On Windows and X11 the position is intersected with the screens there are, as for floating panels: a window whose monitor has gone comes back whole on the primary screen, and sizes are kept in device-independent pixels across monitors of different scales. **On Wayland only the size comes back**, because the compositor places windows.

The Session stores the windows: for each, its geometry and its tabs, the tab rows keeping everything they hold today, including each tab's arrangement. A Session written before this change, or legacy's, opens as one window. Crash recovery restores every window the same way. The saved layout file beside the settings keeps the arrangement shown last in the active window, so a start without a Session opens one window that looks as that window did.

A tab's arrangement stays a value about one window and its floating panels, whichever window shows it. The adapter gives each window's docking area and panels names of their own in the engine, and stores arrangements without them, so a tab's `[Hikari]` section (WS2), its Session row and a saved layout read the same in any window. Layout validation therefore still accepts one main window per arrangement; what it no longer assumes is that the application has only one.

## Close and quit

- **Closing a window** (its title bar ×, the window manager, or Close window 5263) reviews the unsaved changes of that window's tabs only, then closes them with the window. The other windows stay open.
- **Closing the last window quits.**
- **Alt+F4** is Close window's default key ([#866](https://github.com/altqx/hikari/issues/866)), so it closes only its window on every platform. On Windows, Qt passes Alt+F4 on to the system, which closes the window. On Linux the window manager or the app gets the key, and both close only that window.
- **Quit** (File › Exit, the platform's quit) reviews the unsaved changes of every window in one review, then closes all the windows together and saves the Session with every window in it, so all of them come back.

## Platforms

- **Windows and X11:** the drag-out follows the pointer, the new window is placed under it, and window positions are restored.
- **Wayland:** a client can't place or move its windows. The tab drag out is a drag and drop (the tab's image follows the pointer), and the window is created at the drop, where the compositor puts it. The xdg-toplevel-drag protocol, where a compositor offers it, can make the window itself follow the pointer later; it is not required.
- **Multi-monitor and per-monitor DPI** follow the floating-panel rules of the [docking contract](../docking.md#chrome).

The native gate proves the drag out, back and between windows, the keyboard commands, restore after a restart and after a crash, and close and quit on Windows, X11, sway, KWin and mutter.

## What changes

- [docking.md](../docking.md): the WindowId row and the one-main-window paragraph, the WS2 rules on main windows, and "the main window keeps its geometry" in the per-tab section become this record's rules.
- ADR 0020: "the main window keeps its geometry" reads "the tab's window keeps its geometry".
- [workspaces.md](../ux/workspaces.md) and `CONTEXT.md` (Workspace, Editing target, Session) name the window rather than the main window.

The code starts after DK2 [#588](https://github.com/altqx/hikari/issues/588) and DK3 [#589](https://github.com/altqx/hikari/issues/589) merge, as they change the same docking code.

Amended by [ADR 0030](0030-open-a-folder-as-a-project.md): the Project is per-window state, one per window, and the Session records each window's Project. The Project tier of settings, catalogues, dictionary words, layouts and presets applies per window, on top of the app-wide user tier; a change to the user tier still applies to every window at once. Each window spells with its own dictionary language, and windows on one language share a checker; grammar follows the active window.
