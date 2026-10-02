# Stateless CMake workflow

**Accepted build contract, 2026-09-27:** pinned vcpkg manifests/overlays, CMake-managed pinned official Qt provisioning, declared development-tool prerequisites and `cmake --workflow --preset …`. [ADR 0008](../adr/0008-stateless-cmake-workflow.md) records the choice from [Choose the dependency and build strategy for the stateless CMake build](https://github.com/altqx/hikari/issues/24). This selects the mechanism; installer behavior, exact locks and clean Windows/Linux reconstruction remain unproved.

## One-command entry and prerequisites

After a fresh clone on a machine with the declared toolchain, `cmake --workflow --preset windows-x64-release` configures and builds. Use corresponding `ubuntu-x64-release` and `fedora-x64-release` workflows. Separate `*-verify` workflows add tests and packaging. Bare `cmake --preset NAME` only configures; `cmake --build --preset NAME` builds an already configured tree. The workflow spelling corrects the map's original shorthand.

The machine prerequisites are Git, a pinned compatible CMake/Ninja kit, network/disk capacity and the named compiler/SDK. In the follow-up review on 2026-09-27, altqx selected **latest MSVC**, superseding the initial MSVC2022-only prerequisite, with Windows SDK in its initialized development environment. Linux uses the selected distro's GCC and system development packages. Pin exact compiler/SDK/CMake/Ninja/Git versions for each qualified build rather than using a floating latest toolset inside a reproducibility claim. A clean-machine test starts without HikariSub/Qt/dependency caches, not without an installed development toolchain. Compiler installation and required Qt account/license authorization are explicit prerequisites, not implicit consent supplied by CMake.

The current workstation has VS2026/MSVC19.51 and official Qt6.11.2 kits at `C:/Qt/6.11.2/mingw_64` and `C:/Qt/6.11.2/msvc2022_64`. The latter was added successfully through the existing official MaintenanceTool on 2026-09-27; the tool reused its existing authorized installation and requested no new license acceptance. The [sanitized SDK observation](https://github.com/altqx/hikari/blob/4959243fefdf0737e655985e300ff9f20e12d9ac/HikariSub/prototypes/build-workflow/windows-sdk-observed.json) records package revisions and selected file hashes. The MinGW kit must not be linked by MSVC. The [C++ Grid experiment](https://github.com/altqx/hikari/blob/be0c5821c9c6baed2fe2381a3f9ad25fde89f359/HikariSub/prototypes/grid-accessibility-cpp/README.md) built, linked and ran the Qt Quick slice with MSVC19.51.36260, toolset14.51.36231 and SDK10.0.26100.0; loaded Qt DLL paths matched the MSVC kit. This is limited installed-kit compatibility evidence, not complete dependency, deployment or clean-provisioning qualification. The vendor kit directory's older MSVC name does not select Hikari's compiler version.

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

## First Linux CI preflight

The [first GitHub Actions run](https://github.com/altqx/hikari/actions/runs/36299849291) completed on Ubuntu26.04.1/image20260920.143.1. The [retained report](https://github.com/altqx/hikari/blob/3df736df3f44afeb64dc731f75972a739027504b/HikariSub/prototypes/build-workflow/linux-ci/ci-run-notes.md) records GCC15.2, glibc2.43, CMake4.4.3 and Ninja1.13.2, and verifies the pinned public Qt metadata and nine publisher checksum declarations. The workflow used `project(... LANGUAGES NONE)` and a report target; no dependency was installed or built. It establishes a working CI execution route and host observations, not immutable environment or Windows/Linux build qualification.

NASM and Meson were absent from PATH. The report distinguishes absent commands from package-query/version-flag limitations. Official Qt CI authentication/terms scope, complete installer-consumed artifact enforcement, the owned FFMS2 graph and cold/warm/deployment/failure-case evidence remain required. No workstation credentials were exported.

## Linux source-build observation

[Run 36302050812](https://github.com/altqx/hikari/actions/runs/36302050812) subsequently compiled the owned FFMS2 plus pinned FFmpeg/zlib graph and passed its three-frame/private-metadata CTest on Ubuntu 26.04.1/GCC 15.2. The [retained report](https://github.com/altqx/hikari/blob/8e93cc24d3a72befb55e439664ba7da68a69c94a/HikariSub/prototypes/build-workflow/ffms2-overlay/linux-ci/ci-run-notes.md) pins the run source, resolved features/host tools, archive/recipe hashes and actual observations. All 18 captured recipe digests matched the run's Git blobs; 22 retained text files have byte-level publication checks.

The top-level manifest explicitly disables FFmpeg default features, correcting an actual resolver finding that a transitive request alone did not suppress avdevice/avfilter. This source slice establishes matching fork/private headers and library, metadata lookup and exact pixels for a small synthetic random-order frame test. It does not establish the full Qt/libass provisioner, standalone FFMS-only static closure, audio behavior, full media corpus, deployment, other platforms or cold/warm/offline failure qualification. The FFmpeg version remains a compatibility candidate rather than a shipping-maintenance pin.

## Windows source-build observation and Qt acquisition follow-up

The [Windows report](https://github.com/altqx/hikari/blob/1ee21bbd5e3aed25ebb44aa9ba1fb46f7ccf277e/HikariSub/prototypes/build-workflow/ffms2-overlay/evidence/windows-20260927/README.md) records successful MSVC source builds and both the three-frame/private-metadata specimen and an independent consumer linking only `HikariFFMS2::ffms2`. That consumer decoded the authored first frame with the expected pixel, qualifying the exercised Windows static target closure. All 20 canonical input hashes match the unchanged lock, and a non-line-ending corruption specimen still fails without rewriting its bad input. Root checked all 33 retained evidence files against published Git bytes.

The original deep-root workflow failed in a compiler probe. Success used documented short-directory overrides with separate configure/build/CTest commands; it is not a bare `windows-overlay` or full one-command provisioning pass. Git's Windows archive line endings required an exact-digest-guarded LF restoration in the port; source content/API behavior and lock digests did not change. Both earlier failure evidence and sampled resource bounds are retained. Actual audio, wider private APIs/media corpus, deployment and reconstruction remain unproved.

The [official Qt metadata/script audit](https://github.com/altqx/hikari/blob/6e2dcb7e82fd8bc37acd836e5b508703cb3f6325/docs/research/qt-provisioning-followup.md) inspected 27 hash-verified metadata archives without running scripts/installers or acquiring SDK payloads. Base documentation/example dependencies persist in the inspected scripts; the current `qt.tools.qtcreator` ID supplies SDKTool, with full Creator GUI separate. A frozen official repository snapshot consumed by the official installer is the candidate for complete content locking. Exact runtime-resolved closure, support scripts/conditional components, controller behavior, authorization and bounded acquisition still need a concrete execution plan and proof; the minimal-module preflight cannot stand in for them.

## Official Qt provisioning proved on Linux (2026-09-29)

With the owner's secret and license authorization, [run 36577045109](https://github.com/altqx/hikari/actions/runs/36577045109) ran the [frozen repository experiment](https://github.com/altqx/hikari/blob/f9e0cb999831b7b174631d4d29f5c282c86ef8c5/HikariSub/prototypes/build-workflow/qt-frozen/README.md) on Ubuntu 26.04. Its [evidence](https://github.com/altqx/hikari/blob/f9e0cb999831b7b174631d4d29f5c282c86ef8c5/HikariSub/prototypes/build-workflow/qt-frozen/evidence/run-36577045109/README.md) shows:

- A CMake stage before `project()` acquired a script-resolved lock of 125 objects, verified them and rejected a corrupted one. A warm re-run refetched only that object.
- Official installer 4.11.0 installed Qt 6.11.2 from the loopback mirror, reading exactly the locked objects and nothing outside them.
- A credential-free Quick/Multimedia/ShaderTools/Linguist consumer built and passed offscreen and Xvfb/OpenGL render tests.

The token reached only the install step. The earlier run showed that the installer skips virtual package IDs yet exits 0, so a closure check against the lock is mandatory.

altqx closed [Prove pinned CMake workflow provisioning on Windows and Linux](https://github.com/altqx/hikari/issues/48) on this basis. The mechanism is viable. These obligations transfer by name to the B1/B2 build cards as required evidence, not passes:

- **B48-win-qt:** the same frozen run for `win64_msvc2022_64` with latest MSVC.
- **B48-libass:** an owned libass/fontconfig/harfbuzz recipe on both platforms.
- **B48-combined:** one Qt + FFMS2 + libass slice per platform.
- **B48-workflow:** a one-command user entry, with the installer still isolated from later build processes.
- **B48-winpath:** the Windows deep-path compiler probe without manual short-directory overrides.
- **B48-fedora:** a Fedora host.
- **B48-offline:** offline prefetch and cold reconstruction on a clean non-CI machine.
- **B48-deploy:** deployment.

A failure in any of them reopens the build decision rather than substituting another Qt source.

## Production build inputs (B1, 2026-09-29)

[B1](https://github.com/altqx/hikari/issues/67) promoted the proven slices into the `qt` root. Linux is first; Windows follows on a Windows host (B2-W).

- **Entry:** `cmake --workflow --preset ubuntu-x64-release` (or `-verify` to add tests, `fedora-x64-*` on Fedora). The root `CMakeLists.txt` includes `cmake/provision/Provision.cmake` before `project()`. That include checks the declared host tools, then verifies or acquires the Qt mirror, installs Qt from it, provisions the pinned vcpkg checkout and tool, and sets the toolchain.
- **Locks:** [`cmake/locks/qt-linux.lock.json`](../../cmake/locks/qt-linux.lock.json) (the lock proven in CI) and [`cmake/locks/vcpkg.lock.json`](../../cmake/locks/vcpkg.lock.json). `tools/qt-lock/make-lock.py` regenerates Qt locks. [`cmake/locks/qt-windows.lock.json`](../../cmake/locks/qt-windows.lock.json) (B2-W) locks `win64_msvc2022_64` with the same add-ons: 125 objects, 487 MB, reusing the shared `all_os` digests from the Linux lock. Its 18 metadata files are retained in the release [`qt-metadata-windows-6.11.2-20261002`](https://github.com/altqx/hikari/releases/tag/qt-metadata-windows-6.11.2-20261002).
- **Dependencies:** the root [`vcpkg.json`](../../vcpkg.json) pins FFmpeg 7.1.2#5 (four features), libass 0.17.5 and the owned `ports/hikari-ffms2`, with release-only static `triplets/`. Qt never enters the vcpkg graph. `cmake/HikariDependencies.cmake` fails if `Qt6_DIR` is outside the provisioned prefix, and links libass through static pkg-config.
- **SDK state:** `out/sdk` holds the Qt mirror, the Qt installation (stamped with the lock digest), the metadata cache and the vcpkg checkout. It is shared by presets, ignored, and reconstructible. A Qt installation from a different lock stops the configure instead of being reused.
- **Authentication:** with `QT_INSTALLER_JWT_TOKEN` set, the installer runs under a private HOME (on Windows also a private `APPDATA` and `LOCALAPPDATA`, where it keeps its account file) that is deleted afterwards. Without it, the installer uses the developer's existing Qt Account login. The owner authorized local and CI installs under the locked license set on 2026-09-29.
- **Isolation in CI:** `cmake --workflow --preset provision` is the only step given the secret. The credential-free `*-verify` workflow then reuses the stamped installation.
- **Linux host tools** (checked up front): git, ninja, a C/C++ compiler, make, pkg-config, autoconf, autoconf-archive, automake, libtool, nasm, python3, curl, tar, zip and unzip, plus the ALSA development files (PortAudio's host API links the system ALSA library).

Failure behavior exercised locally: a corrupted or stray Qt mirror object and a vcpkg checkout at another commit each stop the configure with the path named; a tampered vcpkg tool is replaced by the verified download. The wx Linux root build moved to `cmake/linux-compat/LegacyWxLinux.CMakeLists.txt` as reference.

## Retained Qt repository metadata (2026-10-02)

Qt republishes and deletes its online-repository metadata. On 2026-09-28 `all_os/default_install` was republished and its previous metadata archive removed, so clean builds could no longer reproduce the Sept 29 lock (B2-U/F runs failed at acquisition with a SHA-256 mismatch, as designed). altqx chose to keep our own copy of the pinned metadata:

- The 9 `Updates.xml` indexes and 9 metadata archives (about 300 KB, including Qt's license texts and installer scripts) are assets of the release [`qt-metadata-linux-6.11.2-20261002`](https://github.com/altqx/hikari/releases/tag/qt-metadata-linux-6.11.2-20261002), redistributed unchanged.
- The lock gives each such entry a `retained_url`, and acquisition prefers it. Versioned payload archives still come from download.qt.io. The SHA-256 check is the same either way.
- `tools/qt-lock/make-lock.py --previous <lock> --retention-base <url>` reuses verified digests by URL and length, and hashes only new or changed objects. The refresh reused 124 of 125; the selected closure and license texts are unchanged.
- Acquisition removes mirror files the lock no longer lists; verify-only mode still reports them as failures. The installed-Qt stamp now identifies installed content (requested packages and payload digests), so a metadata-only refresh keeps an existing installation.

Payloads remain exposed to Qt moving 6.11.2 off its online repository; retaining them too is a separate, unmade decision.

## Windows builds (B2-W, 2026-10-02)

- **CI:** `.github/workflows/qt-windows.yml` runs on `windows-2025` with the newest Visual Studio's x64 developer environment (Visual Studio 18, MSVC 14.51 on the first runs). The empty-cache job provisions with the secret in one step only, rejects and repairs a corrupted mirror object, then builds and tests without credentials. It saves `out/sdk` for a second job, which reruns the verify workflow from that warm SDK with no secret at all.
- **Windows specifics found by the first runs:** the vcpkg partial clone's on-demand fetch of an override's port tree is now prefetched with retries during provisioning; FFmpeg is found before Qt, because Qt's `FindFFmpeg.cmake` shadows vcpkg's `FindFFMPEG` on a case-insensitive filesystem; MSVC compiles sources as UTF-8 (`/utf-8`); and the Qt installer's private home also redirects `APPDATA`/`LOCALAPPDATA`, where it keeps its account file on Windows.
- **Host tools:** git, ninja and cl, checked up front. vcpkg downloads its own CMake, 7-Zip and MSYS2 tools.
- **Local Windows VM:** [`winix.yaml`](../../winix.yaml) runs the same presets through Winix (`provision`, `build`, `test`, `backends`). `tools/winix/msvc.ps1` loads the developer environment and falls back to Visual Studio's bundled Git. `out/` stays in the VM. Qt provisioning there uses a Qt Account login made once inside the VM; the CI secret is never copied into it.
- **vcpkg binary cache:** `out/sdk/vcpkg-archives`, on every platform, so one directory makes a run warm.

## Phase 3 native inputs (B4, 2026-10-03)

- **PortAudio** comes from the owned overlay `ports/portaudio`: upstream vcpkg's revision `147dd722…` and patches, without the jack2 dependency, with the host APIs pinned. Linux gets ALSA only (PipeWire and PulseAudio desktops reach it through their ALSA plugins); Windows gets WASAPI and DirectSound (the legacy app used DirectSound). JACK, ASIO, WDM-KS, MME and PulseAudio are off. `dependency-load` fails if the runtime host API set differs. Target: `Hikari::portaudio`.
- **LuaJIT** comes from vcpkg's port at the baseline (2026-09-08 snapshot, static, with FFI). Its link flags export the executable's symbols (`-Wl,-E`), which Lua native modules need, so `Hikari::luajit` is for the Lua helper only, never the application. `luajit-load` runs it in its own process and checks the JIT and FFI.
