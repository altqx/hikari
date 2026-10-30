# Moving HikariSub from Qt 6.11.2 to Qt 6.12

This report covers what Qt 6.12 changes and adds, and where each item lands in HikariSub. It answers the
owner's request to move to Qt 6.12 and "use the new features to the fullest". It records facts and
effort estimates. Every choice is left to the owner (section 5).

Sources read:

- The worktree at `991d61480` (branch `research/qt612`, off `main`). Paths such as `src/ui/Popover.qml:38`
  refer to that commit.
- Qt's own pages, all fetched on **2026-10-30**: What's New in Qt 6.12 [WN], the 6.12.0 release notes
  [RN] (Qt 6 has no `dist/changes-*` files; these notes replace them), the 6.11.3 release notes [RN113],
  the release blog [BLOG], the hot reload blog [HOT], the release wiki pages [WIKI] [WIKI611], the
  platform matrices [SP] [SP611], the module docs listed at the end, Qt's online repository indexes
  [REPO] [REPOW], and Qt headers at the `v6.11.2` and `v6.12.0` tags on code.qt.io [HDR].
- Qt's Jira through its public REST API, for the fix versions of two bugs [J149181] [J147011].
- KDAB/KDDockWidgets releases and commits on GitHub [KDDW250] [KDDW251] [KDDW-skip].

Nothing was built or run. Counts come from grepping `src/` (C++, QML and JS) unless noted otherwise.

## 0. Summary

In priority order, for this app:

1. **6.12 is the LTS that still supports Windows 10.** Qt 6.12.0 was released on 2026-09-30 [WIKI]
   [BLOG]. "Qt 6.12 will be the last version to support Windows 10" [SP]. Open-source users get only the
   *initial* LTS patch releases; later LTS patches are commercial-only [REL]. The public patch dates are
   6.12.1 on 2026-11-12, 6.12.2 on 2027-01-14 and 6.12.3 on 2027-03-04 [WIKI]. The release notes list
   16 CVEs fixed against 6.11.x, 7 of them in qtbase [RN]. 6.12 is binary compatible with 6.11 [RN].
   Qt 6.11.3 also came out, on 2026-10-26 [WIKI611] [RN113].
2. **The bump is mechanical but wide.** The version is hard-coded in about 40 places outside the locks
   (section 1). Both Qt locks and the retained-metadata GitHub releases must be regenerated. The vcpkg
   triplets hash `HIKARI_QT_PREFIX`, which has the version in its path, into every port's ABI. That means
   a full rebuild of the vcpkg graph on every host and CI cache.
3. **Private and QPA API still compiles.** Every private symbol the app calls is still present at
   `v6.12.0`, with the same signature where the app relies on it (section 2.2). KDDockWidgets links
   `GuiPrivate` and `QuickPrivate` and must be rebuilt. Its pinned 2.4.1 predates KDAB's 6.12 CI work.
4. **Six behaviour changes will show in the app or the tests.** These are new ToolTip delay and timeout
   defaults, the end of the nested-popup close cascade, a default SpinBox context menu, MenuItem shortcut
   display, QString arguments from JS `null`/`undefined`, and Qt Multimedia moving from FFmpeg 7.1 to
   9.0.1 (section 2).
5. **Seven features are worth taking with the bump, all small:**
   - QML hot reload for development.
   - `Qt.escapeHtml()`, which replaces 8 hand-written escape helpers.
   - Multi-argument `String.arg()`, for 59 chained `.arg().arg()` calls.
   - `QSortFilterProxyModel::dataChangeRelevanceFlags()` for the Grid's filter model.
   - Text underline styles, which retire one Canvas workaround.
   - `QAccessibleViewportInterface` for the painted Grid.
   - Removing the PulseAudio-forcing workaround once it is qualified.
6. **The headline features fit poorly here:**
   - StyleKit is in Qt Labs, with no compatibility guarantee.
   - Canvas2D is a Technology Preview.
   - QtQuick.Controls.Native is a compile-time platform style that conflicts with the owned
     `HikariStyle`.
   - QRhi indirect draws do not apply: the presenter issues no draw calls of its own.
   - rcc deduplication finds nothing to merge (0 duplicates among the 304 icons).
   - The faster date parser runs on 3 cold ISO parses.
   - QDom changes do not apply, because QDom is unused.
   - QRangeModel sorting does not apply, because QRangeModel is unused.

## 1. Version bump touch points

### 1.1 Build and provisioning

- `CMakeLists.txt:50` has `find_package(Qt6 6.11.2 EXACT …)` and `CMakeLists.txt:51` has
  `qt_standard_project_setup(REQUIRES 6.11)`.
  - Raising `REQUIRES` to 6.12 switches policies QTP0006, QTP0007 and QTP0008 to NEW [RN].
  - QTP0006 changes the Wayland protocol code default. `src/ui/CMakeLists.txt:121` already passes
    `PRIVATE_CODE`.
  - QTP0007 and QTP0008 change `qt_generate_deploy_*script()`. The project calls
    `qt_deploy_qml_imports` and `qt_deploy_runtime_dependencies` directly (`cmake/HikariInstall.cmake:95-96`)
    and no `qt_generate_deploy_*`, so none of the three should change behaviour. A build would confirm it.
- More `6.11.2 EXACT` lines:
  - `cmake/HikariDependencies.cmake:8` and `:11`
  - `i18n/CMakeLists.txt:1`
  - `src/app/CMakeLists.txt:25` (CorePrivate)
  - `src/ui/CMakeLists.txt:30`, `:107`, `:119` and `:138` (WaylandClientPrivate)
  - `tests/ui/CMakeLists.txt:418`

  `cmake/HikariTesting.cmake:17` has no version.
