# Linux dependency preflight — throwaway, no build claim

This is the first runnable prerequisite probe for [Prove pinned CMake workflow provisioning on Windows and Linux](https://github.com/altqx/hikari/issues/48). It asks whether GitHub Actions can record the selected Linux host and verify the proposed **public metadata** before the dependency provisioner exists. It does not implement the Qt/FFMS2/libass build or supersede the accepted CMake-managed official Qt and pinned vcpkg contract.

From this directory, with CMake 3.25+ and Ninja on PATH:

```sh
cmake --workflow --preset linux-ci-preflight
```

The workflow really runs configure followed by the `preflight-report` build target. The include runs before `project()`, and the project enables no compiler language. The target reports the captured state; it builds no C++ code. On another OS, `cmake --workflow --preset metadata-only-preflight` provides a clearly labelled public-metadata check, not Linux validation.

## What executes

- Narrow version queries for the compiler, CMake/Ninja/Git and prospective recipe host tools; selected OS package versions, glibc, kernel, memory/CPU count and disk snapshot. No packages are installed. Missing tools remain visible.
- HTTPS downloads of the exact Qt 6.11.2 Linux repository `Updates.xml` and nine small SHA-256 declarations: the versioned official 4.11.0 installer plus eight timestamped base-kit archives. Metadata is compared against [public-metadata-lock.json](public-metadata-lock.json). No installer/SDK/archive binary is downloaded or executed.
- JSON/Markdown reports and fetched public metadata under ignored `_out/<preset>/evidence/`. The artifact includes only this dedicated evidence directory, not CMake caches, home directories or arbitrary logs.

The registry snapshot and binary checksum declarations were fetched from official Qt endpoints on 2026-09-27. All checksum values describe publisher declarations; **no matching binary-content verification follows**. A changed metadata snapshot/checksum or a failed fetch stops configure after preserving the report, without adopting replacement values. Versioned URLs and digests do not guarantee upstream retention.

`qt.qt6.6112.linux_gcc_64` is revision `6.11.2-0-202608131018`. Its eight archives include Base, Declarative, Tools, SVG, translations, Wayland and ICU. Declared dependencies also include Creator/docs/examples. Their parent/license components, scripts and transitive archives are **not locked here**. Proving that the official installer consumes only verified bytes, and establishing a permitted retained/offline reconstruction route, remain separate gates. Direct extraction, aqt, distro Qt and vcpkg Qt are not fallbacks.

## CI scope and authorization

[qt-dependency-feasibility.yml](../../../../.github/workflows/qt-dependency-feasibility.yml) runs only for pushes to `codex/qt-design-prototypes` changing this directory or that workflow. It does not run on pull requests, release tags or `main`. Checkout and artifact upload are commit-pinned; the token has `contents: read`, checkout does not persist credentials, no submodules are fetched, and no release or cache publication occurs. Evidence retention is seven days.

The runner is `ubuntu-26.04`. Its label and bundled toolchain are mutable: recorded versions are observations, not a reproducible-environment lock. The probe installs no OS prerequisites and does not select a production compiler/triplet. CPU/memory observations are not the accepted performance reference hardware. The complete native platform and Fedora gates remain open.

A repository secret-name query returned no secrets on 2026-09-27. Future CI evaluates only whether `QT_INSTALLER_JWT_TOKEN` is nonempty and passes **a boolean**; the token itself is never injected into this job's commands. Presence cannot prove that the account is authorized or unexpired. The CMake code never reads account files, a JWT or unrelated environment variables. It records only a fixed allowlist of runner identity variables.

There are **no license/obligation acceptance flags and no installer execution path**, even if the boolean is true. Installing MinGW/MSVC locally or reusing a local authorized MaintenanceTool session does not provision the ephemeral Linux CI account. A future installer run must have its own authorized account/terms scope and must preserve the official installer owner.

## Interpreting the result

**A green preflight workflow means collection and public metadata comparison succeeded. Dependency build status is always `blocked_not_attempted`.** The report separately names missing/unvalidated authentication, absent terms scope, incomplete artifact closure, missing vcpkg graph/FFMS2 recipe and unexecuted native qualification. Expected outstanding gates are not mislabeled as a passed dependency build. A Linux preset on a non-Linux host fails after saving evidence.

Next experiment: supply the full tool/registry/overlay/artifact lock, build the fork and pinned `indexing_additional.cpp` together with a compatible FFmpeg graph, and implement the authorized pre-`project()` provisioner. Then exercise Quick/QML, Linguist, a real private FFMS2 API, libass, cold/warm reconstruction, deployment and disposable failure cases. This directory deliberately contains none of those production or integration implementations.

Local validation on 2026-09-27 used Windows, VS-bundled CMake 4.3.1-msvc1 and Ninja 1.13.2. `metadata-only-preflight` completed both workflow steps: the metadata snapshot and all nine publisher checksum declarations matched; its report correctly retained `blocked_not_attempted` and `linux_execution: not_executed`. A second configure using an ignored disposable source copy with a deliberately wrong metadata hash exited 1 and retained the mismatch report. The committed candidate was unchanged. Relative links, JSON and source whitespace were checked. These are bounded probe observations, not an application test suite.

The first Linux CI result remains unexecuted at authoring. Windows metadata checks cannot prove availability of the GitHub runner, Linux packages, an installer login, or the dependency ABI. No CI dispatch is part of authoring this artifact.

Sources: [accepted build contract](https://github.com/altqx/hikari/blob/c2258a7a7b100a68d0a22f932c087b9b334ee411/docs/qt/build.md), [Qt CLI authentication and explicit acceptance](https://doc.qt.io/qt-6/get-and-install-qt-cli.html), [official installer offline workflow](https://doc.qt.io/qtinstallerframework/ifw-use-cases-cli.html), [GitHub runner labels and resources](https://docs.github.com/en/actions/reference/runners/github-hosted-runners), [branch-local push triggers](https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows#push).
