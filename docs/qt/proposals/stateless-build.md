# Stateless CMake build proposal

**Proposal for [Choose the dependency and build strategy for the stateless CMake build](https://github.com/altqx/hikari/issues/24), awaiting acceptance.** Based on the [completed build research](https://github.com/altqx/hikari/blob/38f8c1434a51e335eed57cdf058fb9dc2ccee62b/docs/research/stateless-build.md) and accepted [platform policy](../platform-policy.md). No provisioning, dependency build or clean-machine result is claimed.

## Recommended contract

After an ordinary fresh clone on a machine with the declared prerequisites, run **`cmake --workflow --preset windows-x64-release`**, or the corresponding `ubuntu-x64-release` / `fedora-x64-release` workflow. Each configures and builds; separate `*-verify` workflows add tests and packaging. Plain `cmake --preset NAME` only configures, and `cmake --build --preset NAME` builds an already configured tree. Workflow presets require CMake 3.25 or newer. Accept this spelling correction to the map's one-command promise. [CMake presets](https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html).

“Clean machine” means **no HikariSub/Qt/dependency cache**, not an operating system without development tools. Prerequisites are Git, a pinned compatible CMake/Ninja kit, network access, disk space, and the declared compiler/SDK: MSVC 2022 plus Windows SDK in an initialized developer environment, or the selected distro's GCC/system development packages. Recipe-specific host tools and Qt installer runtime/account/license requirements must be enumerated before advertising this contract. Compiler installation and license authorization are not implicit CMake actions.

Use Ninja, no `.sln`/`.vcxproj` pipeline and no repository shell/PowerShell/Python bootstrap scripts. All HikariSub provisioning orchestration lives in CMake; dependency recipes may invoke upstream build tools.

## Dependency ownership

Recommend **vcpkg manifest mode with repository-owned overlay ports and triplets**, plus a separately pinned Qt SDK. Pin the vcpkg checkout, compatible tool executable, registry baseline, package versions/features, compiler/runtime and overlay contents. One owner supplies each dependency; forbid automatic fallback to distro libraries or a second Qt installation.

| Alternative | Tradeoff |
| --- | --- |
| **vcpkg + overlays — recommended** | CMake integration and ABI-based binary caching; custom FFMS2 and optional-plugin recipes remain our responsibility. |
| FetchContent / ExternalProject | Adequate for small CMake dependencies; the mixed FFmpeg/libass/LuaJIT graph requires us to maintain recipes and binary-cache compatibility ourselves. |
| Conan 2 | Credible profiles, lockfiles and binary packages, but adds Python recipe/tool maintenance without a demonstrated project advantage. |

The [research comparison](https://github.com/altqx/hikari/blob/38f8c1434a51e335eed57cdf058fb9dc2ccee62b/docs/research/stateless-build.md#comparison) contains the primary references. A baseline alone does not lock machine inputs or guarantee bit-identical binaries.

## Provision before loading the toolchain

The root `CMakeLists.txt` must perform an idempotent CMake provisioning include **before its first `project()`**. It checks prerequisites, reads the committed artifact lock, fetches the exact vcpkg Git revision and downloads its pinned compatible executable with digest verification. Official [vcpkg-tool releases](https://github.com/microsoft/vcpkg-tool/releases/tag/2026-07-27) provide Windows and Linux binaries; that availability does not prove the chosen tool/registry pair works.

Only after those files exist does CMake set `CMAKE_TOOLCHAIN_FILE`, host/target triplets and manifest settings, then call `project()`. Presets must not point directly at an absent downloaded toolchain. Supplying the executable avoids reliance on vcpkg's automatic bootstrap script. Missing artifacts fail explicitly; repeated configure and compiler probes reuse verified provisioning. The sequencing follows [vcpkg's integration contract](https://learn.microsoft.com/en-us/vcpkg/users/buildsystems/cmake-integration).

The same provisioning phase downloads a **pinned official Qt installer**, verifies its digest and invokes its CLI into an ignored SDK directory. Lock exact package IDs, Qt patch, architecture, module/host-tool set, repository metadata and package archive digests; mutable aliases or installer version alone are insufficient. Install Qt 6.11.2 initially, then qualify published 6.12 stable under the accepted policy. Include Quick/QML and Linguist tooling; exclude Qt from vcpkg's graph and audit conflicting transitive libraries.

Qt documents unattended installation and offline-installer creation, but that is not proof that the selected public SDK's authentication, licensing and archive-retention path is unattended or reproducible. Verify that path and retain permitted immutable package artifacts before claiming success; fail on drift or missing entitlement rather than silently selecting another version/installer. No paid entitlement is assumed, and credentials stay outside source and logs. [Official installer CLI](https://doc.qt.io/qtinstallerframework/ifw-use-cases-cli.html).

## Patched forks and outputs

Carry the [patch ledger](../../../Thirdparty/PATCHES.md) into overlay provenance: FFMS2 starts from `45d5f72100d88c52acdd54bfedcc0315a44c735d`; optional Windows xy-VSFilter from `c4297ed033ab899c86023543bd65ff08c7ba20a3`. Pin archive hashes and patches too. FFMS2's additional API implementation currently lives outside its fork: its source identity must accompany the recipe, with private headers and linked library built from the same revision. Never reproduce the existing Linux fork-header/distro-library ABI hazard. Optional VSFilter needs a CMake recipe; its old MSBuild projects are not the new build path. Keep it outside the default cross-platform graph.

Commit manifest/lock files, overlays and `CMakePresets.json`; keep personal paths in ignored `CMakeUserPresets.json`. Separate `out/build/<preset>`, SDK, download and binary-package directories. Deleting generated state must be recoverable from declarations. Pin native debug/release presets for Windows x64, Ubuntu and Fedora; arm64 remains an additional feasibility target.

## CI and approval

Use the same workflows in pinned Windows/Linux build environments, plus native runtime/deployment jobs on Windows 10/11 and the selected Ubuntu/Fedora releases. Retain Windows 10 gates until explicit retirement. Cache downloads separately from ABI-keyed packages; fork PRs cannot publish trusted caches. Schedule empty-cache reconstruction and verify missing/corrupt-artifact failures.

**Decision requested:** accept vcpkg/overlays, CMake-driven pinned official Qt provisioning, and the corrected workflow command with declared machine prerequisites. Then prove one complete Windows/Linux FFMS2+libass+Qt slice, installer behavior, dependency ABI/linkage, cold/warm reconstruction and deployment. Exact artifact locks are implementation evidence still to produce; failure reopens the relevant mechanism instead of weakening the contract silently.