- Provisioning:
  - `cmake/provision/Provision.cmake:93` and `:96` hold the Qt prefix: `…/6.11.2/msvc2022_64` and
    `…/6.11.2/gcc_64`.
  - `cmake/provision/Qt.cmake:186` holds the SDK folder (`qt-6.11.2-<platform>`).
  - `cmake/provision/Qt.cmake:268` holds the status message.
- Locks:
  - `tools/qt-lock/make-lock.py:10-24` and `:34-47` hard-code the roots
    (`linux_x64/desktop/qt6_6112/qt6_6112/`, `all_os/qt/qt6_6112_*_line_endings_src/` and
    `all_os/qt_patchers/6112/`) and the package ids (`qt.qt6.6112.*`).
  - `cmake/locks/qt-linux.lock.json` and `cmake/locks/qt-windows.lock.json` are regenerated from that
    script.
  - The 6.12.0 repositories have the same layout:
    - Linux: `qt6_6120/qt6_6120/` with `qt.qt6.6120.linux_gcc_64` [REPO].
    - Windows: `qt6_6120_msvc2022_64/` with `qt.qt6.6120.win64_msvc2022_64` [REPOW].
    - Both offer `qt.qt6.6120.addons.qtmultimedia` and `.qtshadertools`.
    - `all_os/qt_patchers/6120/` exists [PATCH].
    - The newest online installer is still 4.11 [INST], so the installer entries in `make-lock.py` can stay.
- `CMakePresets.json:157`, `:172` and `:214` put `out/sdk/qt-6.11.2-windows/6.11.2/msvc2022_64/bin` on PATH.
- Retained metadata: the releases `qt-metadata-linux-6.11.2-20261002` and
  `qt-metadata-windows-6.11.2-20261002` (`docs/build.md:87`, `:100`) need 6.12.0 counterparts.
  `make-lock.py --previous … --retention-base …` writes the `retained_url`s (`docs/build.md:102`).
- vcpkg ABI:
  - `triplets/x64-hikari-linux-release.cmake:6-8` and the Windows triplet's line 8 set
    `VCPKG_ENV_PASSTHROUGH HIKARI_QT_PREFIX` for every port.
  - vcpkg hashes passthrough values into the package ABI [VCPKGT], and the prefix path contains the
    version. Every port of both triplets therefore misses the binary cache once, not only
    `ports/kddockwidgets` and `ports/spix`.
  - The Windows guest's full rebuild took over 3 h before.
- CI workflows do not name the version. They read the locks (`.github/workflows/linux.yml:77`,
  `windows.yml:95`).
- The Linux floor stays valid:
  - The 6.12.0 Linux archives are still built on RHEL 9.6, for example
    `qtbase-Linux-RHEL_9_6-GCC-…X86_64.7z` [REPO], the same host as 6.11.2 in `qt-linux.lock.json`.
  - Qt still states "Linux binary packages are linked against glibc 2.34" [SP].
  - The AlmaLinux 9 floor (`packaging/linux/floor/almalinux9.sh`, `packaging/linux/make-packages.sh:16`)
    and `check-payload.sh` therefore keep their ceiling.
- The minimum CMake rises to 3.25 [WN]. The project already needs 3.28 (`CMakeLists.txt:1`), and the
  floor pins 3.31.8.
- Windows manifest: `hikarisub` is a `qt_add_executable` target (`src/app/CMakeLists.txt:43`), and Qt
  embeds a generated application manifest into such targets [WDEP]. The property docs date that to
  **6.11.1** [MANI], so 6.11.2 builds already do it, although [WN] lists it under 6.12. Nothing changes.

### 1.2 Packaging, tools and tests

- `packaging/qt-deployment.json:3` has `"qtVersion": "6.11.2"`. Every plugin it lists was checked
  against the 6.12.0 tree. The `bradient` decoration source is under qtbase at `v6.12.0` [HDR-deco].
  `adwaita` is mapped to the qtwayland archive; the `qt-deployment-manifest` ctest re-verifies it
  against the new lock.
- `tests/dependency-load/main.cpp:43-44` refuses any runtime other than `"6.11.2"`.
- Native-gate and Winix scripts put the 6.11.2 SDK on PATH:
  - `tools/native-gate/env.sh:9`
  - `tools/native-gate/windows/gate.ps1:23`
  - `tools/native-gate/windows/tests.ps1:8`
  - `tools/winix/qttest.ps1:5`
  - `tools/winix/review-window.ps1:5`
  - `winix.yaml`: 18 lines (29, 32, 35, 39, 72, 75, 79, 83, 159, 163, 201, 206, 210, 216, 223, 227,
    232, 259)
- The test fonts come from `Examples/Qt-6.11.2/…`:
  - `tests/backends/CMakeLists.txt:25`, `:31`, `:37`, `:195`, `:318`, `:332`, `:337`, `:342` and `:360`
  - `tests/perf/CMakeLists.txt:18`
  - `tests/runners/image/CMakeLists.txt:10`
  - `tests/ui/CMakeLists.txt:7`, `:31`, `:77`, `:93` and `:94`

  The three font files and the `quick/advancedtext/fonts` folder still exist in qtdeclarative at
  `v6.12.0` [HDR-fonts]. Only the folder name changes.
