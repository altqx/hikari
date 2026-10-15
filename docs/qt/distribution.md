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
| Arch Linux | AUR package `hikarisub-stable-bin` (stable releases) or `hikarisub-beta-bin` (every release), built from the tar.gz | `/opt/hikarisub`, `/usr/bin/hikarisub` | Notice: update with the package manager |

The release also carries `SHA256SUMS`, the AppImage's `.zsync` for external AppImage updaters, and `hikarisub-update.json`, the update manifest. Nothing is code-signed.

Each installed copy has `package-kind.txt` beside the executable: `nsis-user`, `nsis-machine`, `appimage`, `deb`, `rpm` or `arch`. No marker means a portable copy. About and `hikarisub --version` name the kind, so bug reports carry it.

The rewrite keeps its settings in the platform's user folders (`QStandardPaths`), whatever the kind. Removing or upgrading a package never deletes them, nor the update cache. The settings importer also looks in installed legacy's data folders, `%LOCALAPPDATA%\HikariSub\Legacy` and `~/.local/share/HikariSub/Legacy`.

## The Windows installer

- One installer, built with `makensis` 3.13 on Linux from the staged Windows payload, for both codebases. `packaging/windows/build-makensis.sh` builds it from the NSIS release's source and zip, pinned by SHA-256 in `tools.lock`: Ubuntu's `nsis` is 3.09, and NSIS before 3.11 has CVE-2025-43715 (a local user creates an elevated installer's plug-ins folder first and plants a DLL in it). The script `packaging/windows/hikarisub.nsi` has the same bytes on both branches; the codebase's part is generated into `payload.nsh`.
- The user chooses **Just me** (the default; no administrator, `%LOCALAPPDATA%\Programs\HikariSub`, `HKCU`) or **All users** (one UAC prompt, `%ProgramFiles%\HikariSub`, `HKLM`). For that prompt setup starts its own file again elevated; it holds that file open with reads shared only from its start, and every folder above it with deletes not shared, so no other process can rename or replace the file, or rename a folder on the way, before UAC finds the path again; from a path through a junction or symbolic link, a network path, or without the holds, it says to run setup as administrator instead. An existing install fixes the choice; changing it means uninstalling first. An all-users install goes only to a new folder directly in Program Files, which takes Program Files' rights (or stays where the existing all-users install is): a folder elsewhere could have been made by a standard user, and one already in Program Files may be another program's with rights that let users change it, or hold a junction; either way the user could then replace the program every user starts. `/D=` anywhere else exits 2. A folder an uninstall left behind (it keeps files the user added) is not reused. The elevated relaunch and an all-users update do not read HKCU for a per-user install, since it is the HKCU of whichever administrator approved UAC.
- The program is `HikariSub.exe` in both codebases, so the Start menu shortcut, the App Paths key and the file associations stay valid when Qt replaces legacy. The Qt executable carries legacy's file-type icon resources with legacy's ids.
- Optional components: **Associate subtitle files** (on by default) registers `.ass .ssa .srt .sub` as an eligible handler and becomes the default only where no other app is; never `.txt`. **Desktop shortcut** is off by default.
- The payload ships the Qt runtime from `windeployqt` and the app-local MSVC runtime from the Visual Studio redistributable folder, never from System32. The installer runs no `VC_redist`, so a per-user install needs no administrator.
- An upgrade (legacy to legacy, legacy to Qt, Qt to Qt) installs over the previous files and removes the files of the previous payload that the new one lacks, using the `install-manifest.txt` each installer leaves. It never deletes a file it did not install. A downgrade asks first, and a silent downgrade is refused.
- Silent switches for the updater: `/S`, `/CURRENTUSER` or `/ALLUSERS`, `/UPDATE`, `/WAITPID=<pid>`, `/RELAUNCH`, `/D=<dir>`, with fixed exit codes. The installer waits for the running app to exit and never kills it.
- Uninstall removes the files, shortcuts, associations and registry keys it created, and never the user's settings, documents, recovery data or the update cache.
- An all-users uninstall started without administrator rights starts itself again through UAC only when the install folder is protected from the user, by the updater's rule (`packaging/windows/protected.nsh`, each right opened alone under the user's own token). Otherwise it says so and exits 4: in a folder the user can change, `package-kind.txt` and the uninstaller that would run elevated are the user's to rewrite.

## Update check

The check reads GitHub's release list, 50 releases to a page and four pages at most. It keeps the releases that are not drafts, have a `v` + SemVer tag, are newer than the running version and fit the channel ("Stable versions only" skips GitHub prereleases and SemVer prerelease tags on a stable build; a prerelease build always takes prereleases, as legacy does). It walks at most five of them, highest version first:

