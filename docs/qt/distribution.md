# Distribution and update policy

**Decided by altqx on 2026-10-13; [ADR-0019](../adr/0019-installers-and-seamless-updates.md) records it.** It replaces the policy of 2026-09-27 ([ADR-0012](../adr/0012-distribution-and-updates.md): Inno Setup, notification-only updates, a signing gate for stable releases). The legacy app (0.0.x, branch `main`) and the rewrite (0.1.0 and later, branch `qt`) follow the same rules, so the rewrite can replace legacy in place.

## Packages

Every release publishes these assets, where `<V>` is the version without its `v`:

| Package kind | Asset | Installs to | Updates |
| --- | --- | --- | --- |
| Windows installer (NSIS, unsigned) | `HikariSub-<V>-windows-x64-setup.exe` | "Just me": `%LOCALAPPDATA%\Programs\HikariSub`, no administrator; "All users": `%ProgramFiles%\HikariSub` | In the app, silently |
| Windows portable | `HikariSub-<V>-windows-x64.zip` | Where it is extracted | Notice and release page |
| AppImage | `HikariSub-<V>-linux-x86_64.AppImage` | The file itself | In the app: it replaces itself |
| deb | `hikarisub-<V>-linux-amd64.deb` | `/opt/hikarisub`, `/usr/bin/hikarisub` | In the app, through `pkexec` |
| rpm | `hikarisub-<V>-linux-x86_64.rpm` | `/opt/hikarisub`, `/usr/bin/hikarisub` | In the app, through `pkexec` |
| Linux portable | `hikarisub-<V>-linux-x86_64.tar.gz` | Where it is extracted | Notice and release page |
| Arch Linux | AUR package `hikarisub-bin`, built from the tar.gz | `/opt/hikarisub`, `/usr/bin/hikarisub` | Notice: update with the package manager |

The release also carries `SHA256SUMS`, the AppImage's `.zsync` for external AppImage updaters, and `hikarisub-update.json`, the update manifest. Nothing is code-signed.

Each installed copy has `package-kind.txt` beside the executable: `nsis-user`, `nsis-machine`, `appimage`, `deb`, `rpm` or `arch`. No marker means a portable copy. About and `hikarisub --version` name the kind, so bug reports carry it.

The rewrite keeps its settings in the platform's user folders (`QStandardPaths`), whatever the kind. Removing or upgrading a package never deletes them, nor the update cache. The settings importer also looks in installed legacy's data folders, `%LOCALAPPDATA%\HikariSub\Legacy` and `~/.local/share/HikariSub/Legacy`.

## Update check

The check reads GitHub's release list, 50 releases to a page and four pages at most. It keeps the releases that are not drafts, have a `v` + SemVer tag, are newer than the running version and fit the channel ("Stable versions only" skips GitHub prereleases and SemVer prerelease tags). It walks at most five of them, highest version first:

- A release with `hikarisub-update.json` has its manifest read (64 KiB at most). A manifest that does not arrive or breaks its rules skips the release. One with an unknown schema counts as no manifest.
- A release without a manifest (every release before this policy) takes its packages from the assets' GitHub digests, and is offered to the codebase its version names.
- A release whose manifest's `offer_to` does not name `qt` is skipped.

URLs are built from the tag and the asset name (`https://github.com/altqx/hikari/releases/download/v<V>/<asset>`), never taken from a reply. Downloads may be redirected only to `github.com`, `objects.githubusercontent.com` and `release-assets.githubusercontent.com` over https. The automatic check keeps legacy's schedule (a day after each check, "Remind me in a week").

What the first passing release offers depends on the package kind:

- **NSIS, AppImage, deb, rpm** with a package for the kind: the download. A package that needs a newer Windows (`min_os`) or glibc (`min_glibc`) than the system has is only named, with the requirement.
- **AUR:** "HikariSub `<V>` is available; you have `<old>`. You installed HikariSub from the AUR, so update it with your package manager or AUR helper, for example yay -Syu or paru -Syu." A prerelease points at the AppImage instead, as the AUR package carries stable versions only. Nothing is downloaded.
- **Portable**, or "Install updates automatically" off: the notice and the release page.