- `tests/support/i18n/include/hikari/i18n/ts.h:2` and `:46` mirror "lupdate 6.11.2" output and numerus
  forms. lupdate 6.12 now preserves the existing line endings in `.ts` files [WN]. Re-check the mirror
  against 6.12's `ts.cpp`.
- Fixtures that name 6.11.2 as data need no change: `tests/ui/crash_report_tests.cpp:799`,
  `tests/application/whats_new_tests.cpp:32` and `tests/fixtures/github-releases/releases.json`.

### 1.3 Documents that state the baseline

- `README.md:194`
- `docs/build.md:11`, `:21`, `:64`, `:87`, `:100` and `:105`
- `docs/platform-policy.md:7`
- `docs/README.md:15`
- `docs/architecture.md:61`
- ADR 0004 (`docs/adr/0004-qt-baseline-and-platform-support.md:7`), ADR 0008 (line 11) and ADR 0017
  (line 11, "an explicit Qt 6.11.2 native qualification matrix")
- `docs/docking.md:80`
- The perf host records `docs/perf/hosts/{ruby,sapphire}.{md,json}`

The platform policy's gate already lists the work a bump needs: verify artifacts and licences, rebuild
FFMS2, libass and the adapters, run clean-machine packaging and the QML, IME, accessibility, DPI, media
and performance checks, and review private and QRhi changes (`docs/platform-policy.md:23`).

Three code comments name 6.11 workarounds to re-test:

- `src/app/include/hikari/app/composition.h:17`: PipeWire (section 4.7).
- `src/ui/AutomationDialog.qml:187`: a ComboBox list width that needs re-aligning.
- `src/core/karaoke_checks.cpp:64`: Kelvin sign case folding.

## 2. Risks and breaking changes

### 2.1 Modules the app uses

- Root and application: `Core Gui Qml Quick Multimedia Network Svg QuickControls2` (`CMakeLists.txt:50`,
  `cmake/HikariDependencies.cmake:8`).
- Linux only: `DBus` and `WaylandClient`.
- Tests: `Test` and `QuickTest`. Tools: `ShaderTools` and `LinguistTools`.
- Private modules:
  - `CorePrivate` (`src/app/CMakeLists.txt:26`)
  - `GuiPrivate` (`src/ui/CMakeLists.txt:36`, `tests/ui/CMakeLists.txt:82`, `:110`, `:232`)
  - `WaylandClientPrivate` (`src/ui/CMakeLists.txt:139`)
  - KDDockWidgets adds `QuickPrivate` (`ports/kddockwidgets/portfile.cmake:41-45`).
- The QML imports include two internal `impl` modules:
  - `QtQuick.Controls.Fusion.impl` in 8 files and `QtQuick.Controls.impl` in 3: the `src/ui/style/*`
    controls, `ShellMenuItem.qml`, `VisualTagPicker.qml` and `VisualToolOption.qml`.
  - Like private C++ headers, these carry no compatibility promise. The owned style module
    `HikariStyle` (`src/ui/style/CMakeLists.txt`) overrides 15 Fusion controls.

### 2.2 Private and QPA call sites, checked against `v6.12.0`

Each header was diffed between the `v6.11.2` and `v6.12.0` tags [HDR].

| Call site | Private API | 6.11.2 → 6.12.0 |
|---|---|---|
| `src/app/crash_reports.cpp:18`, `:523` | `QZipWriter` (`qzipwriter_p.h`) | unchanged |
| `src/ui/window_frame.cpp:204` | `QHighDpi::toNativeGlobalPosition` | unchanged. Only `to/fromNativeWindowGeometry` lost their template context type. |
| `src/ui/window_frame.cpp:227-233` | `QtWaylandClient::QWaylandWindow::display()/shellSurface()`, `QWaylandDisplay::lastInputDevice()/defaultInputDevice()`, `QWaylandShellSurface::showWindowMenu()` | all present. `qwaylandshellsurface_p.h` is unchanged. `setLastInputDevice` now takes a `QWaylandSurface*`, which the app does not call. Members moved from `QScopedPointer` to `std::unique_ptr`. |
| `src/ui/window_decorations.cpp:29-33` | `QWaylandClientExtensionTemplate<…>(1)` | the constructor is now `explicit (int, QObject *parent = nullptr)`. The direct `: QWaylandClientExtensionTemplate(1)` still compiles. The class lost `Q_WAYLANDCLIENT_EXPORT`, which does not matter for a header template. |
| `src/ui/window_decorations.cpp:178-193` | `QNativeInterface::Private::QWaylandWindow::surfaceRole<xdg_toplevel>()` | unchanged. `qplatformwindow_p.h` only gained QCocoaWindow v3 and HarmonyOS interfaces. |
| `src/ui/docking.cpp:37`, `src/ui/window_frame.cpp:12` | `QPlatformNativeInterface::nativeResourceForScreen` | one-line diff (a header comment) |
| `tests/ui/docking_qualification_tests.cpp:26`, `tests/ui/video_fullscreen_workflow.cpp:18` | `QWindowSystemInterface::flushWindowSystemEvents`, `handlePrimaryScreenChanged` | unchanged. Only the tablet-event signatures changed. |
| `src/ui/video_presenter.cpp:12`, `src/ui/ycbcr_node.cpp:9` | `<rhi/qrhi.h>`: `QRhi::resourceLimit`, `QRhiResourceUpdateBatch` uploads, `QSGTexture` subclasses | additions only (indirect draw and feature flags). QRhi has "no source or binary compatibility guarantees" between minor versions [QRHI]. |

KDDockWidgets:

