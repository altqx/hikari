# Qt baseline and platform support

**Accepted 2026-09-27.** In the live planning session, altqx chose **“Advance Qt; explicitly retire Windows 10 later.”** This resolves [the platform-policy decision](https://github.com/altqx/hikari/issues/37); [ADR 0004](../adr/0004-qt-baseline-and-platform-support.md) records the choice. This specification sets future release requirements and does not assert that any platform qualification has passed.

## Qt adoption

Pin **Qt 6.11.2** for the current prototypes and initial reproducible build. Move to **Qt 6.12 stable after its actual publication and successful qualification**, then advance to later Qt versions through the same gates. Advancement is deliberate, rather than an automatic update to every newest release.

As checked on 2026-09-27, Qt's [6.11 release schedule](https://wiki.qt.io/Qt_6.11_Release) records 6.11.2 released on August 18. Its [6.12 schedule](https://wiki.qt.io/Qt_6.12_Release) records a release candidate and a September 30 final target, with no realized final date. A planned release or release candidate does not satisfy the stable-publication gate. These observations supplement the [completed Qt survey](https://github.com/altqx/hikari/blob/3c1e557af5deb52a4fcfdc3f169b9da7afa67776/docs/research/qt6.md).

## Supported platforms and Windows 10 retirement

Windows 10 x64 remains an **initial first-class target**, alongside Windows 11 x64, the current Ubuntu LTS and Fedora. Windows 11 x64, current Ubuntu LTS and Fedora remain the intended first-class targets as Qt advances. Each release must identify the exact operating-system versions, architectures, compiler/SDK revisions and Linux runtime requirements it qualifies; these names do not promise every compiler, older distro or architecture combination.

Qt's [current platform matrix](https://doc.qt.io/qt-6/supported-platforms.html) lists Windows 10 1809+ and Windows 11 x64 with MSVC 2022 or MinGW-w64 13.1, and states that **Qt 6.12 will be the last version supporting Windows 10**. Its listed Ubuntu versions are 22.04 and 24.04; Fedora is not listed. HikariSub's current-Ubuntu-LTS and Fedora commitment therefore requires its own pinned build, packaging and qualification jobs wherever the chosen combination is outside Qt's published matrix. Qt's matrix alone does not qualify HikariSub, FFMS2, libass or the selected adapters.

The mainline may advance beyond the last Windows-10-compatible Qt. **Before doing so, record an explicit Windows 10 retirement decision and publish a notice** identifying the final supported HikariSub/Qt combination, the support boundary and users' migration options. No retirement date or notice period has been chosen. Windows 10 retains its initial support obligations until that explicit change; a failed or unverified build cannot silently retain a supported label.

This supersedes any interpretation that Windows 10 must indefinitely receive every future mainline release. A shared Qt ceiling and a separate Windows 10 maintenance branch are not defaults; either would require another explicit decision. Preserve portable boundaries and a possible macOS port. Initial Windows/Linux delivery does not wait for macOS, and decisions must not block a later port.

## Release and maintenance gates

Before adopting a Qt release, verify public artifacts, licenses and the actual platform matrix; pin the complete kit and reproducible source/artifact identities. Rebuild FFMS2, libass and selected adapters. Require clean-machine packaging and relevant QML, keyboard/IME, accessibility, DPI, custom-rendering, media-clock and performance checks on every claimed target, including Windows 10 until retirement. Review private/QRhi API changes explicitly. Failed qualification keeps the last qualified kit available for rollback; publication alone is insufficient. Apply relevant gates to patch updates too. Detailed test coverage and performance budgets remain separate decisions.

The release owner must name a maintainer responsible for dependency advisories, public/community patch provenance, backport review, reproducible builds and regressions for every supported line, including Windows 10 until retirement. This document assigns that responsibility requirement, not an unconfirmed person. If necessary fixes or qualified tools become unavailable, expose the gap and obtain an explicit support decision before shipping with an unsupported claim.

No paid Qt purchase is assumed. Qt's [release policy](https://doc.qt.io/qt-6/qt-releases.html) distinguishes public initial LTS patches from immediate commercial extended-LTS access; its five-year commercial term is not a promise of five years of free patches. Community maintenance has an owner and verification cost, not a guaranteed upstream supply.
