---
status: accepted
supersedes: 0012
---

# Ship installers and native packages with seamless updates

On 2026-10-13 altqx decided how HikariSub is installed and updated, for the legacy app (0.0.x) and the rewrite (0.1.0 on) alike. This supersedes [ADR-0012](0012-distribution-and-updates.md): Inno Setup, notification-only updates and the signing gate for stable releases no longer apply. The [distribution policy](../qt/distribution.md) has the rules.

- **Windows:** an unsigned NSIS installer that asks "Just me" (no administrator, `%LOCALAPPDATA%\Programs\HikariSub`) or "All users" (Program Files), with silent switches for the updater, plus the portable ZIP.
- **Linux:** an AppImage that bundles every library the app needs; `.deb` and `.rpm` packages that declare their host dependencies; and two AUR packages built from the release tar.gz, which the release jobs publish: `hikarisub-stable-bin` for stable releases and `hikarisub-beta-bin` for every release, prereleases included. The portable tar.gz stays.
- **Seamless update** for NSIS, AppImage, deb and rpm installs: the app downloads the matching package in the background, checks its SHA-256 and shows "Update ready" with Restart now and Later; Later installs it at the next quit. deb and rpm install through the system password prompt (`pkexec` running apt, dnf or zypper); the AppImage replaces itself.
- **Notify only** for AUR installs, which are told to update with the package manager or AUR helper, and for the portable ZIP and tar.gz.
- **The rewrite replaces legacy** under the same identity: install folder, Uninstall key, package names and desktop id `io.github.altqx.HikariSub`. Every rewrite version is 0.1.0 or later and every legacy version 0.0.x, and each release's `hikarisub-update.json` names the codebases it may be offered to, so a rewrite release reaches legacy users only when the owner opens that gate.

Nothing is code-signed; SHA-256 from GitHub over HTTPS is the only integrity check. This records the decision; installers, packages and the update flows still need their release tests on every claimed platform.