- `ports/kddockwidgets` pins v2.4.1 at `c1d28d25` (ADR 0017) and builds against the provisioned Qt.
  The tag is from 2026-07-20.
- KDAB's main branch added 6.12-beta CI after that tag. It also skips two tests on Qt 6.12 for an ASan
  heap-use-after-free that the commits put in Qt's `QQuickItemView`/`QQuickListView` code [KDDW-skip].
- v2.5.0 (2026-10-18) adds QtQuick dark mode, `LayoutSaver::addWindowToSave()` and
  `RestoreOption_CloseUnknownDockWidgets` [KDDW250]. It warns about a ViewFactory `-Woverloaded-virtual`
  source break.
- v2.5.1 (2026-10-23) bumps the soname for an ABI break [KDDW251].
- Whether 2.4.1 builds unchanged against 6.12 is unverified.

### 2.3 Behaviour changes that reach this code

- **ToolTip defaults.**
  - Qt 6.12 adds `ToolTip.policy`, default `Automatic` [TT]. Under it, `ToolTip.delay` defaults to
    `QStyleHints::toolTipWakeUpDelay()` instead of 0. `ToolTip.timeout` defaults to a text-length-based
    value of at least 10 s. Both apply "regardless of whether ToolTip.visible or ToolTip.text have been
    set" [TT].
  - The app sets `ToolTip.text` 99 times in 41 files and `ToolTip.visible` 98 times. 26 of the 42 files
    with `ToolTip.visible` set no `ToolTip.delay`, and no file sets `ToolTip.timeout`.
  - Icon-only strips will show their names after the platform delay and hide them after the timeout.
    `ToolTip.policy: ToolTip.Manual` restores the 6.11 defaults [TT]. 6 test lines read ToolTip
    properties.
- **Nested popups no longer close together.**
  - Clicking outside now closes only the top-most popup unless it sets the new `Popup.CloseMultiple`.
    Closing all of them "was previously the default behavior for popups with the Popup.Item popupType"
    [RN] [POPUP].
  - `src/ui/Popover.qml:38` and `src/ui/ColourPopover.qml:64` switch between `Popup.Item` and
    `Popup.Window` and use `CloseOnPressOutside` (`Popover.qml:92`, `ColourPopover.qml:216`).
  - 15 files instantiate them. Several hold ComboBoxes, whose lists are nested popups:
    `SettingsDialog.qml` (4), `StyleManager.qml` (4) and `Main.qml` (3).
- **SpinBox gains a default text-editing ContextMenu** [RN]. The app has 21 SpinBox uses in 13 files,
  and `src/ui/style/SpinBox.qml` derives from `F.SpinBox`. `TimeField.qml:84` and `NumberField.qml:77`
  already swallow context menus with an empty `ContextMenu.onRequested`. SpinBoxes will show Qt's
  menu, with Qt's own translations of its entries.
- **MenuItem shows its Action's shortcut** (`Qt::AA_DontShowShortcutsInContextMenus` turns it off)
  [RN].
  - `ShellMenuItem.qml` replaces `contentItem` and draws `keysText` itself, so it is most likely
    unaffected.
  - Plain `MenuItem`s bound to an `Action` with `shortcut:` (3 `shortcut:` lines in 2 files) may now
    show a key label.
- **JS → C++ argument coercion.**
  - Calling a Q_INVOKABLE that takes a `QString` with JS `null` or `undefined` now passes `"null"` or
    `"undefined"`. Some 6.11 call paths passed an empty string [RN].
  - A JS array passed to a `QVariant` parameter now always arrives as `QVariantList` [RN].
  - `src/` declares 254 Q_INVOKABLEs with QString parameters. No QML call passes a literal
    `null`/`undefined`, so the exposure comes from values that may be undefined. Only a full suite run
    shows it.
- **`Component.createObject()` aborts** when a required property cannot be set because of a type
  mismatch [RN]. There are 8 calls in 5 files: `AutomationDialog.qml:362`, `MacroMenuEntries.qml:52`,
  `:57`, `MessageHost.qml:40`, `MessageBox.qml:186` and `VisualToolDialogs.qml:34`, `:46`.
- **Hover is throttled.**
  - When items animate under a stationary pointer, hover and cursor now update at most every 100 ms
    (`QT_QUICK_FRAME_SYNCHRONOUS_HOVER_INTERVAL`, `-1` updates only on real moves) [RN].
  - Effectively invisible items no longer trigger re-rendering [RN].
  - UI tests that move an item under a parked pointer and expect a hover change in the same frame may
    need a wait.
- **Qt Multimedia ships FFmpeg 9.0.1** [RN]. 6.11.x shipped 7.1.x: the 6.11.3 notes have "Update FFmpeg
  to version 7.1.5" [RN113], and `docs/perf/hosts/ruby.json:139` records 7.1.5.
  - This is the general player (`src/backends/qt_general_player.cpp`, the `ffmpegmediaplugin` in
    `packaging/qt-deployment.json:41`, `:58`).
  - FFMS2's own vcpkg FFmpeg 7.1.2 is separate (`docs/build.md:88`, ADR 0016).
  - Expect changes in decode, metadata and track behaviour of `QtGeneralPlayer`. Expect new library
    names in the payload; `check-payload.sh` re-checks the glibc ceiling.
- **Text rendering shifts.**
  - HarfBuzz is upgraded to 14.5.0 [RN].
  - On Windows, DirectWrite now picks antialiasing per font and size from the GASP table [WN].
  - Screens report the colour space from their ICC profile instead of always sRGB [WN].
  - Image, golden and perf baselines that include Qt-drawn text may move. libass renders with vcpkg's
    HarfBuzz, so subtitle output is not affected.
