# Distribution and update policy

**Decided by altqx on 2026-10-13; [ADR-0019](../adr/0019-installers-and-seamless-updates.md) records the decision.** It replaces the policy of 2026-09-27 ([ADR-0012](../adr/0012-distribution-and-updates.md): Inno Setup, notification-only updates, a signing gate for stable Windows releases). The [reviewed proposal of #40](proposals/distribution.md) and the [packaging research](https://github.com/altqx/hikari/blob/07f7005f742a7d44c6b169d11b90e0d2c62b7fea/docs/research/packaging.md) remain the review record. The policy applies to both codebases: legacy (the wx app on `main`, versions 0.0.x) and the Qt rewrite (versions 0.1.0 and later), which replaces legacy. Follow the accepted [CMake build](build.md) and [platform policy](platform-policy.md).

## Packages

Every release of either codebase publishes these x64 assets on GitHub Releases, with a `SHA256SUMS` file and the update manifest `hikarisub-update.json`:

| Asset | Name | Install and update |
| --- | --- | --- |
| Windows installer | `HikariSub-<V>-windows-x64-setup.exe` | NSIS, unsigned. Installs and updates itself |
| Windows portable | `HikariSub-<V>-windows-x64.zip` | Notify only |
| AppImage | `HikariSub-<V>-linux-x86_64.AppImage` (and its `.zsync`) | Replaces itself |
| Debian, Ubuntu | `hikarisub-<V>-linux-amd64.deb` | Installs through `pkexec` |
| Fedora, openSUSE | `hikarisub-<V>-linux-x86_64.rpm` | Installs through `pkexec` |
| Linux portable | `hikarisub-<V>-linux-x86_64.tar.gz` | Notify only. Also the AUR package's source |

- **Arch Linux** installs `hikarisub-bin` from the AUR. Its `PKGBUILD` and `.SRCINFO` live in the repository, are built and checked with `makepkg` in an Arch container by the release job, and are pushed to the AUR by the owner. It carries stable releases only. No Arch binary package is a release asset.
- **Native packages** (`.deb`, `.rpm`, AUR) install to `/opt/hikarisub` with a `/usr/bin/hikarisub` link, and declare every host library the app needs (GStreamer plugins included), so the package manager makes the app ready to use.
- **The AppImage** bundles the app's libraries so that it starts on the major distributions. The Qt build floor is AlmaLinux 9 with `gcc-toolset-14` (glibc 2.34).
- File names use the SemVer version `<V>`; the `.deb` and `.rpm` metadata use the `~` prerelease form (`0.1.0~beta.2-1`), which sorts correctly in dpkg and rpm.
- Every package installs and starts with the network off. End users need no Qt account, compiler or online installer.

Flatpak and store submission stay deferred.

## The Windows installer

- One installer, built with `makensis` on Linux from the staged Windows payload, for both codebases. The script `packaging/windows/hikarisub.nsi` has the same bytes on both branches; the codebase's part is generated into `payload.nsh`.
- The user chooses **Just me** (the default; no administrator, `%LOCALAPPDATA%\Programs\HikariSub`, `HKCU`) or **All users** (one UAC prompt, `%ProgramFiles%\HikariSub`, `HKLM`). An existing install fixes the choice; changing it means uninstalling first.
- The program is `HikariSub.exe` in both codebases, so the Start menu shortcut, the App Paths key and the file associations stay valid when Qt replaces legacy. The Qt executable carries legacy's file-type icon resources with legacy's ids.
- Optional components: **Associate subtitle files** (on by default) registers `.ass .ssa .srt .sub` as an eligible handler and becomes the default only where no other app is; never `.txt`. **Desktop shortcut** is off by default.
- The payload ships the Qt runtime from `windeployqt` and the app-local MSVC runtime from the Visual Studio redistributable folder, never from System32. The installer runs no `VC_redist`, so a per-user install needs no administrator.
- An upgrade (legacy to legacy, legacy to Qt, Qt to Qt) installs over the previous files and removes the files of the previous payload that the new one lacks, using the `install-manifest.txt` each installer leaves. It never deletes a file it did not install. A downgrade asks first, and a silent downgrade is refused.
- Silent switches for the updater: `/S`, `/CURRENTUSER` or `/ALLUSERS`, `/UPDATE`, `/WAITPID=<pid>`, `/RELAUNCH`, `/D=<dir>`, with fixed exit codes. The installer waits for the running app to exit and never kills it.
- Uninstall removes the files, shortcuts, associations and registry keys it created, and never the user's settings, documents, recovery data or the update cache.

## Updates

The app reads the GitHub release list, keeps the releases newer than itself on its channel (stable by default; prereleases by opt-in), and reads each candidate's manifest. A release whose manifest does not name the app's codebase in `offer_to` is skipped. Releases from before the manifest fall back to the asset names and GitHub's `digest` values. Failed or network-disabled checks stay distinct from "up to date", and editing never needs the network.

What happens next depends on how the app was installed, which a `package-kind.txt` marker beside the executable records:

- **NSIS, AppImage, `.deb`, `.rpm`:** the app downloads the matching asset in the background, verifies its size and SHA-256, and shows "Update ready" with **Restart now** and **Later**. Restart now runs the normal quit, with the save prompt for every modified document; cancelling it cancels the update. Later installs the update at the next normal quit. The NSIS installer runs silently in the install's scope (one UAC prompt for an all-users install) and relaunches the app unelevated; the AppImage replaces its own file; `.deb` and `.rpm` install through `pkexec` running apt, dnf or zypper, with a small root helper and a polkit policy. Without `pkexec` the app opens the download and offers the install command to copy.
- **AUR:** notify only. The app says that a new version is available and tells the user to update with the package manager or AUR helper (for example `yay -Syu` or `paru -Syu`). It never downloads or installs anything.
- **Portable zip and tar.gz:** notify only, with the release page.

"Install updates automatically", a new setting on by default, turns the install flows back into notification. The automatic check schedule, "Remind me in a week" and the stable/prerelease choice keep their behaviour. No flow installs a version that is not newer than the running one. Download URLs are built from the tag and the asset name, never taken from the reply, and redirects are followed only to GitHub's download hosts.

## Qt replacing legacy

- Versions: legacy stays at 0.0.x, Qt starts at 0.1.0, so every Qt version sorts above every legacy version and the version names the codebase.
- Qt releases name only `qt` in `offer_to` until the owner sets `HIKARISUB_OFFER_QT_TO_LEGACY`; from then on, legacy builds that read manifests offer them.
- Legacy 0.0.1 reads no manifest and offers the highest stable release by notification. **No stable Qt release is published before Qt is meant for every legacy user.** Qt prereleases reach only legacy users who opted into prereleases.
- The AUR package follows legacy until the owner opens the gate, then Qt only; its name never changes.
- Installed legacy builds keep their settings in `%LOCALAPPDATA%\HikariSub\Legacy` or `~/.local/share/HikariSub/Legacy`, since installed program folders are read-only; the Qt settings importer reads them there. Qt keeps its state in the platform's user folders in every package, and has no sibling-data portable mode.

## Payload, trust and release gates

The payload is staged from the build and audited: every direct import of every shipped binary must resolve inside the payload or to an operating-system library, and Linux packages carry only `$ORIGIN` run paths. Dynamic Qt deployment keeps its license notices and source obligations ([Qt licensing](https://doc.qt.io/qt-6/licensing.html), [Qt's LGPL obligations](https://www.qt.io/development/open-source-lgpl-obligations)).

Nothing is code-signed for now. The SHA-256 published by GitHub, fetched over HTTPS, is the only integrity check of a download; it detects corruption on the way, not a compromised release account. A signature of `hikarisub-update.json` can be added later as a separate asset without changing the manifest schema.

The owner controls tags, releases, the `offer_to` gate and the AUR push. Release gates cover install, upgrade over legacy and over itself, the silent update in both scopes, uninstall, each Linux format in its distribution containers and the Arch package.

**Verification status:** this records the decision. Each installer, package and update flow still needs its own build and test evidence before a release.
