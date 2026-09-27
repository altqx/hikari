# Qt frozen repository: bounded Linux-first plan

**Proposed experiment for [Prove pinned CMake workflow provisioning on Windows and Linux](https://github.com/altqx/hikari/issues/48), 2026-09-27. Not installation authorization or an executed provisioner.** This follows the [verified package/script investigation](../../../docs/research/qt-provisioning-followup.md) and [actual Linux preflight](linux-ci/ci-run-notes.md). It preserves CMake ownership and the official Qt installer. Preparation used existing evidence, public XML/HTTP HEAD and repository secret **names** only. No SDK payload, installer execution, account access or terms acceptance occurred.

## Candidate repositories and components

Start on one disposable Ubuntu 26.04 GitHub Actions runner with an empty target, Qt 6.11.2 and official installer 4.11.0. Request exactly:

```text
qt.qt6.6112.linux_gcc_64
qt.qt6.6112.addons.qtmultimedia.linux_gcc_64
qt.qt6.6112.addons.qtshadertools.linux_gcc_64
```

Windows later substitutes `win64_msvc2022_64` in these three IDs. The package name remains MSVC2022 even though latest MSVC was accepted separately; that acceptance does not prove compatibility. Avoid the broader `sdk` alias.

Each suffix below joins `https://download.qt.io/online/qtsdkrepository/` and contains `Updates.xml`. Preserve the original indexes and metadata bytes, including shared scripts; do not rewrite dependency lists. These are nine **candidate** repository roots, not an executed closure. The research note pins inspected metadata archives; a complete machine-readable lock remains to be authored.

| Repository suffix | Components and revisions |
| --- | --- |
| `linux_x64/desktop/qt6_6112/qt6_6112/` | Three target leaves, `6.11.2-0-202608131018`. Base selects eight archives; each add-on selects one. Includes parent/automatic-node metadata. |
| `all_os/qt/qt6_6112_unix_line_endings_src/` | `qt.qt6.6112`, `.doc`, `.examples`, `.doc.qtmultimedia`, `.examples.qtmultimedia`, `.doc.qtshadertools`, all `6.11.2-0-202608131018`. Base docs/examples are hard dependencies. |
| `linux_x64/desktop/tools_generic/` | `qt` `1.0.22`; `qt.tools` `1.2.6-0-202609031112`. Shared qmake/patch/qt.conf functions and license dependencies. |
| `linux_x64/desktop/sdktool/` | `qt.tools.qtcreator` is **SDKTool**, `20.0.2-0-202609221200`, with one SDKTool archive. |
| `all_os/license_agreements/licenses/` | Required candidate nodes: `qt.license.lgpl` `1.0.2-1`, `qt.license.thirdparty` `1.0.0-1`, `qt.license.acceptance` `1.0.0`. GPL-exception node `1.0.0-2` is present, selected status unresolved. Preserve all license metadata. |
| `all_os/qt_patchers/6112/` | `qt.qt6.6112.patcher` `6.11.2-0-202608131018`, metadata/helper functions. |
| `all_os/unified_patching/` | `qt.unified_patching` `1.0.0-202311211039`, metadata/helper functions. |
| `all_os/dependencycheck/` | `qt.dependencycheck` `1.0.0-0-202608260909`, metadata/host checks. |
| `all_os/default_install/` | `qt.default.install` `1.0.2-0-202608180452`. Preserve metadata; do not invoke the no-argument default install. |

Sources: [base](https://download.qt.io/online/qtsdkrepository/linux_x64/desktop/qt6_6112/qt6_6112/Updates.xml), [shared Qt](https://download.qt.io/online/qtsdkrepository/all_os/qt/qt6_6112_unix_line_endings_src/Updates.xml), [tools](https://download.qt.io/online/qtsdkrepository/linux_x64/desktop/tools_generic/Updates.xml), [SDKTool](https://download.qt.io/online/qtsdkrepository/linux_x64/desktop/sdktool/Updates.xml), [licenses](https://download.qt.io/online/qtsdkrepository/all_os/license_agreements/licenses/Updates.xml).

Windows uses `windows_x86/desktop/qt6_6112/qt6_6112_msvc2022_64/` with architecture revision `6.11.2-0-202608131017`, `all_os/qt/qt6_6112_windows_line_endings_src/` with shared revision ending `1018`, and Windows counterparts of `tools_generic/` and `sdktool/`. Other shared roots are unchanged. If `windows_x86/desktop/tools_vcredist/` is enabled, SDKTool adds its available `qt.tools.vcredist_msvc2019_x86` and `qt.tools.vcredist_msvc2022_x64` nodes, both observed revision `2025-11-26`; the named ARM64 node is absent from that captured XML. Their scripts/operations require inspection before a Windows run. [Redistributable XML](https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/tools_vcredist/Updates.xml), SHA-256 `edd185bb40a77271d968c89afeeaf51e445d8e468c8a85a3b1b579cf751dcd0f`.

**Docs/examples versus Creator:** inspected scripts retain base docs/examples. SDKTool is small and used by base installation operations. The full IDE is separate `qt.tools.qtcreator_gui`; this fresh candidate excludes its repository, debug symbols, sources, WebEngine and unrelated add-ons. This is a proposed bounded repository set, not proof that the official controller resolves it successfully.

**Final closure unknowns:** direct virtual-leaf selection may also select add-on parents; the shared Qt root may add `sha1s.txt.7z`; automatic nodes and embedded installer/controller configuration need review. Both bases conditionally add `mocwrapper` when available, but its applicable repository/version was not established for this open-source snapshot. Do not assume a commercial license-service repository is required or silently disable an applicable component. An unexpected node/repository stops this candidate and requires an explained lock revision. Preserve shared script visibility even when the support node is not installed.

## Publisher sizes and proposed stop bounds

These sums use `UpdateFile` metadata, not downloaded-content measurements or actual disk use. The known set has 53 data archives per platform: base, two add-on leaves, base docs/examples, SDKTool and third-party licenses.

| Group | Linux compressed / expanded bytes | Windows compressed / expanded bytes |
| --- | ---: | ---: |
| Base | 195,151,785 / 1,565,732,389 | 250,502,370 / 2,323,566,995 |
| Multimedia | 37,380,303 / 243,519,571 | 43,228,051 / 485,224,127 |
| Shader tools | 1,692,415 / 6,113,384 | 3,293,622 / 18,597,228 |
| Base docs | 102,381,572 / 238,285,095 | 119,042,676 / 238,285,095 |
| Base examples | 61,372,870 / 84,708,293 | 61,372,755 / 84,708,293 |
| SDKTool + third-party licenses | 1,251,641 / 4,382,024 | 1,170,804 / 3,602,288 |
| **Known payload subtotal** | **399,230,586 / 2,142,740,756** | **478,610,278 / 3,153,984,026** |
| Installer executable | 81,242,039 | 58,051,784 |
| **Subtotal with installer** | **480,472,625 (458.21 MiB)** | **536,662,062 (511.80 MiB)** |

If add-on parents select their docs/examples, add Linux **7,081,801 compressed / 13,537,186 expanded bytes**, or Windows **8,349,303 / 13,537,186**. The shared root checksum archive adds 1,657 / 2,494 bytes if selected. The two available Windows redistributables add 24,914,233 / 25,435,232 bytes; subsequent system installation footprint is not included. Metadata, installer runtime extraction, temporary copies, logs and retry traffic are additional. Full repository metadata can describe unselected packages without acquiring their SDK payloads.

HEAD on 2026-09-27 confirmed the versioned [Linux installer](https://download.qt.io/archive/online_installers/4.11/qt-online-installer-linux-x64-4.11.0.run) and [Windows installer](https://download.qt.io/archive/online_installers/4.11/qt-online-installer-windows-x64-4.11.0.exe) lengths. No executable bytes were fetched. Declared SHA-256: Linux `40b76bdf74f6a396341efb70ae2e754fcd878474babb6cd9d7f07eff12a85c62`; Windows `ae919bc9b224b8ccdada69ec787a9f69330001f227f3fcbfb4a11a4adb3786f6`. These still need verification against acquired content.

**Proposed first run:** Linux only, 768 MiB acquisition including installer/metadata/payload/retries, 8 GiB dedicated scratch/target, at least 12 GiB free initially, 30 minutes, two concurrent extraction operations maximum. The completed manifest must fit first; abort with margin on resource growth rather than expanding the scope. Redirect temporary paths into dedicated scratch. These are proposed stop bounds, not sufficient-capacity or duration claims. FFMS2/libass builds, Windows, another configuration and SDK/cache publication are outside this experiment.

## CMake acquisition and execution stages

1. **Complete the lock without credentials.** Use public metadata/HEAD/checksum sidecars to capture installer identity, all repository XML, per-component or unified metadata, scripts/licenses, full component graph, exact data URLs/lengths/SHA-256 and publisher SHA-1 sidecars. Preserve original XML and timestamped names. Audit dynamic repository references and extraction destinations. The eight-base-archive preflight lock is insufficient.
2. **Acquire verified objects without credentials.** A CMake provisioner, reached before `project()` enables compilers, checks the lock, then uses `file(DOWNLOAD ... EXPECTED_HASH SHA256=...)`, verifies lengths, bounds total acquisition and stages a local repository tree. Reject changed/missing objects instead of adopting new metadata. Do not run repogen or modify dependencies. Retention/redistribution terms must be settled before publishing or sharing any SDK mirror; this experiment publishes no SDK.
3. **Invoke the official installer only after authorization.** Separate argv entries supply `--root <scratch>/qt`, `--cache-path <scratch>/metadata-cache`, `--set-temp-repository <comma-separated file:///snapshot roots>`, `--no-default-installations`, `--no-force-installations`, `--max-concurrent-operations 2`, exact package IDs and authorized acceptance options. No user controller script, blanket default answers or disabled disk checking. This assembled invocation remains untested. [IFW options](https://doc.qt.io/qtinstallerframework/ifw-cli.html).
4. **Verify consumption and closure.** Local repositories contain only locked metadata and allowlisted payloads. Restrict/observe outbound data access while separately accounting for required authentication endpoints; the repository option is not a network sandbox. Record resolved/installed IDs/revisions, accessed paths and exit status. Unknown payloads fail against the local allowlist. Do not claim an established dry-run resolver command; a failed run may leave the disposable target partially populated. If byte-consumption enforcement cannot be shown, that gate remains failed.
5. **Run a small Qt proof from a credential-free process.** Check qmake/qtpaths and CMake package origins; compile a tiny Quick/QML, Multimedia, ShaderTools and Linguist consumer, generate/load a QM and launch synthetic content. Record host libraries and actual platform-plugin mode. Provisioning, compilation, deployment, native desktop, media, cold/warm reconstruction and corrupt-input rejection are separate gates.

Proposed public entry: `cmake --workflow --preset qt-frozen-linux-feasibility`, explicitly sequencing provisioning/configure/build steps. Bare `cmake --preset` configures only. Keep the JWT exclusive to the installer subprocess, absent from later compiler/build/upload processes. A separate installation-only CMake invocation/CI step is preferable if the driver cannot guarantee that lifecycle; the one-command contract stays open until demonstrated. No project bootstrap script, aqt, distro Qt or vcpkg Qt substitution is introduced.

## Minimal CI authorization and next action

A fresh `gh secret list -R altqx/hikari --json name` returned `[]` on 2026-09-27. The owner must provide an authorized, unexpired token under the proposed GitHub secret name **`QT_INSTALLER_JWT_TOKEN`**, outside agent logs/source. The installer consumes that environment variable; never put its value in argv or CMake cache, print/export it, inspect claims or read a developer account cache. Local MaintenanceTool history does not authorize the ephemeral CI login. Secret presence alone does not prove validity. [Qt authentication](https://doc.qt.io/qt-6/get-and-install-qt-cli.html#providing-login-information).

Owner authorization must cover the pinned open-source license/obligation set and permit `--accept-licenses`, separate `--accept-obligations`, and `--confirm-command` for this isolated run. This plan does not grant it. Changed license bytes require fresh review; no paid entitlement or purchase is assumed. Documented optional responses include `telemetry-question=No` and `AssociateCommonFiletypes=No`; fail/stop installation errors instead of blanket default acceptance. `installationErrorWithIgnore` offers Retry/Ignore, so an unattended prompt must time out/fail for review rather than choose Ignore. The complete prompt set is not characterized. [Unattended options](https://doc.qt.io/qt-6/get-and-install-qt-cli.html#options-for-unattended-usage).

A future branch-local job keeps SHA-pinned actions, `contents: read`, no persisted checkout credentials, no PR trigger, finite timeout and no SDK/binary cache/release publication. Only the install step receives the named secret. Publish sanitized package IDs/hashes/sizes/exit/failure-stage and runner/tool identities; exclude raw installer/account logs, caches, environments and JWT data. Ubuntu runner versions are observed, not immutable. Prior evidence found glibc 2.43 and CMake 4.4.3; headless execution cannot close Fedora or native desktop qualification.

**Concrete next action:** implement the metadata-only lock-completion stage and publish its computed acquisition list first. Complete the reviewable CMake/CI driver within the authorized feasibility task and stated resource bounds, then request only the missing Qt license/obligation authorization and owner-managed secret needed for the isolated installer run. Public metadata inspection and bounded preparation need no repeated permission. Unknown nodes, missing scripts, changed metadata or absent installer authorization stop that stage with evidence; they do not authorize a larger download or another provisioning mechanism.