- **Linux `QDesktopServices::openUrl`** now goes through the XDG OpenURI portal when one exists [WN].
  It is reached from `src/backends/desktop_handoff.cpp:40`. A 6.12-beta crash in it on Wayland is fixed
  in 6.12.0 (QTBUG-147629) [RN].
- **qmllint** gains warnings for shadowed members, types referred to by file name, recursive
  instantiation and `new Array(a, b)` [WN].
  - It renamed `translation-function-mismatch` and removed `restricted-type` [RN]. The app has no
    qmllint directives in QML.
  - `modules.qmllint` builds `hikari_ui_qmllint` (`tests/modules/CMakeLists.txt:16-18`), and qmllint
    exits non-zero past `--max-warnings` [QMLLINT]. The new categories may turn it red. Only a run will
    tell.
- **Deprecations in 6.12.** None is used in `src/` or `tests/`:
  - `Q_ENUMS`/`Q_FLAGS` now warn on Clang and MSVC.
  - `QTextStream::operator>>(char*)`.
  - `QMediaMetaData::ThumbnailImage`.
  - QuickTest `TestCase.optional`.
  - The QML `SortFilterProxyModel.model` property and `FunctionSorter.sort`.
  - `decodeMSG()` and `qmlDeprecatedRegisterTypesAndRevisions` [RN] [HDR-dep].
- **No effect here:**
  - The QDom default `InvalidDataPolicy` becomes `ReturnNullNode` [QDOM] [RN]. QDom and QXmlStream are
    unused. The notes also list CVE-2026-15037 under qtbase. I did not confirm that it is the QDom fix.
  - Qt WebEngine and Qt PDF no longer ship in the 6.12 release [RN]. Neither is used.
  - QSaveFile now defers permissions until `commit()` [RN]. No call site sets permissions on a QSaveFile.
  - Date/time format-string quoting changed, and the `t` specifier no longer accepts zone names such as
    "CET" (QTBUG-149288, listed as a fix) [RN]. The 3 `fromString` calls use `Qt::ISODate`.

## 3. Feature-by-feature mapping

Effort: S is under a day, M a few days, L a week or more. Status follows Qt's labels: stable, TP
(Technology Preview) or Labs ("may be removed … not subject to any compatibility guarantee" [LABS]).