- A release with `hikarisub-update.json` has its manifest read (64 KiB at most). A manifest that does not arrive or breaks its rules skips the release. One with an unknown schema counts as no manifest.
- A release without a manifest (every release before this policy) takes its packages from the assets' GitHub digests, and is offered to the codebase its version names.
- A release whose manifest's `offer_to` does not name `qt` is skipped.

URLs are built from the tag and the asset name (`https://github.com/altqx/hikari/releases/download/v<V>/<asset>`), never taken from a reply. Downloads may be redirected only to `github.com`, `objects.githubusercontent.com` and `release-assets.githubusercontent.com` over https. The automatic check keeps legacy's schedule (a day after each check, "Remind me in a week").

What the first passing release offers depends on the package kind:

- **NSIS, AppImage, deb, rpm** with a package for the kind: the download. A package that needs a newer Windows (`min_os`) or glibc (`min_glibc`) than the system has is only named, with the requirement.
- **AUR:** "HikariSub `<V>` is available; you have `<old>`. You installed HikariSub from the AUR, so update it with your package manager or AUR helper, for example yay -Syu or paru -Syu." `aur-channel.txt` beside the executable names the package's channel: `hikarisub-beta-bin` ("beta") is told about prereleases and `hikarisub-stable-bin` ("stable") only about stable releases, whatever "Stable versions only" says, and the option is hidden for them. A prerelease that the install's package does not carry points at the AppImage instead, as that package carries stable versions only. Nothing is downloaded.
- **Portable**, or "Install updates automatically" off: the notice and the release page.

## Seamless update

"Install updates automatically" is on by default and shown only for the kinds that install. The automatic check then downloads without asking; the menu's check offers "Download and install", with a progress line in its place while it runs.

1. The package goes to `%LOCALAPPDATA%\HikariSub\Updates\<copy>\<V>\` or `${XDG_CACHE_HOME:-~/.cache}/HikariSub/Updates/<copy>/<V>/` (an AppImage's beside the AppImage, as `.<image name>.<V>.update`), as `<name>.part`. Its size and SHA-256 are checked as it arrives; a mismatch deletes it. `pending-<copy>.json` in the cache records it. The cache is the user's and shared by every copy they keep, so each copy has its own record and markers, named for the kind (`deb`, `nsis-machine`, ...) and, for an AppImage, a hash of its path. A copy never offers, installs or removes another copy's update, and clears only its own download folders.
2. A bar under the menus says "HikariSub `<V>` is ready to install", with What's new, **Restart now** and **Later**. It comes back at each start while the update waits.
3. Restart now runs the normal quit: the save review of every modified document, which can cancel it. Later installs at the next normal quit, without the restart; an AppImage at the start after that quit (below).
4. After the main window has closed, the file's size and hash are checked again, then it is handed off:
   - **NSIS:** the app starts its own installed `hikarisub.exe --hikari-apply-update <V> <file> <pid> <0|1>` and waits for it with a busy notice: elevated through UAC for "All users", unelevated for "Just me"; declining UAC keeps the update pending. `package-kind.txt` is no more protected than its folder, so UAC is asked only when the unelevated user can change nothing in the install folder or under it (no write, delete, `WRITE_DAC` or `WRITE_OWNER`, no reparse points) and can replace no folder on the way to it; otherwise the update is not installed automatically and the next start says to run the installer by hand. That process trusts nothing the user's side wrote, the pending record included. It checks that `<V>` is a release version newer than its own, downloads the release's `SHA256SUMS` itself over HTTPS from `https://github.com/altqx/hikari/releases/download/v<V>/SHA256SUMS` (following each redirect itself, only to GitHub's hosts, and taking a server's certificate only when it chains to a root in the local machine's stores: the user's own root store, which an unelevated process can add to, does not count), and, for "All users", copies the installer into a new folder under `%windir%\Temp` that only SYSTEM and administrators can write. It hashes the file through a handle that denies writes and renames and runs `setup.exe /S /UPDATE /WAITPID=<pid> [/RELAUNCH] /CURRENTUSER|/ALLUSERS /D=<install dir>` only when the digest is the one listed for `HikariSub-<V>-windows-x64-setup.exe`. A refused file is deleted with its pending update, and the app says why. The installer waits for the app and its mutex `Global\io.github.altqx.HikariSub.Running`, and restarts it unelevated. It holds `Global\io.github.altqx.HikariSub.Installing` from its start to its exit, and the handoff returns only once it does: an installed copy started meanwhile would hold its program open and stop the update, so it leaves at once and sets the installer's event `Global\io.github.altqx.HikariSub.StartAfterUpdate` (any signed-in user may set it: an all-users installer that another administrator approved runs in that account's profile), and the installer starts HikariSub as the desktop user when it is done, as with Restart now.
   - **AppImage:** the new image replaces `$APPIMAGE`, which stays as `$APPIMAGE.old`, and the old process waits up to 60 s for the new one to report its window (`--hikari-health=<file>`, named for the copy and the version), and otherwise puts the old image back and starts it. With Restart now that happens after the quit. An ordinary quit cannot watch the new image's first start, so it only writes `at-start-<copy>` in the update cache; the next start of the old image does the swap and the watch before anything else, passing on its files, and exits.
   - **deb, rpm:** "Installing update..." while `pkexec /opt/hikarisub/libexec/hikarisub-update-helper install <file> <version>` asks for the password. The helper takes no digest or package kind from its caller. It checks that the version is a release version newer than the installed package, copies the file into its root-owned `/var/cache/hikarisub-update`, downloads that release's `SHA256SUMS` itself with curl over HTTPS from the fixed GitHub URL, and installs the copy with apt, dnf or zypper only when its digest is the one listed for this kind's release file and its name, architecture and version are right. The packages depend on `curl` and `ca-certificates` for it. Restart now then starts `/usr/bin/hikarisub`. A dismissed prompt keeps the update pending; a refused package is deleted. Without `pkexec` or a polkit agent, the bar names the downloaded file, with Show in folder and Copy install command. The command is `sudo /opt/hikarisub/libexec/hikarisub-update-helper install <file> <version>`: the same helper and checks, run through sudo, which names the user in `SUDO_UID`. The package manager is never handed the user's own file, which another process of the user's could replace after the app's check.
