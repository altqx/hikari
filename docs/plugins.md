# Plugins

A plugin extends HikariSub itself through the [Hikari API](api.md). The decision is [ADR 0022](adr/0022-hikarisub-plugins.md). This page is the contract for the package, install and signatures (PL1, [#413](https://github.com/altqx/hikari/issues/413)) and for running a plugin (PL2, [#414](https://github.com/altqx/hikari/issues/414)). Documents and selections (PL3), panels and the full plugin manager (PL4), and files and network (PL5) come later.

## The package

A `.hikariplugin` file is a zip. It holds `hikari-plugin.json` at its top level and the plugin's files beside it.

- Entry names are `/`-separated paths of letters, digits, spaces and `_ . - +`. HikariSub refuses an absolute path, `.` or `..`, a backslash or colon, a name ending in a dot or space, a Windows device name (`CON`, `NUL`, `COM1`…), two names that differ only in case, a file whose name is also a folder, and a link.
- A package holds at most 4096 files and 64 MB unpacked.
- `main` must be one of the package's files.
- The marketplace's media rules below hold for `icon`, `screenshots` and `README.md`.

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
| `summary` | no | One line, at most 100 characters and no control characters, for the marketplace's cards and the plugin page. |
| `icon` | no | A package file: a 256 × 256 px PNG or a square SVG. |
| `screenshots` | no | One to six `{path, caption}` objects. `path` is a package `.png` or `.jpg`; `caption`, at most 120 characters, shows under the image and is its alt text. |
| `publicKey` | no | The author's minisign public key, the base64 line of its `.pub`. |
| `permissions` | no | `documents.all` (the reason), `network` (`hosts` and a `reason`), `files` (folder requests, each `id`, `title`, `writable`, `reason`). |
| `contributes` | no | `commands` and `panels` (`id`, `title`), and `settings` (`boolean`, `integer`, `number`, `string` or `choice`, each with a `default`). |

Beyond the schema, an id repeated within `files`, `commands`, `panels` or `settings` is refused, and so is a setting default outside its `options` or its `minimum`/`maximum`.

### Media and README

The registry CI enforces these rules too ([#664](https://github.com/altqx/hikari/issues/664)). A breach refuses the package and names the file.

- **Paths.** `icon` and every screenshot `path` name a file in the package, with the case matching exactly. A screenshot path appears once.
- **PNG icon.** A real PNG (its signature and an `IHDR`), exactly 256 × 256 px, and at most 256 KiB.
- **SVG icon.** At most 64 KiB of UTF-8 XML whose root is `<svg>` with a `viewBox` as wide as it is tall. It holds no `<script>` or `<foreignObject>`, no `on…` attribute, no `href` or `xlink:href` except `#…`, and no `url(` except `url(#…)`. Character references count as what they stand for, and a DOCTYPE with an internal subset is refused, since it can add attributes.
- **Screenshots.** A PNG, or a JPEG named `.jpg`, whose signature matches its extension. 16:9, from 1280 × 720 to 1920 × 1080 px, allowing for rounding (`abs(w × 9 − h × 16) ≤ 16`), and at most 2 MiB each. HikariSub reads the size from the `IHDR` or the JPEG frame header and decodes nothing.
- **Together.** The icon and screenshots come to at most 8 MiB, within the 64 MB package limit.
- **`README.md`.** Optional, at the package root under exactly that name: UTF-8 and at most 128 KiB. HikariSub checks only that. The site and the app render it as CommonMark with GFM tables, strikethrough and task lists. Raw HTML is escaped, images show as links, links keep only `https:`, `http:` and `mailto:` targets, and relative links become plain text.

A marketplace listing also needs `summary`, `README.md` and an SPDX `license`. The registry checks that, and HikariSub doesn't.

## Signatures

An author signs the `.hikariplugin` file with minisign and ships the `.minisig` beside it, with the same name plus `.minisig`. Both `ED` (prehashed) and legacy `Ed` signatures are read. `hikari_minisign` (`src/minisign`, on libsodium) checks them; the update check (MS1 [#656](https://github.com/altqx/hikari/issues/656)) uses the same library.

- **First install.** A signature is checked against the manifest's `publicKey`, and that key is pinned in `plugins.json`. A package with a signature but no `publicKey` is refused.
- **Update.** A signature is checked against the pinned key first. A signature by another key, valid against the new manifest's `publicKey`, needs the user to trust the new key; so does an unsigned package over a signed install. Re-trusting pins the new key, or none.
- **Refused outright.** A signature that does not match the package or its trusted comment, whatever the user trusts.
- **Unverified.** Every plugin installed from a file is a sideload and shows as unverified, signed or not; it never updates by itself. A signed one shows its key id, as minisign prints it (16 hex digits). Listed plugins, automatic updates, permission-increase consent, roll back and revocation come from the HikariSub marketplace (PL6 [#418](https://github.com/altqx/hikari/issues/418)), which checks its signed index and then this same pinned author key.

## Folders and `plugins.json`

| Path | Holds |
| --- | --- |
| `<user folder>/plugins/<id>/` | The installed files. An install stages them in `.staging-<id>` and swaps them in, so a failed install leaves the old copy. |
| `<user folder>/plugin-data/<id>/` | The plugin's data. It survives updates; uninstall asks whether to remove it. |
| `<cache>/plugins/<id>/` | The plugin's cache, cleared on every install and on uninstall. |
| `<user folder>/plugins.json` | Per id: `version`, `enabled`, the pinned `key`, the remembered folder `grants` and the `settings` a plugin or the user changed. |

An update keeps the enabled state, the grants whose ids the new manifest still asks for, and the setting values that still fit the new manifest. A `plugins.json` HikariSub can't read stops installs rather than forgetting pinned keys.

## The plugin manager

Automation > Plugins… (`GLOBAL_PLUGINS`, 5231) lists the installed plugins with their trust, turns each on or off, installs from a file after showing what the package asks for, and uninstalls. PL4 ([#416](https://github.com/altqx/hikari/issues/416)) gives it its full design.

## Running a plugin

Each enabled plugin runs in its own `hikari-plugin-helper` process, started when HikariSub starts or when the plugin is turned on, installed or updated. The helper uses the same framing and endpoint as the automation helper. A plugin's crash ends only its own helper.

- **Restricted (the default).** A fresh LuaJIT state with `string` (without `dump`), `table`, `math`, `bit`, `coroutine` and the base functions, minus `dofile`, `loadfile` and `module`. `load` and `loadstring` take text only, so bytecode never loads. There's no `ffi`, `io`, `os`, `debug`, `jit` or `package`, so no `package.loadlib` either. `require "a.b"` reads only `lua/a/b.lua` or `lua/a/b/init.lua` in the package, and only as text.
- **Unrestricted.** The full LuaJIT libraries, with `package.path` pointed at the package's `lua/` folder and `package.cpath` at its `lib/`. Installing one shows the full-trust warning.
- **Memory.** A counting allocator caps the state at 256 MB. When the plugin hits the cap, the code that ran gets "not enough memory" and the plugin keeps running.
- **Time.** The load, each command and each event runs one at a time. One that runs longer than 10 seconds is force stopped: HikariSub ends the helper and says so in a toast. Time HikariSub spends answering the plugin (an API call, a setting) doesn't count. The plugin's next command starts a new helper.

These limits come from process isolation and the restricted state; they are not an operating-system sandbox. That's PL7 ([#419](https://github.com/altqx/hikari/issues/419)).

### The helper protocol

The helper framing ([`helper_protocol.h`](../src/backends/include/hikari/backends/helper_protocol.h)) has an `Event` frame (17) in which the host pushes application events to a helper. It waits behind the helper's running work and is answered like a request, so an event handler can call back into HikariSub. A helper without an event handler answers `Unsupported`. The plugin protocol ([`plugin_protocol.h`](../src/backends/include/hikari/backends/plugin_protocol.h)) has two requests, `Load` and `Run`, plus events and four services: `Call`, `Notify`, `SetSetting` and `Subscribe`. When several events of one name wait, only the newest is delivered.

## The `hikari` global

| Member | What it does |
| --- | --- |
| `hikari.commands.register(id, fn)` | Gives a command from the manifest's `contributes.commands` its handler, once. An id the manifest doesn't declare is refused. |
| `hikari.commands.run(id)` | Runs a HikariSub command (`GLOBAL_SAVE_SUBS`), as `commands.run` does. |
| `hikari.call(operation, params)` | Calls any [Hikari API](api.md) operation as the plugin. It returns the result, or `nil` and the error `{code, message, data}`. Without `documents.all` the plugin sees the editing target only. |
| `hikari.events.on(name, fn)` | Calls `fn(data)` for each Hikari API event of that name (`document.changed`, `selection.changed`, `documents.changed`, `panel.changed`, `command.ran`). An unknown name is refused. |
| `hikari.notify(text, level)` | A toast and a Log panel entry under the plugin's name. `level` is `"info"` (the default), `"warning"` or `"error"`. `print` writes to the Log panel only. |
| `hikari.settings.get(id)`, `hikari.settings.set(id, value)` | The plugin's settings from `contributes.settings`. A value that doesn't fit the setting's type, options or bounds is refused, and kept values live in `plugins.json`. |
| `hikari.i18n.locale`, `hikari.i18n.t(key, ...)` | The UI language (`"pt_BR"`, `"en"`) and the text for `key` from `i18n/<locale>.json`, then `i18n/<language>.json`, then `i18n/en.json`, then the key itself. `%1` to `%9` take the arguments. Each file is a JSON object of strings. |
| `hikari.plugin.id`, `hikari.plugin.version`, `hikari.api_version` | Who the plugin is, and the Hikari API number. |
| `hikari.null`, `hikari.array(t)` | JSON `null`, and a table marked as a JSON array (so `{}` can be sent as `[]`). Arrays that come back from HikariSub are marked too. |

Tables go to HikariSub as JSON: a table with keys 1 to n is an array, one with text keys an object. Numbers must be finite, text must be UTF-8, and a table that holds itself is refused.

Errors carry the file and line inside the package (`lua/tools.lua:12: …`) and a traceback, never a path on disk.

## Commands and shortcuts

A command `greet` of `com.example.hello` is `plugin.com.example.hello.greet`. While the plugin runs, its commands sit in the Automation menu after Plugins…, in a submenu named after the plugin. In Options > Hotkeys, the **Plugin shortcuts** filter lists them. A plugin command's shortcut is global, and it is kept with the macro shortcuts in the hotkeys file. A shortcut whose plugin isn't installed or turned on stays listed with a note, ready for when it comes back.

When a command fails, its error shows in a toast. When a plugin fails to load, the toast says it didn't start, and its details hold the error. Errors in event handlers go to the Log panel only.