| Feature | Status | Where it applies here | Effort | Recommendation |
|---|---|---|---|---|
| QML hot reload in the engine; reworked `qmlpreview`; `<exe>_qmlpreview` target [HOT] [WN] | stable, "first release" per [HOT] | The app is QML-heavy: 185 QML files, 42.7 k lines, `Main.qml` alone 7,215. It needs a dev-only preset with `QT_QML_DEBUG`, which opens a debug port; [HOT] says "Don't ship it". All 8 configure presets are Release. The target is generated "for every executable that has a QML module". Here the module is on the static library `hikari_ui` (`src/ui/CMakeLists.txt:19`) linked into `hikarisub`, so it is unverified whether `hikarisub_qmlpreview` appears. Not safe with QML engines on several threads [HOT]. | S–M | Adopt now, dev only. It is the biggest everyday win. Run it under `vx11`. |
| `Qt.escapeHtml()` [ESC] | stable | 8 hand-written helpers: `AllTagsEditionDialog.qml:611`, `FontCollectorDialog.qml:84`, `LogPanel.qml:240`, `TitleBarMenuItem.qml:13`, `TemplateEditor.qml:294`, `:608`, `KanjiTimerDialog.qml:731` and `SpellCheckerPanel.qml:369`. It also escapes `"` [TOHTML], which the helpers do not. The character-by-character escapes in `AutomationDialog.qml:153-158` interleave with underlining and stay. | S | Adopt with the bump. It removes duplicated code. |
| `String.arg()` with several arguments [WN] [RN] | stable | 59 chained `.arg(a).arg(b)` calls in 28 QML files. A chain re-substitutes `%n` placeholders that appear inside an earlier argument, for example in user text; the one-call form does not [RN]. | S–M | Adopt with the bump, as a correctness fix. |
| `QSortFilterProxyModel::dataChangeRelevanceFlags()` [QSFPM] | stable (redeclare as slot or Q_INVOKABLE; it becomes virtual only in Qt 7) | `LineFilterModel` (`src/ui/line_table_model.h:218`) filters the Grid with the default dynamic filter. The source emits whole-table `dataChanged` for roles the filter never reads: Active, Selected and Anchor at `line_table_model.cpp:480`, SpellMarks at `:265`, Comparison at `:328` and UnplacedRuns at `:228`. Each one re-runs `filterAcceptsRow`. `LogEntries` (`src/ui/log_controller.h:16`) is the second subclass. | S | Adopt with the bump, measured against the perf contract. |
| Text, TextEdit and TextInput draw all `QTextCharFormat` underline styles (wave, spell-check) on the RHI renderer [WN] [RN] | stable; the software renderer falls back to a single underline [RN] | `TemplateEditor.qml:517-518` paints a wave with a Canvas "because Qt Quick's text draws no wave underline". `EditorHighlighter` (`src/ui/editor_highlighter.cpp:21-41`) could set `WaveUnderline`. The Line editor's spelling marks (`Main.qml:2428`) are background ranges, so that design choice is the owner's. | S | Adopt for the TemplateEditor workaround. The spelling-mark look is the owner's. |
| `QAccessibleViewportInterface` [AVI] [NC]; ListView now exposes it [RN] | stable | The painted Grid's `LineGridAccessible` (`src/ui/line_grid_accessible.h:16-18`) implements the table and selection interfaces only. A viewport interface would give screen readers the visible range and row count. QML ListViews get it for free. | S–M | Adopt later, with an NVDA/Orca check. |
| QML `Accessible.expanded` / `expandable`; AT-SPI reports expanded-state changes [NC] [WN] | stable | Folded runs in the Grid (`FoldRole`, `line_table_model.cpp`) and the dock menus | S | Later, with the a11y work. |
| `Item.mutabilityGroup` [ITEM] | stable, "advanced" | Keeps rapidly updated geometry (playheads, the audio cursor) out of batches with static geometry | S | Only with a profile that shows batching cost. |
| `QAccessibilityHints::motionPreference` [WN] | stable | Only 6 QML animations exist | S | Skip for now. |
| `QStyleHints::toolTipWakeUpDelay` [WN] | stable | Interacts with the ToolTip defaults in 2.3. The 24 explicit `ToolTip.delay` values are 500 ms (15), 600 ms (7), 300 ms (1) and the press-and-hold interval (1). | S | Decide together with the ToolTip policy. |
| `Color` QML singleton; `color.rgba`; `Rectangle.color` reset [WN] [RN] | stable | 44 `Qt.darker/lighter/alpha/rgba…` calls in 27 files. The singleton gives typed versions, which the compiler can type-check. | S | Optional cleanup. |
| `QQmlEngine::setExternalSingletonInstance`; `QML_UNCREATABLE` with `QML_SINGLETON` [WN] | stable | Singletons with `create()` factories: `Theme` (`theme.h:162`), `IconTheme` (`icon_theme.h:49`) and `StyleColours` (`style_colours.h:22`) | S | Skip unless the factories need it. |
| qmltypes "opaque" types, one warning per unknown type, `--only-explicit-categories` [WN] | stable | `hikari_ui_qmllint` (`tests/modules/CMakeLists.txt:16`) | S | Take it with the bump. Fix the new warnings. |
| Qt CanvasPainter (C++) [CP] [WN] | stable since 6.12 | Possible GPU paths for the painted Grid (`LineGrid : QQuickPaintedItem`, `src/ui/line_grid.h:34`, ADR 0003) and the audio display. The audio display rasterises with QPainter into textures (`src/ui/audio_display_item.cpp:200-206`, `:512-518`) and must match legacy pixels (`src/application/audio_spectrum.cpp:19`). | L | Later. It only pays off if a perf budget is missed. |
| Canvas2D QML element [C2D] | **TP** ("technology preview and is subject to change" [C2D]) | 10 `Canvas` items in 8 files. All are small: hatching (`Main.qml:3150`, `KanjiTimerDialog.qml:796`), a checkerboard (`AutomationDialog.qml:1223`), chevrons and arrows (`FormSection.qml:47`, `Main.qml:3548`), a tap chart (`TapCalibrationWindow.qml:100`, `:138`), a swatch (`StyleManager.qml:936`), a wave (`TemplateEditor.qml:518`) and the visual-tool overlay on video (`VisualOverlay.qml:89`). | M | Skip while it is TP. Re-check at 6.13. Retiring the wave Canvas is better done with underline styles. |
| StyleKit (`Qt.labs.StyleKit`, `QStyleKitStyle`) [SK] [BLOG] | **Labs** [LABS] | The owned theme system: 4 theme codes, including two high-contrast ones (`src/ui/theme.h:28`), per-theme accents and 16 roles, plus `HikariStyle` with 15 Fusion-based controls and `impl` imports (`src/ui/style/`). StyleKit could replace the style QML, but its API may change "without deprecation" [LABS]. | L | Later, as a prototype only. Not for 0.x releases. |
| `QtQuick.Controls.Native` [STYLES] | stable | A compile-time platform style. The app selects `HikariStyle` with a Fusion fallback at run time (`src/ui/theme.cpp:714-720`), and the docs say not to combine Native with other style imports [STYLES]. | — | Skip. It conflicts with the owned look. |
| QRangeModel `sort()`, `match()` and drag and drop [WN] [BLOG] | stable | QRangeModel is unused. The models are `LineTableModel`, `AutomationRuns` and `AutomationRunLines` (`QAbstract*Model`), plus JS arrays. The 5 JS `.sort()` calls sort small arrays. | — | Skip. |
| QML `SortFilterProxyModel` filters (Range, RegExp, AllOf, AnyOf) [WN] | stable | No QML SortFilterProxyModel is used | — | Skip. |
| Faster `QDateTime/QDate/QTime::fromString` [WN] | stable | 3 calls, all `Qt::ISODate` on cold paths: `application.cpp:3494`, `crash_report.cpp:342` and `whats_new.cpp:47`. Subtitle times are parsed by core code. | — | No action. |
| rcc content deduplication [WN] | stable, automatic | Resources: 304 icon SVGs (107,798 B) with **0** duplicate contents, plus one PNG, `hyph_en_US.dic` (106 KB), the karaoke presets (12 KB) and the translations | — | No action. The gain is about zero. |
| QRhi indirect draws, `ShaderDrawParameters` [WN] [QRHI] | stable (limited-compatibility API) | The presenter only uploads textures and uses a scene-graph material (`ycbcr_node.cpp`, `video_presenter.cpp`). It issues no draws of its own. | — | Skip. |
| Qt Graphs: log axis, CanvasPainter backend [WN] | stable; the backend is a Qt configure flag, so not in the official binaries [WN] | No charts. The waveform and spectrum are custom (ADR 0010). | — | Skip. |
| `QAudioSink::setNativePeriodFrameCount` [WN] | stable | Editor output is PortAudio (ADR 0010). Qt Multimedia only drives `QMediaPlayer` and `QAudioOutput` in `qt_general_player.cpp`. | — | Skip. |
| XdgSessionManager, linux-dmabuf v4, bradient button hints [WN] | stable | The app draws its own title bars and asks for client-side decorations (`window_decorations.cpp:201`) | — | No action. |
| Windows: Windows 11 25H2 constant, ICC colour space, DirectWrite GASP [WN] | stable | Shows up only as rendering shifts (2.3) | — | No action. |

