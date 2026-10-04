# Legacy characterization capture

Records what the legacy wx HikariSub actually does on the [approved-departure fixtures](../../docs/qt/fixtures/approved-departures/README.md), next to the approved expectation, as E1 requires. It builds the legacy app at its pinned baseline commit and never touches a user profile. The legacy app keeps its configuration beside its executable, so every UI capture runs a fresh copy of the package on a private X server.

[`plan.json`](plan.json) gives each fixture case one route:

- **time:** [`time_capture.cpp`](time_capture.cpp) links the real legacy `SubsTime.cpp` and `Timebase.cpp` and evaluates the fixture values. T42-A uses the legacy composition at `SubsGridBase.cpp:534`.
- **save-as:** [`drive.py`](drive.py) opens a scratch copy in the real app under Xvfb and uses Save As (Ctrl+Shift+S) to write a new path. It then hashes the output and records whether it is byte-identical. Failures keep a screenshot and the app's log tail.
- **none:** the case needs a UI flow that is not driven yet. It is recorded as not captured, with the reason; it is not assumed.
- **automation** (S3, the plan's `automation` section): two launches of the real app on a scratch document. The first keeps the shipped Autoload scripts and records the popups and a screenshot at startup. The second empties Autoload; its scripts become the corpus. It installs [`automation/capture-probe.lua`](automation/capture-probe.lua) in Autoload and runs each macro from Automation > Hikari capture probe: the mouse opens the menu and hovers the macro, and Return runs it through the menu bar's keyboard navigation (legacy draws its own menu bar; a click on a submenu item and the script hotkeys never fire under Xvfb); the menu positions are plan values, and a screenshot of the submenu is kept. Each macro writes one JSON line to `$HIKARI_CAPTURE_OUT`. The dialog cases come first (the L2 subset: every control class with its round trip, Return and Escape, button IDs known, unknown and invalid, Up in float and int edits, typed Unicode). The corpus capture comes last (L6). It records the Lua version, module availability, the C06 stubs, and for each corpus script its load result, script_* globals and registrations. Corpus scripts load in the probe's Lua state with their own globals and the register functions intercepted, so `package.loaded` is shared between them. That is recorded, not hidden. The legacy Linux build links the distribution's LuaJIT, not the Windows build's.

**The dialog cases never complete on the legacy baseline, on either platform.** `LuaCommand::Run` (`Automation.cpp:993-996`) starts the macro on a `LuaThreadedCall` and joins it with `wxThread::Wait()` on the main thread, while `aegisub.dialog.display` (`LuaProgressSink::LuaDisplayDialog`, `AutomationProgress.cpp:240-258`) queues the dialog to the main thread and waits on a semaphore. The pinned wxWidgets 3.3.3 defines `wxTHREAD_WAIT_DEFAULT` as `wxTHREAD_WAIT_BLOCK`, so `Wait()` pumps no messages on wxMSW either (`WaitForSingleObject` through `wxAppTraits::DoSimpleWaitForThread`); on wxGTK it is a plain `pthread_join`. Every macro that shows a dialog leaves the app not responding with no dialog on screen. Macros without a dialog (the corpus capture) run.

### F1 and F3 ui cases

The plan's `ui` section ([`ui_capture.py`](ui_capture.py), run by `drive.py` after the automation route) drives the legacy Find and replace dialog and the Spellchecker window on capture-only documents in [`inputs/`](inputs). Each case runs a fresh copy of the package with a `Config.txt` holding the case's options (a header that does not match the build makes legacy load its defaults first, then the listed values), Autoload emptied but for [`automation/state-dump.lua`](automation/state-dump.lua), and the `en_TEST` fixture dictionary. The steps are semantic (select rows, open a tab, type the search or replacement, Find, Replace next, Replace all, Find all, Replace checked, answer a box, dump, open the Spellchecker and walk it by Ignore), so the rewrite replays the same plan (`FindReplaceCapture.ReplaysTheLegacyCaptures`). A dump runs the state-dump macro from Automation > Hikari state dump by menu hover and Return; it first clicks the Line editor's Effect field (`focus_at`), because after a message box closes legacy's frame has no focused window and the menu bar gets no keys. Documents are dated two minutes back: the Linux build compares a file's time (with milliseconds) with the load time in whole seconds and would otherwise ask "Subtitles were modified by another program. Reload?". Positions (menus, dialog buttons relative to their window, grid rows) are plan values for the 1280x800 Xvfb screen. To run only these cases (for example in the `hikari-legacy-env` container with the package from a run's `legacy-app-*` artifact):

```
python3 tools/legacy-capture/ui_capture.py --plan tools/legacy-capture/plan.json \
    --package hikarisub-linux-x86_64.tar.gz --out capture/observations.json [--only F1-next-text ...]
python3 tools/legacy-capture/compare_ui.py capture/observations.json \
    <build>/tests/application/artifacts/find-replace-capture.json
```

[`compare_ui.py`](compare_ui.py) prints each F1 dump and box beside the rewrite's replay and the F3 walk; the reviewed capture is [`local-f1f3-20261004`](../../tests/fixtures/legacy-observations/local-f1f3-20261004/README.md).

