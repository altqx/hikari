# Stateless CMake builds: dependency manifests, Qt install, patched forks, CI

Research for [#17](https://github.com/altqx/hikari/issues/17), feeding [#24](https://github.com/altqx/hikari/issues/24). Completed 2026-09-27 against HikariSub baseline `20d647c4c769ab7f5d383cf3c1c33f03876a94e9`. Source-based comparison; no clean-machine builds, cache-speed benchmarks or arm64 builds were performed. Recommendations are not decisions.

## Answer

A pinned vcpkg manifest plus repository-owned overlay recipes is a good default candidate for the native dependency graph. A pinned Qt binary installation can reduce local bootstrap work; building Qt through vcpkg provides a more uniform recipe graph at greater cold-build cost. Conan 2 is credible if maintaining Python recipes and a package remote is preferable. FetchContent/CPM and submodules are useful supplements, but they do not remove the recipe work needed for FFmpeg, LuaJIT, libass and the legacy optional plugin.

Clarify the promise: `cmake --preset name` configures; it does not build. Use `cmake --build --preset name` afterward, or a configure/build/test workflow invoked with `cmake --workflow --preset name` (workflow presets require CMake 3.25+). A fresh machine still needs a compiler/SDK and bootstrap prerequisites. A preset cannot itself install Visual Studio. [CMake CLI](https://cmake.org/cmake/help/latest/manual/cmake.1.html), [presets](https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html).

## Existing inventory and correctness hazards

The Windows manifest pins archives with SHA-256: FriBidi 1.0.16, Boost 1.91.0, ICU 78.3, zlib 1.3.2, curl 8.20.0 and a Gyan FFmpeg 7.1.1 developer bundle. These are observed repository pins, not recommendations for current versions. Other libraries are submodules or checked-in source. Linux currently uses distro packages through pkg-config for libass, FFMS2, LuaJIT, Hunspell, uchardet, curl, ICU, FFmpeg and fontconfig; it separately requires a pinned wxWidgets build. [Manifest](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/Thirdparty/dependencies.json), [current CMake](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/CMakeLists.txt), [submodules](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/.gitmodules).

The FFMS2 pin is `45d5f72100d88c52acdd54bfedcc0315a44c735d`; the xy-VSFilter pin is `c4297ed033ab899c86023543bd65ff08c7ba20a3`. FFMS2's extra API definitions are in HikariSub's `Thirdparty/Build/FFMS2/indexing_additional.cpp`, not fully contained by the fork. A recipe must package both parts at compatible revisions. Linux currently compiles against the fork's private class header but links distro FFMS2: matching public version names alone do not guarantee that private ABI. Prefer building the complete fork or removing the private-header dependency. [Patch ledger](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/Thirdparty/PATCHES.md).

The manifest's sentence saying GPL FFmpeg is “required” because the application is GPL is not established by this research. Codec/features and redistribution obligations should be recorded explicitly rather than deriving a codec build configuration from the application's license name.

## Comparison

| Strategy | Pins and patched forks | Binary caching / CI | Main tradeoff |
|---|---|---|---|
| vcpkg manifest | Pin vcpkg checkout, registry baseline, features and overlay recipes. Overlay can replace or add a port; fork URL/commit/hash and patches live in source control. | Built-in ABI-based binary cache; use a files cache with Actions storage or NuGet/GitHub Packages. Pin toolchain and triplets. | Good CMake integration; custom fork recipe still required; baseline is not a lock of all external machine inputs. |
| Conan 2 | Versioned recipes plus revisions, lockfile and explicit build/host profiles. Export a project-owned FFMS2 recipe that includes local implementation sources. | Remote binary packages or `conan cache save/restore` artifacts; package IDs encode settings/options under recipe rules. | Strong cross-build model, extra Python/tool/remote maintenance; lockfile does not replace compiler/profile pinning. |
| FetchContent / CPM | Full git commits or archive URL + hash; explicit patch steps; CPM can generate a package lock and source cache. | Primarily source caching plus compiler caching. No general binary-package ABI cache supplied by CPM. | Simple for cooperative CMake subprojects; non-CMake dependencies need ExternalProject/custom recipes. |
| Submodules + add_subdirectory | Gitlink pins exact commit; fork delta remains reviewable. Must initialize submodules or bootstrap them. | Source checkout plus compiler cache; no native reusable binary-package graph. | Excellent editable source ownership; only works directly when upstream supports embedded CMake builds. |
| Hybrid | One owner per dependency: e.g. vcpkg native graph, Qt SDK pin, submodule/overlay for project forks, FetchContent for tiny build-only tools. | Cache each layer with its own identity. | Avoid installing two conflicting copies of zlib/Freetype/Qt through different managers. |

Sources: [vcpkg manifest](https://learn.microsoft.com/en-us/vcpkg/concepts/manifest-mode), [overlays](https://learn.microsoft.com/en-us/vcpkg/concepts/overlay-ports), [binary cache](https://learn.microsoft.com/en-us/vcpkg/reference/binarycaching), [GitHub integration](https://learn.microsoft.com/en-us/vcpkg/github-integration), [Conan lockfiles](https://docs.conan.io/2/tutorial/versioning/lockfiles.html), [Conan cross contexts](https://docs.conan.io/2/tutorial/consuming_packages/cross_building_with_conan.html), [Conan cache transport](https://docs.conan.io/2/devops/save_restore.html), [FetchContent](https://cmake.org/cmake/help/latest/module/FetchContent.html), [CPM documentation](https://github.com/cpm-cmake/CPM.cmake).

A vcpkg toolchain can install a manifest automatically during configure and bootstrap the executable when its source checkout exists. Set toolchain/triplet variables before `project()`. It does not magically provide a missing checkout: use a documented prerequisite, pinned bootstrap script or configure-time downloader before including the toolchain. [CMake integration](https://learn.microsoft.com/en-us/vcpkg/users/buildsystems/cmake-integration).

## Mapping the dependency inventory

These are proposed ownership rules, not a tested manifest. Port availability was sampled at vcpkg `ee6a47dac031930715bea2e6dfb9ad60caaa5d83`; FFMS2 had no `ports/ffms2/vcpkg.json` at inspection, so budget a custom recipe rather than assuming a stock port.

| Dependency | Recipe/compatibility requirement |
|---|---|
| FFmpeg | Pin version, codec/features, linkage and artifact provenance; a Windows x64 developer ZIP is not an arm64 solution. Ensure FFMS2 and playback backend use a compatible single FFmpeg ABI. |
| FFMS2 fork | Overlay/Conan recipe or dedicated CMake wrapper; pin fork plus HikariSub extra implementation; compile as one compatible unit. |
| libass | Preserve provider choice and shaping dependencies. Current vcpkg recipe depends on Freetype, FriBidi, HarfBuzz and fontconfig on Linux. |
| Freetype / HarfBuzz / FriBidi | Pin together with libass and explicit feature choices. FriBidi's release archive contains generated tables absent from the raw git tag, per repository inventory. |
| ICU | Pin runtime/data configuration and build host tools separately when cross compiling. Existing archive only uses common/i18n, which may differ from a manager's default feature set. |
| Hunspell / uchardet | Standard package candidates; match imported targets and runtime dictionary/encoding data behavior. |
| LuaJIT | Pin an exact rolling-release commit; its supported architecture and host build tools must be checked in the recipe. Package metadata permitting arm64 is not an actual build result. |
| curl / Boost / zlib | Pin only needed features/components; audit duplicate transitive copies and Windows runtime selection. |
| xy-VSFilter | Optional Windows plugin with its own fork and legacy build graph. Keep out of Linux/default required graph; arm64 feasibility is unproven. Packaging needs matching plugin architecture. |

[libass recipe](https://github.com/microsoft/vcpkg/blob/ee6a47dac031930715bea2e6dfb9ad60caaa5d83/ports/libass/vcpkg.json), [LuaJIT recipe](https://github.com/microsoft/vcpkg/blob/ee6a47dac031930715bea2e6dfb9ad60caaa5d83/ports/luajit/vcpkg.json), [registry tree](https://github.com/microsoft/vcpkg/tree/ee6a47dac031930715bea2e6dfb9ad60caaa5d83/ports). Availability does not prove the selected versions/features build on every triplet.

## Qt provisioning

| Approach | Pin/reproducibility story | Cost / caveat |
|---|---|---|
| vcpkg Qt | Pin registry plus qtbase/qtdeclarative and required module features, host tools, triplet and compiler. Source recipes and cache identities are reviewable. | Cold Qt build can be large; qtbase alone does not provide QML/Quick. Actual duration is unmeasured. |
| aqtinstall | Pin Python package version, Qt version, host/target architecture and module list; cache the installed SDK under that full identity. It selects downloadable Qt SDK archives. | Third-party installer, not the Qt Company's official installer. Archive availability and upstream retention matter; preserve archive digests/mirror for long-term reproduction. |
| Official online installer | CLI supports explicit packages, install roots and unattended options. Pin installer version/checksum and exact repository package IDs. | Current online repository contents, account/license flow and package availability remain external inputs; a generic “latest Qt” install is not reproducible. |

Sources: [Qt vcpkg recipe](https://github.com/microsoft/vcpkg/blob/ee6a47dac031930715bea2e6dfb9ad60caaa5d83/ports/qtbase/vcpkg.json), [aqt CLI](https://aqtinstall.readthedocs.io/en/latest/cli.html), [official installer CLI](https://doc.qt.io/qtinstallerframework/ifw-use-cases-cli.html). Qt license eligibility and redistributable modules are separate checks for the selected distribution; this report does not choose a license.

## Windows arm64

Use an x64 build host and arm64 target toolchain/SDK, with target and host packages separated. vcpkg exposes `VCPKG_TARGET_TRIPLET` / `VCPKG_HOST_TRIPLET`; Conan uses host/build profiles. Qt's Windows source-build instructions document cross compilation and host Qt tools. aqt supports Windows arm64 package selection, but query availability for the exact Qt version instead of assuming every module exists. [Triplets](https://learn.microsoft.com/en-us/vcpkg/users/triplets), [Conan cross build](https://docs.conan.io/2/tutorial/consuming_packages/cross_building_with_conan.html), [Qt Windows builds](https://doc.qt.io/qt-6/windows-building.html), [aqt CLI](https://aqtinstall.readthedocs.io/en/latest/cli.html).

Blocking proof points are FFmpeg assembly/tooling, LuaJIT host build generation, optional VSFilter, and a complete arm64 runtime deployment. An arm64 configure preset is not evidence of an arm64 runnable application.

## Preset and CI proposal

Commit `CMakePresets.json` with a hidden common base and explicit `windows-x64-debug/release`, `linux-x64-debug/release`, `windows-arm64-release` presets. Use distinct `out/build/${presetName}` directories; set generator, build type/configuration, toolchain, host/target triplets, optional-plugin flags and test options explicitly. Add matching build/test presets and configure-build-test workflows. Keep machine paths in ignored `CMakeUserPresets.json`; do not commit developer absolute paths. Conan's generated toolchain/presets should integrate through its documented generated-user-preset mechanism, not overwrite the project's hand-maintained file. [Preset schema](https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html), [Conan toolchain](https://docs.conan.io/2/reference/tools/cmake/cmaketoolchain.html).

Cache downloads separately from binary packages. Key app/compiler caches by OS/architecture/compiler/runtime/configuration and dependency identities. For vcpkg use documented files/NuGet providers; older examples of the experimental Actions cache provider should not be copied without checking current tool support. Fork PRs should read trusted caches but must not publish to a trusted package feed. Periodically test an empty cache; cache hits must be an optimization, not an undeclared dependency.

Linux can offer a separate distro/system preset using explicit `find_package`/pkg-config version requirements. Release CI should use a pinned environment and dependency graph. System mode is valuable for distributors but is not bit-for-bit equivalent to a vendored release graph, especially for fonts, codecs and FFMS2's private ABI. Do not silently switch to system packages when a pinned recipe fails.

## MuseScore and Audacity 4 prior art

At MuseScore main `1c81f0a6f3eeb1acff185b4b67569903faf1df36` and Audacity master `36146d838c934ae429cb164e8eb3d39af75a65ff`, both current CMake dependency setup files load a `muse_deps` submodule manifest and application/framework dependency manifests, install consumed runtime libraries/licenses and expose a source-prefetch target. This current Audacity tree includes the legacy `au3` subtree: old Audacity 3 Conan advice should not be generalized to the new top-level build. [MuseScore setup](https://github.com/musescore/MuseScore/blob/1c81f0a6f3eeb1acff185b4b67569903faf1df36/buildscripts/cmake/SetupDependencies.cmake), [Audacity setup](https://github.com/audacity/audacity/blob/36146d838c934ae429cb164e8eb3d39af75a65ff/buildscripts/cmake/SetupDependencies.cmake), [Audacity submodules](https://github.com/audacity/audacity/blob/36146d838c934ae429cb164e8eb3d39af75a65ff/.gitmodules).

The dependency engine documents source pins, patch recipes, an immutable prebuilt lock with SHA-256 and per-dependency PREBUILT/REBUILD/SYSTEM modes. The inspected Muse framework Qt setup uses `find_package(Qt6 6.8 REQUIRED ...)`, so SDK provisioning remains a separate concern. This is evidence for a hybrid/prebuilt strategy, not a reason HikariSub must adopt its bespoke engine. [Engine README](https://github.com/musescore/muse_deps), [framework Qt setup](https://github.com/musescore/muse_framework/blob/1d8529e782618391bf0187a2e34c85f499b0680e/buildscripts/cmake/SetupQt6.cmake).

## Decision and validation still needed

Choose dependency manager and Qt provisioning separately in #24. Then implement one clean-machine Windows/Linux build slice with libass plus the complete FFMS2 fork; prove warm-cache and empty-cache behavior and runtime deployment. Pin actual versions after that slice, then test arm64 as a separate acceptance gate. The comparative question is answered; no build-success or cache-duration result is claimed.