## 4. Notes behind the table

### 4.1 Hot reload, in more detail

- On each save the debug service recompiles the changed document and diffs it against the running one
  [HOT]:
  - Edits to binding expressions and function bodies are patched in place.
  - Structural edits rebuild the enclosing component, keeping its root and the externally set state.
  - If the diff cannot be applied, `qmlpreview` restarts the app and replays the recorded input.
- `QMLPREVIEW_HOTRELOAD=0` restores 6.11 behaviour. Files compiled into resources are mapped back to
  sources through `--resource` [HOT] [QMLPREV].
- Two limits from [HOT] matter here:
  - Rebuilding a component loses the procedurally created objects inside it. The app makes dialogs with
    `createObject` (`VisualToolDialogs.qml:34`) and builds the dock layout through KDDockWidgets.
  - Replay is fragile with timing-dependent UIs.

### 4.2 Why StyleKit waits

The theme system already covers what StyleKit offers: light, dark, two high-contrast themes, accents
and a palette that follows the portal scheme (`src/ui/portal_colour_scheme.h`, `theme.cpp:480-626`).
Moving it means rewriting `HikariStyle`'s 15 controls on an API Qt may change without deprecation
[LABS]. The UI suites also pin those visuals.

### 4.3 The audio workaround

- `preferPulseAudioBackend()` (`src/app/composition.cpp:66-71`, called at `:182` and in
  `tests/ui/shell_tests.cpp:21918`) forces PulseAudio because "Qt 6.11's PipeWire backend crashes when a
  playing stream's sink goes away".
