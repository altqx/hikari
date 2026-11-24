# Plugins

A plugin extends HikariSub itself through the [Hikari API](api.md). The decision is [ADR 0022](adr/0022-hikarisub-plugins.md). This page is the contract for the package, install and signatures (PL1, [#413](https://github.com/altqx/hikari/issues/413)) and for running a plugin (PL2, [#414](https://github.com/altqx/hikari/issues/414)). It also covers documents and selections (PL3), panels and the full plugin manager (PL4), files and network (PL5, [#417](https://github.com/altqx/hikari/issues/417)), and the marketplace with its automatic updates, roll back and revocation (PL6, [#418](https://github.com/altqx/hikari/issues/418)). To write a first plugin step by step, start with the [guide](plugin-guide.md) (PL9, [#421](https://github.com/altqx/hikari/issues/421)); every operation and event is in the [Hikari API reference](api-reference.md).

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
| `permissions` | no | `documents.all` (the reason), `network` (`hosts` and a `reason`), `files` (folder requests, each `id`, `title`, `writable`, `reason`; the ids `data` and `cache` are the plugin's own), `vcs.read` (the reason). See [Files](#files), [Network](#network) and [Git](#git). |
| `contributes` | no | `commands` (`id`, `title`), `panels` (`id`, `title`, `area`: `left`, `right` or `bottom`, where the panel docks when first shown, `right` by default; and for a Full trust plugin `qml`, a package `.qml` file that makes it a [QML panel](#qml-panels)), `settings` (`boolean`, `integer`, `number`, `string` or `choice`, each with a `default`), and `nodes`, [node-effects nodes](#nodes). |

Beyond the schema, an id repeated within `files`, `commands`, `panels`, `settings` or `nodes` is refused, and so is a port name repeated across a node's inputs and outputs, or an input `default` that isn't of its type; a setting default outside its `options` or its `minimum`/`maximum` is refused too. A `qml` panel in a manifest that isn't `unrestricted` is refused with "Only Full trust plugins can ship QML panels." A package with QML panels must hold each panel's file, and every `.qml`, `.js` and `.mjs` file in it must keep to the [import rule](#qml-panels).

### Media and README

The registry CI enforces these rules too ([#664](https://github.com/altqx/hikari/issues/664)). A breach refuses the package and names the file.

- **Paths.** `icon` and every screenshot `path` name a file in the package, with the case matching exactly. A screenshot path appears once.
- **PNG icon.** A real PNG (its signature and an `IHDR`), exactly 256 × 256 px, and at most 256 KiB.
- **SVG icon.** At most 64 KiB of UTF-8 XML whose root is `<svg>` with a `viewBox` as wide as it is tall. It holds no `<script>` or `<foreignObject>`, no `on…` attribute, no `href` or `xlink:href` except `#…`, and no `url(` except `url(#…)`. Character references count as what they stand for, and a DOCTYPE with an internal subset is refused, since it can add attributes.
- **Screenshots.** A PNG, or a JPEG named `.jpg`, whose signature matches its extension. 16:9, from 1280 × 720 to 1920 × 1080 px, allowing for rounding (`abs(w × 9 − h × 16) ≤ 16`), and at most 2 MiB each. HikariSub reads the size from the `IHDR` or the JPEG frame header and decodes nothing.
- **Together.** The icon and screenshots come to at most 8 MiB, within the 64 MB package limit.
- **`README.md`.** Optional, at the package root under exactly that name: UTF-8 and at most 128 KiB. HikariSub checks only that. The site and the app render it as CommonMark with GFM tables, strikethrough and task lists. Raw HTML is escaped, images show as links, links keep only `https:`, `http:` and `mailto:` targets, and relative links become plain text.

A marketplace listing also needs `summary`, `README.md` and an SPDX `license`. The registry checks that, and HikariSub doesn't.

### Nodes

`contributes.nodes` lists Lua nodes for [node effects](node-effects.md#plugin-nodes), at most 64.

| Field | Required | Meaning |
| --- | --- | --- |
| `id` | yes | Unique in the plugin. The node's type is `plugin:<plugin id>/<id>`. |
| `title`, `summary` | `title` | What the Add node list and the Node panel show. |
| `file` | yes | A package `.lua` file with the node's code, at most 1 MiB. It reads its inputs and sets its outputs as globals, like a `lua.code` node. |
| `inputs` | no | Up to 32 `{name, type, default}` ports. `type` is `Number`, `Text`, `Colour`, `Alpha`, `Time`, `Point`, `Bool` or `Tags`; an input with a `default` gets a field. |
| `outputs` | yes | One to 32 `{name, type}` ports. |

The nodes run only while the plugin is turned on, not revoked, and signed by the key pinned at install. They run in the node sandbox, not the plugin's helper, whatever the plugin's tier. When a plugin's node file can't be read, its nodes are unavailable and the log says why.

## Signatures

An author signs the `.hikariplugin` file with minisign and ships the `.minisig` beside it, with the same name plus `.minisig`. Both `ED` (prehashed) and legacy `Ed` signatures are read. `hikari_minisign` (`src/minisign`, on libsodium) checks them; the update check (MS1 [#656](https://github.com/altqx/hikari/issues/656)) uses the same library.

- **First install.** A signature is checked against the manifest's `publicKey`, and that key is pinned in `plugins.json`. A package with a signature but no `publicKey` is refused.
- **Update.** A signature is checked against the pinned key first. A signature by another key, valid against the new manifest's `publicKey`, needs the user to trust the new key; so does an unsigned package over a signed install. Re-trusting pins the new key, or none.
- **Refused outright.** A signature that does not match the package or its trusted comment, whatever the user trusts.
- **Unverified.** Every plugin installed from a file is a sideload and shows as unverified, signed or not; it never updates by itself. A signed one shows its key id, as minisign prints it (16 hex digits). Plugins from the [marketplace](#the-marketplace) are checked against its signed index instead, and then against this same pinned author key.
- **By HikariSub.** HikariSub's own plugins have ids under the `hikarisub` handle and are signed with HikariSub's author key, C399EBCEA48BCB30, which HikariSub pins (`kOfficialKey`). Only a plugin with both shows "By HikariSub". A `hikarisub.*` package signed with any other key gets no badge.

## The marketplace

The HikariSub marketplace (PL6 [#418](https://github.com/altqx/hikari/issues/418)) lists plugins from [altqx/hikari-plugins](https://github.com/altqx/hikari-plugins). Its index is `https://marketplace.hikarisub.org/index.json`, signed by `index.json.minisig`. The index format is [docs/schemas/marketplace-index.schema.json](schemas/marketplace-index.schema.json), a copy of the registry's `schemas/index.schema.json`.

- **The marketplace key.** The index must verify with the marketplace key, `EF622DD4D472E463`, compiled into HikariSub (`plugins::kMarketplaceKey`). It is not the release key that signs HikariSub's own updates.
- **The trusted comment** must be `file:index.json serial:<serial> generated:<generated>`, matching the index's own fields, so a signature for another file or another index can't stand in.
- **Rollback.** HikariSub keeps the highest serial it has accepted in `<user folder>/marketplace.json` and refuses a lower one.
- **Privacy.** HikariSub downloads the whole index and never sends which plugins are installed: two GETs with no query string, no cookies and no cache. Icons and screenshots come only from the marketplace's own origin, and only while the Marketplace view shows them. HikariSub fetches each one itself, with the same request rules, checks it against the media rules above, and keeps it in memory only.
- **When.** At start and every 24 hours while "Check for plugin updates" (`plugins.checkUpdates`, on by default) is on, and whenever the Marketplace view or Check for updates asks.
- **Install.** HikariSub downloads the listed version's `url`, which must be on the marketplace's origin and no larger than `size`. Then it checks `size` and `sha256`, runs the package checks, and checks that the manifest has the listed id, version, tier and API. The manifest's `publicKey` must be the listing's `publicKey` and, for an installed plugin, the pinned key. The marketplace packages carry no `.minisig`: the signed index vouches for the listing's author key. A downgrade is refused, except Install <last good> after a revocation. The install question always opens first, except for an [automatic update](#updates-roll-back-and-revocation).
- **Source.** `plugins.json` records `"source": "marketplace"` for a marketplace install; a missing `source` means a file. Installing from a file over a marketplace copy makes it a file install again.

### Installing from the website

The marketplace website's Install in HikariSub opens `hikarisub://plugin/install?id=<id>` (MK1 [#667](https://github.com/altqx/hikari/issues/667)). [Distribution](distribution.md#packages) says how each package registers the scheme.

- **The only link.** HikariSub takes exactly that form, with an id that fits the manifest's pattern and 100 characters at most. It ignores anything else (another action, an extra parameter, a fragment, a percent-encoded character) and only notes it in the `hikari.links` log. The link names nothing but the id.
- **Where it goes.** A start with a link hands it to the HikariSub already running, like a file, and the last active window takes it. A start that opens the first window takes it once that window is up. Of several links that come together, only the last counts.
- **What it does.** HikariSub opens the plugin manager at the Marketplace view and fetches the index again, so a plugin listed a minute ago is found. Then it chooses the listing and starts the same download as Install, which ends in the install question. When the newest version this HikariSub can run is already installed, it only chooses the listing. A plugin the index doesn't list, or one this HikariSub can't run, gets "<id> isn’t in the marketplace for this HikariSub.", and an index that doesn't arrive gets the usual message.
- **Plugin updates off.** While "Check for plugin updates" is off, a link only asks: "<id> can’t be installed while plugin updates are off.", with Close and Turn on plugin updates. Turning them on carries on as above; nothing is fetched before that.

### Updates, roll back and revocation

- **Automatic updates.** After each index HikariSub accepts, it installs the newest version a marketplace plugin can run when "Install updates automatically" (`plugins.autoUpdate`, on by default, under "Check for plugin updates") and the plugin's own switch are both on. The download gets the same checks as Install. File installs never update by themselves.
- **When it applies.** Only while the plugin isn't running a command or an event. An update that finishes downloading during one waits for it to end. One still waiting at quit is fetched again at the next start.
- **Asking for more.** An update that asks for more than the installed version waits for the user instead. It asks for more when it moves to Full trust, asks for every open document, adds a network host, or adds a folder or makes one writable. `plugins.json` keeps its version as `held`. Review… opens the update question, which marks each permission New or allowed now and shows the listing's changelog. Keep <installed> drops it, and automatic updates then pass over that version.
- **Roll back.** An install over another version keeps the replaced copy in `<user folder>/plugins/.previous/<id>/`, one step back. Roll back to <version> swaps the two copies. Automatic updates pass over the version left (`skip`), and the toast's Undo swaps them again and clears that. Data, settings and folder grants stay; the cache is cleared.
- **Revocation.** When the index revokes the installed version, or the whole plugin, HikariSub turns the plugin off, says so in a toast, and records `revoked` in `plugins.json`. The plugin can't be turned back on unless a later index lifts the revocation. Its details offer Install <last good>, the newest version it can run that isn't revoked, and Uninstall. Installing any other version clears the mark and turns it back on. Revocation applies only when the installed key is the listing's key.

## Folders and `plugins.json`

| Path | Holds |
| --- | --- |
| `<user folder>/plugins/<id>/` | The installed files. An install stages them in `.staging-<id>` and swaps them in, so a failed install leaves the old copy. |
| `<user folder>/plugin-data/<id>/` | The plugin's data. It survives updates; uninstall asks whether to remove it. |
| `<cache>/plugins/<id>/` | The plugin's cache, cleared on every install and on uninstall. |
| `<user folder>/plugins/.previous/<id>/` | The version the last update replaced, for Roll back. |
| `<user folder>/plugin-programs/<id>/<sha256>/` | Its [downloaded programs](#programs), one folder per download. |
| `<user folder>/plugins.json` | Per id: `version`, `enabled`, the pinned `key`, the remembered folder `grants`, the `settings` a plugin or the user changed, `source`, and `installed`, its place in install order (kept across updates, which decides which plugin's [format](#formats) reads a file first); for [updates](#updates-roll-back-and-revocation), `autoUpdate` (only when off), `skip`, `previous`, `held` and `revoked` (`reason`, `lastGood`). |
| `<user folder>/marketplace.json` | `serial`: the highest marketplace index serial accepted. |

An update keeps the enabled state, the grants whose ids the new manifest still asks for, and the setting values that still fit the new manifest. A `plugins.json` HikariSub can't read stops installs rather than forgetting pinned keys.

### Plugins in a Project (PJ8 [#776](https://github.com/altqx/hikari/issues/776))

Plugins are still installed per user. A trusted Project's `.hikari/project.toml` can turn them on or off and override their settings:

```toml
[plugins]
enabled = ["com.example.keyframe-generator"]
disabled = ["qc-pack"]

[plugins."com.example.keyframe-generator"]
threshold = 0.4
```

- **On and off.** `enabled` and `disabled` override your switch for that id. A revoked plugin stays off. `project.local.toml`'s lists replace `project.toml`'s.
- **Settings.** `[plugins."<id>"]` values go over yours key by key, and `project.local.toml`'s go over `project.toml`'s. `hikari.settings.get` reads them from the Project of the window you're working in.
- **Across windows (Q365).** A plugin runs while it is on in at least one window. Its commands, panels and events show only in windows where it is on.
- **Trust (Q367).** None of `[plugins]` applies in an untrusted Project. The plugin manager then shows a lock on the plugins it names, with **Trust this folder to use them**.
- **A missing plugin.** When a trusted Project enables a plugin that isn't installed, the plugin manager shows "This project uses <id>, which isn't installed." with **Install from the marketplace**, which opens the marketplace view and searches for the id.

## The plugin manager

Automation > Plugins… (`GLOBAL_PLUGINS`, 5231) opens one resizable window with two views, Installed and Marketplace: the plugins on the left, the chosen one's details on the right. It also opens at a plugin from About <plugin> in that plugin's panel menu. Installed counts the updates that wait for the user, and the newer versions that won't install automatically. The tool strip holds Install from file…, Check for updates and Plugin updates, whose popover has "Check for plugin updates" and "Install updates automatically".

- **The list.** Each row shows the package icon, name, version and signer (From the marketplace for a marketplace install). HikariSub's own plugins carry the By HikariSub icon, named on hover. It shows a state only when the plugin isn't simply running: Revoked, <version> waits for you, Turned off, Didn't start, Stopped or Full trust. The filter matches name, author and description. Space turns the chosen plugin on or off, and Delete asks to uninstall it.
- **The details.** What the plugin is and who made it, with the By HikariSub badge for HikariSub's own plugins, its trust, its permissions with their reasons, and what it adds (commands and panels). On turns it on or off. A marketplace plugin has its own "Install updates automatically", and Roll back to <version> sits beside the version when a replaced copy is kept. A held update shows a bar with Review…; a revoked plugin shows a red bar with Install <last good> and Uninstall….
- **Remembered folders.** Each folder the user let the plugin keep, with Forget. A forgotten folder is asked for again, and the plugin's open files in it close.
- **Settings.** Open in Settings… shows the plugin's section under Settings › Plugins, where its settings are kept ([Settings, grown](#settings-grown)).
- **Errors.** A plugin that didn't start, crashed or was stopped shows the error and its traceback, with Start again and Copy.
- **Actions.** Install from file… for a new plugin, then per plugin Update to <version> (a marketplace install with a newer listed version), Update from file…, Open data folder and Uninstall…. Update from file… refuses a package with a different id.
- **Move to the marketplace.** A file install whose id the marketplace lists, signed with the listing's author key and not newer than the listed version, offers "Move to the marketplace", which installs the listed version as a marketplace install, or "Not now".
- **Marketplace.** Search matches name, summary and author, and the box narrows to one category. A listing shows the newest version this HikariSub can run, its description, screenshots, permissions, what it needs ("HikariSub <version> or later"), licence, last update and source. Install or Update to <version> downloads it and opens the install question, which says it was signed by the author and checked against the marketplace. A plugin with full access warns that it gets full access to the computer and asks for Install with full access.
- **From an agent.** An Agent thread can scaffold a plugin into `<user folder>/ai/plugins/` and ask to install it, or a `.hikariplugin` the user gave it. Every install asks in the Agent panel with the same facts as the install question, whatever the thread's mode ([AI-written plugins](ai-agent.md#ai-written-plugins)).

## Running a plugin

Each enabled plugin runs in its own `hikari-plugin-helper` process, started when HikariSub starts or when the plugin is turned on, installed or updated. The helper uses the same framing and endpoint as the automation helper. A plugin's crash ends only its own helper.

- **Restricted (the default).** A fresh LuaJIT state with `string` (without `dump`), `table`, `math`, `bit`, `coroutine` and the base functions, minus `dofile`, `loadfile` and `module`. `load` and `loadstring` take text only, so bytecode never loads. There's no `ffi`, `io`, `os`, `debug`, `jit` or `package`, so no `package.loadlib` either. `require "a.b"` reads only `lua/a/b.lua` or `lua/a/b/init.lua` in the package, and only as text.
- **Unrestricted.** The full LuaJIT libraries, with `package.path` pointed at the package's `lua/` folder and `package.cpath` at its `lib/`. Installing one shows the full-trust warning. Only these plugins can ship [QML panels](#qml-panels), and the warning then adds "Shows its own panels inside HikariSub".
- **Memory.** A counting allocator caps the state at 2 GB. A command or event that hits the cap fails with "the plugin ran out of memory: it may hold 2 GB", and the plugin keeps running.
- **Time.** The load, each command, each event and each timer callback runs one at a time, with no time limit. One that runs longer than 10 seconds, and isn't a [job](#long-jobs), shows a "still working" card with Stop ([Background plugins and time](#background-plugins-and-time)). Time HikariSub spends answering the plugin (an API call, a setting) doesn't count toward the 10 seconds.
- **The sandbox.** A restricted plugin's helper starts in an operating-system sandbox, `hikari_sandbox` ([ADR 0029](adr/0029-the-ai-sidecar-runs-in-an-os-sandbox.md)): Landlock and seccomp on Linux, an AppContainer on Windows. Its profile is `HikariSub.Plugin.<the first 32 hex digits of the id's SHA-256>`: the helper's folder to read and execute, the plugin's package folder to read, nothing else, and no network. The helper needs no more, because HikariSub itself opens the files a plugin was granted (`hikari.files`) and makes its requests (`hikari.net.fetch`).
- **Full trust plugins run outside the sandbox,** with the same access as HikariSub.
- **Where the sandbox can't apply,** for example on a kernel without Landlock or seccomp, restricted plugins still run under the limits above. HikariSub logs what is missing under `hikari.plugins.sandbox`, and the plugin's details say "HikariSub keeps it to these permissions, but it isn’t sandboxed on this system."

### The helper protocol

The helper framing ([`helper_protocol.h`](../src/backends/include/hikari/backends/helper_protocol.h)) has an `Event` frame (17) in which the host pushes application events to a helper. It waits behind the helper's running work and is answered like a request, so an event handler can call back into HikariSub. A helper without an event handler answers `Unsupported`. The plugin protocol ([`plugin_protocol.h`](../src/backends/include/hikari/backends/plugin_protocol.h), version 1) has two requests, `Load` and `Run`, plus events and ten services: `Call`, `Notify`, `SetSetting`, `Subscribe`, `Lock`, `Unlock`, `Ui`, `Files`, `Net` and `Job`. `Ui` carries the panel, dialog and popup operations (`panels.render`, `panels.show`, `panels.post`, `dialogs.show`, `dialogs.ask`, `dialogs.close`, and `quick_pick.*` and `input_box.*`), `Files` the file operations (`files.pick`, `files.folder`, `files.open`, `files.entries`), `Net` `net.fetch` and `Job` `job.begin`, `job.progress` and `job.cancelled`, which the helper process answers itself. Each takes JSON parameters and is answered like `Call`. When several events of one name wait, only the newest is delivered. Panel and dialog events merge more finely: only text edits, slider drags and pointer moves of one widget replace each other.

## The `hikari` global

| Member | What it does |
| --- | --- |
| `hikari.commands.register(id, fn)` | Gives a command from the manifest's `contributes.commands` its handler, once. An id the manifest doesn't declare is refused. The handler is `fn(args, context)`: `args` is a table (`{}` from a menu, key or status item) and `context.via` says what ran it: `menu`, `key`, `status`, `api` or `ai`. |
| `hikari.commands.run(id)` | Runs a HikariSub command (`GLOBAL_SAVE_SUBS`), as `commands.run` does. |
| `hikari.call(operation, params)` | Calls any [Hikari API](api.md) operation as the plugin. It returns the result, or `nil` and the error `{code, message, data}`. Without `documents.all` the plugin sees the editing target only. |
| `hikari.events.on(name, fn)` | Calls `fn(data)` for each Hikari API event of that name (`document.changed`, `document.saved`, `selection.changed`, `documents.changed`, `panel.changed`, `command.ran`, `project.changed`, `macro.ran`). An unknown name is refused. `document.saved` comes once the file is on disk; `command.ran` only says a command was triggered, so a Save that asks first reports before anything is written. |
| `hikari.notify(text, level)` | A toast and a Log panel entry under the plugin's name. `level` is `"info"` (the default), `"warning"` or `"error"`. `print` writes to the Log panel only. |
| `hikari.settings.get(id)`, `hikari.settings.set(id, value)` | The plugin's settings from `contributes.settings`. A value that doesn't fit the setting's type, options or bounds is refused, and kept values live in `plugins.json`. |
| `hikari.i18n.locale`, `hikari.i18n.t(key, ...)` | The UI language (`"pt_BR"`, `"en"`) and the text for `key` from `i18n/<locale>.json`, then `i18n/<language>.json`, then `i18n/en.json`, then the key itself. `%1` to `%9` take the arguments. Each file is a JSON object of strings. |
| `hikari.project.info()` | The [Project](adr/0030-open-a-folder-as-a-project.md) of the window the user works in, `{name, trusted}`, or `hikari.null` without one, as [`project.info`](api-reference.md#projectinfo) answers. `project.changed` comes when it changes. |
| `hikari.plugin.id`, `hikari.plugin.version`, `hikari.api_version` | Who the plugin is, and the Hikari API number. |
| `hikari.null`, `hikari.array(t)` | JSON `null`, and a table marked as a JSON array (so `{}` can be sent as `[]`). Arrays that come back from HikariSub are marked too. |
| `hikari.document.list()`, `hikari.document.read(document)`, `hikari.document.edit(name, fn, options)`, `hikari.document.group(name, fn)` | The open Documents, a snapshot of one, one edit applied as one undo step, and the steps `fn` makes as one undo step. See [Documents and selections](#documents-and-selections) and [Undo groups](#macros-keyframes-and-frames). |
| `hikari.selection.get(document)`, `hikari.selection.set(selected, active, document)` | The selected Lines and the active Line, as `selection.get` and `selection.set` give and take them. |
| `hikari.panels.render(panel, tree)`, `hikari.panels.show(panel)`, `hikari.panels.on(panel, fn)` | Draws a panel from `contributes.panels`, opens it, and calls `fn(event)` for its events. See [Panels and dialogs](#panels-and-dialogs). |
| `hikari.panels.post(panel, data)` | Sends `data` to a [QML panel](#qml-panels)'s `api.received`. It returns `true` when the panel is loaded, and `false` otherwise. |
| `hikari.files.open(place, path)`, `hikari.files.entries(place, path)`, `hikari.files.pick(options)`, `hikari.files.folder(id)`, `hikari.files.list()`, `hikari.files.read(file, encoding)`, `hikari.files.write(file, data, encoding)` | Files in the plugin's own folders, in folders the user chose, and that the user picked or dropped. See [Files](#files). |
| `hikari.net.fetch(request)` | An HTTPS request that HikariSub makes to a host from the manifest. See [Network](#network). |
| `hikari.job.begin{title}`, `hikari.job.progress(fraction, text)`, `hikari.job.cancelled()` | Turns the running command, a background plugin's timer callback, or a click in the plugin's own panel or dialog, into a job with a progress card and Cancel. See [Long jobs](#long-jobs). |
| `hikari.timer.after(ms, fn)`, `hikari.timer.every(ms, fn)`, `hikari.timer.cancel(id)` | A background plugin's timers. See [Background plugins and time](#background-plugins-and-time). |
| `hikari.formats.exporter(id, fn)`, `hikari.formats.importer(id, fn)` | Writes and reads a subtitle format from `contributes.formats`. See [Formats](#formats). |
| `hikari.marks.set(marks, document)`, `hikari.marks.clear(lines, document)`, `hikari.marks.list(document)` | The plugin's Grid row marks. See [Grid row marks](#grid-row-marks). |
| `hikari.problems.set(problems, {revision, set, document})`, `hikari.problems.clear{document, set}`, `hikari.problems.list{document, lines, severity, source}`, `hikari.problems.fix(problem, fix, document)` | The plugin's problems: squiggles, Grid marks and rows of the Problems list. See [Language features and problems](#language-features-and-problems). |
| `hikari.vcs.refs(options)`, `hikari.vcs.show(ref, document)` | The git repository holding a Document, read-only, with `vcs.read`. See [Git](#git). |
| `hikari.presence.set_text(text)` | The plugin's text for `{plugin}` in the user's Discord status, with `presence`. See [Discord status text](#discord-status-text). |
| `hikari.dialogs.show(dialog, spec)`, `hikari.dialogs.ask(dialog, spec)`, `hikari.dialogs.close(dialog)`, `hikari.dialogs.on(dialog, fn)` | Opens a dialog, or opens one and waits for its answer `button, values`; closes it; and calls `fn(event)` for its events. |

Tables go to HikariSub as JSON: a table with keys 1 to n is an array, one with text keys an object. Numbers must be finite, text must be UTF-8, and a table that holds itself is refused.

Errors carry the file and line inside the package (`lua/tools.lua:12: …`) and a traceback, never a path on disk.

## Documents and selections

A plugin sees the active subtitle file. With the `documents.all` permission it also sees the other tabs. Without it, `hikari.document.list()` lists the active Document only, and naming another Document gives the error `not_granted`. Every `document` argument is optional and defaults to the active Document.

`hikari.document.read(document)` returns a snapshot: `{document, revision, title, format, lines, styles, info}`. `lines` are the `lines.list` Lines (`id`, `row` and every field, such as `text` and `start`), `styles` the `styles.list` Styles and `info` the `info.list` properties, all at one revision. When the Document changes during the read, it starts again. After three tries it returns `nil` and the error `busy`.

`hikari.document.edit(name, fn, options)` makes one change:

```lua
local done, err = hikari.document.edit("Strip line breaks", function(doc, change)
    for _, line in ipairs(doc.lines) do
        change:set_line(line.id, {text = line.text:gsub("\\N", " ")})
    end
end)
```

`fn` gets a fresh snapshot (`options.document` picks the Document) and a `change` that stages edits: `set_line(line, fields)`, `insert_line(fields, {after = line} or {before = line})`, `delete_line(line)`, `move_line(line, before)` (to the end when `before` is `nil`), `set_style(name, fields)`, `delete_style(name)` and `set_info(key, value)` (removed when `value` is `nil`). Then HikariSub applies them with `document.apply` as one undo step, named `<plugin name>: <name>`. It returns the `document.apply` result `{revision, inserted, changed}`, or `nil` and the error. When nothing is staged, nothing is applied, and `changed` is `false`. When the Document changed after the snapshot, for example because the user typed, the error is `stale_revision`. An error in `fn` passes through. An edit inside another edit is refused.

Only one plugin edits at a time, app-wide. An edit waits for another plugin's edit to finish. After 5 seconds it gives up and returns the error `busy`. Commands and event handlers of different plugins still run side by side; only their edits take turns. A plugin that stops, crashes or is turned off ends its turn.

## Commands and shortcuts

A command `greet` of `com.example.hello` is `plugin.com.example.hello.greet`. While the plugin runs, its commands sit in the Automation menu after Plugins…, in a submenu named after the plugin. In Settings › Shortcuts, the **Plugin shortcuts** filter lists them. A plugin command's shortcut is global, and it is kept with the macro shortcuts in the hotkeys file. A shortcut whose plugin isn't installed or turned on stays listed with a note, ready for when it comes back. A plugin can also place its commands in right-click menus and the menu bar, show status bar items, suggest default shortcuts and disable a command under conditions; see [Menus, status items and shortcuts](#menus-status-items-and-shortcuts).

When a command fails, its error shows in a toast. When a plugin fails to load, the toast says it didn't start, and its details hold the error. Errors in event handlers go to the Log panel only.

## Panels and dialogs

A panel from `contributes.panels` is an ordinary HikariSub panel. It docks, tabs and floats, takes the focus mark and keeps its place in layouts and arrangements. Its dock id is `plugin:<plugin id>/<panel id>`. The header shows only its title. View › Panels lists plugin panels in a group after HikariSub's own, sorted by title, with the plugin's name after each. A panel stays closed until the user shows it or the plugin calls `hikari.panels.show`. It first docks in the manifest's `area`, tabbed with the panel already there, and after that wherever the user left it.

The plugin never draws pixels. It gives HikariSub a tree of widgets, and HikariSub draws it with its own controls in the current theme:

```lua
hikari.panels.on("checks", function(e)
    if e.kind == "show" or (e.kind == "click" and e.widget == "run") then
        hikari.panels.render("checks", {kind = "column", children = {
            {kind = "number", id = "cps", label = "Characters per second", value = 17, minimum = 1},
            {kind = "button", id = "run", text = "Check", style = "primary"},
            {kind = "list", id = "found", rows = {{id = "12", text = "Line 12", detail = "21 CPS"}}},
        }})
    end
end)
```

The [Line tools sample](examples/line-tools/) draws a reading-speed panel with a chart, a list, a menu and a dialog.

A panel's root may carry `menu = {{id, text, checked, disabled}}`. Those rows lead the panel's ⋯ menu, and choosing one sends the event `{kind = "menu", widget = id}`. With `drop = true` on the root, the panel takes files dropped on it. See [Files](#files).

### Widgets

Every widget has a `kind` and may have an `id` (needed for anything that sends events, and unique in the tree), a `name` for screen readers, a `tooltip`, `disabled` and `grow` (take the spare room). A tree holds at most 2000 widgets, 24 levels deep.

| `kind` | Fields | Events |
| --- | --- | --- |
| `label` | `text`, `style` (`heading`, `body`, `muted`, `small`), `wrap` | |
| `button` | `text`, `style` (`normal`, `primary`, `danger`), `icon` | `click` |
| `icon_button` | `icon`, `name`, `checked` | `click` |
| `text`, `text_area` | `value`, `placeholder`, `label`; `rows` for `text_area` | `change`; `submit` on Enter in `text` |
| `number` | `value`, `minimum`, `maximum`, `step`, `decimals`, `unit`, `label` | `change` |
| `time` | `value` in milliseconds, `label` | `change` |
| `check` | `text`, `value` | `change` |
| `choice` (2 to 4 options), `radio`, `dropdown` | `options` (text, or `{value, text}`), `value`, `label` | `change` |
| `colour` | `value` as an ASS colour (`&H00FF00&`, or `&HAABBGGRR` with `alpha`), `label` | `change` |
| `slider` | `value`, `minimum`, `maximum`, `step`, `label` | `change` |
| `progress` | `value` from 0 to 1 (none: busy), `text` | |
| `list`, `tree` | `rows` (`{id, text, detail, icon, muted}`; a `tree` row may also have `children` and `expanded`), `selected`, `height`; at most 20000 rows | `select`, `activate` (double-click or Enter) |
| `tabs` | `tabs` (`{text, body}`, at most 20), `selected` | `change` with the tab's index |
| `group` | `title`, `children` | |
| `row`, `column` | `children` | |
| `grid` | `columns`, `children` | |
| `link` | `text` | `click` |
| `image` | `src` (a `.png`, `.svg` or `.jpg` in the package), `width`, `height` | |
| `empty` | `text`, for an empty state | |
| `canvas` | `height`, `width`, `draw`, `name`, `description`, `hover`, `tip` | `pointer` |

`icon` names one of HikariSub's icons. Widgets take their colours from the theme only.

A `canvas` draws a list of up to 10000 operations: `rect` (`x`, `y`, `w`, `h`, `radius`), `line` (`x1`, `y1`, `x2`, `y2`, `dash`), `polyline` (`points`, `closed`, `dash`), `path` (`d`, SVG path data), `ellipse`, `text` (`x`, `y`, `text`, `align`, `size` `body` or `small`) and `image` (`x`, `y`, `w`, `h`, `src`). Shapes take `fill`, `stroke` and `width`. A colour is a theme role (`accent`, `text`, `muted`, `line`, `danger`, `warning`, `success`, `panel`, `field`), or an ASS colour for subtitle content, which high contrast outlines in the text colour. Never make colour the only cue. `tip = {x, y, text}` draws a tooltip. A press, release, the pointer leaving, and with `hover` each move, send `{kind = "pointer", type, x, y, buttons, modifiers}`.

### Events

An event is a table `{kind, widget, value, panel or dialog, values}`. `values` holds the current value of every input in the panel or dialog, by id, so a click handler can read the form. A panel also gets `{kind = "show"}` when it opens, and again after its plugin starts while it is open. Render the tree then.

### Dialogs

```lua
local button, values = hikari.dialogs.ask("rename", {
    title = "Rename style",
    body = {kind = "text", id = "name", label = "Name", value = "Default"},
    buttons = {{id = "ok", text = "Rename", style = "primary", default = true}, {id = "cancel", text = "Cancel", cancel = true}},
})
```

A dialog spec is `{title, body, buttons, width, height}`, with 1 to 4 buttons `{id, text, style, default, cancel, stays_open}`, at most one `default` and one `cancel`. A dialog's id is 1 to 64 lowercase letters, digits and `-`, and HikariSub remembers its size and place as `plugin.<plugin id>.<dialog id>`. A button closes the dialog unless it has `stays_open`. Closing it with the close button or Esc presses the `cancel` button. Without one, `ask` returns `nil` and `show` sends `{kind = "closed"}`.

`ask` is modal and waits. `show` returns at once, and its buttons send `{kind = "button", button = id}`. Showing a dialog that is open again replaces its content. A plugin that stops or is turned off closes its dialogs.

### When the plugin isn't there

A layout can keep a place for a panel whose plugin is missing, turned off or stopped. The panel keeps the title the layout saved and shows why, with the one action that helps: Open Plugins… when the plugin isn't installed, Turn on when it is off, and Start again with Details when it stopped. When the plugin comes back, its panel fills the place. Closing a placeholder for a plugin that isn't installed, or View › Panels › <panel> › Remove from the layout, forgets the place.

### QML panels

A Full trust plugin may draw a panel itself, in QML, by naming a package file in the panel's `qml` field. Everything above about docking, the dock id, View › Panels, the first `area` and the placeholders still holds. Restricted plugins keep the widget tree.

```qml
import QtQuick
import QtQuick.Controls
import Hikari.Plugin

Column {
    required property var api
    implicitWidth: 240
    Label { text: qsTr("Count: %1").arg(api.settings.count) }
    IconButton {
        name: "restart"
        text: qsTr("Count lines")
        onClicked: api.call("lines.list", {}).then(result => api.send({ lines: result.lines.length }))
    }
    Connections {
        target: api
        function onReceived(data) { console.log(data.echo) }
    }
}
```

- **Loading.** The file loads when the panel is first shown, not when HikariSub starts, so a closed panel costs nothing. Its root item fills the panel. Its `Layout.minimumWidth` and `Layout.minimumHeight`, or else its implicit size, set the panel's smallest size, from 160 × 80 up to 320 × 120 px. The panel is destroyed when its plugin stops, is turned off, uninstalled or updated. An update loads the new version in its place.
- **`api`.** The root item declares `required property var api`, the only supported way into HikariSub:
  - `api.call(operation, params)` returns a Promise of a [Hikari API](api.md) result, called as the plugin with the same permissions as `hikari.call`. It rejects with an `Error` whose `code` is the API's error code.
  - `api.send(data)` gives the plugin's `hikari.panels.on` handler the event `{kind = "message", data}`, and `api.received(data)` is the signal for each `hikari.panels.post`. Logic and state belong in the helper, and the panel is its view.
  - `api.settings` holds the plugin's settings by id and follows every change. `api.setSetting(id, value)` keeps one, and throws when the setting's type, options or bounds refuse the value.
  - `api.theme` is the current palette (`text`, `muted`, `accent`, `panel`, `field`, `line`, `warning`, `danger`), and `api.locale` HikariSub's language (`"en"`, `"ko"`, …).
  - HikariSub's own objects, documents as live objects, file dialogs and the network aren't in `api`. A panel gets files and the network through its helper, with `hikari.files` and `hikari.net.fetch`.
- **The engine.** The panel runs in HikariSub's own QML engine, in a context of its own. HikariSub refuses remote URLs there too, so no image, XHR or import comes from the network.
- **Imports.** A panel may import `QtQuick`, `QtQuick.Controls`, `QtQuick.Layouts` and `Hikari.Plugin`, and paths inside its package (`import "parts"`, `.import "util.js" as Util`, `import … from "./a.mjs"`). Any other module, `Hikari.Ui` included, and any absolute path, URL or path that climbs out of the package is refused, both at install and at each load. This keeps plugins on an API HikariSub supports. It isn't a security boundary: Full trust code can do anything HikariSub can.
- **When it can't load.** A missing file, a syntax error, a refused import or a component error shows in the panel: "The panel couldn’t load.", the first error with its package path and line, Copy, and Load again. The whole error goes to the Log panel under the plugin's name. The helper and the plugin's other panels keep running.
- **Warnings.** Exceptions and binding errors in the package's files go to the Log panel under the plugin's name, at most 20 a minute; the rest are left out. The panel stays up.
- **Crashes and hangs.** A QML panel runs on HikariSub's UI thread, so a crash or an endless loop in it ends or freezes HikariSub. HikariSub keeps `plugin-panels.json` beside `plugins.json`, naming the plugins whose QML panels are loaded. When HikariSub didn't close cleanly with one loaded, the next start logs that it closed while the plugin's panel was open. That plugin's QML panels then say "HikariSub closed while <plugin>’s panel was open." and stay closed until the user chooses Load again in one of them, even across later starts.

### The `Hikari.Plugin` module

`import Hikari.Plugin` gives QML panels HikariSub's look, and its types are a supported API:

| Type | Properties |
| --- | --- |
| `Icon` | `name`, one of HikariSub's icon roles (`plugins`, `restart`, `edit-copy`, …), `size` in px (16 by default), and `color`. It follows the theme and right-to-left mirroring. |
| `IconButton` | `name` as for `Icon`, and `text`, its tooltip and accessible name. It shows only the icon. |

`QtQuick.Controls` already takes HikariSub's style, so a panel's buttons, fields and lists match the app.

## Files

A restricted plugin never names a path on disk. It reaches a file only through a handle, and HikariSub hands one out only for a file the plugin may use:

- **Its own folders.** `data` is `<user folder>/plugin-data/<id>/` and `cache` is `<cache>/plugins/<id>/`. Both are writable, and HikariSub makes them when first used.
- **The Project.** `project` is the folder of the [Project](adr/0030-open-a-folder-as-a-project.md) open in the window the user works in, writable once the user trusts that folder (Settings › General › Trusted folders). Without a trusted Project, opening it answers `not_found`. It follows the user from window to window; listen to `project.changed` to know when.
- **Folders the user chose.** Each `permissions.files` request `{id, title, writable, reason}` names a folder the plugin asks for, such as `{id = "fonts", title = "Font folder", writable = false, reason = "Reads fonts to measure lines"}`.
- **Files the user picked or dropped.** `hikari.files.pick` opens the system file dialog, and a panel with `drop = true` takes dropped files.

```lua
local note = hikari.files.open("data", "notes/today.txt")
hikari.files.write(note, "hello")
if hikari.files.folder("fonts") then
    for _, entry in ipairs(hikari.files.entries("fonts", "")) do
        print(entry.name, entry.folder)
    end
end
```

`hikari.files.open(place, path)` returns `{file, name, access}`: the handle, the path asked for, and `"read"` or `"write"`. `place` is `"data"`, `"cache"`, `"project"` or a folder id; a folder request can't take those three ids. A writable place makes the missing folders on the way. `hikari.files.entries(place, path)` lists a folder in name order as `{name, folder}`, at most 4096 entries. `hikari.files.read(file, encoding)` and `hikari.files.write(file, data, encoding)` take a handle or the table that holds one, as [`files.read` and `files.write`](api.md) do, up to 16 MiB; `encoding` is `"utf8"` (the default) or `"base64"`. `hikari.files.list()` lists the handles the plugin holds.

A `path` is relative, with `/` between its parts, at most 1024 bytes of UTF-8. A part that is empty, `.` or `..`, that holds a control character or one of `\ : * ? " < > |`, that ends with a dot or a space, or that Windows reserves (`CON`, `NUL`, `COM1` and so on) is refused with `not_granted`, as is a path whose links lead outside the place. An empty path is `invalid_params` for `open` and the place itself for `entries`.

`hikari.files.folder(id)` asks the user for the folder the first time and returns whether the plugin has it. The question shows the plugin's name, the request's `title` and `reason`, and **Remember this folder for <plugin>**, checked. A remembered folder is kept in `plugins.json` `grants` and is used without asking from then on, until the user forgets it in the plugin manager or uninstalls the plugin. Otherwise the folder lasts until the plugin stops. A request with `writable = false` is read-only whatever the folder allows. Opening a place the plugin doesn't have yet asks for it the same way, and declining gives `not_granted` with `reason = "declined"`.

`hikari.files.pick{title, save, name, filters}` opens the file dialog and returns `{file, name, access}`, or `nil` when the user cancels. `save = true` asks for a file to write, with `name` as the suggestion. `filters` is a list of up to 16 `{name, patterns}`, where a pattern is `*` or `*.ext`. A file dropped on a panel with `drop = true` sends `{kind = "drop", files = {{file, name, access}}}`, read-only. A pick or drop grants that one file only.

A plugin that stops, crashes or is turned off loses its handles, its folders for this run, and any file dialog still open for it.

## Long jobs

A command or a click that takes longer than 10 seconds, such as scanning a video, calls `hikari.job.begin` first:

```lua
hikari.commands.register("scan", function()
    hikari.job.begin{title = "Finding keyframes"}
    for i = first, last, 64 do
        if hikari.job.cancelled() then
            return
        end
        hikari.job.progress((i - first) / (last - first), "Frame " .. i)
        local batch = hikari.call("video.frames", {from = i, to = math.min(i + 63, last)})
        -- …
    end
end)
```

From `begin` until the command or the click's handler returns, a card above the status bar shows the plugin's name, the title, the progress and **Cancel**. `hikari.job.progress(nil, text)` shows a bar that moves without a fraction. **Cancel** only makes `hikari.job.cancelled()` answer `true`, so check it often. When the plugin hasn't stopped 5 seconds later, the card says so and offers **Stop**, which force stops it; work it hasn't applied is lost. Only a command, a background plugin's timer callback, or a `click` in the plugin's own panel or dialog (a dialog's `button` press too), can become a job, once; anywhere else, such as other events, or a panel's `change` or `show`, the job calls answer `nil` and an error. A panel's button can so start a scan with the card and **Cancel**:

```lua
hikari.panels.on("scan", function(e)
    if e.kind == "click" and e.widget == "find" then
        hikari.job.begin{title = "Finding keyframes"}
        -- …
    end
end)
```

While the job runs, the plugin's other events and commands wait behind it. Several plugins' jobs stack, the newest first.

## Grid row marks

A plugin can mark Grid rows with a coloured bar at the right of the row number, for example the Lines a git ref changed:

```lua
hikari.marks.set({
    {line = 12, colour = "warning", tooltip = "Changed since v1.0"},
    {line = 15, colour = "danger", tooltip = "Removed in the next ref"},
})
```

`colour` is a theme role: `accent`, `warning`, `danger`, `success` or `muted`. `tooltip` (1 to 200 characters) shows on hover after the plugin's name, and screen readers read the row as "marked by <plugin>: <tooltip>". Setting a Line again replaces the plugin's own mark there. When several plugins mark one Line, the bar splits. A plugin marks at most 20000 Lines of a Document. `hikari.marks.clear(lines)` removes some marks, `hikari.marks.clear()` all of them, and `hikari.marks.list()` gives them back.

Marks are never saved and aren't undo steps. They go when the plugin stops or is turned off, and when the Document closes. The plugin manager's details list them per file, as "Marks 3 lines in ep01.ass", with **Clear marks**.

## Macros, keyframes and frames

Any plugin can use these through `hikari.call`; none needs a permission.

- **`automation.macros` and `automation.run_macro`.** The Automation macros the user loaded, and a run of one on the active tab's selection, as the menu runs it, as one undo step. Set the selection first with `hikari.selection.set`. `answers` fills the macro's dialogs: one `{key, values, button, controls}` per dialog, as the [`macro.ran`](api-reference.md#macroran) event gives them after the user runs a macro. A dialog whose key matches is answered without showing it, unless `show = "prefilled"`; a question and a dialog that changed are always shown. An answer without a key, such as one imported from another tool, gives the dialog's `controls` instead: it fills the dialog of the running script whose value-bearing controls have those kinds and names, in order. When an answer has both, the key wins, so an answer whose key no longer matches shows its dialog. The result gives the run's own `dialogs`, as `macro.ran` would, when the plugin sees the Document it ran on, so a plugin learns each run's answers even when several runs' `macro.ran` events reach it as one. AI callers can't run installed macros.
- **`controls` on `macro.ran`.** Each dialog's value-bearing controls, in order: `name`, `kind`, a checkbox's `label`, the `hint`, a dropdown's `items`, and `min`, `max` and a floatedit's `step` when the script set them. That's enough to ask the same question in a dialog of the plugin's own.
- **Undo groups.** `hikari.document.group(name, fn)` runs `fn`, and every step the plugin commits on the editing target meanwhile, by `document.apply`, `automation.run_macro`, `commands.run` or any other write, becomes one undo step named "<plugin name>: <name>". It returns `{steps}`, how many steps became one, then what `fn` returned; an error in `fn` ends the group and passes through. Underneath are `document.group_begin {name}` and `document.group_end`. A group also ends when the command or event handler returns, when the plugin stops, or at the first step someone else makes, such as the user typing or pressing Undo: the plugin's steps before it become one, and the later ones stay steps of their own. A plugin has one group open at a time (a second `group_begin` is `invalid_params`), and while one plugin groups a Document another's `group_begin` there answers `busy`.
- **`video.set_keyframes`.** Replaces the video's keyframes as Open keyframes does, or with `file` (a writable handle from `hikari.files.pick`) writes an Aegisub keyframes file there first and opens it.
- **`video.frames`.** Up to 256 frames at once, every `stride`-th, scaled down to `maxEdge` (16 to 512, 64 by default), as one byte of luma per pixel or as BGRA, in base64.

## Network

A plugin can't open sockets. `hikari.net.fetch` asks HikariSub to make the request:

```lua
local response, err = hikari.net.fetch({
    url = "https://api.example.com/v1/translate",
    method = "POST",
    headers = {["content-type"] = "application/json"},
    body = '{"text": "Hello"}',
})
if response then
    print(response.status, response.body)
end
```

A request is a URL, or `{url, method, headers, body, encoding}`. `method` is `GET` (the default), `HEAD`, `POST`, `PUT`, `PATCH` or `DELETE`. `headers` are text, and HikariSub refuses `host`, `cookie`, `connection`, `content-length`, `transfer-encoding`, `upgrade`, `te`, `trailer`, `keep-alive`, `expect` and `proxy-…`, and any value with a line break. `body` is at most 16 MiB, never on `GET` or `HEAD`, and `encoding = "base64"` sends and returns bytes. The answer is `{status, url, headers, encoding, body}`, with lower-case header names. A header that comes more than once is one value, joined with `, `, except `set-cookie`, joined with line breaks. An HTTP error status is an answer too.

HikariSub refuses the request, with the error `refused` and its `reason`, or `not_granted`:

- **`network_off`.** Settings › Updates and privacy › Let plugins connect to the internet is off. It turns off `hikari.net.fetch` for every plugin at once. A Full trust plugin can still open its own connections, because it runs outside the sandbox.
- **`not_https`.** The URL isn't `https://`. **`port`.** It names a port other than 443.
- **`not_granted`**, with `host`. The host isn't one of the manifest's `permissions.network.hosts`, matched exactly. The plugin manager shows those hosts, and so does the install.
- **`too_large`**, **`not_text`** (an answer that isn't UTF-8, without `encoding = "base64"`), **`failed`** and **`timeout`** (30 seconds).

Redirects are followed up to 5 times, each checked the same way. HikariSub sends no cookies and keeps no cache. Requests still running when the plugin stops are dropped.

## Git

HikariSub runs git for a plugin, read-only, so the plugin needs no [program](#programs) of its own for it. The manifest asks with `"vcs.read": "<reason>"`, and the install question, the plugin manager and the marketplace show it as "Read the branches, tags and history of the git repository holding your subtitle files: <reason>". Every plugin that asks gets it on the same terms; AI and MCP callers never do.

```lua
local refs = hikari.vcs.refs({ limit = 20 })
for _, tag in ipairs(refs.tags) do
    local text, shown = hikari.vcs.show(tag.name)
    if text then
        print(tag.name, shown.commit, #text)
    end
end
```

- **`hikari.vcs.refs(options)`** (`vcs.refs`) gives the repository holding the active Document, or `options.document`: `{root, path, head, branches, tags, commits}`. `path` is the Document's path in the repository; `head` is `{ref, commit}`, with `ref` null when detached, and null before the first commit. Branches and tags (at most 500 each) come newest first, each with its `commit`; `commits` are the newest `limit` (50 by default, at most 200) from HEAD, each `{commit, summary, author, time}`. A Document that was never saved gives the Project's repository, with `path` null.
- **`hikari.vcs.show(ref, document)`** (`vcs.show`) gives the Document's own file as `ref` has it, as UTF-8 text, and `{ref, commit, path, text}` second. `ref` is a branch, tag, commit or revision such as `HEAD~2`; it can't start with `-` or hold `:` or spaces.

HikariSub runs the git on PATH (Git for Windows on Windows) without the system config, with no pager, hooks, fsmonitor, signature checks, filters, textconv or external diff, and with every transport off, so nothing is fetched. The user's own git config still counts, for `safe.directory`. Each git run has 10 seconds, and a file at most 16 MiB.

The errors carry a `reason`: `unavailable` with `no_git` (git isn't installed or on PATH), `timed_out` or `failed`; `not_found` with `no_repository`, `no_ref`, `no_file` (the file isn't in that ref, as after a rename) or `no_path` (the Document was never saved); and `refused` with `unsafe_repository` (git distrusts a repository another user owns; add it to `safe.directory`), `too_large` or `not_text`.

## Extensions

[ADR 0034](adr/0034-plugins-grow-into-an-extension-system.md) grows plugins into an extension system. Everything here ships in API 1 on beta.5, through the cards [#1222](https://github.com/altqx/hikari/issues/1222)–[#1231](https://github.com/altqx/hikari/issues/1231). The [manifest schema](schemas/hikari-plugin.schema.json) already holds every key below and still refuses unknown ones. When a card lands, it replaces the text it changes above, such as "a plugin can't run programs". New operations and events are in [api.md](api.md#extension-operations).

### Kinds of package

| `main` | `programs` | Kind | Runs |
| --- | --- | --- | --- |
| a `.lua` file | any | Lua plugin | A helper, as today. |
| `{"program": "<id>"}` | must hold `<id>`, with `"api": true` | Program plugin | The program, speaking the [plugin protocol over JSON-RPC](#program-plugins). |
| none | some | Program package | Only its programs, such as a language server or an MCP server. |
| none | none | Data-only | Nothing. |

A data-only package never starts a helper. It may hold only `themes`, `iconThemes`, `icons`, `styles`, `karaokePresets`, `graphs`, `snippets`, `dictionaries`, `layouts` and `nodes`, which run in the node sandbox as today. It can't be `unrestricted` and has no `permissions`. Its install question shows what it adds and who signed it, with no permission list. Signatures, the marketplace, automatic updates, roll back and revocation are the same for every kind.

### The manifest, continued

| Field | Meaning |
| --- | --- |
| `main` | A `.lua` file, or `{"program": "<id>"}`. Optional now. |
| `permissions.programs` | The reason for running the plugin's programs. Needed when `programs` isn't empty. Shown as **Run programs**: "Runs programs that come with it: <titles>: <reason>". |
| `permissions.background` | The reason for working in the background. Shown as "Works in the background while HikariSub is open: <reason>". |
| `permissions.presence` | The reason for setting text in the user's Discord status. Shown as "Set text in your Discord status: <reason>". See [Discord status text](#discord-status-text). |
| `contributes.commands[]` | Gains `description` (one line for the command palette and the AI), `icon`, `when` and `palette` (`false` keeps it out of the [command palette](#the-command-palette)). The description is in the next row's terms. A handler is now `fn(args, context)`: `args` is the table a caller passed (`{}` from a menu or key), `context.via` is `menu`, `key`, `palette`, `status`, `api` or `ai`, and what it returns is the command's result. |
| `contributes.panels[]` | Gains `icon`, shown on the panel's tab and in a side bar. |
| `contributes.themes`, `iconThemes`, `icons` | [Themes and icons](#themes-and-icons). |
| `contributes.styles`, `karaokePresets`, `graphs`, `snippets`, `dictionaries`, `layouts` | [Content packs](#content-packs). |
| `contributes.menus`, `statusItems`, `keybindings` | [Menus, status items and shortcuts](#menus-status-items-and-shortcuts). |
| `contributes.programs` | [Programs](#programs). |
| `contributes.languageServers`, `providers` | [Language features](#language-features-and-problems). |
| `contributes.aiTools`, `mcpServers` | [AI tools and MCP servers](#ai-tools-and-mcp-servers). |
| `contributes.formats` | [Formats](#formats). |
| `contributes.settings[]` | Gains `description`, `keywords`, `deprecated`, `group`, `order` and `projectable`; `string` gains `multiline`, `choice` takes `{value, title}` options, and the types `list` and `secret` are new. See [Settings](#settings-grown). |

Beyond the schema, HikariSub refuses a manifest where:

- **A reference doesn't resolve.** A `menus`, `keybindings`, `statusItems` or `aiTools` entry names a command that `contributes.commands` doesn't declare. A `languageServers`, `mcpServers` or `main` entry names a program that `contributes.programs` doesn't declare. An `own:` icon names an id that `contributes.icons` doesn't declare.
- **An id repeats.** Ids are unique within each new list: `themes`, `iconThemes`, `icons`, `styles`, `karaokePresets`, `graphs`, `snippets`, `layouts`, `statusItems`, `programs`, `languageServers`, `mcpServers` and `formats`. So are a dictionary's `language`, a command's `aiTools` entry, and a `menus` entry's command within one `location`.
- **A kind is broken.**
  - A data-only package or a program package holds something that needs code: `commands`, `panels`, `menus`, `statusItems`, `keybindings`, `providers`, `aiTools`, `formats`, `permissions.background` or `permissions.presence`.
  - A data-only package has `settings` or `permissions`.
  - A program plugin or a program package is `unrestricted`. `tier` concerns only a Lua `main`: programs always run sandboxed, and only a Full trust Lua plugin ships QML panels.
  - A program package has `settings` without a language server or MCP server that reads them.
- **A permission is missing.**
  - `programs` isn't empty without `permissions.programs`.
  - `main` names a program without `"api": true`.
  - An `mcpServers` or `languageServers` entry names a program with `"api": true`.
- **A file is wrong.**
  - A file the manifest names isn't in the package, with the case matching. Bundled program `path`s count only for this platform, since a [per-platform package](#programs) holds only its own.
  - A program's bundled `path` for this platform isn't in the package.
  - An icon-theme folder misses a role.
  - A theme fails its schema or the [contrast check](#themes-and-icons), or its entry's `type` differs from its file's.
  - A content file fails its own reader.
- **A value is out of range.**
  - A format has neither `import` nor `export`.
  - An `aiTools` `parameters` or `result` isn't a schema in the [API's subset](api.md#operations), or `parameters`' root isn't an object with `additionalProperties: false`.
  - A key has neither Ctrl nor Alt and isn't F1 to F24.
  - A download has `archive` without `entry`, or `entry` without `archive`.
  - An `args` value names a `secret` setting.
  - Two `keybindings` of one plugin take the same key.
  - A `list` setting's default has more than its `maxItems`.

**Asking for more.** An update also [asks for more](#updates-roll-back-and-revocation), and is held, when it:

- adds `permissions.programs`, `permissions.background` or `permissions.presence`;
- adds a program;
- gives a program `network`;
- moves a program to `lowIntegrity`;
- adds an `mcpServers` entry.

A new download URL or hash for an existing program is not more, since the signed manifest pins it.

### Themes and icons

**Colour themes** (#1222, Q672–Q683). The four built-in themes and plugin themes are the same thing: a JSON file that follows [`hikari-theme.schema.json`](schemas/hikari-theme.schema.json), read by one loader and held to one contrast check. Each `contributes.themes` entry is `{id, title, type, path}`, where `path` is the theme file in the package and `type` repeats the file's own `type`, so the theme can be listed without reading it. An entry whose `type` differs from its file's is refused.

- **Built-in themes.** Light, Dark, High contrast white and High contrast black are theme files in the app's resources, with the ids `light`, `dark`, `highContrastWhite` and `highContrastBlack`. Their values are Quiet's, from [ADR 0033](adr/0033-quiet-visual-language.md) and the [visual language](ux/visual-language.md), `hover` and `pressed` included. A build test runs the schema and the contrast check on them. If one can't be read or fails, HikariSub shows a compiled-in Dark and says why in the Log. Built-in themes don't appear in the plugin manager.
- **Types.** `type` is `light`, `dark`, `highContrastLight` or `highContrastDark`. The built-in high-contrast themes are `highContrastLight` (white) and `highContrastDark` (black).
- **What a theme sets.** These keys are required:
  - the interface roles `background`, `panel`, `raised`, `field`, `text`, `muted`, `line`, `disabled`, `accent`, `select`, `danger`, `warning` and `success`, plus `control` in a light or dark theme;
  - `spellcheck` and `grammar`;
  - the Grid's `alternate`, `warning`, `comment`, five `comparison` colours, and the `labels` of the Line-number column for `normal`, `modified`, `saved` and `doubtful` Lines;
  - the line editor's 12 `syntax` colours;
  - the 8 node `ports`;
  - 19 `audio` display colours;
  - the `visualTools` handle colours: `handle`, `handleSelected` and `handleBorder`.

  The video background and the other on-video marks (guides, crosshairs, masks) stay fixed. Interface roles are opaque `#RRGGBB`, except `hover` and `pressed`. Every other colour may add alpha (`#RRGGBBAA`).
- **What a theme may leave out** (Q676). Each of these is worked out from other colours when it's missing:

  | Key | When left out |
  | --- | --- |
  | `roles.focus` | `text` |
  | `roles.hover` | `text` at 5 % in a light theme, 6 % in dark, 8 % in `highContrastLight`, 12 % in `highContrastDark` |
  | `roles.pressed` | `text` at twice `hover`'s alpha |
  | `roles.control` | `line`; only a high-contrast theme may leave it out |
  | `roles.onAccent`, an accent choice's `onAccent` | black or white, whichever contrasts more with the accent |
  | `content.syntax.default` | `text` |
  | `content.grid.comparisonSelection` | `accent` at alpha 75 |
  | `content.audio.selectionBackground` | `accent` at alpha 55 |
  | `content.audio.selectionModified` | `selectionBackground` |
  | `content.audio.waveformSelected` | `accent` |

  A theme may set any of them, and then its own value is checked like the rest.
- **Accent choices** (Q674). `accents` lists 2 to 16 named choices, each `{id, title, accent, onAccent, select}`. One choice must equal the theme's `accent`, `onAccent` and `select` roles, and that one is the default. Built-in Light and Dark carry today's seven: `red`, `orange`, `gold`, `green` (the default), `blue`, `purple` and `pink`. A theme without `accents` hides the accent picker. The user's pick is stored per theme in `appearance.accents`, a list of `<theme id>=<choice id>` entries. It replaces `appearance.lightAccent` and `appearance.darkAccent`, whose values move to the `light` and `dark` entries. A pick whose choice is gone falls back to the default.
- **Colours the user may change** (Q675). `userColours` lists up to 6 interface roles that Settings › Appearance › UI colours lets the user change on this theme. Built-in High contrast white and High contrast black list `accent`, `text` and `line`, as today. No other colour can be changed, and a theme without `userColours` shows no UI colours section. The user's colours are stored in `appearance.colours`, a list of `<theme id>/<role>=#RRGGBB` entries split at the last `/`. It replaces the six `appearance.highContrastWhite.*` and `appearance.highContrastBlack.*` settings, whose `border` becomes `line`. Colours the theme leaves out are worked out from the user's colours, so a changed `accent` brings its own `onAccent`. The user's colours aren't held to the contrast check: they're chosen for the user's own eyes.
- **The contrast check.** HikariSub checks every theme as it loads it: plugin themes at install, built-in themes in the build test. Registry CI checks plugin themes at listing. A theme below any floor is refused with the role pair and the ratio it measured. Each accent choice is checked as if it were the theme's accent. "Surfaces" are `background`, `panel`, `raised` and `field`.

  | Check | Light and dark | High contrast |
  | --- | --- | --- |
  | `text`, `muted`, `danger`, `warning` and `accent` on surfaces | 4.5:1 | 7:1 |
  | `success` on surfaces | 4.5:1, except in a `light` theme, as built-in Light's | 7:1 |
  | `line` on surfaces | none | 7:1 |
  | `focus`, `disabled` and `control` on surfaces | 3:1 | 3:1 |
  | `onAccent` on `accent`, `text` on `select` | 4.5:1 | 7:1 |
  | `disabled` against `text` | 2:1 | 2:1 |
  | `spellcheck` on `field`, `panel` and the Grid's `alternate`; `grammar` on `field` | 3:1 | 3:1 |
  | The waveform on the audio background | 3:1, except in a `dark` theme, as built-in Dark's | 4.5:1 |
  | The cursor on the audio background | 3:1, except in a `dark` theme | 3:1 |
  | The timescale text on its background | 4.5:1, except in a `dark` theme | 4.5:1 |

  A plugin may ship a high-contrast theme if it passes the high-contrast column (Q673).
- **Titles.** A theme's title and its accent choices' titles are translatable text through [#1239](https://github.com/altqx/hikari/issues/1239), for built-in and plugin themes alike.
- **Choosing one.** A plugin theme's id is `plugin:<plugin id>/<theme id>`. Settings › Appearance lists themes by type, the built-ins first and plugin themes after them with the plugin's name. `appearance.theme` is the theme shown while Follow system theme is off. Four settings say which theme each side shows while it's on, and each takes only themes of its type:

  | Setting | Type | Default |
  | --- | --- | --- |
  | `appearance.lightTheme` | `light` | `light` |
  | `appearance.darkTheme` | `dark` | `dark` |
  | `appearance.highContrastLightTheme` | `highContrastLight` | `highContrastWhite` |
  | `appearance.highContrastDarkTheme` | `highContrastDark` | `highContrastBlack` |

  While Follow system theme is on, the system's light or dark scheme picks the side. Whether the high-contrast pair or the ordinary pair is used comes from the chosen theme's type. Following never turns high contrast on or off; it moves between the two members of a pair, as it does today. Choosing a theme also makes it its type's member of the pair. If its side is not the system's, Follow system theme turns off, as choosing a theme by hand does today.
- **When a theme goes.** When the plugin is turned off, uninstalled or revoked, a window showing one of its themes falls back to the built-in of the same type, and a pair setting that named it goes back to its default. Its `appearance.accents` and `appearance.colours` entries stay, so they come back with the theme.

**Icon themes** (#1222). Each `contributes.iconThemes` entry is `{id, title, path}`, where `path` is a package folder holding `<role>.svg` for every role in `src/ui/icons/manifest.json`. Settings › Appearance › Icons picks one, as `appearance.iconTheme` (`hikarisub` by default, or `plugin:<plugin id>/<theme id>`). HikariSub's own icons are the icon theme `hikarisub` in the app's resources (Q680). The same loader reads it, and like the built-in colour themes it isn't in the plugin manager. Each SVG:

- keeps the [SVG rules](#media-and-readme);
- has a `viewBox` of `0 0 16 16`;
- draws in `currentColor` only, with an optional `<g id="accent">` layer that HikariSub paints in the accent.

Other colours are refused, so the icons follow the theme and high contrast. A role that a later HikariSub adds shows the built-in icon until the theme covers it, and the plugin manager says how many roles the theme covers. Registry review checks the icons against the [icon grid](ux/icons.md).

**Own icons** (#1222). `contributes.icons` is a list of `{id, path}` SVGs with the same rules. `own:<id>` names one wherever an icon is asked for: a command's, panel's or status item's `icon`, a widget's `icon`, and `Icon { name: "own:<id>" }` in a QML panel. Without an icon, a command shows the plugin's own `icon` in menus, or the `plugins` role.

### Content packs

#1223. Every item lands beside its built-in counterpart with the plugin's name, is read-only, and goes when the plugin is turned off or uninstalled. A file that fails its reader refuses the package at install and names the file.

| Key | Each entry | Lands in |
| --- | --- | --- |
| `styles` | `{id, title, path}`, a `.sty` catalogue of `Style:` lines | The Style manager's catalogue list, as `<title> (<plugin>)`. Copy to my catalogues makes an editable copy. |
| `karaokePresets` | `{id, path}`, one preset `.ass` in the [preset format](karaoke-presets.md), whose Title and Description show | The Template editor's presets, after the built-in ones. |
| `graphs` | `{id, title, path}`, a graph `.json` in the [graph format](node-effects.md#the-graph-format) | The node-effect graph library, in a group per plugin. |
| `snippets` | `{id, title, insert, prefix, description}`. `insert` is ASS text with `$1` to `$9`, `${1:default}` and `$0` tab stops. | The line editor's tag completion, after the built-in tags, when the typed text starts `prefix`, such as `\fad`. Tab moves between stops. |
| `dictionaries` | `{language, title, aff, dic}`, a Hunspell pair such as `en_US` | The spell checker's languages, after the user's and the bundled ones. A plugin dictionary never replaces a language the user already has. |
| `layouts` | `{id, title, path}`, a `.json` holding one arrangement envelope as in [docking](docking.md) | View › Layout, the layout switcher and Manage layouts, as `<title> (<plugin>)`, after the user's own layouts and in a group per plugin. Applying it works like applying a saved layout, and Reset this layout returns to it. A saved layout of the same name wins. Save layout as… makes an editable copy. |

### Menus, status items and shortcuts

#1224.

**Menus.** Each `contributes.menus` entry is `{command, location, group}`:

| `location` | Where |
| --- | --- |
| `grid.context` | The Grid's right-click menu (`gridMenu`), in a section after HikariSub's own items. |
| `lineEditor.context` | The line editor text's right-click menu (`lineTextMenu`), in a section after HikariSub's own items. |
| `menuBar` | The plugin section of the menu bar: the Automation menu, after Plugins… and before the per-plugin submenus. |

- **Order and groups.** Items keep manifest order. Items of one `group` sit together, and a separator parts groups and plugins. Each item shows its command's title and icon, with the plugin's name on hover.
- **What stays.** Every command still sits in Automation › <plugin>, and in the [command palette](#the-command-palette) once #1236 brings it.
- **Items.** They are `ShellMenuItem`s named `pluginMenu_<plugin id>_<command>`, and the listed-menu tests skip that prefix as they skip `macro_`.

**Conditions.** A command's `when` is a list of conditions that must all hold. `!` in front negates one, as in `["selection", "!video"]`.

| Condition | Holds while |
| --- | --- |
| `document` | The window has an editing target. |
| `selection` | One or more Lines are selected. |
| `multipleLines` | Two or more Lines are selected. |
| `video` | A video is open. |
| `audio` | Audio is open. |
| `karaoke` | The active Line has karaoke tags (`\k`, `\K`, `\kf`, `\ko`). |
| `translation` | The Document is in Translation mode. |

A command whose conditions fail is disabled in every menu and the palette, and its keys do nothing. `commands.list` gives `enabled: false` with the `disabledReason` "Needs a selection" (one phrase per condition), and `commands.run` and `plugins.run` answer `refused` with reason `disabled`.

**Status items.** Each `contributes.statusItems` entry is `{id, title, text, icon, tooltip, command}`, with `text`, `icon` or both. A plugin has at most 4. They sit in the status bar's trailing part (`statusTrailing`), in plugin order and then manifest order, as text, an icon, or both. `command` makes the item a button. The plugin changes an item with `hikari.status.set(item, {text, icon, tooltip, tone, visible})`, which is [`status_items.set`](api.md#status_itemsset). `tone` is `normal`, `warning` or `danger`, drawn in those roles. The status bar's right-click menu lists every plugin item by `title`, with a check box to hide it, kept as `statusBar.hiddenItems`. Items go when the plugin stops.

**Default shortcuts.** Each `contributes.keybindings` entry is `{command, key}`, with one chord in Qt's portable text and modifiers in the order Ctrl, Alt, Shift, Meta, such as `Ctrl+Alt+K`.

- **When a default applies.** It applies while the plugin runs, unless the user bound or cleared that command's shortcut, or the key is taken.
- **When the key is taken.** A key that the user, HikariSub or an earlier-installed plugin already binds stays theirs, in any context. The plugin's default is dropped, and Settings › Shortcuts and the plugin's details say "Ctrl+Alt+K is taken by <owner>".
- **Changing it.** The user changes or clears a plugin default like any shortcut, and the change is kept in the hotkeys file with the other plugin shortcuts. A cleared default is kept as a row without keys, so it stays cleared. A default never overwrites a kept row. Plugin shortcuts stay global.

### Quick picks and input boxes

#1236. A plugin can open HikariSub's own quick-pick list and input box, the popups the command palette and Quick Open use, modelled on VS Code's `showQuickPick`, `showInputBox` and `createQuickPick`. Their look, place and keys are in the command palette spec (`docs/ux/command-palette.md`, [#1236](https://github.com/altqx/hikari/issues/1236)). They need no permission. AI and MCP callers get `not_granted`.

**When a plugin may open one.** Only while it answers the user:

- a command the user started: from a menu item, a key, the palette or a status item (`context.via` other than `api` and `ai`);
- a `click` or `button` event in its own panel or dialog;
- an `accept`, `button` or `back` event of its own live quick pick or input box, so a flow can show its next step;
- a job one of those began, while it runs.

Anywhere else, such as a timer, an event, another plugin's `plugins.run` or an AI tool, the call answers `refused` with reason `not_user_initiated`, so no popup takes the focus unasked. One quick pick or input box shows at a time in a window. Opening another closes the shown one, which then answers as cancelled.

**What every popup shows.** A plugin's popup always has its title row, with `title` (when given) and the plugin's name at the trailing end, so it never looks like HikariSub's own. All text is plain: HikariSub never reads markup in it. A popup closes, answering as cancelled, when its plugin stops.

**Ask for a pick.** `hikari.quick_pick.ask(spec)` is [`quick_pick.ask`](api.md#quick_pickask): it shows the list and waits.

```lua
local picked = hikari.quick_pick.ask{
    title = "Apply a style",
    placeholder = "Style name",
    items = {
        {id = "main", label = "Main", description = "Default dialogue"},
        {kind = "separator", label = "Signs"},
        {id = "sign", label = "Sign", detail = "Top, 48 px", icon = "style"},
    },
}
if picked then print(picked[1]) end
```

- **Items.** At most 10000, each `{id, label, description, detail, icon, picked, always_show, disabled, buttons}`. `disabled` is the reason an item can't be picked: it shows greyed with that reason and Enter skips it. A `kind = "separator"` item is a group heading with only a `label`. `icon` is an icon role or `own:<id>`. `buttons` are up to 2 icon buttons `{id, icon, tooltip}`, shown on the active row.
- **Filtering.** The user's typing filters the list as they type, with the palette's fuzzy match on `label`, and on `description` and `detail` when `match_on` names them. Matches sort best first unless `keep_order = true`. `always_show` items stay whatever is typed. `value` fills the box at the start.
- **Picking several.** `many = true` shows a check box per item, starting from `picked`. Enter takes every checked item.
- **The answer.** A list of the picked ids, in list order, or `nil` when the user pressed Esc, clicked outside, moved the focus away, or another popup took the place. The operation answers `{cancelled = true}` then. A title or item button the user pressed answers `nil, "button", id`.
- **Steps.** `step = {current, total}` shows "2 / 3" for a flow of several popups. `back = true` adds Back to the title row. Pressing it, or Alt+Left, answers `nil, "back"` (`{cancelled = true, back = true}`), so the flow can show the step before.

**Ask for text.** `hikari.input_box.ask(spec)` is [`input_box.ask`](api.md#input_boxask). `spec` is `{title, prompt, placeholder, value, selection, password, validation}`. `selection` is `{from, to}` in `value`. `password = true` shows dots. `validation = {pattern, message, min_length, max_length}` is checked as the user types: while it fails, `message` shows under the box as an error and Enter does nothing. `step` and `back` work as for a quick pick. It returns the text, or `nil` when cancelled (with `"back"` second after Back).

**A live input box.** For checks that take time or have levels, `hikari.input_box.open(spec)` is [`input_box.open`](api.md#input_boxopen-input_boxupdate-and-input_boxclose). It takes `ask`'s spec and returns an id, and its events reach `hikari.input_box.on(box, fn)`: `value` `{value}` as the user types (only the newest waits), `accept` `{value}`, `back` and `hide`.

- **Messages.** `hikari.input_box.update(box, {message = {severity, text}, busy})` shows a message under the box. `severity` is `error`, `warning` or `info`, drawn in `danger`, `warning` and `muted`. `message = hikari.null` clears it.
- **Enter.** While the message is an error, Enter does nothing. While `busy` is true, a check is pending: Enter waits and sends `accept` once `busy` turns false without an error.
- **Closing.** The box stays open after `accept` until the plugin calls `hikari.input_box.close(box)`, so it can refuse the value and say why.

**A live quick pick.** For a list that changes as the user types, such as a search over a glossary, `hikari.quick_pick.open(spec)` is [`quick_pick.open`](api.md#quick_pickopen-quick_pickupdate-and-quick_pickclose). It shows the list and returns its id at once.

```lua
local pick = hikari.quick_pick.open{placeholder = "Search the glossary", filter = "none", busy = true}
hikari.quick_pick.on(pick, function(e)
    if e.kind == "value" then
        hikari.quick_pick.update(pick, {items = search(e.value), busy = false})
    elseif e.kind == "accept" then
        insert(e.items[1])
        hikari.quick_pick.close(pick)
    end
end)
```

- **`filter`.** `"fuzzy"` (the default) filters as `ask` does. `"none"` shows the items as given, for a plugin that filters itself.
- **`hikari.quick_pick.update(pick, fields)`** changes any field of the spec, plus `busy`, a moving bar, and `active`, the id of the highlighted item.
- **Events.** Each is `{kind, pick, …}`:
  - `value` `{value}`, as the user types. Only the newest waits, like a text field's.
  - `active` `{item}`.
  - `accept` `{items}`.
  - `button` `{button, item}`.
  - `back`, with `back = true`.
  - `hide`. The list is gone for good, by Esc, a click outside, another popup or `close`.
- **Lifetime.** The list stays open after `accept` until the plugin closes it, so a flow can go on. It closes when the plugin stops. While it is open, the plugin's handler may run as long as it needs, and the list shows `busy` only when the plugin says so.

### The command palette

The palette itself is #1236's, in the command palette spec (`docs/ux/command-palette.md`, [#1236](https://github.com/altqx/hikari/issues/1236)). For plugins:

- **What it lists.** Every command of a plugin that is on in that window, unless the command has `"palette": false`.
- **How a row reads.** The command's `title`, with its `icon` and shortcut. Its description is its menu path, "Automation › <plugin>" (its Automation submenu, not a [menu placement](#menus-status-items-and-shortcuts)). The command's own `description` is the row's tooltip and accessible description.
- **Search.** It matches the title, then the menu path, so typing the plugin's name finds its commands.
- **Conditions.** A command whose `when` fails stays listed, greyed, with the same reason `commands.list` gives.
- **Running it.** Choosing it runs the command with `args = {}` and `context.via = "palette"`. A command that then needs input asks with `hikari.quick_pick.ask` or `hikari.input_box.ask`, which the palette's own popup hands over to.
- **Quick Open.** Plugins add no Quick Open prefixes or result sources in API 1.

### Background plugins and time

- **No time limit.** A load, command, event, timer callback, provider call or format call that runs longer than 10 seconds and isn't a job isn't stopped. A card above the status bar says "<plugin> is still working", with Stop. Stop force stops the helper, as the job card's Stop does, and the plugin's next command starts a new one. Jobs keep their own cards.
- **Memory.** The Lua heap cap is 2 GB: "the plugin ran out of memory: it may hold 2 GB".
- **Background.** A plugin with `permissions.background` starts with HikariSub and can use timers:
  - `hikari.timer.after(ms, fn)` calls `fn` once after `ms` milliseconds, and `hikari.timer.every(ms, fn)` every `ms` milliseconds, at least 100. Both return an id.
  - `hikari.timer.cancel(id)` stops one.
  - A timer callback runs in turn with the plugin's other work. An `every` timer that comes due while its last callback still runs is skipped once, not queued.
  - A timer callback may begin a [job](#long-jobs).
  - Without the permission, the timer calls answer `nil` and the error `not_granted`.
- **Showing what runs.** While a background plugin works (a timer callback, a job, or an event or command past 10 seconds), the status bar's leading part shows a busy icon with the count. Its popover lists each working plugin with what it does and Stop. Like the other status bar fields, the icon isn't in the Tab order; from the keyboard, turn the plugin off in the plugin manager. Stop ends that helper. A background plugin stopped this way starts again at the next HikariSub start or with Start again in its details.
- **The edit turn.** With no time limit, the app-wide edit turn still ends when the handler that took it returns: `hikari.document.edit` and `hikari.document.group` hold it only while `fn` runs. Another plugin waiting for it still gives up after 5 seconds with `busy`.
- **Unchanged.** Each plugin still has its own helper, its work still runs one thing at a time, and the restricted tier and the sandbox don't change.

### Programs

#1226. Each `contributes.programs` entry is:

```json
{
  "id": "ltex",
  "title": "LTeX language server",
  "network": false,
  "api": false,
  "confinement": "container",
  "platforms": {
    "linux-x64": {"path": "bin/linux-x64/ltex-ls", "args": ["--stdio"]},
    "windows-x64": {
      "download": {
        "url": "https://github.com/example/ltex/releases/download/v1/ltex-windows-x64.zip",
        "sha256": "<64 hex digits>",
        "size": 81234567,
        "archive": "zip",
        "entry": "ltex-ls/bin/ltex-ls.exe"
      },
      "args": ["--stdio"],
      "env": {"LTEX_TOKEN": "${setting:token}"}
    }
  }
}
```

- **Platforms.**
  - Keys are `windows-x64`, `linux-x64`, `windows-arm64`, `linux-arm64`, `macos-x64` and `macos-arm64`, and HikariSub uses the one it runs on.
  - A program with no entry for this platform is unavailable, and so is what needs it. The plugin manager says "<title> isn't available for this computer". The plugin still installs, unless it is a program plugin whose `main` program is missing.
- **Bundled.** `path` is the program's file in the package. Its folder and the files beside it are readable by the program.
- **Downloaded.**
  - **When.** HikariSub downloads the file when the plugin starts and lacks it: after an install, after an update, or after a download that failed. It uses the same request rules as [`hikari.net.fetch`](#network): HTTPS, at most 5 redirects, no cookies.
  - **The check.** It checks `size` (at most 1 GiB) and `sha256` before unpacking. A `zip` or `tar.gz` archive is unpacked with the [package path rules](#the-package), into at most 16384 files and 1 GiB, with no links. `entry` names the program inside it. A file that isn't an archive keeps the name its URL ends in.
  - **Where it goes.** It is kept in `<user folder>/plugin-programs/<plugin id>/<sha256>/` until neither the installed version nor the one kept for Roll back names that hash, or the plugin is uninstalled.
  - **The install question** shows each download's host and size.
  - **When it can't.** The download obeys "Let plugins connect to the internet". With that off, or offline, the program is "not downloaded", and the plugin's details offer Download now.
- **Arguments and environment.** In `args` and `env` values:
  - `${package}`, `${data}` and `${cache}` become the plugin's folders.
  - `${setting:<id>}` becomes a setting's value. A `list` setting written as a whole `args` value becomes one argument per item, and in `env` its items joined by `\n`. A `secret` may be named in `env` only, never in `args`, which other processes can read.
  - `${file:N}` (in `programs.run` and `programs.start` only) becomes the path of the call's `files[N]`.

  The program gets no other environment from HikariSub than `PATH`, `HOME` or `USERPROFILE`, `TEMP`, `LANG` and these. It starts in `${data}`. On Windows it also gets `LOCALAPPDATA`, without which an AppContainer doesn't start. In an AppContainer, Windows points `LOCALAPPDATA`, `TEMP` and `TMP` at the container's own folders; elsewhere `TEMP` is `${cache}/tmp`.
- **The sandbox.** HikariSub starts every program in `hikari_sandbox` with the profile `HikariSub.Program.<the first 32 hex digits of the SHA-256 of "<plugin id>/<program id>">`. That holds for Full trust plugins too.
  - **Access.** The program may read and run its own folder, read and write `${data}` and `${cache}`, and use the files handed to it.
  - **Network.** It has network only with `"network": true`, and the install question then says "Connects to the internet", since the sandbox can't limit a program to some hosts. While "Let plugins connect to the internet" is off, a program with `network` starts without it on Linux and in an AppContainer, and a `lowIntegrity` one doesn't start at all.
  - **Windows.** By default (`"confinement": "container"`) the program runs in an AppContainer. `"lowIntegrity"` runs it at low integrity through `hikarisub-confine.exe` instead, as [ADR 0029](adr/0029-the-ai-sidecar-runs-in-an-os-sandbox.md) does for Codex. That is for runtimes an AppContainer stops, such as Node and some Rust programs. A low-integrity process always has the network, so `lowIntegrity` needs `"network": true`, and the install question adds "Can read your files". Linux ignores `confinement`.
  - **Where the sandbox can't apply.** The program doesn't start, and its details say why. A program never runs unconfined.
- **Lifetime.** A program ends when its plugin stops, is turned off, or is updated. Its stderr goes to the Log panel under the plugin's name, at most 20 lines a minute.

**Downloads kept.** In `<user folder>/plugin-programs/<plugin id>/<sha256>/`, HikariSub keeps the hashes the installed version and the version kept for [Roll back](#updates-roll-back-and-revocation) name, so a roll back needs no download.

**From Lua.** These are the [`programs.*`](api.md#programsrun) operations:

```lua
local out = hikari.programs.run("ffprobe", {args = {"-show_streams", "${file:0}"}, files = {picked.file}})
local p = hikari.programs.start("server")
hikari.events.on("program.output", function(e) if e.process == p then print(e.data) end end)
hikari.programs.send(p, "ping\n")
hikari.programs.stop(p)
```

**Packages per platform.** A package whose programs are bundled may be built per platform: the same signed manifest, with only that platform's files.

- **The index.** A version in the marketplace index then has `packages` instead of `url`, `sha256` and `size`: `{"linux-x64": {url, sha256, size}, "windows-x64": {…}}`.
- **Installing.** HikariSub takes its own platform's package. A listing without one shows "Not available for this computer".
- **Size.** Each per-platform package may be 256 MB unpacked. A package for every platform stays at 64 MB.
- **Registry CI** builds each platform's listing from the release's `<id>-<version>-<platform>.hikariplugin` files. It checks that their manifests are byte for byte the same, and downloads and checks every declared download.

This changes the index schema, `docs/schemas/marketplace-index.schema.json` and the registry's copy, in #1226.

### Program plugins

#1226. A program plugin's `main` program speaks JSON-RPC 2.0 with HikariSub on its stdin and stdout. Each message is one line of UTF-8 JSON, ended by `\n`, at most 96 MiB, as MCP's stdio transport frames it. The operations, events, permissions, limits and errors are those of a Lua plugin.

- **When it runs.** It starts with HikariSub, as a Lua helper does. Without `permissions.background`, its calls to HikariSub are refused with `not_granted` and reason `not_background` unless a request from HikariSub to it is unanswered or a job of its own runs. So, like a Lua plugin, it works only inside a command, event, provider or format call.
- **Working.** For the still-working card and the status bar, it is working while a request from HikariSub is unanswered or a job runs.

- **HikariSub calls the program:**

  | Method | Params | Answer |
  | --- | --- | --- |
  | `initialize` | `{protocol: 1, api: 1, plugin: {id, version}, locale, settings}` | `{}` once it is ready. A program that doesn't answer in 30 seconds didn't start. |
  | `command` | `{command, args, context}` | `null`, or what the command gives back. `args` and `context` are a Lua handler's: `args` is `{}` from a menu, key or status item, and `context.via` says what ran it. |
  | `event` | `{name, data}` | `null`, for each event it subscribed to. |
  | `panel`, `dialog` | `{panel or dialog, event}` | `null`, as `hikari.panels.on` and `hikari.dialogs.on` get them. |
  | `quick_pick`, `input_box` | `{pick or box, event}` | `null`, for an [open quick pick or input box](#quick-picks-and-input-boxes)'s events. |
  | `complete`, `hover` | As for [Lua providers](#language-features-and-problems) | The items, or the hover. |
  | `export` | `{format, document, options}`, as a [Lua exporter](#formats) gets them | `{data, encoding}`: the file as text, or as base64 with `encoding: "base64"`. |
  | `import` | `{format, data, name}`, with the file's bytes in base64 | `{lines, styles, info}`, as a Lua importer returns. |
  | `shutdown` | `{}` | `null`. HikariSub then sends the notification `exit` and closes stdin, and ends the program 5 seconds later. It sends these when the plugin is turned off, updated or removed; Force stop ends the program at once. |

- **The program calls HikariSub:**
  - **Operations.** Every [Hikari API](api.md) operation is a method of its own name with its own params, such as `{"jsonrpc": "2.0", "id": 7, "method": "lines.list", "params": {}}`. So are the plugin services: `panels.*`, `dialogs.*`, `quick_pick.*`, `input_box.*`, `files.*`, `net.fetch`, `job.begin`, `job.progress` and `job.cancelled`.
  - **What Lua's members do.** `hikari/notify` `{text, level}`, `hikari/settings.get` `{id}`, `hikari/settings.set` `{id, value}`, `hikari/subscribe` `{events}`, `hikari/unsubscribe` `{events}`, `hikari/edit.begin` `{name}` and `hikari/edit.end` `{}`. The last two take and give back the app-wide edit turn that `hikari.document.edit` holds. Every step the program commits between them is one undo step, "<plugin name>: <name>", as `document.group_begin` makes. The turn also ends when the request from HikariSub it was taken in is answered.
- **Errors.** A Hikari API error comes back as `{"code": -32000, "message", "data": {"code": "not_granted", "data": {…}}}`. An unknown method is `-32601`, and parameters that break the schema are `-32602` with `data.pointer`.
- **Other output.** stderr goes to the Log panel, at most 20 lines a minute. Anything on stdout that isn't a JSON-RPC message stops the program, as a crash does, and its next command starts it again.
- **Its sandbox.** The `main` program runs as any [program](#programs) does, with the same profile, folders and network. A program plugin whose program is still downloading starts once the download is checked.
- **Types.** [`hikari.d.ts`](types/hikari.d.ts) covers the protocol, and the [Word count](examples/word-count/) sample, a program plugin in TypeScript built into one file with `bun build --compile`, uses it. See [Developer tools](#developer-tools).

### Language features and problems

#1227. These are one provider system for the line editor's text and translation fields.

**Problems.** A plugin sets problems with `hikari.problems.set(problems, {revision, set, document})`, which is [`problems.set`](api.md#problemsset). A problem names a Line, a field, an optional `from`–`to` range in UTF-16 offsets, a `severity` (`error`, `warning`, `info` or `hint`), a `message`, an optional `code` and up to 8 quick `fixes`. A plugin usually sets them again on `document.changed`.

- **Ranges.** `to` comes with `from`, and `from` ≤ `to` ≤ the field's length at `revision`, or the call is `invalid_params`.
- **Edits.** Offsets never move. When a Line's field changes, every plugin's and server's problems on that field of that Line go, until they are set again.

**The problems list.** It is the QC notes panel grown, titled **Problems** with dock id `QcNotes` and panel registry 12 unchanged.

- **What it holds.** The open QC notes as the `QC notes` source, and every plugin's and language server's problems, grouped by Line in Document order.
- **Reading.** HikariSub's own source `reading` lists the Lines past the [reading limits](ux/grid-operations.md#reading-limits) as whole-Line problems: an error at the CPS error level, a warning at the CPS warning level, and a warning for each line longer than the characters-per-line limit and for more lines than the Lines limit. Comment Lines are skipped and there are no quick fixes. The Grid's CPS and Wraps tint already shows these, so they get no Grid marks. **Show reading-limit problems** (`problems.reading`, on by default) in Settings › Grid › Counting turns the source off (Q799).
- **QC notes.** A note sits on the Line the QC notes panel already matches it to (the latest start at its time, else the nearest within 15 seconds), or under No line. Its section is its source's `set`. QC notes keep their own Grid flags and get no problem marks.
- **The toolbar.** It filters by severity and source and keeps QC notes' own Add note and import and export.
- **Using a row.** Choosing a row selects its Line and seeks the video to its start. A row's quick fixes are in its menu and under Quick fix (Alt+Enter, Global 5373) in the line editor.
- **A quick fix** is one undo step named "<source>: <fix>".
- **QC notes** keep their editor, times, sections and done state.

**Where problems show.**

- **Squiggles.** In the line editor, a wavy underline under each problem's range: `danger` for errors, `warning` for warnings, `accent` for info, and a dotted `muted` line for hints, beside today's spelling marks. Hovering one shows its message and source.
- **Grid.** A Grid row with plugins' or servers' problems shows a mark in its row-mark bar, in the colour of its worst severity, and its tooltip counts them. These marks are apart from `marks.*` and don't count toward a plugin's 20000.

**Lua providers.** `contributes.providers` declares `completion` (`triggers` and `fields`) and `hover` (`fields`), and the plugin answers through handlers:

```lua
hikari.providers.completion(function(r)
    -- r = {document, line, field, text, offset, trigger}
    return {{label = "\\fad", insert = "\\fad(${1:200},${2:200})", detail = "Fade in and out", from = r.offset - 3, to = r.offset}}
end)
hikari.providers.hover(function(r)
    return {text = "Fade: 200 ms in, 200 ms out", from = 4, to = 18}
end)
```

- **Completion.** It is asked when the user types a trigger character, or Ctrl+Space. An item is `{label, insert, detail, from, to, snippet}`, at most 200, where `snippet = true` reads `insert`'s tab stops.
- **Hover.** It is asked when the pointer rests on text for 500 ms. A hover is `{text, from, to}` or `nil`.
- **Late answers.** Each gets 1 second. A later answer is dropped, without stopping the plugin.
- **Order.** Completions from every plugin and server, and from the built-in tag list and snippets, show in one list, built-ins first.

**Language servers.** Each `contributes.languageServers` entry names a program that speaks LSP 3.17 on stdio. HikariSub owns the client:

- **Scope `document`** (the default) is one text document per Document and field, `hikari://document/<document id>/<field>`. LSP line *n* is the *n*-th Line in Document order. Comment Lines and graph Lines are empty lines, so the numbers still match.
- **Scope `line`** is one text document for the line editor's Line alone, `hikari://document/<document id>/line/<line id>/<field>`. It is opened when the line editor shows that Line and closed when it leaves.
- **`tags: "blank"`** (the default) turns each override block, `\N`, `\n` and `\h` into as many spaces, so offsets still match. `keep` sends the text as it is, for servers that check tags.
- **Sync.** The text goes as full text (`TextDocumentSyncKind.Full`), 250 ms after the last change, with the Document's revision as the `version`. The position encoding is UTF-16.
- **What HikariSub uses.** `publishDiagnostics` become problems of the source `plugin:<plugin id>/<server id>`. HikariSub also uses `completion`, `hover` and `codeAction`, of kind `quickfix`. A `WorkspaceEdit` is a fix only when it stays inside one Line and touches no range that `tags: "blank"` blanked, so a fix never erases tags. `workspace/configuration` is answered with the plugin's settings. `window/logMessage` and `window/showMessage` go to the Log panel.
- **Lifetime.** The server starts when its first text document opens. After a crash it starts again, up to 3 times in 5 minutes. After that the plugin's details say it stopped, with Start again. `languageId` and `initializationOptions` go as declared.

### AI tools and MCP servers

#1228.

**AI tools.** Each `contributes.aiTools` entry is `{command, description, parameters, result, mark}`.

- **What the AI sees.** The tool's name is `p_<the first 8 hex digits of the SHA-256 of "<plugin id>/<command>">_<command>`, with `-` as `_` and cut to 64 characters, so it fits every provider. Its description starts with the plugin's name, then `description`, and its input is `parameters`.
- **A call** runs the command through a tool gate of its own, not `plugins.run`, with `args` checked against `parameters` and `context.via = "ai"`. The handler's return value, checked against `result` when given, is the tool's answer.
- **Trust.** Like an MCP server's, a plugin's marks count only once the user turns on "Trust <plugin>'s tool marks" in its details, off by default. Until then every tool of the plugin counts as a write.
- **Approval.** A trusted `read` tool runs without asking, like HikariSub's own reads. A `write` tool asks or runs by the thread's mode, like other writes.
- **The read mark is enforced.** While a `read` tool runs, every write operation the plugin makes, and `net.fetch`, `files.write`, `hikari/settings.set` and `programs.*`, answer `refused` with reason `read_tool`.
- **Who runs it.** The command runs as the plugin, with the plugin's grants, never the AI's.
- **What stays always-ask.** `plugins.run`, `commands.run` of a `plugin.` command, and plugin commands without an `aiTools` entry stay on the always-ask list.
- **Over MCP.** HikariSub's MCP server lists a plugin's trusted `read` tools with `readOnlyHint`. It lists no `write` tool, as it refuses every write but `document.apply`.
- **Docs.** #1228 amends [ai-agent.md](ai-agent.md)'s always-ask list and classification, [ai-mcp.md](ai-mcp.md) and [ai-mcp-clients.md](ai-mcp-clients.md) to match. `problems.fix` is a write that asks by the thread's mode.

**MCP servers.** Each `contributes.mcpServers` entry is `{id, title, description, program}`.

- **In Settings › AI › MCP servers** it shows as `<title> (<plugin>)`, off until the user turns it on. The user can't edit its command; its environment comes from the plugin's settings through `${setting:<id>}`.
- **Its tools** follow the [MCP rules](ai-mcp-clients.md), with trust off by default.
- **Sandboxed.** It runs in the sandbox as a program, unlike a server the user adds.
- **When it goes.** Turning the plugin off or uninstalling it removes the server from every thread. Uninstalling also forgets that the user turned it on.
- **How Qt runs it:** [plugin servers](ai-mcp-clients.md#plugin-servers).

### Formats

#1229. Each `contributes.formats` entry is `{id, title, extensions, import, export}`. Its id in `formats.list` and `documents.list`'s `format` is `plugin:<plugin id>/<format id>`.

```lua
hikari.formats.exporter("ebu-stl", function(doc, options)
    -- doc = a hikari.document.read snapshot; options = {frameRate, video, path}
    return bytes, "base64"
end)
hikari.formats.importer("ebu-stl", function(data, name)
    -- data = the file's bytes in base64
    return {lines = {...}, styles = {...}, info = {...}}
end)
```

- **Handlers.** `hikari.formats.exporter(id, fn)` and `hikari.formats.importer(id, fn)` take a format id the manifest declares with `export` or `import`, once each.
- **Export.** It shows in File › Export subtitles and in Save as, as `<title> (<plugin>)` with its extensions. HikariSub gives the snapshot and gets back at most 48 MiB, then writes the file itself. The exporter returns the file as UTF-8 text, or as base64 with `"base64"` as its second result. In Export subtitles the snapshot holds the Lines the dialog's Lines choice names. `options.frameRate` is the dialog's frame rate, else the video's, else `nil`; `options.video` is the open video's path or `nil`; `options.path` is the file being written.
- **Import.** It shows in Open subtitles' filter and in drag and drop for its extensions, and takes files of at most 48 MiB. A built-in format of the same extension is tried first, then plugins' formats in the order their plugins were installed; the first that reads the file wins. Open subtitles' filter lists each format, so choosing one there tries only it.
- **What an import gives.** Lines in the shape of `document.apply`'s `insert_line` fields, Styles as `set_style` fields with their `name`, and Script Info as text by key. HikariSub builds an ASS Document from them; its Default Style stays only when no Styles come. A result that breaks those shapes fails the open with the plugin's name and where it broke, such as `/lines/3/start`.
- **Saving.** A Document opened from a plugin format saves back through the plugin when the format has `export`, and through Save as otherwise.
- **Format calls** are treated like commands: no time limit, the still-working card, and a job when the plugin begins one.

### Settings, grown

#1230, with the unified Settings window of #1238 (owner Q638–Q649). HikariSub has one Settings window, like VS Code's, for its own settings, a Project's and every plugin's. It applies each change at once, with Reset on each row and no OK, Cancel or Apply. Its two tabs are **Yours** and **Project**.

**Where plugin settings show** (Q643). Only under Settings › Plugins › <plugin name>, one page per plugin with settings, in the window's search and contents tree. A manifest can't place a setting anywhere else, and HikariSub's own page ids are never API. The plugin manager's Settings opens that page.

**Every setting may add:**

| Field | Meaning |
| --- | --- |
| `description` | Up to 500 characters of plain text under the title. |
| `keywords` | Up to 16 more words the Settings search matches. |
| `deprecated` | Why the setting is going, up to 300 characters. Settings hides it unless its value isn't the default, and then shows this under it. `hikari.settings.get` still reads it. |
| `group` | A heading on the plugin's page. Groups show in the order of their first setting. |
| `order` | 0 to 9999. Settings sort by it, then by manifest order. |
| `projectable` | `true` (the default): a trusted Project may override it on the Project tab. `false`: only Yours holds it. |

**Types:**

- **`string`** gains `multiline`, a box of several lines.
- **`choice`**'s options become `{value, title}` (Q641): `value` is what is saved and what `hikari.settings.get` returns, a lowercase id that is never shown, and `title` is what the drop-down shows. `default` names a value. A translated or renamed title then never changes the saved value. The schema file still holds today's list of titles until #1230 changes the schema, `manifest.cpp`'s default check and their tests together. #1230 also moves the official plugins' manifests over.
- **`list`** is an array of strings, edited as rows with Add and Remove, with `default` and `maxItems` (256 at most).
- **`secret`** has no default.
  - **Where it's kept.** In the system keychain under the service `HikariSub` as `plugin/<plugin id>/<setting id>`: QtKeychain on Linux, and the DPAPI key store on Windows, as for AI provider keys ([AI providers](ai-providers.md)). Never in `plugins.json` or a Project.
  - **Projectable.** Never: `projectable` is always `false`.
  - **The field** shows dots, with Show and Clear. `hikari.settings.get` returns the text, or `nil` when unset. Uninstalling the plugin removes it.
  - **No keychain.** On a system with no keychain, setting one fails with "No system keychain to keep it in".
  - **Until #1230** wires the keychain, `hikari.settings.get` returns `nil` for a secret and `hikari.settings.set` refuses it with "“<id>” is kept in the system keychain". A value in `plugins.json` or a Project is never read, and an update that turns a setting into a secret drops its kept value.

**Ids and links** (Q644). A plugin setting's id outside the plugin is `plugin.<plugin id>.<setting id>`. The link `hikarisub://settings/<id>` opens the Settings window at a setting, HikariSub's own (`hikarisub://settings/appearance.theme`) or a plugin's. `?scope=project` opens it on the Project tab.

- **The only form.** The id fits `^[A-Za-z0-9_.-]{1,200}$`, and `scope=project` is the only parameter. Like the [install link](#installing-from-the-website), HikariSub ignores anything else and notes it in the `hikari.links` log.
- **What it does.** It only opens the window and shows the setting, never changes one. An id Settings doesn't know opens the window with the id in its search. A Project scope without a trusted Project opens the Project tab with its "Trust this folder" note.
- **From a plugin**, [`settings.open`](api.md#settingsopen) does the same.

**Changes.** A change from Settings, the plugin manager or a Project sends `plugin_settings.changed` to the plugin.

**Not in 0.1.0** (Q649). Named profiles, managed or policy settings, settings sync and per-language settings stay out ([#1240](https://github.com/altqx/hikari/issues/1240)–[#1243](https://github.com/altqx/hikari/issues/1243)), and the manifest has no key for them. Translatable plugin text is [#1239](https://github.com/altqx/hikari/issues/1239).

### Discord status text

#1245 (Q693). A plugin with `permissions.presence` can put a short text in the user's Discord status, such as the episode a team's tracker knows.

- **Setting it.** `hikari.presence.set_text(text)` in Lua, [`presence.set_text`](api.md#presenceset_text) `{text}` from a program plugin. Empty text or `nil` clears it. Errors come back as for any call.
- **Plain text.** At most 64 characters, with no line breaks or other control characters, or the call is `invalid_params`. HikariSub shows it as written, never as Markdown or a link.
- **Where it shows.** Only where the user's own presence text (My own text in Settings › Discord) has the placeholder `{plugin}`, and only from the one plugin picked in Settings › Discord › Text from plugin. That is the setting `privacy.discordPlugin`, a plugin id, empty by default, and its list offers the installed plugins that hold the permission. Anywhere else `{plugin}` stays empty, as `{file}` does while its switch is off.
- **When it's sent.** Only while presence is on. Text from a plugin that isn't picked, or set while presence is off, is kept but goes nowhere, and the call still succeeds. HikariSub sends presence at most once every 4 seconds, as today, so only the newest text in that gap goes out; calling more often isn't an error.
- **How long it stays.** Until the plugin sets or clears it, or is turned off, uninstalled or revoked. It isn't kept when HikariSub quits.
- **Refusals.** Without the permission the call is `not_granted`. AI and MCP callers get `not_granted`. While the plugin runs as a `read` [AI tool](#ai-tools-and-mcp-servers) it's refused with reason `read_tool`, as its other writes are.

### Developer tools

#1231.

**Install dev plugin.**

- **Loading.** Install dev plugin… in the plugin manager's tool strip picks a folder holding `hikari-plugin.json` and runs it in place, without packing or signing. `plugins.json` keeps `"source": "dev"` and the folder's path.
- **Its own data.** A dev plugin never shares an installed plugin's data, secrets or folder grants: it has `plugin-data/.dev/<id>/`, `<cache>/plugins/.dev/<id>/`, keychain entries under `plugin-dev/<id>/` and grants of its own.
- **How it shows.** It carries a Dev badge and never updates by itself; the marketplace leaves it alone. While loaded, it overrides an installed plugin of the same id, and its details say "The installed <version> is overridden by a dev plugin until you unload it."
- **Unloading.** Unload… in its details takes it out of `plugins.json`, and can remove its data. Its folder stays as it is, and an installed plugin of the same id runs again.
- **Reloading.** When a file in the folder changes, HikariSub waits 300 ms, checks the manifest and package rules again, and restarts the plugin, keeping its data, settings and grants. Files and folders whose names start with `.` are skipped, and a symbolic link is refused.
- **Permissions.** A dev plugin gets the install question once. A reload that asks for more than the user said yes to waits for them: its details offer Review…, which asks again.
- **Errors.** A reload that breaks a rule keeps the last good files running. Why shows in its details and the Log panel; a plugin that doesn't start shows as any plugin's does.

**Save a copy as a theme plugin…** (Q681). In the dev tools, this writes the theme being shown, built-in or a plugin's, as a new plugin folder: a `hikari-plugin.json` with one `contributes.themes` entry and the theme file with every key set, worked-out colours included. The folder is then loaded as a dev plugin.

**The test harness.** `hikarisub --test-plugin <folder> <tests.jsonl> [--document <fixture.ass>]` runs headless, prints each failed step, and exits with 0 when every step passed and 1 otherwise. Each line of the script is one step:

- `{"run": "<command>", "args": {…}, "expect": …}`
- `{"event": "<name>", "data": {…}}`
- `{"call": "<operation>", "params": {…}, "expect": …}`
- `{"complete": {…}, "expect": …}`
- `{"export": "<format>", "expectFile": "<path>"}`
- `{"expectDocument": "<fixture.ass>"}`

It builds on `tests/api/scripts/`. Dialogs answer from `{"answer": {…}}` steps.

**Type hints.** `hikarisub --export-docs <dir>` also writes `hikari.lua`, LuaLS `---@meta` annotations for the `hikari` global and every operation, and `hikari.d.ts`, TypeScript types for every operation, event and protocol message, both generated from `api.json`. [docs/types/](types/) holds both for this version.

- **`hikari.lua`.** A class for each type, and each operation's params and result. `hikari.call` has an overload per operation and `hikari.events.on` one per event, so the editor knows what `hikari.call("lines.list", …)` gives back. The other members of the global carry their own signatures, and `PluginStateTest.TheLuaTypeHintsDeclareEveryMemberOfTheHikariGlobal` fails when the helper's global and the file differ.
- **`hikari.d.ts`.** An interface for each type and each operation's params and result. `Operations` and `Events` map names to them, and the protocol has `InitializeParams`, `HostRequests`, `PluginMethods`, `ProgramCalls`, `Request`, `Response` and the error codes.
- **The sample.** [Word count](examples/word-count/) is a program plugin in TypeScript. In builds with the AI sidecar's Bun, `hikari_word_count_sample_types` type-checks it with the sidecar's `tsc`, and skips when that isn't installed. On Linux, `hikari_word_count_sample_build` compiles it with `bun build --compile`, and `hikari_ui_program_plugin_tests` runs it as a plugin's main program, skipping where it wasn't compiled.

**The guide.** [plugin-guide.md](plugin-guide.md) gains a page for each capability above.
