# Native LuaJIT isolation and typed QML reply experiment

Throwaway feasibility evidence for [Choose how automation dialogs render in QML](https://github.com/altqx/hikari/issues/33) and [Prototype automation manager, script dialogs and cancellation](https://github.com/altqx/hikari/issues/51). This supplements the earlier HTML manager study; it does not choose its layout, the helper lifetime, concurrency, cancellation timeout or Document transaction policy.

**Observed on Windows 11 build 26200, 2026-09-27:** a separate native C helper ran the existing LuaJIT archive, exchanged typed values through a dedicated Windows named pipe, synchronously waited for an actual QML `Dialog`, and resumed with its values while the GUI event loop continued. The captured run completed 16 bounded scenarios, with all 23 derived observation checks substantiated. This is not a production acceptance suite or complete script compatibility result.

Open [report.html](report.html) for the readable case results and provenance, or [evidence/observations.json](evidence/observations.json) for exact messages, replies, stdout/stderr, process exits, runtime/module hashes and experiment parameters.

## Run

From this checkout in PowerShell:

```powershell
& "$env:LOCALAPPDATA/HikariSub/prototype-runtime/Scripts/python.exe" HikariSub/prototypes/lua-isolation/run.py --source C:/Work/Kainote
```

The default is Qt's `offscreen` platform with the Quick software backend. `--visible` shows the same automatically driven fixture; it is not a manual interaction review mode and still does not qualify OS input. No installation, network request, account, credentials or runtime download occurs. The run uses an existing installed PySide6/Qt 6.11.2 environment and MSVC tools discovered through `vswhere`; it fails if they or the expected archive are unavailable.

The script creates a new ignored `_run/<UTC timestamp>/` for its C object/executable/PDB, build log and raw/full observations. It updates only this folder's `evidence/observations.json` and `report.html`. It reads the existing edgeblur script in place, records its before/after hash and never edits it. No application configuration, user subtitle file, clipboard, dictionary, external script updater or production module is used. No binary or dependency tree is intended for Git.

The fixture deliberately terminates only its own child process. A stuck-case watchdog and a 350-ms scheduled force stop bound the experiment. These are harness parameters, not proposed product escalation defaults. Normal execution finishes without the ten-second per-case watchdog firing.

## What actually ran

| Boundary | Actual mechanism and observation |
| --- | --- |
| Lua runtime | `helper.c` links the existing `C:/Work/Kainote/bin/x64/Debug/LuaJit.lib`. Runtime reports `LuaJIT 2.1.ROLLING`, version number 20199, x64 Windows and enabled JIT. Built-in FFI calls `kernel32.GetCurrentProcessId`; it agrees with the GUI-owned child PID. This is one FFI call, not third-party native-module qualification. |
| Protocol versus diagnostics | QLocalServer supplies a unique Windows named pipe; native `CreateFileW`/`ReadFile`/`WriteFile` use that channel. A version/type/generation/run/request header and tagged values are length-prefixed. Native `printf`, Lua stdout and Lua stderr remain on separately captured QProcess channels. Their noise did not become protocol data. |
| Synchronous call | Lua calls a native C `aegisub.dialog.display` fixture entry. The C callback sends a typed request and waits in the helper. The GUI creates fixed label/edit/intedit/checkbox/dropdown controls in QML and sends a typed reply. No Lua state pointer or Qt object crosses the pipe, and no Lua callback enters Python. Lua errors unwind through native `lua_pcall`, not Python callback frames. |
| QML readback | The actual Dialog's metaobject ancestry includes `QQuickDialog`. Automated assignments through its controls return `静かな港 🌙`, numeric 37, boolean true and choice `beta`; those values arrive back in Lua. QML recorded 15–16 timer ticks during each approximately 300-ms dialog wait in this run. This demonstrates event-loop activity, not calibrated latency, focus usability, rendering speed or IME behavior. |
| Experimental state | Two invocations in one helper observe counters 1 and 2. After deliberate helper loss and restart the new state begins at 1. This is an experimental persistent-state case; it does not accept one long-lived helper/state or a concurrency policy. |
| Cancellation | A synthetic loop polls the native cancellation entry and exits with a Lua error after a real CANCEL message. Cancellation while waiting for a dialog arrives before the false-plus-values reply and is observable in Lua. An uncooperative Lua loop does not poll; the harness explicitly terminates its owned process. No claim is made that arbitrary native/FFI code cooperates. |
| Abrupt loss / restart | The native helper deliberately calls `TerminateProcess(GetCurrentProcess(), 77)`. The GUI observes exit 77 and starts a new generation. This exercises abrupt child-loss handling, not an accidental access violation, arbitrary native-module crash or external-side-effect rollback. Restart is an explicit harness step, not automatic macro rerun. |
| Stale / duplicate messages | The GUI rejects a controlled old-generation RESULT replay. The restarted native helper rejects a real old-generation REPLY sent over its pipe. A controlled duplicate dialog reply is rejected at the polling boundary. These are labelled injections; the report does not claim an exhaustive race scheduler. |
| Protocol failures | Separate native sessions receive version 99, a declared 100-byte frame followed by ten bytes and EOF, and an oversized length. Each reports the specific failure and exits with code 21. No comprehensive malformed-value/fuzz, authentication or hostile-peer audit was performed. |

The protocol caps frames at 65,536 bytes, nested values at depth 12 and tables at 128 entries. Captured stdout/stderr retain at most 131,072 characters each. These finite fixtures do not establish production backpressure, OOM handling, untrusted-input safety or resource-growth budgets. Process isolation and a unique pipe name are not a filesystem/network security sandbox.

## Existing script versus synthetic dialog

The unchanged [bundled edgeblur macro](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/Automation/automation/Autoload/macro-1-edgeblur.lua) is a separate scenario. Minimal host functions register it, and a two-record proxy copies reads and counts writeback. Selecting the first record changes `Harbor` to `{\be1}Harbor`, leaves `Unselected` unchanged, performs one writeback and calls the retained no-op `set_undo_point` stub once. Source SHA-256 before and after is `47963f2587798f6c7ba4ecbfdc63cf46123ff0b6671883e73949c1530968c442`.

That script has **no dialog**. It proves only this unchanged small macro executes against the stated sample adapter. The proxy is not the full subtitle userdata, real application history, validation, selection/dirty rollback or staged Document commit. No authoritative Document exists in this experiment.

All dialog content and polling/error/stuck fixtures in `fixtures.lua` are original. They exercise source-aligned distinctions: custom `Cancel` returns the string `"Cancel"`; default OK returns the empty string; default close returns false and a values table. The native entry discards the third button-ID map, matching the inspected entry rather than silently fixing C08. These expected distinctions come from [AutomationProgress.cpp](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutomationProgress.cpp#L240-L258) and [AutomationDialog.cpp](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutomationDialog.cpp#L434-L532); no differential run against the old GUI was performed. “Window close” is an explicit sample response action, not an observed OS close/keyboard mapping.

Complete coercions, duplicate/empty names, all layout spans/classes, float/alpha behavior and unsupported controls remain unimplemented here. Existing `register_filter`, mutable validation/callback order, undo and other C06–C08 semantics are not changed or proved. No DependencyControl/autoupdater or config-writing macro ran.

## Provenance and build boundary

Final captured run: **2026-09-27T01:04:31.552981+00:00**, Python 3.12.14, PySide/Qt 6.11.2, Windows 11 build 26200, offscreen/software. The report records exact source/header, compiler, archive, helper and selected Python/Qt runtime hashes, plus module-path/hash snapshots of the GUI and each owned helper (121 and 13 modules respectively in this run). Module enumeration is a snapshot, not an exhaustive dependency or deployment audit.

- LuaJIT archive: 5,167,468 bytes; SHA-256 `53045303e129de91e13a389d59ce83ebd49e3747374fe9eda96012b25b7059d4`.
- Final helper SHA-256: `4a21782e126ce3d5f599cb715f55decb5c91ea71e2d0f19f5ffc8d4cb46891db`.
- Vendored source tree: `33e8be4371e80732d26b3f831509f0f8f5b83a05`, source baseline `20d647c4c769ab7f5d383cf3c1c33f03876a94e9`; `luajit_relver.txt` contains `1753364724`. These source identities do not attest the old prebuilt archive's build recipe.
- Existing MSVC 14.51.36231 toolset / Community 2026, `/MDd` Debug runtime. This is **not** the accepted MSVC2022/official-Qt/CMake provisioning proof. No LuaJIT source rebuild, new SDK or alternative production dependency mechanism was introduced.

The initial wrapper incorrectly gave the `.lib` to `/TC` before `/link`; moving it into linker arguments fixed that build-command error. A first report attempt shadowed `QObject.event` with a logger, making a QChildEvent non-serializable; the logger was renamed. Intermediate captured checks exposed a generic duplicate-rejection diagnostic and a too-specific QML class-name check; the diagnostic now identifies the duplicate, and the report checks the actual metaobject ancestry. Earlier run directories retain their local outputs where generated, including the preceding captured observations. The final source was reread and rerun after these corrections; no failed production gate was reclassified as passing.

## Remaining review and proof

This supports continued feasibility work on the accepted separate-process boundary. It does not complete either ticket. Human manager/dialog reaction and the pending lifetime/concurrency/escalation choices remain open. Native Linux, real OS IME/focus/keyboard/UIA/AT-SPI, full unchanged scripts and native modules/DependencyControl, media/font/clipboard IPC, resource cleanup/growth, production protocol robustness and complete compatibility/transaction evidence are still required. A helper kill cannot undo arbitrary script file or network effects.

The prototype is frozen for source/evidence review; it selects no production IPC encoding, ABI, process count, timeout or atomicity policy. Intended tracked artifacts are the small original sources, this README, `.gitignore`, the observations JSON and the self-contained HTML report. `_run/` and binary build products remain ignored.