5. A failed hand-off is told once at the next start (`last-result-<copy>.json`, which another copy never reads or removes; the installer writes `last-result-nsis-user.json` or `last-result-nsis-machine.json` for its scope). After Restart now, a hand-off that installed nothing (a changed download, an installer that could not start, an AppImage folder that became read-only) starts the version still installed, which says why. The next start of the new version deletes the update's leftovers.

No flow installs a version that is not newer than the running one.

## Automation folders

A portable copy keeps the Automation folder beside the program, as legacy does. An installed copy cannot write its program folder, so what scripts write (`?user`, `?local` and `?temp`, logs and autosaves) goes to its own Automation folder in the app's data folder (`%APPDATA%\HikariSub\HikariSub\Automation`, `~/.local/share/HikariSub/HikariSub/Automation`), laid out as the shipped one. Autoload scripts load from the shipped `automation/Autoload`, then from `automation/Autoload` and DependencyControl's `automation/autoload` there; a script there replaces a shipped script of the same name. Its `automation/Include` and `include` are searched before the shipped `Include` ([L6-user-automation-folder](compatibility-decisions.md)).

## Versions and the move from legacy

Legacy stays at 0.0.x and the rewrite starts at 0.1.0; `HIKARI_VERSION_SUFFIX` adds a prerelease (`0.1.0-beta.2`). deb and rpm versions put `~` before the prerelease inside the package (`0.1.0~beta.2-1`), never in the file name. Moving from legacy to the rewrite is an ordinary upgrade of one package (`hikarisub`, or `hikarisub-stable-bin` and `hikarisub-beta-bin` on the AUR, whose `pkgver` is the version without its `-`: `0.1.0-beta.2` is `0.1.0beta.2`, which vercmp sorts above every legacy 0.0.x and below `0.1.0`) or one installation.

A rewrite release names `"offer_to": ["qt"]` until the owner sets `HIKARISUB_OFFER_QT_TO_LEGACY`; then `["qt", "legacy"]`. Legacy 0.0.1 knows nothing of manifests and offers the highest stable SemVer release it finds, so no stable rewrite release is published before the rewrite is meant for every legacy user.

## Payload and licences

Packages are staged from CMake install rules and Qt's QML deployment support, and the staged tree is audited (Qt plugins and imports, FFMS2/FFmpeg, libass and font libraries, the audio adapter, dictionaries, translations, helpers). Qt is linked dynamically and shipped with its licence notices; the obligations of each module and dependency are checked before a release. The AppImage bundles every library the app needs; deb and rpm declare their host dependencies.

## Testing an update

A test build (`-DHIKARI_UPDATE_TEST_FEED=ON`; off in releases, whose CI fails if the binary names `HIKARISUB_UPDATE_FEED`) reads `HIKARISUB_UPDATE_FEED=<base URL>`: `<base>releases.json` for the list, `<base>download/v<V>/<asset>` for the files and `<base>releases/tag/v<V>` for the pages. It also reads `HIKARISUB_PACKAGE_KIND=<token>` in place of the marker (and `HIKARISUB_AUR_CHANNEL` in place of `aur-channel.txt`), so the AUR notice and the other flows can be tried without installing a package. `tools/update-feed/make-feed.py` builds such a feed from a folder of packages.

**Verification status:** the update logic and its hand-offs have unit tests; installers, packages and the update flows still need their release tests on every claimed platform.
