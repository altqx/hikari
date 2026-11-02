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

- Plugins are listed in a curated index repository, where additions go through PR review.
- A `.hikariplugin` file can also be sideloaded, behind an "unverified" warning.
- v1 has no arbitrary feeds.
- Authors sign their packages with minisign, and HikariSub pins the author's key at first install.

## What we call it

v1 is the narrow API plus process isolation, and the docs and the app describe it in those plain terms. The OS sandbox of plugin helpers (Landlock on Linux, AppContainer on Windows) comes later, in [PL7 #419](https://github.com/altqx/hikari/issues/419). Nothing calls plugins "sandboxed" until it lands.

## Around the plugins

- **Analytics** ([AN1 #404](https://github.com/altqx/hikari/issues/404)) counts plugin use by feature id only.
- **The plugin manager** and the declarative panels ([PL4 #416](https://github.com/altqx/hikari/issues/416)) are a design gate: they need owner-approved light and dark mockups before production.
- **The site** gets a plugin-authoring guide with an API reference generated from the Hikari API ([PL9 #421](https://github.com/altqx/hikari/issues/421)).
- **Order:** HA1 comes first. Plugins and the node effect editor then run in parallel.
