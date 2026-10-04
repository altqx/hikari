# D1 native gate harness (Linux)

Observes the native gate in [docs/qt/docking.md](../../docs/qt/docking.md)
("Required native gate and review") for card D1 (#152) on real Linux
compositors without touching the developer's desktop: every compositor runs
headless inside a container and HikariSub runs as a client of it.

| Session | Compositor | Input path | Screenshots | Window geometry |
| --- | --- | --- | --- | --- |
| `sway` | sway 1.12 (wlroots 0.20), headless backend, two outputs (HEADLESS-2 at scale 2) | `zwlr_virtual_pointer_v1` (`vpointer`) and `wtype` (virtual keyboard) | `grim` | `swaymsg -t get_tree` |
| `kwin` | KWin 6.7 `--virtual` | libei through `org.kde.KWin.EIS.RemoteDesktop` (`eiinject`) | `org.kde.KWin.ScreenShot2` | KWin script |
| `mutter` | mutter 50 `--headless` with virtual monitors (gnome-shell needs systemd-logind, which a container lacks) | libei through `org.gnome.Mutter.RemoteDesktop` | `org.gnome.Mutter.ScreenCast` + PipeWire | none (mutter alone publishes none) |
| `x11` | Xvfb 21.1 + openbox | XTEST (`xdotool`) | `import` | `xdotool`, AT-SPI |

Accessibility is read over AT-SPI (`atspi_tool.py`, python-gobject `Atspi`)
with `QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1`; Orca runs with `--debug-file` and
its `SPEECH OUTPUT` lines are kept.

## Running

```sh
tools/native-gate/run.sh                 # all four sessions, every step
tools/native-gate/run.sh kwin -- video   # one session, chosen steps
```

`HIKARI_TREE` names the checkout whose build is tested (default
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

## Steps (`gate.py SESSION [STEP...]`)

| Step | Gate item |
| --- | --- |
| `default` | default arrangement (one window, Video left, Audio over the Line editor, Grid below) |
| `kbd` | View > Panels > Line editor > Float and Dock from the keyboard; the draft and the editing target across both |
| `f6` | focus after Float, F6 from and into a floating panel, Ctrl+Shift+H from it |
| `move` | View > Move panel… (keyboard placement window): Audio left of the Grid |
| `pointer` | float button, double-click title bars, drag a floating panel onto the Grid with real compositor pointer input |
| `video` | Video panel with `cfr.mkv` (barcode frames) docked, floating, redocked: frames keep presenting while stepping |
| `persist` | float Audio, close the main window through the compositor, restart: Audio comes back floating |
| `fullscreen` | main window fullscreen by the compositor with a floating panel |
| `outputs` | a floating panel on a scale-2 output (mixed DPI), then that output removed |
| `orca` | Orca's speech while F6 moves through the panels and a panel is floated from the menu |
| `menutext` | Alt+V from the Line text field |
| `tests` | `hikari_ui_shell_tests` (D1 functions), `hikari_ui_docking_qualification_tests`, `hikari_ui_workspace_layout_tests` under the real platform |
| `a11y` | what AT-SPI exposes of the docking controls: named title-bar buttons and tabs (each with Float and Close), the Grid's table in the panel named Grid; pressing them through AT-SPI floats and docks |

## Other tools

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
  main window with a floating panel).
- `sessions/enter.sh SESSION CMD...`: run a command inside a session;
  `sessions/stop.sh SESSION` ends one. Sessions share the container, so
  `app.sh` only touches the HikariSub of its own session.

`gate.py sway-activate STEP...` (run inside the sway session) repeats steps
with `focus_on_window_activation focus`: sway's default (`urgent`) marks a
window that asks for activation through xdg_activation_v1 urgent instead of
focusing it, so cross-window F6 there depends on that setting. Its evidence
goes to `sway-activate/`.

Harness lessons: the menu bar has two Alt+V mnemonics (&Video, &View) and Qt
cycles between them, so `open_view()` presses Alt+V until the View menu shows;
Qt keeps hidden windows titled HikariSub, so X11 window searches use
`--onlyvisible`; removing a RandR monitor on Xvfb leaves the output's own
monitor covering the whole framebuffer, so the X11 monitor removal also
shrinks the output; sway needs one virtual keyboard that stays (each `wtype`
run otherwise toggles the seat's keyboard and the client loses focus);
mutter and KWin replace their libei devices when outputs change
(`eiinject` follows the newest); PID 1 must reap (`docker run --init`).
