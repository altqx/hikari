# D1/D3 native gate harness (Linux)

Observes the native gate in [docs/qt/docking.md](../../docs/qt/docking.md)
("Required native gate and review") for card D1 (#152) on real Linux
compositors without touching the developer's desktop: every compositor runs
headless inside a container and HikariSub runs as a client of it.

| Session | Compositor | Input path | Screenshots | Window geometry |
| --- | --- | --- | --- | --- |
| `sway` | sway 1.12 (wlroots 0.20), headless backend, two outputs (HEADLESS-2 at scale 2) | `zwlr_virtual_pointer_v1` (`vpointer`) and `wtype` (virtual keyboard) | `grim` | `swaymsg -t get_tree` |
| `kwin` | KWin 6.7 `--virtual` | libei through `org.kde.KWin.EIS.RemoteDesktop` (`eiinject`) | `org.kde.KWin.ScreenShot2` | KWin script |
| `mutter` | mutter 51 `--headless` with virtual monitors (gnome-shell needs systemd-logind, which a container lacks) | libei through `org.gnome.Mutter.RemoteDesktop` | `org.gnome.Mutter.ScreenCast` + PipeWire | none (mutter alone publishes none) |
| `x11` | Xvfb 21.1 + openbox | XTEST (`xdotool`) | `import` | `xdotool`, AT-SPI |
| `hyprland` | Hyprland 0.56 nested in KWin `--virtual` (Hyprland has no headless backend; sway offers xdg_wm_base 5, Hyprland binds 6), one 1600x1000 output | `vpointer` and `wtype`, as on sway | `grim` | `hyprctl clients -j` |

Accessibility is read over AT-SPI (`atspi_tool.py`, python-gobject `Atspi`)
with `QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1`; Orca runs with `--debug-file` and
its `SPEECH OUTPUT` lines are kept.

## Running

```sh
tools/native-gate/run.sh                 # all four sessions, every step
tools/native-gate/run.sh kwin -- video   # one session, chosen steps
```

`GATE_CONTAINER` names the container (default `d1gate`), so a gate of
another worktree can run beside one already up; `GATE_EVIDENCE` names the
evidence directory (default `out/native-gate-evidence`). `HIKARI_TREE` names the checkout whose build is tested (default
`/home/altq/Work/hikari-qt`; `HIKARI_TREE=$PWD tools/native-gate/run.sh` tests
this worktree's build). `run.sh` builds the `hikari-d1-gate` image (Arch Linux, the host's distro, so
the host-built binaries and the Qt SDK under `out/sdk` run unchanged), starts
a container with the build tree mounted read-only at its own path, starts
each session and runs `gate.py`. It only needs the build tree
(`out/build/ubuntu-x64-release`: the app, the UI test executables and the
media fixtures); nothing is built from the project. KWin is given the host's
render node so it composites with OpenGL (its screenshot API needs it); Mesa
still renders with llvmpipe.

Evidence goes to `out/native-gate-evidence/<session>/`: one PNG and one TXT
(compositor window list and AT-SPI state as JSON) per observation,
`steps.log`, and `results.json` with a verdict per gate item: `observed`,
`failed` or `not-observable` with the reason. A failed item is never
recorded as a pass.

Session `hyprland` (D3) is not in the default set: `run.sh hyprland -- STEP...`.
Hyprland's nested backend allocates GBM buffers on the render node; on an
NVIDIA host Mesa cannot (it falls back to dumb buffers, which a render node
refuses), so the image needs the host's NVIDIA user-space driver at the
kernel module's version: build `Dockerfile.hyprland` (its header says how)
and pass `GATE_IMAGE`; `run.sh` then adds the `/dev/nvidia*` devices. Its
session directory is `hypr` (`sessions/enter.sh hypr ...`): Hyprland's IPC
socket path must fit a Unix socket address.

## Steps (`gate.py SESSION [STEP...]`)

| Step | Gate item |
| --- | --- |
| `default` | default arrangement (one window, Video left, Audio over the Line editor, Grid below) |
| `kbd` | View > Panels > Line editor > Float and Dock from the keyboard; the draft and the editing target across both |
| `f6` | focus after Float, F6 from and into a floating panel, Ctrl+Shift+H from it |
| `move` | View > Move panel… (keyboard placement window): Audio left of the Grid |
| `pointer` | D3 with real compositor pointer input: Audio's "⋯" button and Undock in its menu; double-click on the docked Video header (floats) and on its floating header's title (docks); a floating panel dragged by its title over the Grid, with the accent drop highlight measured in the screenshot (`#9CDBC9`, the dark theme's default accent), and released there (docks) |
| `floating` | D3's floating window: no frame or title bar from the window system (sway's container border, KWin's frame geometry, openbox's `_NET_FRAME_EXTENTS`, mutter's screenshot); the header inside the frame; the drawn shadow (see-through, not black) or, on X11 without a compositing manager, no shadow and no black margin, then the shadow again under picom; moved by the header's free part through the window system (`startSystemMove`, every session since 2026-10-12) and, on X11, by its title (the engine's drag), then a double-click on the free part docks and resized from the bottom-right corner through the window system; on sway, `border normal` on the window (sway then sends server-side decorations through xdg-decoration) takes the header's title away, its "⋯" stays, and `border csd` gives the title back (`floating-forced-frame`; the app logs `hikari.decorations`) |
| `header` | D3's header from the keyboard: Shift+Tab from inside the Grid to its tab, Right to its "⋯", Space opens the menu, Escape closes it; Undock from the menu and Dock from the floating Grid's own menu; a title bar's "⋯" from the panel's first control |
| `video` | Video panel with `cfr.mkv` (barcode frames) docked, floating, redocked: frames keep presenting while stepping |
| `persist` | float Audio, close the main window through the compositor, restart: every panel comes back in the same window (docked in the main window or floating) with the same rectangle in it, the floating windows and the main window at the same size (within 2 px), and on X11 the floating windows at the same position (on Wayland the compositor places windows); a difference is read again after 4 s before it fails |
| `fullscreen` | main window fullscreen by the compositor with a floating panel (mutter: its toggle-fullscreen keybinding, bound to Super+F in the session) |
| `outputs` | a floating panel on a scale-2 output (mixed DPI; on mutter moved by its Super+Shift+Right and found at 2x in the screenshot), then that output removed; run last (sway's virtual pointer is given the new layout's extent with every move, but the output is gone for later steps) |
| `orca` | Orca's speech while F6 moves through the panels, a panel is floated from the menu, and the Grid's tab, "⋯" button and menu are reached from the keyboard |
| `menutext` | Alt+V from the Line text field |
| `tests` | `hikari_ui_shell_tests` (D1 and D3 functions), `hikari_ui_docking_qualification_tests`, `hikari_ui_workspace_layout_tests`, `hikari_ui_tab_layouts_tests` (WS1) under the real platform |
| `views` | D2 (#201): View > Only subtitles, Only video, Video and subs, Audio and subs and All from the keyboard (with `ep1.mkv` and blank audio from Audio > Open blank 2h30m audio): the core panels each shows, the focus on a shown panel, the Line editor's draft kept, All back to the panels' places |
| `editor` | D2: Ctrl+E (GLOBAL_EDITOR) through the compositor: only the Video panel, with the focus; Ctrl+E again: the arrangement back at its places, the focus on the Grid, the draft kept |
| `tabs` | WS1 (#215): Audio floated in the first tab, Ctrl+T (the new tab copies it), Audio docked and the Line editor floated there, then Ctrl+PgUp and Ctrl+PgDn through the compositor's keyboard: each tab's panels back in the same windows with the same rectangles and window sizes (positions on X11) |
| `videofs` | V5 (#184): F in the Video panel shows the video fullscreen on the main window's output, Space and Right work there, Esc leaves with the main window's geometry and the focus restored; the context menu's "Open in full screen on monitor 2" puts it on the second output (scale 2 on sway, KWin and mutter; two RandR monitors on X11) and Esc brings the docked video back; then the `hikari_ui_video_fullscreen_workflow --monitors 2` Spix workflow under the session |
| `tagspeech` | E6 (#188): "{\\" in the Line text opens the tag list, "b", Down, Return complete "\\blur"; the list's announcements (`Accessible.announce` from the Line text, which keeps the focus) as AT-SPI `object:announcement` events and in Orca's speech |
| `statusspeech` | P10 (#200): the status bar's fields over AT-SPI and Orca's "read status bar" (Orca+KP_Enter twice, desktop layout) through the compositor's keyboard |
| `refspeech` | R2 (#194): the reference tray (Ctrl+Q with a session restoring a reference): its controls' accessible names, then F6 into it under Orca, the lines and match navigation spoken |
| `icons` | K1 (#204): every icon-only control named over AT-SPI in the main window, a floating panel, the video fullscreen and the Style manager |
| `ime` | E6 with a real input method: ibus and ibus-hangul (Qt's ibus input context) compose 한 under the open tag list and commit it into the Line; the focus never leaves the Line text (on sway the romaja keyboard: wtype's own keymap sends key codes the 2-set keyboard reads through a US keymap) |
| `scheme` | K2 (#206): Follow system theme through the XDG settings portal (xdg-desktop-portal-gtk reading `org.gnome.desktop.interface color-scheme`), changed live: the window's brightness under the session's own platform theme, under Qt's gnome theme and under `QT_QPA_PLATFORMTHEME=xdgdesktopportal` |
| `dropper` | Y7 (#190): the Line editor's colour picker's screen dropper: on X11 the simple picker's own pointer grab picking a colour patch with a real right click; on KWin the screenshot portal's PickColor (xdg-desktop-portal-kde, KWin's picker) answered by a click on the patch; elsewhere not-observable (no backend with PickColor) |
| `edgetile` | D3 (#207): the floating panel's free header part dragged to the screen's left edge through the window system's move: whether the window system tiles it (a note either way) |
| `a11y` | what AT-SPI exposes of the D3 headers: each lone panel's title bar named after it (Qt's AT-SPI bridge gives a title bar the role `text`) with its "<panel> options" button, the Grid's page tab; the menu's Undock and Dock pressed through AT-SPI float and dock Audio; a tab group's page tabs checked when selected, the options button on the selected one, a tab pressed selects it, and Undock floats Shift times; the Grid's table in the panel named Grid |

## Other tools

- `gatelib.py`: the parts of `gate.py` that need no session (mutter's
  frame search in a screenshot, drag steps, the test skips the gate
  accepts, the default step order). `test_gatelib.py` tests them, also as
  the ctest `native_gate_lib`
  (`python3 -m unittest discover -s tools/native-gate -p 'test_*.py'`).
- `hold.sh SECONDS TEST_EXE FUNCTION`: runs a QtTest executable under gdb,
  stops at FUNCTION and spins the Qt event loop from gdb, so a test fixture's
  window (for example the bare KDDockWidgets shell of
  `hikari_ui_docking_qualification_tests`) can be driven from outside. This
  separates engine behaviour from the shell's.
- `trace_drop.py`: gdb script (`gdb -x trace_drop.py --args $APP`) that logs
  what KDDockWidgets' DragController finds under the cursor during a drag
  (`qtTopLevelUnderCursor`, `dropAreaUnderCursor`); with `HIDE_DROPAREA=1`
  and a `kill -INT` to the app it first hides the shell's file-drop
  `DropArea` (objectName `dropArea`) through `qt_qFindChild_helper` and
  `QQuickItem::setVisible`, without rebuilding anything.
- `review.py EVIDENCE_DIR`: adds the screenshot reviews (items a script
  cannot judge, such as 2x rendering) to `results.json` and writes
  `summary.txt`.
- `probe_*.py`: single questions asked during the gate (F6 after Float, F6
  into a floating panel, the third-level View menu, fullscreen, closing the
  main window with a floating panel, the View > Panels submenu one key at a
  time, the X11 colour picker's pointer grab, the screenshot portal's
  PickColor outside HikariSub).
- `sessions/enter.sh SESSION CMD...`: run a command inside a session;
  `sessions/stop.sh SESSION` ends one. Sessions share the container, so
  `app.sh` only touches the HikariSub of its own session.

`gate.py sway-activate STEP...` (run inside the sway session) repeats steps
with `focus_on_window_activation focus`: sway's default (`urgent`) marks a
window that asks for activation through xdg_activation_v1 urgent instead of
focusing it, so cross-window F6 there depends on that setting. Its evidence
goes to `sway-activate/`; `run.sh` does this for steps `f6`, `fullscreen`, `tests` and
`outputs` after the sway session, in a fresh sway. Under sway's default a
cross-window focus item is recorded not-observable only when the window that
asked for activation is marked urgent (the app asked, sway refused).

Harness lessons: the View menu starts with D2's five arrangements, enabled
by what is open, and Down skips disabled items, so `view_to()` presses Down
until the wanted item has the AT-SPI focus instead of counting positions;
the menu bar has two Alt+V mnemonics (&Video, &View) and Qt
cycles between them, so `open_view()` presses Alt+V until the View menu shows;
Qt keeps hidden windows titled HikariSub, so X11 window searches use
`--onlyvisible`; removing a RandR monitor on Xvfb leaves the output's own
monitor covering the whole framebuffer, so the X11 monitor removal also
shrinks the output; sway needs one virtual keyboard that stays (each `wtype`
run otherwise toggles the seat's keyboard and the client loses focus);
mutter and KWin replace their libei devices when outputs change
(`eiinject` follows the newest); PID 1 must reap (`docker run --init`).
D3: pointer targets come from AT-SPI in window coordinates (`atspi_tool.py
where`) plus the window's position from the compositor; mutter publishes no
window geometry, so a floating panel there is found in a screenshot by its
frame's 1-pixel boundary (`locate_floating`). The X11 session runs no
compositing manager, so a floating panel has no shadow there; the `floating`
step starts picom with an empty configuration (Arch's default one draws
picom's own shadows) to see the drawn shadow, and moves the window over the
main window first so the shadow has something to show through.

Sweep lessons (2026-10-14): a submenu opened from the keyboard under a
resting pointer highlights the item under the pointer, so keyboard steps
first park the pointer (`park_pointer`); KWin's own Qt Quick windows (its
colour picker's prompt) run on Qt Quick's software backend, as with OpenGL
they abort it here; the image has a machine id (libdbus aborts a client
that opens a private bus without one) and qt6-wayland (the KDE portal
backend's Qt); a drag through the window system's move goes in steps of at
most 8 pixels (the window lags the pointer by the step that crosses the
drag distance); `locate_floating` takes a 1-pixel line whose bottom edge is
where the window's height puts it (a flat stretch of background, or the
Line editor's text box, otherwise matches first); wtype's keymap has one
level per key, so "{" is typed as text on sway, not Shift+[.
