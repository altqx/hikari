---
status: superseded
superseded_by: 0019
---

# Ship offline packages with notification-only updates

Superseded by [ADR-0019](0019-installers-and-seamless-updates.md) on 2026-10-13.

In the live review on 2026-09-27, altqx accepted the [distribution policy](../distribution.md): per-user offline Inno Setup and portable ZIP packages on Windows, AppImage plus a recovery tar archive on Linux, and notification-only updates, deferring managed Linux packages and internal installation automation to avoid additional initial maintenance. Public stable Windows installers and application binaries require trusted, timestamped Authenticode signing, while labelled unsigned prereleases are permitted and all direct-download artifacts require the defined hash/signature verification procedure. This creates owner-controlled signing and release-qualification obligations; it neither selects or purchases a provider nor proves clean-machine deployment, offline operation, licensing, upgrade/recovery or signing gates have passed.
