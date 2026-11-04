---
status: accepted
amends: 0006
---

# Add HikariSub plugins: a LuaJIT format in isolated helpers

On 2026-10-28 altqx settled HikariSub plugins in the wave-8 grilling session ([#410](https://github.com/altqx/hikari/issues/410), card [PL0 #412](https://github.com/altqx/hikari/issues/412)). A plugin is a new HikariSub format that extends the application itself: it adds commands and panels, reads and edits Documents, and reaches files and the network only through HikariSub. The options and prior art are in the [plugins research note](../research/hikari-plugins.md); where it differs from this record, this record wins.

This supersedes, for plugins only, the [architecture](../architecture.md) rule that the foundation needs no third-party plugin SDK, the map's exclusion of a general plugin ecosystem, and the [backends](../backends.md) rule against a general extension SDK. Backend adapters and the Qt plugins a build ships are unchanged.

## Automation stays as it is

Plugins are a second runtime beside Aegisub automation, not a replacement. Automation scripts and DependencyControl keep running in today's host, unchanged ([ADR 0006](0006-isolated-lua-automation-host.md), [automation](../automation.md)). A plugin has no `aegisub` global and no access to automation scripts.

## The Hikari API

Plugins call the one typed, versioned Hikari API ([HA1 #411](https://github.com/altqx/hikari/issues/411)) through LuaJIT bindings. AI tools, the MCP server and node-editor nodes use the same API. It covers Documents, selection, panels and their state, commands, settings, video frames, audio and granted files.

- The API version is a running number. It is 1 when HA1 is done, and every release that grows the API bumps it.
- A plugin declares the lowest API number it needs.
- Changes are add-only. A removal is deprecated for at least one number first, and a plugin that uses a deprecated member gets a warning when it loads.

## Isolation and tiers

- **Restricted (the default).** Each plugin runs in its own helper process, in a restricted LuaJIT state with no `ffi`, `io`, `os` or `debug`, and no bytecode loading.
- **Unrestricted.** FFI and native libraries are allowed behind a full-trust prompt, and the plugin carries a warning badge in the index.
- **Panels.** A restricted plugin builds panels and dialogs from a declarative widget tree that HikariSub renders, including a canvas draw list. Custom QML panels are allowed in the unrestricted tier only.

## What a plugin reaches

- **Documents.** The active subtitle and its selection. Other open Documents need the `documents.all` permission.
- **Events and edits.** A plugin receives events. Its edits apply as one undo step, and only one transaction applies at a time, application-wide. This extends ADR 0006's one active macro to every staged transaction, macro or plugin.
- **Files.** Handles from pickers and drag and drop, the plugin's own `data/` and `cache/` folders, and folder grants the user asked HikariSub to remember.
- **Network.** HTTPS to the hosts the manifest declares, and nothing else. HikariSub makes the requests, and one global switch turns plugin network access off.

## Package

- A `.hikariplugin` file is a zip holding `hikari-plugin.json`, a manifest validated by a published JSON Schema.
- A plugin installs to `plugins/<id>/` in the user folder, and its data lives in `plugin-data/<id>/`.
- Panel ids are `plugin:<id>/<panel>`, outside the numbered panel registry. A saved layout keeps a placeholder for the panel of a plugin that isn't installed.
- Commands are `plugin.<id>.<command>`, and the shortcut editor has a Plugins filter.

## Distribution and signatures

On 2026-11-02 altqx replaced the curated index repository with the HikariSub plugin marketplace. It is run only by us, and authors ship updates through it.

- **One signed index.** HikariSub reads a single `index.json` with a minisign signature from our marketplace key, which is separate from the update-manifest key. The index lists each plugin, its versions, package URL, SHA-256, permissions, API number and author key. HikariSub downloads packages only from our hosts. The index first comes from a repository of ours and later from the hosted marketplace, at the same address, with no change to the app.
- **Authors.** Authors sign every package with minisign, and HikariSub checks both the index signature and the author's key, which is pinned at first install. A plugin's first listing is reviewed by us. After that, the author publishes updates themselves, signed with the pinned key, after automatic checks. An update that asks for more permissions (network hosts, files, `documents.all`, the unrestricted tier) goes back to review. A change of author key goes through us.
- **Updates.** Plugin updates install automatically by default, with a global switch and one per plugin. They download in the background and apply when the plugin isn't running or at the next start. An update that asks for more permissions waits for the user's consent, and the user can roll back to the previous version.
- **Revocation.** The index can revoke a version or a whole plugin. HikariSub disables it at the next check, says why, and offers the last good version.
- **Privacy.** "Check for plugin updates" is a setting of its own, on by default. HikariSub fetches the whole index and decides locally, so it never sends the list of installed plugins.
- **Other sources.** There are no third-party feeds. A `.hikariplugin` file can still be sideloaded, behind an "unverified" warning, and it never updates by itself. If the same id with the same author key appears in the marketplace, HikariSub offers to move it there.
- **v1 scope.** Free plugins only, with no ratings, reviews or payments. A listing shows the name, author, description, version, permissions, API number, screenshots and a source link.
- **Hosting later.** The hosted marketplace (author accounts, upload, the review queue, web pages on the site) is [PL10 #664](https://github.com/altqx/hikari/issues/664), with its own grilling and ADR when altqx decides to host it.

## What we call it

v1 is the narrow API plus process isolation, and the docs and the app describe it in those plain terms. The OS sandbox of plugin helpers (Landlock on Linux, AppContainer on Windows) comes later, in [PL7 #419](https://github.com/altqx/hikari/issues/419). Nothing calls plugins "sandboxed" until it lands.

## Plugin first

altqx decided on 2026-11-04 that a new feature ships as an official plugin when it fits the plugin API and most users won't need it. Fitting the API means it needs only commands, panels, document edits, and files and network.

- **Stays native:** core editing, video, audio and timing, the document model, and anything that draws on or reads input from the video surface.
- **New cards** are checked against this rule. A card that could be a plugin says why it is native.
- **First official plugins:** macro chains, a keyframe generator and Git-ref line marks. Their cards are filed once the marketplace ([PL6 #418](https://github.com/altqx/hikari/issues/418)) ships.
- **Exception:** FA20's QC notes ([#577](https://github.com/altqx/hikari/issues/577)) stay native, because they mark Grid rows, add a Grid menu entry and move the video, which the plugin API can't do.

## Around the plugins

- **Analytics** ([AN1 #404](https://github.com/altqx/hikari/issues/404)) counts plugin use by feature id only.
- **The plugin manager** and the declarative panels ([PL4 #416](https://github.com/altqx/hikari/issues/416)) are a design gate: they need owner-approved light and dark mockups before production.
- **The site** gets a plugin-authoring guide with an API reference generated from the Hikari API ([PL9 #421](https://github.com/altqx/hikari/issues/421)).
- **Order:** HA1 comes first. Plugins and the node effect editor then run in parallel.