- The matching upstream bug, QTBUG-149181 ("QAudioSink PipeWire – spins or crashes when server died
  while playing"), is fixed in **6.12.0 RC** and 6.11.3 [J149181].
- QTBUG-147011 (a PipeWire crash on exit) is fixed in 6.11.2 and 6.12.0 Beta2 [J147011].
- Dropping the workaround needs the sink-removal case re-run on PipeWire.

## 5. Open questions for the owner

1. **Which kit to adopt:** 6.12.0 now, or 6.12.1 on its planned date of 2026-11-12 [WIKI]. 6.11.3
   (2026-10-26) is the low-risk intermediate if 6.12 qualification slips [WIKI611].
2. **KDDockWidgets:** keep 2.4.1 and prove it on 6.12, or move to 2.5.x as well. 2.5.x brings QtQuick
   dark mode and per-window layout saving, but has an ABI break and the ViewFactory source note
   [KDDW250] [KDDW251]. ADR 0017 requires migration and restore evidence for any engine upgrade.
3. **ToolTip timing:** accept 6.12's platform delay and 10 s+ timeout for the 26 files that rely on
   defaults, or set `ToolTip.policy: ToolTip.Manual` (or explicit values) to keep today's instant,
   persistent tooltips [TT].
4. **Nested popups:** should a click outside a Popover with an open ComboBox close both, as in 6.11? If
   so, `Popover.qml` and `ColourPopover.qml` add `Popup.CloseMultiple` [POPUP].
5. **SpinBox context menu:** keep Qt's new default menu, or suppress it as `TimeField`/`NumberField` do.
6. **Spelling marks:** keep background ranges, or switch to `SpellCheckUnderline`/`WaveUnderline` now
   that Qt Quick draws them. This conflicts with legacy parity if legacy differs.
7. **The PulseAudio workaround:** drop it after a PipeWire sink-removal re-test, or keep it through 0.x.
8. **Dev preset:** is a `QT_QML_DEBUG` development preset acceptable in `CMakePresets.json`, given that
   it must never feed a package (the release and package presets stay as they are)?
9. **Payload retention:** `docs/build.md:105` leaves retaining Qt payload archives as an unmade
   decision. Moving off 6.11.2 is when its online payloads start to matter for rollback (the policy keeps
   "the last qualified kit available", `docs/platform-policy.md:23`).
10. **Windows 10:** 6.12 keeps it supported [SP], so nothing is forced now. The next Qt minor (6.13)
    triggers the explicit retirement decision that ADR 0004 requires.

## Sources

- [WN]: https://doc.qt.io/qt-6/whatsnew612.html
- [RN]: https://code.qt.io/cgit/qt/qtreleasenotes.git/about/qt/6.12.0/release-note.md
- [RN113]: https://code.qt.io/cgit/qt/qtreleasenotes.git/about/qt/6.11.3/release-note.md
- [BLOG]: https://www.qt.io/blog/qt-6.12-released
- [HOT]: https://www.qt.io/blog/hot-reload-in-qt-6.12
- [WIKI]: https://wiki.qt.io/Qt_6.12_Release
- [WIKI611]: https://wiki.qt.io/Qt_6.11_Release
- [SP]: https://doc.qt.io/qt-6/supported-platforms.html
- [SP611]: https://doc.qt.io/qt-6.11/supported-platforms.html
- [REL]: https://doc.qt.io/qt-6/qt-releases.html
- [NC]: https://doc.qt.io/qt-6/newclasses612.html
- [REPO]: https://download.qt.io/online/qtsdkrepository/linux_x64/desktop/qt6_6120/qt6_6120/Updates.xml
- [REPOW]: https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_6120/qt6_6120_msvc2022_64/Updates.xml
- [PATCH]: https://download.qt.io/online/qtsdkrepository/all_os/qt_patchers/
- [INST]: https://download.qt.io/archive/online_installers/
- [LABS]: https://doc.qt.io/qt-6/qtlabs-modules.html
- [SK]: https://doc.qt.io/qt-6/qtlabsstylekit-index.html
- [CP]: https://doc.qt.io/qt-6/qtcanvaspainter-index.html
- [C2D]: https://doc.qt.io/qt-6/qtcanvas2d-qmlmodule.html
- [STYLES]: https://doc.qt.io/qt-6/qtquickcontrols-styles.html
- [ESC]: https://doc.qt.io/qt-6/qml-qtqml-qt.html#escapeHtml-method
- [TOHTML]: https://doc.qt.io/qt-6/qstring.html#toHtmlEscaped
- [QSFPM]: https://doc.qt.io/qt-6/qsortfilterproxymodel.html#dataChangeRelevanceFlags
- [TT]: https://doc.qt.io/qt-6/qml-qtquick-controls-tooltip.html
- [POPUP]: https://doc.qt.io/qt-6/qml-qtquick-controls-popup.html#closePolicy-prop
- [QDOM]: https://doc.qt.io/qt-6/qdomimplementation.html#InvalidDataPolicy-enum
- [QRHI]: https://doc.qt.io/qt-6/qrhi.html
- [AVI]: https://doc.qt.io/qt-6/qaccessibleviewportinterface.html
- [ITEM]: https://doc.qt.io/qt-6/qml-qtquick-item.html#mutabilityGroup-prop
- [MANI]: https://doc.qt.io/qt-6/cmake-target-property-qt-windows-app-project-execution-level.html
- [WDEP]: https://doc.qt.io/qt-6/windows-deployment.html#windows-application-manifest-generation
- [QMLPREV]: https://doc.qt.io/qt-6/qtqml-tooling-qmlpreview.html
- [QMLLINT]: https://doc.qt.io/qt-6/qtqml-tooling-qmllint.html
- [HDR]: headers diffed at `?h=v6.11.2` and `?h=v6.12.0`:
  - https://code.qt.io/cgit/qt/qtbase.git/tree/src/corelib/io/qzipwriter_p.h
  - https://code.qt.io/cgit/qt/qtbase.git/tree/src/gui/kernel/qhighdpiscaling_p.h
  - https://code.qt.io/cgit/qt/qtbase.git/tree/src/gui/kernel/qplatformwindow_p.h
  - https://code.qt.io/cgit/qt/qtbase.git/tree/src/gui/kernel/qplatformnativeinterface.h
  - https://code.qt.io/cgit/qt/qtbase.git/tree/src/gui/kernel/qwindowsysteminterface.h
  - https://code.qt.io/cgit/qt/qtbase.git/tree/src/gui/rhi/qrhi.h
  - https://code.qt.io/cgit/qt/qtbase.git/tree/src/plugins/platforms/wayland/qwaylandwindow_p.h
  - https://code.qt.io/cgit/qt/qtbase.git/tree/src/plugins/platforms/wayland/qwaylanddisplay_p.h
  - https://code.qt.io/cgit/qt/qtbase.git/tree/src/plugins/platforms/wayland/qwaylandshellsurface_p.h
  - https://code.qt.io/cgit/qt/qtbase.git/tree/src/plugins/platforms/wayland/qwaylandinputdevice_p.h
  - https://code.qt.io/cgit/qt/qtbase.git/tree/src/plugins/platforms/wayland/global/qwaylandclientextension.h
- [HDR-deco]: https://code.qt.io/cgit/qt/qtbase.git/tree/src/plugins/platforms/wayland/plugins/decorations?h=v6.12.0
- [HDR-fonts]: https://code.qt.io/cgit/qt/qtdeclarative.git/tree/examples/quickcontrols/wearable/WearableStyle/fonts?h=v6.12.0
- [HDR-dep]: https://code.qt.io/cgit/qt/qtbase.git/tree/src/corelib/kernel/qcoreapplication.h?h=v6.12.0 and
  https://code.qt.io/cgit/qt/qtdeclarative.git/tree/src/qmlintegration/qqmlintegration.h?h=v6.12.0
- [J149181]: https://qt-project.atlassian.net/browse/QTBUG-149181
- [J147011]: https://qt-project.atlassian.net/browse/QTBUG-147011
- [KDDW250]: https://github.com/KDAB/KDDockWidgets/releases/tag/v2.5.0
- [KDDW251]: https://github.com/KDAB/KDDockWidgets/releases/tag/v2.5.1
- [KDDW-skip]: https://github.com/KDAB/KDDockWidgets/commit/110d0da094 and
  https://github.com/KDAB/KDDockWidgets/commit/b4df4af738
- [VCPKGT]: https://learn.microsoft.com/en-us/vcpkg/users/triplets#vcpkg_env_passthrough
