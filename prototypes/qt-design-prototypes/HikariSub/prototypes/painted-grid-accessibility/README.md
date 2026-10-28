# Painted-grid accessibility boundary study — #45

The painted-grid direction is accepted. This throwaway study asks a narrower feasibility question: **can the installed PySide6 6.11.2 runtime supply the custom table accessibility adapter needed to continue the native grid study?** It cannot expose that complete interface through the inspected public Python bindings. This finding does not change the renderer choice or select a production drawing API.

The earlier [grid spike and keyboard follow-up](https://github.com/altqx/hikari/blob/d99b429e7a6e16107fa6d6bf80cc5fba04a6264d/HikariSub/prototypes/qml-grid/README.md) remain historical evidence. Its renderer-choice discussion predates the user's painted-grid decision. No benchmark is repeated here.

## Run

From the prototype checkout, using the already installed disposable runtime:

```powershell
& "$env:LOCALAPPDATA\HikariSub\prototype-runtime\Scripts\python.exe" HikariSub/prototypes/painted-grid-accessibility/probe.py
```

This opens a native Qt Quick window with a painted 64-row sample. Home/End/Up/Down navigate displayed order; Shift extends from the source-ID anchor; Ctrl moves current without replacing selected IDs. Reverse, RTL filter, empty results and tag display are available. All data stays in memory. The sample's rule that every third row is RTL and its tag-stripping regex are fixture conveniences, not production parsers.

For the bounded Qt observations on this machine:

```powershell
& "$env:LOCALAPPDATA\HikariSub\prototype-runtime\Scripts\python.exe" HikariSub/prototypes/painted-grid-accessibility/probe.py --probe --skip-uia
```

`--probe` without `--skip-uia` also requests an out-of-process Windows UIA observation using the included `uia-observe.ps1`. It only targets the explicit study process ID, limits enumeration depth/count, and records errors as unavailable observations. On this machine Windows PowerShell refuses script execution, so the documented local command skips that observer. No execution policy was changed or bypassed. Do not interpret the executable's exit code as accessibility acceptance: inspect the recorded evidence and unavailable checks.

The [published observation snapshot](evidence/observed.json) preserves the latest five Qt observations, fixture/source provenance and a path-free summary of the first UIA execution-policy denial. It adds no rerun or AT claim. New runs write ignored `local-results/observed.json`; the initial failed observer run was retained locally as `local-results/first-run-uia-policy-block.json`. Generated local output and bytecode are ignored. The study installs nothing and does not read or modify user configuration.

## Observed on 2026-09-27

Environment: Windows 11 build 26200, Python 3.12.14, PySide6/Qt 6.11.2, native `windows` Qt platform. Accessibility was explicitly activated with `QAccessible.setActive(True)`; this does not demonstrate automatic activation by a screen reader. No NVDA process was detected during the bounded prerequisite check. No NVDA or Orca session was exercised.

| Probe | Actual observation | Meaning |
| --- | --- | --- |
| Public binding inventory | `QAccessibleInterface`, `QAccessibleObject`, factory registration, table-cell, selection and text interfaces are present | Basic custom accessible objects can be explored |
| Required table binding | `QtGui.QAccessibleTableInterface` is absent; `QAccessibleInterface.tableInterface` is absent, including in installed `QtGui.pyi` | The inspected Python API cannot directly implement the required Qt table interface |
| Custom factory | Called with `PaintedStudy`; query returns the Python `PaintedAccessible` object, role `Table`, requested aggregate name | Factory/object plumbing works within Qt |
| Table query | `interface_cast(TableInterface)` returned null; child count is deliberately zero | A table role/name is not row/cell/table semantics; this adapter is intentionally incomplete |
| Windows UIA prerequisite | Built-in `.NET UIAutomationClient` / `UIAutomationTypes` load and expose GridPattern/TablePattern identifiers | Observer APIs are installed; no pywinauto/comtypes installation is needed for this narrow probe |
| Out-of-process UIA | Windows PowerShell rejected the local observer script with `UnauthorizedAccess` because script execution is disabled | No native UIA tree/pattern observation was obtained; no TablePattern/GridPattern absence or presence claim is made |

Qt's C++ [table interface](https://doc.qt.io/qt-6/qaccessibletableinterface.html) supplies row/column counts, cell lookup and selection methods. Its [accessible interface documentation](https://doc.qt.io/qt-6/qaccessibleinterface.html#tableInterface) distinguishes these from role/name/state and recommends the table interface for tables. The Python `interface_cast` stub returns an integer pointer type; returning a Python object or fabricated pointer would not implement the missing C++ virtual interface. No pointer/vtable workaround was attempted. This is a boundary of the inspected binding/runtime, not a claim that C++ Qt cannot support the contract or that every possible future binding lacks it.

Five synthetic `QKeyEvent` press/release observations dispatched to the native Qt window matched the expected application state:

| Case | Current source ID | Selected IDs |
| --- | --- | --- |
| Home after reverse + RTL filter (21 displayed) | 63 | 63 |
| Ctrl+End moves current only | 3 | 63 |
| Shift+Home returns to stable anchor | 63 | 63 |
| Shift+Down extends displayed range | 60 | 60, 63 |
| End with empty results | 60 | 60, 63 retained |

The item had active Qt focus in each observation. Restoring all rows retained 64 displayed IDs and the tag-hidden flag changed. These are native application key-routing/state observations, not physical keyboard, speech, accessible selection, visual glyph-quality or OS assistive-technology passes. The aggregate accessible name is only a diagnostic fallback; reading selected IDs aloud is not the proposed production table representation.

Fixture provenance: 64 deterministic generated rows with stable IDs, Arabic/numeric and Japanese/Latin text plus illustrative ASS tags. Canonical fixture bytes are tab-separated ID/raw-text records, UTF-8 with LF separators and no final LF: 2,821 bytes, SHA-256 `e6386563c387886bad8c99361f03efa9d6ece42650b24e8f25638c8dc4100247`. Each run also records the probe source SHA-256. This is not the required representative 50k-line corpus.

## Concrete next boundary

Continue with a native C++ Qt Quick adapter once the pinned official Qt C++ SDK and supported initial MSVC 2022 toolchain are available through the accepted build contract. The current prerequisite audit also lacks a Linux runner. Do not substitute the wheel, a pointer shim, hidden off-screen QML delegates or another grid renderer to declare this design proven.

The next adapter must demonstrate stable accessible row/cell identity independent of paint objects; table/cell lookup for virtual off-screen rows; selected versus current/focused state; bounded caching/lifetime; model/selection/focus events after sort/filter/reset; empty results; appropriate raw versus displayed text; mixed-direction content; and OS table/selection patterns. It must then be driven through Windows UIA and NVDA plus Linux AT-SPI and Orca, including keyboard-only navigation. An approved environment for the observer is still needed; this prototype does not request or make security-policy changes.

The interaction question for human review is **how the real table adapter should announce current-row movement, multi-selection counts, hidden selected rows and tag-hidden text without overwhelming navigation**. The aggregate name here cannot settle that question; review it with the native row/cell adapter and screen readers, not as a preference to replace table semantics.

**#45 remains open.** No virtual table contract, native screen-reader usability, optimized C++ renderer, 50k workload, input latency, presentation timing, CPU/GPU use, bounded memory/cache behavior or accepted performance gate has passed. Actual lower-class Windows/Linux reference machines, fixture hashes and calibration remain prerequisites for the performance verdict. The accepted grid specification should retain these obligations; this partial probe is not grounds to mark the capability complete.
