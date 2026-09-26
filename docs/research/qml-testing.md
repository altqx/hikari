# Testing a QML desktop app: unit, UI automation, visual regression

Research ticket: [#22](https://github.com/altqx/hikari/issues/22). Feeds: Choose the test strategy for the rewrite.
Date: 2026-09-27. Status: source investigation complete; proposed CI matrix has not been executed.

## Answer

Recommendation: retain a fast independent core suite, add QtTest for QObject/model contracts and Qt Quick Test for QML component behavior, then a small native-desktop integration/accessibility suite and controlled visual fixtures. Prefer GoogleTest for a growing plain-C++ suite if a new framework is needed; preserving the current harness until a real need appears is also reasonable. A rewrite should not require every behavior to be exercised through screenshots or an entire application process.

Static QML checks, QML engine interaction, OS accessibility automation and rendering comparisons test different contracts. In particular, Squish and Spix can inspect Qt objects directly; they are not substitutes for checking the UIA/AT-SPI tree. offscreen is a platform plugin, while software scene-graph rendering and software RHI devices are separate choices. The following facts support a decision; they do not adopt one.

## Existing tests and build-tests

The inspected repository baseline is 20d647c4. tests/CMakeLists.txt is a standalone C++20 CMake project, independent of wxWidgets and the main application. It builds one hikarisub_tests executable and registers one CTest entry. Eight source files contain 63 TEST declarations covering blend math, frame queue, parsed-script helpers, playback state, versions, timebase, undo and waveform peaks. The custom check.h/check_main.cpp registry prints results and returns nonzero on failed checks; it is not GoogleTest, Catch2 or QtTest. [CMake](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/tests/CMakeLists.txt), [harness](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/tests/check.h), [runner](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/tests/check_main.cpp).

AssBlendTests compares the application's compositing routine with a reference formula and allows a rounding difference of two channel values. It does not run libass text layout or characterize ASS file parsing. The existing test list should not be reported as full subtitle-renderer or format coverage. [Blend tests](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/tests/AssBlendTests.cpp).

The workflow configures tests into build-tests, builds them and runs CTest on ubuntu-24.04. No tracked files exist under build-tests; it is generated output. Local C:/Work/Kainote/build-tests contains an older KainoteTests solution, kainote_tests targets and a cache pointing at C:/Work/Kainote/tests with Visual Studio 18 2026. The research worktree has no such directory. These observed local artifacts establish neither freshness nor a passing run. [Current CI commands](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/.github/workflows/build.yml).

For a fresh build the existing sequence is: cmake -S tests -B <new-build-dir>, cmake --build <new-build-dir> --config Release, then ctest --test-dir <new-build-dir> -C Release --output-on-failure. This research did not rerun the unchanged suite.

## Core framework comparison

| Candidate | Verified strengths | Tradeoff and suggested use |
| --- | --- | --- |
| GoogleTest / GoogleMock | Fixtures, fatal/nonfatal assertions, parameterization and integrated mocks; normal C++ targets. | Strong candidate for core contracts and controllable service boundaries. Avoid mocks of every internal call. |
| Catch2 v3 | Expression decomposition, sections and generators give compact case exploration; normal CMake integration. | Good plain-C++ alternative. v3 is a compiled library with split headers, so the old single-header argument is outdated. |
| QtTest | Data-driven cases, Qt-aware comparisons, signals/event-driven helpers and GUI input simulation. | Natural for Qt models, signals, queued delivery and adapters; need not force a Qt dependency into a pure domain core. |
| Existing harness | Small, no third-party dependency, already in CI. | Preserve current behavior tests; limited diagnostics/filtering/reporting can justify gradual migration. |

Sources: [GoogleTest primer](https://google.github.io/googletest/primer.html), [GoogleTest advanced features](https://google.github.io/googletest/advanced.html), [Catch2 design](https://catch2-temp.readthedocs.io/en/latest/why-catch.html), [Catch2 v3 migration](https://catch2-temp.readthedocs.io/en/latest/migrate-v2-to-v3.html), [QtTest overview](https://doc.qt.io/qt-6/qtest-overview.html), and the repository harness above. No compile-time, runtime or maintenance benchmark comparing these choices was performed.

Recommendation: keep parsing, time arithmetic, selection/edit transactions, undo, frame scheduling and cache ownership testable without a GUI. Add boundary tests for failure/cancellation and lifetime interactions instead of assertions that merely mirror private implementation structure.

## Characterization and rendering goldens

Recommended format fixtures should retain source bytes and expected parse/diagnostic behavior for ASS/SSA, SRT and other explicitly supported formats: encodings/BOMs, newline forms, malformed timestamps, comments, unusual field order, unknown tags, escapes, round trips and intentional loss. Compare semantic output where normalization is allowed and exact bytes where preservation is the contract. Characterization captures legacy behavior; questionable behavior needs an explicit decision before becoming the new specification.

For libass, use a controlled corpus and renderer configuration rather than hashing whatever fonts happen to be installed. The pinned libass compare tool already loads fonts from fixture directories with ASS_FONTPROVIDER_NONE and renders ASS files at timestamps encoded in PNG names. It supports bitwise or graded image comparison and saves results. That is direct prior art for hermetic subtitle goldens. [libass comparison tool](https://github.com/libass/libass/blob/4a05d8127f525943ebf45fdc6497c9e665947f0d/compare/README.md).

Recommended exact hashes should cover a normalized final RGBA image plus declared dimensions/stride handling, not pointer values, uninitialized padding or raw ASS_Image allocation layouts. Pin fonts/hashes, libass, FreeType, HarfBuzz, shaping options, frame/storage sizes, timestamps and compositing rules. Retain expected/actual/diff images for review. Treat dependency upgrades as explicit baseline reviews; do not let an agent update all goldens merely to make CI green. Cross-platform equality and appropriate tolerance remain empirical questions.

## QML component tests and compilation gates

Qt Quick Test runs tst_*.qml files through a C++ harness linked to Qt6::QuickTest. TestCase discovers test_ functions and supports a setup object for application/engine initialization. Use temporary service fakes with deterministic state for a component suite; reserve application startup tests for a smaller layer. [Quick Test](https://doc.qt.io/qt-6/qtquicktest-index.html).

TestCase supplies keyboard/mouse helpers, SignalSpy integration, tryCompare/tryVerify waits, warning controls and grabImage. Recommended checks include focus restoration, selection, shortcut enablement, model changes, disabled controls, dialog cancellation, and input-method composition guards. Wait for observable state/polish/render completion rather than arbitrary long sleeps; synthetic key events alone do not validate a native IME or screen reader. [TestCase API](https://doc.qt.io/qt-6/qml-qttest-testcase.html).

qmllint checks QML syntax/types and related diagnostics. Feed it generated type information and import paths; configure deliberate warnings as CI failures and narrowly explain suppressions. qt_add_qml_module generates per-module *_qmllint and aggregate all_qmllint targets, but these must be invoked. Its ordinary QML cache generation can fall back to bytecode when a function is not compiled to C++; that is not equivalent to failing all dynamic or invalid behavior. [qmllint](https://doc.qt.io/qt-6/qtqml-tooling-qmllint.html), [module CMake API](https://doc.qt.io/qt-6/qt-add-qml-module.html).

Do not make qmltc a blanket gate under the vague label "QML type compilation." Current Qt documentation calls it Tech Preview, notes private-API dependencies and compatibility limits even between patch versions, and lists unsupported cases. It generates classes requiring different construction code. Recommendation: use registered types, qmllint, normal cache compilation and runtime component loading as baseline gates; evaluate qmltc only for specifically chosen components with a pinned Qt version. [QML type compiler](https://doc.qt.io/qt-6/qtqml-qml-type-compiler.html).

## Whole-app automation and accessibility

| Tool | Actual access mechanism | Best-fitting investigation |
| --- | --- | --- |
| Squish for Qt | Toolkit-aware QObject properties, methods and Qt/QML object recognition, with custom QML extensions. | Commercial candidate when recording/object maps/support justify licensing; success does not prove OS accessibility. |
| Spix | Library linked into the app, named Qt scene/object paths, C++ or HTTP RPC commands, properties and screenshots. | Small opt-in test build for deterministic application flows; no need to ship its RPC server in release builds. |
| pywinauto UIA | Windows UI Automation backend and exposed accessibility controls. | Real UIA roles/names/actions/focus checks; inspect the actual Qt6 tree before committing to coverage. |
| dogtail | Python automation through AT-SPI; current upstream documents Xorg and GNOME Wayland support with injection/coordinate caveats. | Linux accessibility flows in a configured desktop session, not a generic offscreen replacement. |

Sources: [Squish Qt API](https://doc.qt.io/squish/how-to-use-the-qt-api.html), [QML extensions](https://doc.qt.io/squish/how-to-use-the-qml-extension-api.html), [Squish product/licensing entry](https://www.qt.io/quality-assurance/squish), [Spix source snapshot](https://github.com/faaxm/spix/blob/57cf7105e1250b999104d6439df7f449ba1afe93/README.md), [pywinauto guide](https://pywinauto.readthedocs.io/en/latest/getting_started.html), [dogtail upstream](https://gitlab.com/dogtail/dogtail).

Recommendation: use stable object names for in-process automation and useful accessible names/roles for users. Test a small keyboard-only open/edit/undo/save path on Windows and Linux, then independently inspect text/table semantics and run NVDA/Orca acceptance. A screen reader can fail despite a passing internal object-path test. Tool feasibility, runner reliability and custom-control tree coverage are unmeasured.

## CI and visual regression

Qt Quick Test documents -platform offscreen to avoid displaying a window. It can support fast component checks, but cannot establish a desktop window manager, native dialog, clipboard integration, IME candidate window or accessibility bus behavior. Those need separate native-session checks. [Quick Test running guidance](https://doc.qt.io/qt-6/qtquicktest-index.html).

Three settings must stay distinct:

- QT_QPA_PLATFORM=offscreen selects the platform integration.
- QT_QUICK_BACKEND=software selects Qt Quick's software adaptation. ShaderEffect and some other scene features are unsupported, so this is not a faithful test of GPU integrations. [Software adaptation](https://doc.qt.io/qt-6/qtquick-visualcanvas-adaptations-software.html).
- QSG_RHI_BACKEND selects an API such as d3d11, vulkan or opengl; "software" is not a documented value. QSG_RHI_PREFER_SOFTWARE_RENDERER=1 asks supporting APIs to choose a software device and is ignored otherwise. QSG_INFO=1 records the selected backend. [RHI controls](https://doc.qt.io/qt-6/qtquick-visualcanvas-scenegraph-renderer.html).

Recommended matrix: plain core on Windows/Linux every change; Qt adapter/component checks on both with one controlled offscreen configuration; Linux X11 virtual-display and Windows interactive-session smoke paths; periodic real target GPU/Wayland runs for video textures and scene-graph integration. D3D software-device tests and Linux software OpenGL/Vulkan tests require verified runner capabilities; no universal headless recipe was proven here.

For visual tests, fix Qt version, controls style, fonts, viewport, DPI/scale, locale, timezone, theme, clock, animation progress and data. Capture component images after completion and retain baseline/actual/diff artifacts. Use separate backend/OS baselines where measured rasterization differences justify them; prefer geometry/semantic assertions for behavior and images for appearance. Make baseline changes human-reviewable. QML/HTML design prototypes are for human reaction, with Figma Starter used only for occasional handoff; screenshots of HTML cannot validate Qt rendering or accessibility.

## MuseScore and Audacity 4 prior art

MuseScore main at 1c81f0a6 uses SetupGTest for its engraving suite, including score read/write, comparison helpers and many domain operations. Its vtest runner generates reference/current PNGs with separate application binaries and invokes comparison scripts. This is concrete unit-plus-artifact-comparison precedent, not evidence that every QML screen has visual coverage. [Engraving CMake](https://github.com/musescore/MuseScore/blob/1c81f0a6f3eeb1acff185b4b67569903faf1df36/src/engraving/tests/CMakeLists.txt), [visual runner](https://github.com/musescore/MuseScore/blob/1c81f0a6f3eeb1acff185b4b67569903faf1df36/vtest/vtest_gtest_runner.cpp).

Audacity's current Qt app at 36146d83 has module tests using SetupGTest, including track edit/navigation controllers and mocks, with Qt Quick dependencies where required. It also contains JavaScript testflow scripts using the application's dispatcher and testflow API. These are distinct from the legacy au3 test directories, and should not be summarized solely from older Audacity 3 testing advice. [Track-edit tests](https://github.com/audacity/audacity/blob/36146d838c934ae429cb164e8eb3d39af75a65ff/src/trackedit/tests/CMakeLists.txt), [testflow example](https://github.com/audacity/audacity/blob/36146d838c934ae429cb164e8eb3d39af75a65ff/share/testflowscripts/TC1.1_BasicTest.js).

The inspected Muse framework at 1d8529e7 builds module executables with GoogleMock, Qt Core/Gui and CTest; its test main creates QGuiApplication. Thus using GoogleTest there does not imply that its test executables are independent of Qt or need no platform setup. [gtest setup](https://github.com/musescore/muse_framework/blob/1d8529e782618391bf0187a2e34c85f499b0680e/framework/testing/gtest.cmake), [main](https://github.com/musescore/muse_framework/blob/1d8529e782618391bf0187a2e34c85f499b0680e/framework/testing/gmain.cpp).

## What the decision still needs

Choose one core framework or defer migration; specify required per-change gates versus periodic/native acceptance; pick a small end-to-end driver; define font/backend fixture pinning and visual-baseline ownership. Measure representative core and QML suite time on actual runners before assigning timing budgets. Add failure artifacts and deterministic reproduction commands before increasing test breadth. This research adds no production tests and claims no passing native, accessibility or renderer matrix.