### Windows route

[`drive_windows.py`](drive_windows.py) runs the same capture against the legacy Windows release (`v0.0.1-rc.1`, built from the baseline) on the Winix VM desktop. From a worktree with a clean, committed tree:

```
winix --config winix.yaml sync
python3 tools/legacy-capture/drive_windows.py --plan tools/legacy-capture/plan.json \
    --out tools/legacy-capture/captures/windows-20d647c4/observations.json --dump-first-hang
```

The `legacy-win-setup` task ([`windows/setup.ps1`](windows/setup.ps1)) downloads the release zip into `out/legacy-win` in the VM, checks it against the release's `SHA256SUMS` and extracts two copies: one as shipped (startup with the bundled Autoload) and one with Autoload moved into a corpus, the probe installed and its macros bound to Ctrl+Shift+F1..F12 in `Config\Hotkeys.txt`. A desktop launch takes arguments (the scratch document) but no environment, so the probe reads `capture-probe.cfg` beside itself when `HIKARI_CAPTURE_*` are unset. On Windows the script hotkeys reach the app, so no menu positions are needed. Desktop keys go to the foreground window; the driver clicks the main window's status bar before each hotkey. `legacy-win-output` ([`windows/output.ps1`](windows/output.ps1)) returns the probe's lines and whether the app responds; a step without an answer records the app's thread states (`legacy-win-stacks`) and, with `--dump-first-hang`, a minidump (`legacy-win-dump`), then the app is ended and the next step starts a fresh one. [`minidump_stacks.py`](minidump_stacks.py) prints that dump's stacks with the release's own PDB (`llvm-symbolizer`). Desktop actions need an idle VM, so the driver never overlaps them with a task.

The rewrite side of the dialog cases is `AutomationDialogTests::captureProbeDialogCasesAnswer` (`hikari_ui_automation_dialog_tests`): the probe's dialog macros through the QML dialog with the plan's keys, written to `tests/ui/artifacts/automation-capture-dialogs.json` in the build tree.

Run through [`legacy-capture.yml`](../../.github/workflows/legacy-capture.yml) (manual dispatch). Reviewed observation files are committed under `tests/fixtures/legacy-observations/`. An observation records what the old app did; it is not an expectation for the rewrite. Expectations stay in the fixture manifest and the departure ledger.

To compare a capture with the rewrite, run `LuaHelper.CaptureProbeCorpusRunsInThisHost` (it writes `tests/backends/artifacts/automation-capture-corpus.json` and `automation-capture-corpus-macro.json` in the build tree) and then [`compare_automation.py`](compare_automation.py) `<observations.json> <automation-capture-corpus.json> [--macro-corpus <automation-capture-corpus-macro.json>] [--dialogs <automation-capture-dialogs.json>]`. It lists host, module, C06-stub, API and per-script differences for the load-time (and macro-time) corpus, and for each dialog case the legacy answer (or what the legacy app did instead) beside the rewrite's, as candidates for review; none is a verdict.

### L6 third-party corpus

`drive.py --corpus-dir <dir>` runs only the automation probe, with the scripts in `<dir>` as the corpus (the package's Autoload is emptied), `--include-dir` files added to the package's Include (a bundled file is never replaced), `--document` as the probe's document and `--runs` as `HIKARI_CAPTURE_RUNS`; of the plan's steps only the corpus macro runs (the dialog steps would deadlock, A33-dialog-deadlock). With a runs file the probe's corpus macro runs the listed macros right after each script loads, on its own document, answering their dialogs itself and recording their log, returned selection and the document afterwards, then restoring the events. The probe also records the LuaJIT build's Lua 5.2 extensions and string.buffer (`lua_features`). The corpus comes from [`tests/fixtures/automation-thirdparty`](../../tests/fixtures/automation-thirdparty/manifest.json), staged by its `stage.cmake` (the `automation-thirdparty-stage` test leaves it in the build tree: `Autoload/` and the projects' modules alone in `Modules/`). In the `hikari-legacy-env` container (Xvfb, openbox, the package's wx libraries on `LD_LIBRARY_PATH`):

```
python3 tools/legacy-capture/drive.py --plan tools/legacy-capture/plan.json \
    --package hikarisub-linux-x86_64.tar.gz --probe /bin/true --legacy-commit <sha> \
    --corpus-dir <stage>/Autoload --include-dir <stage>/Modules \
    --document tests/fixtures/automation-thirdparty/fixture.ass \
    --runs tests/fixtures/automation-thirdparty/runs.json --out capture/observations.json
python3 tools/legacy-capture/compare_automation.py capture/observations.json \
    <build>/tests/backends/artifacts/automation-capture-thirdparty.json \
    --macro-corpus <build>/tests/backends/artifacts/automation-capture-thirdparty-macro.json
```

The rewrite side is `ThirdPartyCorpus.CaptureProbeRunsTheCorpus` (`hikari_backends_automation_thirdparty_tests`). The reviewed Linux capture and comparison are in [`captures/linux-thirdparty-20d647c4`](captures/linux-thirdparty-20d647c4/compare-rewrite-ubuntu.txt).
