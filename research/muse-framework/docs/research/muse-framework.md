# Muse framework: structure, reuse costs and rendering paths

Research for [hikari#7](https://github.com/altqx/hikari/issues/7), checked **2026-09-27**. This answers the source/reuse question; it does not decide Hikari's framework. No build, binary-size, startup-time, memory or rendering benchmark was performed.

## Source snapshot and conclusion

The framework is split into **[musescore/muse_framework](https://github.com/musescore/muse_framework)**. Both applications use a `muse` Git submodule and a separate `muse_deps` dependency checkout. Audacity is a real second consumer, so third-party reuse is more than a hypothetical extraction from MuseScore. It remains a source-integrated application framework with application initialization, resources and dependency setup responsibilities, not a verified stable binary SDK.

| Inspected project | Immutable snapshot | Framework pin |
|---|---|---|
| MuseScore | [1c81f0a](https://github.com/musescore/MuseScore/tree/1c81f0a6f3eeb1acff185b4b67569903faf1df36), current source, not asserted to equal a release tag | [1d8529e](https://github.com/musescore/muse_framework/tree/1d8529e782618391bf0187a2e34c85f499b0680e) |
| Audacity | [36146d8](https://github.com/audacity/audacity/tree/36146d838c934ae429cb164e8eb3d39af75a65ff), current source | [b1b09fa](https://github.com/musescore/muse_framework/tree/b1b09fa0343a40f6c5a31e9663ed3ed1ab796c70) |

Framework details below use the **Audacity pin b1b09fa** unless specified. The two consumers are not pinned identically; do not assume a source change is deployed by both. The GitHub contents API for each `muse` entry and the applications' [MuseScore .gitmodules](https://github.com/musescore/MuseScore/blob/1c81f0a6f3eeb1acff185b4b67569903faf1df36/.gitmodules) / [Audacity .gitmodules](https://github.com/audacity/audacity/blob/36146d838c934ae429cb164e8eb3d39af75a65ff/.gitmodules) establish the submodule relationship. Older references to `src/framework` describe an earlier layout; current framework code is `muse/framework`.

## Module and dependency model

The [modularity ADR](https://github.com/musescore/muse_framework/blob/b1b09fa0343a40f6c5a31e9663ed3ed1ab796c70/docs/adr/00001-Modularity.md) defines functional modules with public interfaces/types, private `internal/` implementation and QML/view models. Applications include selected module targets and explicitly instantiate module setup objects. Modules are not automatically isolated process/plugin boundaries.

[`IModuleSetup`](https://github.com/musescore/muse_framework/blob/b1b09fa0343a40f6c5a31e9663ed3ed1ab796c70/framework/global/modularity/imodulesetup.h) separates export registration/import resolution, resources, UI types, API registration, pre-init/init/all-inited/delayed-init, app start, deinit and destruction. `IContextSetup` supplies per-context lifecycle. This gives an ordering protocol, but application construction must honor it; adopting the interface alone does not initialize services.

[`ioc.h`](https://github.com/musescore/muse_framework/blob/b1b09fa0343a40f6c5a31e9663ed3ed1ab796c70/framework/global/modularity/ioc.h) wraps `kors_modularity`, distinguishing global and context IoCs, with `GlobalInject`, `ContextInject` and thread-safe variants. QML objects obtain a context through `QmlIoCContext`/`iocCtxForQmlObject`. Compatibility aliases still exist. Interfaces are resolved through service containers rather than passed explicitly through every constructor. Benefit: replaceable module services and per-window/project contexts. Cost/inference: implicit dependencies and lifetime ordering require discipline, especially if Hikari's document/media lifetime differs from Muse's app/window assumptions.

### Actions and shortcuts

[`IActionsDispatcher`](https://github.com/musescore/muse_framework/blob/b1b09fa0343a40f6c5a31e9663ed3ed1ab796c70/framework/actions/iactionsdispatcher.h) registers `Actionable` clients against action codes or URI queries, dispatches optional typed action data, and publishes pre/post-dispatch channels. It does not make commands undoable automatically.

[`UiAction`](https://github.com/musescore/muse_framework/blob/b1b09fa0343a40f6c5a31e9663ed3ed1ab796c70/framework/ui/uiaction.h) adds UI/shortcut contexts, mnemonic title, description, icon, checkable status and parent/child action relationships. [`IUiActionsRegister`](https://github.com/musescore/muse_framework/blob/b1b09fa0343a40f6c5a31e9663ed3ed1ab796c70/framework/ui/iuiactionsregister.h) merges registered action modules and exposes enabled/checked states plus change channels. Menus/toolbars therefore share action metadata without knowing the action implementation.

The inspected [`ShortcutsRegister`](https://github.com/musescore/muse_framework/blob/b1b09fa0343a40f6c5a31e9663ed3ed1ab796c70/framework/shortcuts/internal/shortcutsregister.cpp) reads default/user XML, expands platform standard keys, merges user overrides and additional context shortcuts, writes overrides, and obtains context from UI actions. Merge logic retains default context/auto-repeat metadata rather than trusting old user records. A separate `shortcuts_v2` exists, selected by a flag currently defaulting OFF; neither dialect is Hikari's existing G/S/E/V/A hotkey format. Reuse still needs a one-shot legacy importer, conflict policy and dynamic automation command identity.

### Keyboard navigation

Navigation is explicitly hierarchical: [`NavigationSection`](https://github.com/musescore/muse_framework/blob/b1b09fa0343a40f6c5a31e9663ed3ed1ab796c70/framework/ui/qml/Muse/Ui/navigationsection.h) groups panels and is Regular, Exclusive or Ignore; [`NavigationPanel`](https://github.com/musescore/muse_framework/blob/b1b09fa0343a40f6c5a31e9663ed3ed1ab796c70/framework/ui/qml/Muse/Ui/navigationpanel.h) groups controls with horizontal/vertical/both direction. Both expose order/index, enabled/active state, visual item/window and activation requests. Controls participate in focus/highlighting and accessibility semantics rather than relying solely on incidental QML child order.

This is reusable structure for grid/editor/audio/video regions and modal panels. It is not proof of screen-reader correctness: Hikari must define where Tab/arrows are navigation versus content editing, preserve focus across document/dock changes, and test assistive technology. The [`FlatButton`](https://github.com/musescore/muse_framework/blob/b1b09fa0343a40f6c5a31e9663ed3ed1ab796c70/framework/uicomponents/qml/Muse/UiComponents/FlatButton.qml) shows a concrete control connected to navigation and accessible role/name/description.

### Docking and workspaces

[`dockwindow/CMakeLists`](https://github.com/musescore/muse_framework/blob/b1b09fa0343a40f6c5a31e9663ed3ed1ab796c70/framework/dockwindow/CMakeLists.txt) builds a bundled KDDockWidgets Qt Quick backend with custom dock components. The [`dockwindow_v2`](https://github.com/musescore/muse_framework/blob/b1b09fa0343a40f6c5a31e9663ed3ed1ab796c70/framework/dockwindow_v2/CMakeLists.txt) alternative gets KDDockWidgets through dependency machinery. Both provide Muse wrappers for pages/panels/toolbars/floating windows plus title bars, tab bars, separators and commands. The version selector defaults to the original layer in the inspected options; do not identify it with arbitrary upstream KDDockWidgets HEAD.

[`WorkspaceManager`](https://github.com/musescore/muse_framework/blob/b1b09fa0343a40f6c5a31e9663ed3ed1ab796c70/framework/workspace/internal/workspacemanager.cpp) loads/defaults, switches, creates/clones and saves workspaces with before/after change notifications. [`WorkspaceFile`](https://github.com/musescore/muse_framework/blob/b1b09fa0343a40f6c5a31e9663ed3ed1ab796c70/framework/workspace/internal/workspacefile.cpp) uses a ZIP container with `META-INF/container.xml`, metadata XML and named payloads. This is UI customization persistence, not a substitute for Hikari's subtitle files, autosaves or multi-tab session format. Preserve those external contracts separately.

### Theme and visual components

Theme values are exposed through the UI engine/API and consumed as semantic roles from QML components. [`UiConfiguration`](https://github.com/musescore/muse_framework/blob/b1b09fa0343a40f6c5a31e9663ed3ed1ab796c70/framework/ui/internal/uiconfiguration.cpp) reads application-supplied `:/configs/ui.cfg` and theme resources, follows OS theme optionally, supports light/dark/high-contrast choices, and stores font/theme preferences. [`ThemeApi`](https://github.com/musescore/muse_framework/blob/b1b09fa0343a40f6c5a31e9663ed3ed1ab796c70/framework/ui/api/themeapi.h) supplies roles; this is a framework styling approach, not merely enabling Qt's built-in platform style. `UiTheme` in a design discussion should refer to this concrete configuration/API chain, not assume a single independent `UiTheme` library.

Reusable controls such as FlatButton and [`StyledDialogView`](https://github.com/musescore/muse_framework/blob/b1b09fa0343a40f6c5a31e9663ed3ed1ab796c70/framework/uicomponents/qml/Muse/UiComponents/StyledDialogView.qml) also depend on navigation, icon codes, UI configuration and interaction conventions. Copying their visual file alone loses those services. Hikari should evaluate tokens/density and semantics in agent-built **QML or HTML prototypes**, keeping Figma **Starter**, occasional hand-off only, as the user requested.

### Interactive service and settings

[`IInteractive`](https://github.com/musescore/muse_framework/blob/b1b09fa0343a40f6c5a31e9663ed3ed1ab796c70/framework/interactive/iinteractive.h) centralizes message dialogs, file/folder/color selection, progress, URI opening/raising/closing and current-window/URI stack state. Most operations have promise-based forms; sync forms also exist. [`IInteractiveUriRegister`](https://github.com/musescore/muse_framework/blob/b1b09fa0343a40f6c5a31e9663ed3ed1ab796c70/framework/interactive/iinteractiveuriregister.h) maps URIs to primary pages, QML dialogs or QWidget dialogs. This helps controllers avoid concrete windows, but requires app-specific URI registration and careful async object lifetime.

[`Settings`](https://github.com/musescore/muse_framework/blob/b1b09fa0343a40f6c5a31e9663ed3ed1ab796c70/framework/global/settings.h) is a singleton registry of module/key, current/default value, descriptions, optional editable ranges, change channels and transactions. Shared writes synchronize instances; local writes persist without inter-instance synchronization. Its interface minimizes Qt dependency but the implementation uses QSettings for compatibility. Adopting it does not magically read Hikari config files, nor does a settings transaction imply a durable document undo transaction.

## Can Hikari consume it?

**Yes as GPL-compatible source integration; a low-coupling drop-in SDK is unproven.** Framework file headers and [LICENSE.txt](https://github.com/musescore/muse_framework/blob/b1b09fa0343a40f6c5a31e9663ed3ed1ab796c70/LICENSE.txt) specify GPL-3.0-only. This aligns with GPLv3 Hikari, subject to preserving licenses/source obligations and separately checking vendored dependencies and assets. The MuseScore CLA notice is not evidence that a downstream GPL user must sign a CLA to use the code; contribution terms are a separate question.

[`muse_create_module`](https://github.com/musescore/muse_framework/blob/b1b09fa0343a40f6c5a31e9663ed3ed1ab796c70/framework/cmake/MuseCreateModule.cmake) creates real library targets/aliases, but adds project/app include locations, generated configuration, framework/global headers, PCH and global linkage. Module options can disable components and select stubs, with hard-dependency rules in [`MuseSetupConfiguration`](https://github.com/musescore/muse_framework/blob/b1b09fa0343a40f6c5a31e9663ed3ed1ab796c70/framework/cmake/MuseSetupConfiguration.cmake). Defaults enable many modules irrelevant to subtitles. Disabled modules do not guarantee zero code or QML: stubs may remain.

Audacity's [root build](https://github.com/audacity/audacity/blob/36146d838c934ae429cb164e8eb3d39af75a65ff/CMakeLists.txt) sets the framework path, chooses modules, includes setup files, and adds framework source before app source. It turns Muse audio/MIDI/MPE/MuseSampler off while keeping UI/actions/shortcuts/workspace/accessibility and its own audio core. This is persuasive evidence that selective reuse works, but also shows the host must supply significant integration.

Standalone [framework CMake](https://github.com/musescore/muse_framework/blob/b1b09fa0343a40f6c5a31e9663ed3ed1ab796c70/CMakeLists.txt) selects C++20 and checks Clang 20, AppleClang 21, GCC 14 and MSVC 19.40. [`SetupQt6`](https://github.com/musescore/muse_framework/blob/b1b09fa0343a40f6c5a31e9663ed3ed1ab796c70/buildscripts/cmake/SetupQt6.cmake) requires Qt6.8 and opts into policies through6.10. Those are this framework pin's build checks, not Qt's minimum platform compiler matrix. Audacity has its own setup and an old `QT_MIN_VERSION` variable; do not infer the true resolved requirement from that variable alone.

No stable API/ABI compatibility commitment or independently installed SDK consumption contract was established in inspected files. Recommendation: a small integration spike is required before choosing adoption. Record upstream pin/update ownership and avoid following upstream main blindly.

## Quantified source footprint and dependencies

Counts are reproducible from the GitHub recursive tree API for **b1b09fa**, counting `type=blob` and summing `size`. They include resources/tests; they are **not installed size, lines of code, compiled size or transitive download size**.

| Scope | Files | Bytes |
|---|---:|---:|
| Entire framework repo, all blobs | 3,075 | 25,852,238 |
| `framework/`, excluding paths containing `/thirdparty/` | 2,152 | 13,330,946 |
| `framework/` paths containing `/thirdparty/` | 778 | 11,732,959 |
| First-party-side `.cpp/.h/.qml/.mm/.c` under `framework/`, excluding thirdparty | 1,967 | 7,874,050 |

There are 37 immediate directories under `framework/`, including infrastructure/stubs/versioned alternatives, not 37 independent deployable services. Third-party source includes original KDDockWidgets, FluidSynth, FFmpeg material, moodycamel/stb, kors async/logger/modularity/msgpack/profiler/RPC helpers and sg14. External `muse_deps` content and Qt are **excluded** from these totals. [Tree snapshot](https://github.com/musescore/muse_framework/tree/b1b09fa0343a40f6c5a31e9663ed3ed1ab796c70/framework).

The inspected [external-dependency manifest](https://github.com/musescore/muse_framework/blob/b1b09fa0343a40f6c5a31e9663ed3ed1ab796c70/buildscripts/cmake/ExtDepsManifest.cmake) names **22 distinct requirement identifiers**: unconditional zlib, picojson, pugixml, utfcpp; optional vst3sdk, lodepng, freetype, harfbuzz, libpng, msdfgen, kddockwidgets, asiosdk, ogg, fdk-aac, flac, lame, opus, opusenc, pipewire, googletest, plus crashpad handler/client when enabled. Only four are unconditional here. This is one manifest, not a full dependency-closure count. Feature choices, OS, tests and transitive dependencies change the actual result.

The Qt setup requests Core, Gui, Widgets, Network, Qml, Quick, QuickControls2, QuickWidgets, Xml, Svg, ShaderTools and Core5Compat (12); non-web adds PrintSupport, Linux adds DBus, and options add Concurrent, LinguistTools, NetworkAuth or WebSockets. Even the global module links UI-related Qt libraries when Qt support is on. [`SetupDependencies`](https://github.com/musescore/muse_framework/blob/b1b09fa0343a40f6c5a31e9663ed3ed1ab796c70/buildscripts/cmake/SetupDependencies.cmake) can clone/fetch the pinned muse_deps checkout for standalone builds; app consumers supply their own. Pin/offline-cache this machinery in any Hikari experiment rather than introducing uncontrolled build downloads.

## Audacity's current state and cadence

As of this research date, **Audacity 4.0.0 is released**, published **2026-09-03** and marked non-prerelease in GitHub. Treat old alpha-labelled handbook pages as stale where they conflict with release records. The application is a Qt/QML frontend over substantial retained `au3` functionality, not a complete rewrite of every audio algorithm. [4.0.0 release](https://github.com/audacity/audacity/releases/tag/Audacity-4.0.0), [official changelog](https://www.audacityteam.org/changelog/), [4.0 overview](https://www.audacityteam.org/audacity-4/).

Observed GitHub publication dates: 4.0 alpha2 **2025-11-03**, beta2 **2026-06-11**, beta4 **2026-08-28**, 4.0.0 **2026-09-03**; maintenance 3.7.8 **2026-06-11** and3.7.9 **2026-09-01**. These illustrate overlapping maintenance and prerelease development; they do not establish a guaranteed monthly/quarterly release cadence. [Release history](https://github.com/audacity/audacity/releases). No future release date is asserted.

## Performance-critical views: what source actually does

| View | Concrete path | Implication |
|---|---|---|
| MuseScore notation | [`AbstractNotationPaintView`](https://github.com/musescore/MuseScore/blob/1c81f0a6f3eeb1acff185b4b67569903faf1df36/src/notationscene/qml/MuseScore/NotationScene/abstractnotationpaintview.h) derives from `QuickPaintedView`; its [`paint(QPainter*)`](https://github.com/musescore/MuseScore/blob/1c81f0a6f3eeb1acff185b4b67569903faf1df36/src/notationscene/qml/MuseScore/NotationScene/abstractnotationpaintview.cpp#L727) wraps the painter with `muse::draw::Painter` and calls notation drawing plus overlays | Custom C++ painter view composed into QML; not evidence that each note is a QML item or custom GPU node. |
| Muse wrapper at MuseScore's actual pin | [`QuickPaintedView`](https://github.com/musescore/muse_framework/blob/1d8529e782618391bf0187a2e34c85f499b0680e/framework/uicomponents/qml/Muse/UiComponents/quickpaintedview.cpp) derives from `QQuickPaintedItem`; it adjusts texture size/smoothing then calls base `updatePaintNode` | An override returning `QSGNode*` is not itself a bespoke scenegraph geometry renderer. |
| Audacity waveform | [`WaveView`](https://github.com/audacity/audacity/blob/36146d838c934ae429cb164e8eb3d39af75a65ff/src/projectscene/view/tracksitemsview/waveview.h) directly derives `QQuickPaintedItem`; [`paint`](https://github.com/audacity/audacity/blob/36146d838c934ae429cb164e8eb3d39af75a65ff/src/projectscene/view/tracksitemsview/waveview.cpp#L190) calls `IWavePainter` | C++ QPainter path inside the Qt Quick frontend. |
| Waveform detail modes | [`WavePainterProxy`](https://github.com/audacity/audacity/blob/36146d838c934ae429cb164e8eb3d39af75a65ff/src/projectscene/view/tracksitemsview/wavepainterproxy.cpp) selects connected dots, min/max/RMS or samples. [`MinMaxRMSPainter`](https://github.com/audacity/audacity/blob/36146d838c934ae429cb164e8eb3d39af75a65ff/src/projectscene/view/tracksitemsview/au3/minmaxrmspainter.cpp) reaches au3 project/track/clip data and delegates to `WaveformPainter` | The UI is paired with domain-specific audio representations; the framework alone does not provide Hikari waveform/spectrogram semantics. |
| Audacity clip/track chrome | [`tracksitemsview`](https://github.com/audacity/audacity/tree/36146d838c934ae429cb164e8eb3d39af75a65ff/src/projectscene/qml/Audacity/ProjectScene/tracksitemsview) contains TracksItemsView, TrackClipsContainer, ClipItem, timeline/selection/cursor/handles | QML composes controls around painted content. This directory inventory does not prove all visual layers use one renderer. |

These are verified inheritance/call-chain facts at named source snapshots. They establish neither throughput nor suitability for Hikari's workload, and current HEAD behavior must not be attributed wholesale to every released 4.x binary. Hikari may choose another renderer behind the same application architecture. Adoption of Muse does not require copying these rendering choices.

## Decision options and acceptance evidence

| Option | Benefit / expected cost (inference) | Evidence required before choosing |
|---|---|---|
| Adopt selected Muse modules | Existing actions, focus/navigation, styled controls, dialogs, workspace/docks; pays initialization/build/service coupling and upstream update work | Minimal Hikari shell without audio/cloud/score services; two documents, detached video, saved layouts, shortcut conflicts, a custom grid and accessible modal flow; report resolved dependencies/build size |
| Build a Hikari app layer atop Qt | Domain-specific lifetimes and less imported framework surface; owns focus, actions, settings, docking integration and reusable controls | Equivalent interaction prototype and explicit responsibility list; measured implementation/maintenance cost rather than “small framework” assertion |
| Reuse patterns/components selectively | Can adapt proven concepts without wholesale integration; extraction still brings licenses and hidden service dependencies | Dependency audit per copied component; keep origin/license, avoid an undocumented drifting fork |

For every option, test actual large subtitle documents, IME/text input, keyboard regions, assistive technology and mixed-DPI floating windows. The research is complete; adoption, rendering benchmarks and the user's prototype reaction remain separate decisions.
