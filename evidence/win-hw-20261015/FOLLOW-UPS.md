# Follow-ups after the Windows real-hardware session (2026-10-15)

For the next agent. Two D3 (#207) fixes came out of this evidence. Both were pushed to `qt` at the owner's request on 2026-10-15, but **neither is verified**. Read [d3/results.md](d3/results.md) first.

## 1. Live monitor removal: `366189ea5` (on `qt`)

"D3: a floating panel whose monitor goes away comes back whole at its size."

- **Problem it fixes:** d3 item 8. After Win+P "PC screen only", a floating panel from the 150 % monitor ended at (2387,107) 816×199, with only 173 px on the remaining screen. Another run left an 800×672 window with the panel drawn in only its top-left two-thirds. See [d3/12-removal-geometry-log.txt](d3/12-removal-geometry-log.txt) and [d3/13-restore-geometry-log.txt](d3/13-restore-geometry-log.txt).
- **Change:** the floating window's keeper watches for its screen's removal. Once the window system stops moving the window, it is placed whole on the main window's screen at its size from before the loss. This repeats if the window system moves it again within `dockchrome::kScreenLossWatchMs`. Files: `src/ui/docking.cpp` and `docking.h`, a new test `floatingPanelComesBackWholeWhenItsScreenGoesAway` in `tests/ui/docking_qualification_tests.cpp`, `docs/qt/docking.md`, `docs/qt/coverage.md`, and gate tweaks.
- **Status: CI is RED on Linux.**
  - Ubuntu [38736927730](https://github.com/altqx/hikari/actions/runs/38736927730) and Fedora [38736927716](https://github.com/altqx/hikari/actions/runs/38736927716): `hikari_ui_docking_qualification_tests.offscreen` fails in the new test itself, at `tests/ui/docking_qualification_tests.cpp:1147`. That is the setup step before the removal: once the panel moves onto the 150 % "Side" screen, `floating->size()` is 400×147 where 600×220 is expected. The app log shows `scale change: "Audio" dpr 1 -> 1.5 size QSize(400, 147) -> QSize(400, 147)`. So on Linux offscreen the logical size after the scale change is not what the test assumes. Either the test's expectation or the scale-settle path on offscreen needs fixing.
  - Windows CI [38736927790](https://github.com/altqx/hikari/actions/runs/38736927790) was still running when this was written.
  - Locally it passed only in a harness with stubbed theme singletons (no full app build).
- **To do:** make Linux CI green; then re-run d3 items 8–10 on real hardware: float a panel on a 150 % second monitor, choose Win+P "PC screen only", expect the panel fully on the remaining screen at its logical size, then Extend and restart.

## 2. Whole header docks on Windows: `7812c180f` (on `qt`)

"D3: on Windows the whole floating header is the docking drag." This is the owner's decision of 2026-10-15, reversing the 2026-10-12 split on Windows only.

- **Why:** Windows Snap never takes the borderless tool window during the free part's system move (d3 item 6, decision 50, confirmed on real hardware). The owner's first attempt to dock started right of the title, moved the window and did not dock (d3 item 7a).
- **Change:** `Docking::platformUsesSystemMove` is now true only for Wayland and X11. On Windows, as on offscreen, a press anywhere on a floating header except its buttons drags with the drop highlight and docks. A double-click there still docks; a right-click opens the panel's menu. Other changes:
  - Test: new `wholeFloatingHeaderDocksWhereItDoesNotSplit`; `headerSplitsOnEveryWindowSystem` is renamed `headerSplitsOnWaylandAndX11`.
  - Docs: `docs/qt/docking.md` (Chrome), `docs/qt/coverage.md`, and `docs/qt/compatibility-decisions.md` (D3-engine-move).
  - Windows gate: the `pointer` step now expects `floating-free-part-docks` instead of `floating-system-move`; the gate README is updated.
- **Status:** CI was queued when this was written (Ubuntu 38754156027, Fedora 38754156041, Windows 38754156099, Linux packages 38754156076). It sits on top of fix 1, so the Linux failure above shows in its runs too. Locally the docking tests passed with stubbed theme singletons, including the split-header functions at 125, 150 and 200 %. `hikari_ui_shell_tests` and the Windows gate have not run.
- **To do:**
  - Green CI.
  - Run the Windows gate's `pointer` step.
  - On real hardware: drag a floating panel from right of its title onto the main window and expect the translucent panel, the highlight and a dock.
  - When `ci/evidence-H2` merges, change two things that exist only on that branch (`74cc57df8`): the ledger row `D3-windows-snap` (Snap is no longer offered on Windows, by design), and the gate's `step_snap` (expect the engine's drag, not a system move).

## Still open from this session (owner decisions, no code yet)

- **E6 #188, K7:** with the Korean IME, an Enter during a composition also commits and advances the Line.
- **E6 #188, K8:** with the Korean IME, an Esc during a composition reverts the whole Line draft. See [e6/results.md](e6/results.md).
- **H2 #125 / N6:** the editor output (PortAudio) stays on the device it first opened, and the app has no output-device choice. Qt Multimedia playback does not follow the default output either. See [h2/results.md](h2/results.md).
- **H3 #126:** a Windows perf-harness build, presentation traces, calibration hardware, and whether to bind `ruby` as the Windows reference host. See [h3/results.md](h3/results.md).
