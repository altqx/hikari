# Plugins

A plugin extends HikariSub itself through the [Hikari API](api.md). The decision is [ADR 0022](adr/0022-hikarisub-plugins.md); this page is the package, install and signature contract that PL1 ([#413](https://github.com/altqx/hikari/issues/413)) built. Running a plugin (the helper, PL2) and the full plugin manager (PL4) come later.

## The package

A `.hikariplugin` file is a zip. It holds `hikari-plugin.json` at its top level and the plugin's files beside it.

- Entry names are `/`-separated paths of letters, digits, spaces and `_ . - +`. HikariSub refuses an absolute path, `.` or `..`, a backslash or colon, a name ending in a dot or space, a Windows device name (`CON`, `NUL`, `COM1`…), two names that differ only in case, a file whose name is also a folder, and a link.
- A package holds at most 4096 files and 64 MB unpacked.
- `main` must be one of the package's files.

## The manifest

`hikari-plugin.json` is checked against the published schema, [`docs/schemas/hikari-plugin.schema.json`](schemas/hikari-plugin.schema.json), which HikariSub compiles in. The schema uses only the [validator's subset](api.md) of JSON Schema 2020-12, and a test keeps it that way.

| Field | Required | Meaning |
| --- | --- | --- |
| `id` | yes | Reverse-DNS, lowercase, at least two parts: `com.example.line-tools`. It names the plugin's folders. |
| `name`, `version` | yes | The shown name, and a semantic version 2.0.0 that orders updates. |
| `api` | yes | The lowest Hikari API number the plugin needs. A higher number than HikariSub's `kApiVersion` is refused with both numbers. |
| `main` | yes | The Lua file the helper runs. |
| `authors` | yes | One or more names. The card's "author" is a list, as in research §6. |
| `tier` | no | `restricted` (the default) or `unrestricted`, which needs the user's full trust. |
| `description`, `license`, `homepage` | no | `license` is an SPDX expression; `homepage` is `https://`. |
| `publicKey` | no | The author's minisign public key, the base64 line of its `.pub`. |
| `permissions` | no | `documents.all` (the reason), `network` (`hosts` and a `reason`), `files` (folder requests, each `id`, `title`, `writable`, `reason`). |
| `contributes` | no | `commands` and `panels` (`id`, `title`), and `settings` (`boolean`, `integer`, `number`, `string` or `choice`, each with a `default`). |

Beyond the schema, an id repeated within `files`, `commands`, `panels` or `settings` is refused, and so is a setting default outside its `options` or its `minimum`/`maximum`.

## Signatures

An author signs the `.hikariplugin` file with minisign and ships the `.minisig` beside it, with the same name plus `.minisig`. Both `ED` (prehashed) and legacy `Ed` signatures are read. `hikari_minisign` (`src/minisign`, on libsodium) checks them; the update check (MS1 [#656](https://github.com/altqx/hikari/issues/656)) uses the same library.

- **First install.** A signature is checked against the manifest's `publicKey`, and that key is pinned in `plugins.json`. A package with a signature but no `publicKey` is refused.
- **Update.** A signature is checked against the pinned key first. A signature by another key, valid against the new manifest's `publicKey`, needs the user to trust the new key; so does an unsigned package over a signed install. Re-trusting pins the new key, or none.
- **Refused outright.** A signature that does not match the package or its trusted comment, whatever the user trusts.
- **Unverified.** Until the curated index (PL6), every sideloaded plugin shows as unverified. A signed one shows its key id, as minisign prints it (16 hex digits).

## Folders and `plugins.json`

| Path | Holds |
| --- | --- |
| `<user folder>/plugins/<id>/` | The installed files. An install stages them in `.staging-<id>` and swaps them in, so a failed install leaves the old copy. |
| `<user folder>/plugin-data/<id>/` | The plugin's data. It survives updates; uninstall asks whether to remove it. |
| `<cache>/plugins/<id>/` | The plugin's cache, cleared on every install and on uninstall. |
| `<user folder>/plugins.json` | Per id: `version`, `enabled`, the pinned `key` and the remembered folder `grants`. |

An update keeps the enabled state and the grants whose ids the new manifest still asks for. A `plugins.json` HikariSub can't read stops installs rather than forgetting pinned keys.

## The plugin manager

Automation > Plugins… (`GLOBAL_PLUGINS`, 5231) lists the installed plugins with their trust, turns each on or off, installs from a file after showing what the package asks for, and uninstalls. PL4 ([#416](https://github.com/altqx/hikari/issues/416)) gives it its full design.
