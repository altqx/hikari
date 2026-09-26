# Distribution and update policy proposal

**Proposal for [Choose distribution, signing and update policy for the Qt rewrite](https://github.com/altqx/hikari/issues/40), awaiting acceptance.** The [packaging research](https://github.com/altqx/hikari/blob/07f7005f742a7d44c6b169d11b90e0d2c62b7fea/docs/research/packaging.md) is source evidence, not a built installer or release test. Follow the accepted [CMake build](../build.md) and [platform policy](../platform-policy.md), including initial Windows 10 qualification and explicit later retirement.

## Package routes

Recommend one audited staging manifest feeding these initial x64 artifacts:

| Target | Proposed artifact and behavior |
| --- | --- |
| Windows installed | **Offline Inno Setup EXE**, per-user installation by default, with shortcuts, optional associations and uninstall. No machine-wide/admin installation by default. |
| Windows portable | **ZIP** of the same application payload, with an explicit portable-state marker. No automatic registry integration. |
| Ubuntu LTS / Fedora | **AppImage**, qualified on the exact supported releases; retain a relocatable tar.gz staging artifact for extraction/recovery and diagnostics. |

[Inno Setup](https://jrsoftware.org/isinfo.php) documents non-administrative installs, uninstall, registry/shortcut authoring and signing hooks. Invoke its pinned compiler from CMake packaging targets; an Inno definition is installer authoring, not a separate bootstrap/build pipeline. CPack archive generation can serve ZIP/tar outputs. A stock generator does not design safe upgrades or gather every runtime dependency. WiX/MSI adds enterprise deployment authoring; Qt IFW adds maintenance-tool/repository ownership; neither is needed for the existing notifier. Review the pinned Inno license for the owner's use; this proposal authorizes no purchase.

Defer Flatpak, native DEB/RPM and store submission from the first release. Flatpak remains the preferred later managed route, but needs a separate accepted distribution step: portals/sibling-media access, fontconfig differences, sandbox permissions and runtime compatibility are material to subtitle workflows. It must not silently replace the accepted pinned Qt SDK with a different runtime Qt. This deliberately narrows the research's two-format Linux recommendation to reduce initial maintenance. AppImage remains a portability claim to prove, not a guarantee supplied by its file extension.

## State, associations and offline use

Installed builds keep program files immutable and use platform user-data/config/cache/recovery locations. Portable mode redirects writable state to an explicit sibling data directory; an unwritable location produces an actionable error, not a silent switch. Preserve legacy data through the agreed copy/import process. Changing package type does not import settings implicitly or overwrite the source profile.

Installer associations are opt-in eligible-handler registration, not forced Windows defaults. Portable integration is a separate user action with a repair/remove path after relocation. Linux integration supplies desktop/MIME/icon metadata and preserves the user's default choices. Uninstall removes only application-owned integration and files; user subtitles, settings, recovery data and externally selected fonts remain.

Windows artifacts contain their required Qt/native payload and licensed app-local MSVC redistributable files, avoiding an admin/runtime download prerequisite for the per-user path. Verify that exact redistributable set and its servicing obligations; never harvest System32. AppImage retains declared host kernel/glibc/graphics requirements and a tested extraction fallback when mounting is unavailable. End users do not need a Qt account, compiler or online installer. Network-disabled installation/startup must work after download on a supported host; offline developer builds additionally require prefetched verified inputs.

## Updates and channels

Recommend **check-and-notify only**, preserving existing disabled-check, stable/prerelease and reminder behavior. An available update opens the validated release/download page after user action. Do not download, replace files, restart or install automatically. Failed/network-disabled checks are distinct from “up to date”; checking is optional and editing remains offline-capable.

Use immutable versioned GitHub Releases assets. Stable is the default channel; beta requires opt-in and identifies prereleases; development CI artifacts are explicitly unsupported test builds and never offered as stable updates. Match OS, architecture, package kind and minimum platform requirements before offering a release. Windows 10 retirement notices precede incompatible offers. Beta and stable can use separate installations/profiles; changing channel does not silently migrate or downgrade user data.

Updates use complete packages. Stop safely before replacing installed files; preserve the prior package and a pre-migration profile backup for recovery. Portable upgrades unpack beside the old version, then deliberately transfer compatible data. Never promise automatic rollback across an incompatible data migration. A future managed package must delegate installation/updates to its manager; adding WinSparkle or an internal updater requires a new explicit decision.

## Payload, trust and release gates

Stage from CMake install rules and Qt's QML deployment support, then audit actual Qt plugins/imports, FFMS2/FFmpeg, libass/font libraries, audio adapter, dictionaries, translations and helpers. Fail on missing assets, SDK paths or conflicting FFmpeg ABIs. Dynamic loading requires explicit inventory beyond linker inspection.

Recommend dynamic Qt deployment with a per-component license/source manifest, notices and corresponding source/build/patch materials. Qt documents LGPLv3/GPLv3 alternatives and GPL-only modules; dynamic linking alone does not establish compliance. Preserve applicable library replacement/relinking rights and installation information; signing must not become a prohibition on modified local builds. Verify the selected module/dependency obligations before release. [Qt licensing](https://doc.qt.io/qt-6/licensing.html), [Qt's LGPL obligations](https://www.qt.io/development/open-source-lgpl-obligations).

Recommend **trusted, timestamped Authenticode signing for public stable Windows installers and application binaries**; clearly labelled unsigned beta/test artifacts may precede it. Publish final SHA-256 manifests for all artifacts, with an owner-controlled signature/public-key verification procedure for direct downloads. Hashes beside downloads detect corruption, not independently establish publisher identity. Windows signing options include qualifying open-source programs and paid providers; eligibility, cost and SmartScreen reputation are not guaranteed. [Microsoft signing guidance](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/code-signing-options).

The owner controls publisher identity, provider/repository accounts, keys, recovery/rotation and protected release authorization. Credentials never enter source, documentation or untrusted PR jobs. No registration, certificate purchase, signing or deployment is authorized here.

Release gates cover clean-machine startup, offline install, upgrade/interruption/recovery/uninstall, portable relocation, associations, fonts/media/IME/accessibility, final signatures and source availability on every claimed target. Later macOS needs its own `.app` deployment, Developer ID/notarization and runtime qualification; portable interfaces must permit it.

**Decisions requested:** (1) accept the package bundle and deferred managed Linux packaging; (2) retain notification-only updates; (3) gate stable Windows releases on trusted signing while permitting labelled unsigned prereleases. No gate has been executed.
