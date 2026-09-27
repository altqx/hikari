# Stateless CMake workflow

**Accepted build contract, 2026-09-27:** pinned vcpkg manifests/overlays, CMake-managed pinned official Qt provisioning, declared development-tool prerequisites and `cmake --workflow --preset …`. [ADR 0008](../adr/0008-stateless-cmake-workflow.md) records the choice from [Choose the dependency and build strategy for the stateless CMake build](https://github.com/altqx/hikari/issues/24). This selects the mechanism; installer behavior, exact locks and clean Windows/Linux reconstruction remain unproved.

## One-command entry and prerequisites

After a fresh clone on a machine with the declared toolchain, `cmake --workflow --preset windows-x64-release` configures and builds. Use corresponding `ubuntu-x64-release` and `fedora-x64-release` workflows. Separate `*-verify` workflows add tests and packaging. Bare `cmake --preset NAME` only configures; `cmake --build --preset NAME` builds an already configured tree. The workflow spelling corrects the map's original shorthand.

The machine prerequisites are Git, a pinned compatible CMake/Ninja kit, network/disk capacity and the named compiler/SDK. In the follow-up review on 2026-09-27, altqx selected **latest MSVC**, superseding the initial MSVC2022-only prerequisite, with Windows SDK in its initialized development environment. Linux uses the selected distro's GCC and system development packages. Pin exact compiler/SDK/CMake/Ninja/Git versions for each qualified build rather than using a floating latest toolset inside a reproducibility claim. A clean-machine test starts without HikariSub/Qt/dependency caches, not without an installed development toolchain. Compiler installation and required Qt account/license authorization are explicit prerequisites, not implicit consent supplied by CMake.

The current workstation has VS2026/MSVC19.51 and an official Qt6.11.2 **MinGW** kit at `C:/Qt/6.11.2/mingw_64`. That SDK makes standalone MinGW C++ feasibility experiments possible; it is not an MSVC-compatible Qt library set. The MSVC workflow requires its matching Qt kit and an actual compiler/ABI qualification. A vendor kit directory naming an older MSVC generation does not itself select Hikari's compiler version or prove compatibility with a newer compiler.

Use Ninja and CMake orchestration. Do not add project shell/PowerShell/Python bootstrap scripts, `.sln`/`.vcxproj` build paths or checked-in output. Dependency recipes may invoke their upstream build tools.

## Dependency ownership and provisioning

Use vcpkg manifest mode with repository-owned overlay ports/triplets and a separate pinned official Qt SDK. Pin the vcpkg Git revision, compatible executable/digest, registry baseline, package versions/features, overlays and compiler/runtime inputs. Assign one owner per dependency; do not silently fall back to system libraries or a second Qt. A registry baseline alone does not pin all machine inputs or promise bit-identical binaries.

The root CMake configuration must run its idempotent provisioning include before the first `project()` call. It validates prerequisites, loads the committed artifact lock, fetches the exact vcpkg checkout and verifies the compatible official executable. Only then set the toolchain path and triplets/manifest settings and call `project()`. A preset must not reference a downloaded toolchain that does not yet exist. Supplying the executable avoids relying on automatic vcpkg bootstrap scripts; failed verification must stop explicitly. Reconfiguration/compiler probes reuse verified state.

CMake likewise downloads a digest-pinned official Qt installer and invokes its documented CLI into an ignored SDK directory. Lock exact Qt package IDs, patch version, architecture, module/host-tool set and repository/package archive identities; pinning only the installer or a mutable package alias is insufficient. Start with Qt 6.11.2 and follow the accepted [platform policy](platform-policy.md). Include Quick/QML and Linguist tooling; exclude Qt from vcpkg's graph and audit overlapping transitive libraries.

Before qualifying this path, verify public SDK availability, unattended account/license requirements and permitted immutable artifact retention. Credentials stay outside source and logs. Missing artifacts/entitlement or changed archive hashes fail explicitly instead of selecting another kit. No paid Qt entitlement is assumed.

## Forks, outputs and CI

Use the existing [patch ledger](../../Thirdparty/PATCHES.md) as provenance input, not as a production recipe. FFMS2 starts from `45d5f72100d88c52acdd54bfedcc0315a44c735d`; its additional API implementation outside the fork also needs a pinned source identity. Build private headers and linked library from matching sources; avoid the old fork-header/distro-library ABI mismatch. Pin overlay patches and archive hashes. The optional Windows xy-VSFilter starting revision is `c4297ed033ab899c86023543bd65ff08c7ba20a3`; it needs a CMake recipe and stays outside the default cross-platform graph.

Commit manifests/locks, overlays and shared presets. Ignore machine-specific `CMakeUserPresets.json` and separate generated `out/build/<preset>`, SDK, download and binary-package directories. Generated state must be reconstructible. Native debug/release presets cover Windows x64, Ubuntu and Fedora; arm64 needs separate qualification.

CI uses the same workflows in pinned build environments, with native runtime/deployment jobs for the claimed Windows 10/11 and Linux versions. **Linux validation runs in GitHub Actions for now**, as selected by altqx on 2026-09-27; a local WSL/container runner is not a prerequisite to that work. Record exact runner image, distro, toolchain and package identities. A mutable hosted-runner label alone is not an immutable environment or proof of Fedora/native-desktop coverage. Windows 10 gates remain until explicit retirement. Keep download caches separate from ABI-keyed package caches; untrusted fork jobs cannot publish trusted caches. Schedule empty-cache reconstruction and missing/corrupt-artifact cases.

## Remaining proof

Prove a complete Windows/Linux FFMS2 + libass + Qt slice using the declared prerequisites, actual installer path and exact artifact locks. Observe cold/warm reconstruction, ABI/linkage, deployment and failure diagnostics. If an assumed provisioning mechanism fails, reopen it explicitly rather than weakening the contract. No configure/build, license acceptance, installation or clean-machine pass is claimed by this decision.

Sources and alternatives: [reviewed proposal](proposals/stateless-build.md), [CMake workflow presets](https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html), [vcpkg CMake integration](https://learn.microsoft.com/en-us/vcpkg/users/buildsystems/cmake-integration), [Qt installer CLI](https://doc.qt.io/qtinstallerframework/ifw-use-cases-cli.html).