## Seamless update

"Install updates automatically" is on by default and shown only for the kinds that install. The automatic check then downloads without asking; the menu's check offers "Download and install", with a progress line in its place while it runs.

1. The package goes to `%LOCALAPPDATA%\HikariSub\Updates\<V>\` or `${XDG_CACHE_HOME:-~/.cache}/HikariSub/Updates/<V>/` (an AppImage's beside the AppImage), as `<name>.part`. Its size and SHA-256 are checked as it arrives; a mismatch deletes it. `pending.json` in the cache records it.
2. A bar under the menus says "HikariSub `<V>` is ready to install", with What's new, **Restart now** and **Later**. It comes back at each start while the update waits.
3. Restart now runs the normal quit: the save review of every modified document, which can cancel it. Later installs at the next normal quit, without the restart.
4. After the main window has closed, the file's size and hash are checked again, then it is handed off:
   - **NSIS:** `setup.exe /S /UPDATE /WAITPID=<pid> [/RELAUNCH] /CURRENTUSER|/ALLUSERS /D=<install dir>`. "All users" asks UAC once; declining keeps the update pending. The installer waits for the app and its mutex `Global\io.github.altqx.HikariSub.Running`, and restarts it unelevated.
   - **AppImage:** the new image replaces `$APPIMAGE`, which stays as `$APPIMAGE.old`. With Restart now the old process waits up to 60 s for the new one to report its window (`--hikari-health=<file>`), and otherwise puts the old image back and starts it.
   - **deb, rpm:** "Installing update..." while `pkexec /opt/hikarisub/libexec/hikarisub-update-helper install <kind> <file> <sha256> <version>` asks for the password; the helper checks a root-owned copy and installs it with apt, dnf or zypper. Restart now then starts `/usr/bin/hikarisub`. A dismissed prompt keeps the update pending. Without `pkexec` or a polkit agent, the bar names the downloaded file, with Show in folder and Copy install command.
5. A failed hand-off is told once at the next start (`last-result.json`). The next start of the new version deletes the update's leftovers.

No flow installs a version that is not newer than the running one.

## Versions and the move from legacy

Legacy stays at 0.0.x and the rewrite starts at 0.1.0; `HIKARI_VERSION_SUFFIX` adds a prerelease (`0.1.0-beta.2`). deb and rpm versions put `~` before the prerelease inside the package (`0.1.0~beta.2-1`), never in the file name. Moving from legacy to the rewrite is an ordinary upgrade of one package (`hikarisub`, or `hikarisub-bin` on the AUR) or one installation.

A rewrite release names `"offer_to": ["qt"]` until the owner sets `HIKARISUB_OFFER_QT_TO_LEGACY`; then `["qt", "legacy"]`. Legacy 0.0.1 knows nothing of manifests and offers the highest stable SemVer release it finds, so no stable rewrite release is published before the rewrite is meant for every legacy user.

## Payload and licences

Packages are staged from CMake install rules and Qt's QML deployment support, and the staged tree is audited (Qt plugins and imports, FFMS2/FFmpeg, libass and font libraries, the audio adapter, dictionaries, translations, helpers). Qt is linked dynamically and shipped with its licence notices; the obligations of each module and dependency are checked before a release. The AppImage bundles every library the app needs; deb and rpm declare their host dependencies.

## Testing an update

A test build (`-DHIKARI_UPDATE_TEST_FEED=ON`; off in releases, whose CI fails if the binary names `HIKARISUB_UPDATE_FEED`) reads `HIKARISUB_UPDATE_FEED=<base URL>`: `<base>releases.json` for the list, `<base>download/v<V>/<asset>` for the files and `<base>releases/tag/v<V>` for the pages. It also reads `HIKARISUB_PACKAGE_KIND=<token>` in place of the marker, so the AUR notice and the other flows can be tried without installing a package. `tools/update-feed/make-feed.py` builds such a feed from a folder of packages.

**Verification status:** the update logic and its hand-offs have unit tests; installers, packages and the update flows still need their release tests on every claimed platform.
