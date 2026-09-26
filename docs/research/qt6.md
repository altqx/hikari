# Qt 6 today: releases, platforms, licensing and QML maturity

Research for [hikari#6](https://github.com/altqx/hikari/issues/6). Checked **2026-09-27**. This is evidence for later framework/build/rendering decisions, not an ADR. Documentation under `/qt-6/` currently describes 6.11.2 and will move; release/tag links below preserve the relevant context. No application benchmarks, packaging trials or assistive-technology tests were run for this desk study.

## Decision implications

Qt 6 supplies the requested desktop controls, item models, rendering integration, accessibility metadata and deployment tooling. It does not supply HikariSub's subtitle semantics, docking policy, keyboard conflict policy, frame-accurate media contract or tested accessibility by itself. The immediate decisions are a **pinned minor/patch**, an **upgrade policy**, and an explicitly tested OS/compiler matrix. An open-source project must not equate LTS with five years of free updates.

Two constraints deserve explicit downstream treatment: Qt says **6.12 is the last version supporting Windows 10**, and QRhi-facing rendering code requires version-coupled maintenance. Recommendation: prototype on an available stable release, evaluate 6.12 after its actual release, and record whether retaining Windows 10 means maintaining a frozen Qt line. This note does not choose that trade-off. [Platform matrix](https://doc.qt.io/qt-6/supported-platforms.html), [QQuickRhiItem](https://doc.qt.io/qt-6/qquickrhiitem.html).

## Releases and support

| Line | Verified status at research date | Dates and source |
|---|---|---|
| 6.11 | Latest stable patch **6.11.2** | 6.11.0 released **2026-03-23**; .1 May 13; .2 **Aug 18**. .3 is planned Sept 28, not yet realized. [Release plan](https://wiki.qt.io/Qt_6.11_Release), [release announcement](https://www.qt.io/blog/qt-6.11-released) |
| 6.8 | Current released LTS line | Initial release **2024-10-08**. Current support table lists **6.8.8, commercial only**, ending **2029-10-08**. [6.8 announcement](https://www.qt.io/blog/qt-6.8-released), [support table](https://doc.qt.io/qt-6/qt-releases.html) |
| 6.12 | Next LTS; prerelease | RC realized **2026-09-18**; final updated plan **2026-09-30**, realized column blank. Do not call it released because its original Sept 22 target passed. [Release plan](https://wiki.qt.io/Qt_6.12_Release), [next-LTS announcement](https://www.qt.io/blog/qt-6.12-beta-1-released) |
| 6.5 | Previous LTS, standard support ended | Standard support ended **2026-04-03**; extended security maintenance is a separate offering. [Qt notice](https://www.qt.io/blog/qt-6.5-reaches-end-of-support) |

Qt normally releases two minor versions annually with roughly 2–3 public patches until the following minor. Since 6.8, every fourth minor is LTS. Early LTS patches are public; immediate access to subsequent long-term patches is restricted to commercial customers. The documented one-year standard support and five-year LTS windows are **commercial support**, not an open-source maintenance guarantee. Public code availability at a later date does not guarantee timely updates for a frozen community build. [Qt release policy](https://doc.qt.io/qt-6/qt-releases.html), [LTS maintenance explanation](https://www.qt.io/blog/ensuring-product-longevity-with-qt-long-term-support).

The support table gives 6.11 standard support through **2027-03-17**, while its release announcement is March 23. Preserve the published date discrepancy rather than silently recalculating an entitlement. Treat the 6.12 plan as provisional, and derive its final support end only from the eventual published maintenance table. These distinctions matter for reproducible CI and security-upgrade planning.

## Supported desktop targets

This is Qt's **6.11 matrix**, not a promise that Hikari's other libraries work on the same combinations. [Official supported configurations](https://doc.qt.io/qt-6/supported-platforms.html).

| Target | Architecture | Documented toolchain / qualification |
|---|---|---|
| Windows 10, 1809+ | x64 | MSVC 2022 or MinGW-w64 13.1 |
| Windows 11 | x64 | MSVC 2022 or MinGW-w64 13.1 |
| Windows on ARM | ARM64 | MSVC 2022; ARM64EC is not supported |
| Ubuntu 22.04 | x64 | Canonical GCC 11.x |
| Ubuntu 24.04 | x64, arm64 | Canonical GCC 13.x; ARM desktop reference is Raspberry Pi 5/8GB |
| Ubuntu 26.04 | — | Not listed in this Qt 6.11 matrix; project testing is needed before promising support |
| Fedora, any release | — | Not listed; absence is not evidence that Qt cannot build or run there |
| macOS 13+ (including 26) | x64, x64h, arm64 | Xcode 15/macOS 14 SDK or later; Apple Clang toolchain |

The matrix's MSVC designation is the product generation, not a verified universal 19.xx patch requirement. Qt's [6.11 tools-and-versions inventory](https://wiki.qt.io/Qt_6.11_Tools_and_Versions) separately records actual CI/packaging jobs and distinguishes developer, test-only, and packaging configurations. Pin the full toolchain in Hikari CI; do not infer that an arbitrary Linux Clang version is officially supported because macOS uses Clang. For Fedora, choose explicit release/compiler jobs as a **Hikari-owned support commitment**, then prove dependencies and packaging there.

Binary portability is a separate constraint: x64 installer Linux binaries use glibc **2.34**; the ARM binaries use Ubuntu 24.04/glibc **2.39**. Older glibc targets require rebuilding Qt. Building on the newest distro and copying binaries to an older LTS is not a compatibility strategy. [Binary package notes](https://doc.qt.io/qt-6/supported-platforms.html#availability-of-packages).

## Licensing for GPLv3 HikariSub

The relevant open-source route is available without purchasing Qt. Qt's licensing page explicitly allows compliance under LGPLv3 **or GPLv3**. Inspect the shipped version's file licenses and dependency notices; a module's marketing availability does not replace those. [Qt licensing](https://doc.qt.io/qt-6/licensing.html).

| Category | Modules / tools relevant to evaluating scope | Consequence |
|---|---|---|
| LGPLv3 route available | [Core](https://doc.qt.io/qt-6/qtcore-index.html#licenses-and-attributions), [Gui](https://doc.qt.io/qt-6/qtgui-index.html#licenses-and-attributions), [Qml](https://doc.qt.io/qt-6/qtqml-index.html#licenses-and-attributions), [Quick](https://doc.qt.io/qt-6/qtquick-index.html#licenses-and-attributions), [Quick Controls](https://doc.qt.io/qt-6/qtquickcontrols-index.html#license-and-attributions), [Multimedia](https://doc.qt.io/qt-6/qtmultimedia-index.html#licenses-and-attributions) explicitly document LGPLv3 alongside other licensing choices | Suitable foundation for a GPLv3 application, with notices/source obligations respected. Their transitive dependency licensing still belongs in the final build manifest. |
| GPLv3-only open-source module choices (commercial alternative exists) | Canvas Painter, CoAP, Graphs, GRPC, HTTP Server, Lottie Animation, MQTT, Network Authorization, Qml Compiler, Quick 3D, Quick 3D Physics, Quick Timeline, Virtual Keyboard, Wayland Compositor | These are the current official non-LGPL list, not a list of mandatory dependencies. GPLv3 Hikari can evaluate them; a future proprietary reuse plan would change the analysis. [List](https://doc.qt.io/qt-6/licensing.html) |
| Open-source developer tools | Qt tools generally GPLv3 with Qt GPL exception 1.0; examples BSD-3-Clause | Do not confuse using a build tool with linking its implementation into the application. [Terms](https://doc.qt.io/qt-6/licensing.html) |
| Commercial-only optimization add-on | **Qt Quick Compiler Extensions**, including `qmlsc` | Optional; ordinary `qmlcachegen` and `qmltc` are available without it. Do not require an unavailable commercial compiler in community CI. [Compiler comparison](https://doc.qt.io/qt-6/qtqml-qtquick-compiler-tech.html) |

Qt's SBOM support starts at 6.8. The release artifact must cover third-party libraries independently, especially FFmpeg and any selected codecs; Qt's license does not grant all possible codec/patent rights. This is a dependency selection/release-manifest task, not a recommendation to buy a Qt or Figma subscription. [SBOM and third-party information](https://doc.qt.io/qt-6/licensing.html#software-bill-of-materials-sbom), [Multimedia attributions](https://doc.qt.io/qt-6/qtmultimedia-index.html#licenses-and-attributions).

## QML maturity: capabilities and boundaries

### Controls and desktop styling

Controls offers Basic, Fusion, Imagine, Material, Universal and platform styles. Current defaults are Fusion on Linux, Windows style on Windows, macOS style on macOS, plus mobile defaults. Platform-native-looking styles and cross-platform consistency are distinct choices. **FluentWinUI3** follows modern Windows design but can run on other supported platforms; it is not a WinUI application/runtime requirement. [Style selection](https://doc.qt.io/qt-6/qtquickcontrols-styles.html), [FluentWinUI3](https://doc.qt.io/qt-6/qtquickcontrols-fluentwinui3.html).

6.11 adds **Qt Labs StyleKit**, a declarative theme/style layer over Quick Templates with inherited properties, states and transitions. It is **Technology Preview**, excluded from compatibility promises. The same release adds `DoubleSpinBox`, default-button support in `DialogButtonBox`, and deprecates the old Labs Dialog in favor of QtQuick.Dialogs. Native Windows style cannot draw dark controls; its 6.11 fix forces a light palette, recommending Fusion/FluentWinUI3 for dark UI. [What's new](https://doc.qt.io/qt-6/whatsnew611.html), [StyleKit](https://doc.qt.io/qt-6/qtlabsstylekit-index.html), [tagged release notes at code.qt.io](https://code.qt.io/cgit/qt/qtreleasenotes.git/tree/qt/6.11.0/release-note.md).

Recommendation: make Hikari theme tokens/style choice replaceable, and compare density, disabled/focus/high-contrast states in QML/HTML prototype tickets. User policy is Figma Starter with occasional hand-off only. A StyleKit experiment must not accidentally become a mandatory production dependency before API/maintenance evaluation.

### TableView, TreeView and headers

`TableView` virtualizes and reuses delegates, supports row/column/cell selection through `ItemSelectionModel`, keyboard navigation, resizable geometry, editing delegates and model write-back. It does **not** implement application copy/paste or undo policy. Reused delegates must not own persistent row state. Recommendation: keep stable subtitle identity, selection, validation, sorting/filtering and edits in the C++ model/command layer; measure large documents and multiline text rather than treating virtualization as a benchmark. [TableView](https://doc.qt.io/qt-6/qml-qtquick-tableview.html).

`TreeView` (since 6.3) flattens a `QAbstractItemModel` hierarchy internally; view rows and source model indices are different. It provides expand/collapse and a styled delegate; by default double-click expansion takes precedence over double-click editing. `HorizontalHeaderView`/`VerticalHeaderView` synchronize with a table, taking header data or a separate model; movable columns exist in modern releases. Preserve column identifiers across reordering. [TreeView](https://doc.qt.io/qt-6/qml-qtquick-treeview.html), [HorizontalHeaderView](https://doc.qt.io/qt-6/qml-qtquick-controls-horizontalheaderview.html).

### RHI and custom rendering

`QQuickRhiItem` (since 6.7) is public and renders to an offscreen buffer composed into Quick. QRhi supports D3D11/12, Metal, Vulkan and OpenGL backends, avoiding an OpenGL-only viewport. However **QRhi, QShader and related classes offer no source/binary compatibility guarantee**; QRhi headers require `Qt::GuiPrivate`. Qt aims to limit incompatible changes to minor releases. Calling this an ordinary stable public QRhi API would be misleading. The item does not work with Quick's software scenegraph adaptation. [QQuickRhiItem](https://doc.qt.io/qt-6/qquickrhiitem.html), [QRhi](https://doc.qt.io/qt-6/qrhi.html).

GUI item state and renderer resources live on different threads; synchronize immutable/copyable state while the GUI thread is blocked, and own GPU resources on the render side. Test device loss, window recreation, resizing, DPR changes and renderer fallback. Recommendation: hide QRhi use behind a narrow Hikari rendering interface and pin/test every Qt upgrade. Whether subtitle pixels, video texture upload and waveforms should use this path remains a rendering decision, not established by API availability.

### Shapes and effects

`Shape.CurveRenderer` is a selectable shader-based path renderer that keeps curves smooth during zoom; GeometryRenderer remains the default RHI path and SoftwareRenderer is the software-backend option. CPU preprocessing still exists; asynchronous processing can move it off the GUI thread. Cubics may be approximated by quadratics. It is a candidate for editor handles/vector previews, not proof of ASS/libass fidelity. [Shape](https://doc.qt.io/qt-6/qml-qtquick-shapes-shape.html).

`QtQuick.Effects.MultiEffect` combines common shadow/blur/color/mask operations; it creates separate rendered output unless applied as a layer effect. Effects still incur texture/pixel work; blur and shadow are comparatively expensive, shader-changing options should not be animated, and inactive effects should be hidden. Prefer small, static effect surfaces; benchmark a full-screen blur before choosing it. [MultiEffect](https://doc.qt.io/qt-6/qml-qtquick-effects-multieffect.html).

### Compilation and module organization

Use `qt_add_qml_module` with an explicit URI and declared QML/resources/C++ types. It manages registration, metadata, resource packaging, lint targets and cache generation; avoid ad-hoc imports/files that deployment scanners cannot discover. `qmlcachegen` is an **internal build tool** invoked through the build system, not a command users should wire manually. [CMake module API](https://doc.qt.io/qt-6/qt-add-qml-module.html), [qmlcachegen](https://doc.qt.io/qt-6/qtqml-tool-qmlcachegen.html).

`qmltc` translates QML types to C++ for direct construction from C++. It remains **Tech Preview**, can reject unsupported QML, uses private Qt API, and generated C++ has no compatibility promise even between patch versions. It is not synonymous with the default script/cache compiler. Recommendation: start with normal module/cache tooling and typed, lint-clean QML; only adopt qmltc after measuring an actual object-creation bottleneck and budgeting version coupling. [qmltc](https://doc.qt.io/qt-6/qtqml-qml-type-compiler.html).

### Accessibility, DPI and multiple monitors

Quick controls provide a starting point for keyboard interaction. Custom items need `Accessible` role/name/description, state and equivalent accessibility actions; focus navigation and visual high contrast remain application responsibilities. A custom waveform needs useful semantic controls, not thousands of unlabelled render primitives. No source here proves Hikari works with NVDA, VoiceOver or Orca: prototype/test those independently, especially grid selection, text editing, menus and modal focus restoration. [Quick accessibility](https://doc.qt.io/qt-6/accessible-qtquick.html), [Accessible API](https://doc.qt.io/qt-6/qml-qtquick-accessible.html).

Quick uses device-independent geometry, with per-window DPR for native pixels. Qt6 defaults to Per-Monitor DPI Aware V2 on Windows. Windows virtual desktops can have gaps after scaling; enumerate actual screen geometries rather than assuming adjacent coordinates belong to a monitor. Low-level buffers and rendering must handle DPR changes. X11, Wayland and macOS scaling have different platform constraints; `QT_SCALE_FACTOR` helps testing but cannot replace mixed-monitor hardware tests. Recommendation: test 100/125/150/200%, dragging windows between displays, unplug/replug, restored offscreen geometry, and detached video/docks. [High DPI](https://doc.qt.io/qt-6/highdpi.html).

### Multimedia / FFmpeg

Qt Multimedia defaults to its FFmpeg backend on desktop; current Qt 6.11.2 documentation names **FFmpeg 7.1.3**. Installer binaries link dynamically; bundle compatible FFmpeg libraries or provide an OS dependency. Qt's deployment tools handle these except Linux/X11. Codec coverage and hardware acceleration vary by OS; a custom FFmpeg build must match the major version expected by Qt. [Qt Multimedia backend notes](https://doc.qt.io/qt-6/qtmultimedia-index.html#target-platform-and-backend-notes).

Playback/device APIs and `QVideoSink` do not establish exact seek/step, VFR/timebase, subtitle renderer or long-audio indexing behavior. Treat Qt Multimedia as a candidate consumer-facing playback layer; compare it against the media requirements and separate FFMS2/custom-decoder research. No latency, zero-copy, synchronization or frame-accuracy result was measured in this study.

## Deployment

`qt_generate_deploy_qml_app_script(TARGET ... OUTPUT_SCRIPT ...)` is used with `install(SCRIPT ...)` after target installation, follows standard install layout, and deploys discovered QML imports/plugins. It is a deployment staging mechanism, not an installer, signing or update system. Unsupported-platform suppression is not success: it may produce a no-op deployment. [Command reference](https://doc.qt.io/qt-6/qt-generate-deploy-qml-app-script.html).

**Linux documentation discrepancy resolved from source:** the prose currently mentions runtime dependency deployment only for Windows/macOS, but the **v6.11.2** implementation has a native Unix/non-Apple/non-Android/non-cross-compiling shared-Qt branch that calls both `qt6_deploy_qml_imports` and `qt6_deploy_runtime_dependencies`. Thus native Linux is supported in this implementation; cross-compiling/static cases need separate handling. [Tagged Qt6QmlMacros.cmake](https://code.qt.io/cgit/qt/qtdeclarative.git/tree/src/qml/Qt6QmlMacros.cmake?h=v6.11.2#n5240). Runtime deployment uses CMake's dependency scanning on Linux and normally excludes system library directories. [Runtime dependency command](https://doc.qt.io/qt-6/qt-deploy-runtime-dependencies.html).

| Platform | Practical release work remaining |
|---|---|
| Windows | Run `windeployqt` from the matching Qt kit or the CMake wrapper; supply the QML source directory for scanning; stage platform/image/media plugins, QML modules and compiler runtime. It does not discover every non-Qt library (libass, FFMS2, scripts, fonts). Inspect clean-machine launches without a development PATH, then package/sign separately. [Windows deployment](https://doc.qt.io/qt-6/windows-deployment.html) |
| Linux | Decide distro packages versus a bundle/container format. Bundle or declare the non-system Qt/QML/media/plugin dependencies, control RPATH and plugin/import locations, include XCB/Wayland dependencies, build against an appropriate oldest glibc baseline, and test both sessions. Linux staging does not automatically make one binary work on all distributions. [Linux deployment](https://doc.qt.io/qt-6/linux-deployment.html) |
| macOS | Use app-bundle layout and deployment tooling, then sign/notarize the assembled app and validate both selected architectures. Qt's support table does not validate Hikari's external codecs/renderer binaries. [Qt deployment overview](https://doc.qt.io/qt-6/deployment.html) |

## Required follow-on evidence

1. Framework/build ADR: exact Qt baseline, open-source upgrade cadence, Windows 10 end-of-support choice, supported compiler/distro matrix and private-API budget.
2. Rendering spike: real subtitle fixtures plus video and waveform, resize/DPR/device-loss, software/fallback behavior, measured upload/presentation costs.
3. Grid/input prototype: large documents, IME, multiline edits, selection/header reorder, keyboard shortcuts and assistive technology. User reacts to agent-built QML/HTML; Figma Starter only for occasional hand-off.
4. Packaging smoke test: clean Windows/Linux/macOS hosts, offline startup, plugins/QML/FFmpeg found, notices/source bundle and update strategy verified.

These are validation requirements, not missing research facts disguised as measured success. The research ticket is answered; architecture and runtime acceptance remain downstream decisions.
