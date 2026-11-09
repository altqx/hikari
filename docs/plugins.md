# Plugins

A plugin extends HikariSub itself through the [Hikari API](api.md). The decision is [ADR 0022](adr/0022-hikarisub-plugins.md). This page is the contract for the package, install and signatures (PL1, [#413](https://github.com/altqx/hikari/issues/413)) and for running a plugin (PL2, [#414](https://github.com/altqx/hikari/issues/414)). It also covers documents and selections (PL3), panels and the full plugin manager (PL4), files and network (PL5, [#417](https://github.com/altqx/hikari/issues/417)), and the marketplace with its automatic updates, roll back and revocation (PL6, [#418](https://github.com/altqx/hikari/issues/418)).

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
| `permissions` | no | `documents.all` (the reason), `network` (`hosts` and a `reason`), `files` (folder requests, each `id`, `title`, `writable`, `reason`; the ids `data` and `cache` are the plugin's own). See [Files](#files) and [Network](#network). |
| `contributes` | no | `commands` (`id`, `title`), `panels` (`id`, `title`, and `area`: `left`, `right` or `bottom`, where the panel docks when first shown; `right` by default), and `settings` (`boolean`, `integer`, `number`, `string` or `choice`, each with a `default`). |

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
- **Unverified.** Every plugin installed from a file is a sideload and shows as unverified, signed or not; it never updates by itself. A signed one shows its key id, as minisign prints it (16 hex digits). Plugins from the [marketplace](#the-marketplace) are checked against its signed index instead, and then against this same pinned author key.

## The marketplace

The HikariSub marketplace (PL6 [#418](https://github.com/altqx/hikari/issues/418)) lists plugins from [altqx/hikari-plugins](https://github.com/altqx/hikari-plugins). Its index is `https://marketplace.hikarisub.org/index.json`, signed by `index.json.minisig`. The index format is [docs/schemas/marketplace-index.schema.json](schemas/marketplace-index.schema.json), a copy of the registry's `schemas/index.schema.json`.

- **The marketplace key.** The index must verify with the marketplace key, `EF622DD4D472E463`, compiled into HikariSub (`plugins::kMarketplaceKey`). It is not the release key that signs HikariSub's own updates.
- **The trusted comment** must be `file:index.json serial:<serial> generated:<generated>`, matching the index's own fields, so a signature for another file or another index can't stand in.
- **Rollback.** HikariSub keeps the highest serial it has accepted in `<user folder>/marketplace.json` and refuses a lower one.
- **Privacy.** HikariSub downloads the whole index and never sends which plugins are installed: two GETs with no query string, no cookies and no cache. Icons and screenshots come only from the marketplace's own origin, and only while the Marketplace view shows them. HikariSub fetches each one itself, with the same request rules, checks it against the media rules above, and keeps it in memory only.
- **When.** At start and every 24 hours while "Check for plugin updates" (`plugins.checkUpdates`, on by default) is on, and whenever the Marketplace view or Check for updates asks.
- **Install.** HikariSub downloads the listed version's `url`, which must be on the marketplace's origin and no larger than `size`. Then it checks `size` and `sha256`, runs the package checks, and checks that the manifest has the listed id, version, tier and API. The manifest's `publicKey` must be the listing's `publicKey` and, for an installed plugin, the pinned key. The marketplace packages carry no `.minisig`: the signed index vouches for the listing's author key. A downgrade is refused, except Install <last good> after a revocation. The install question always opens first, except for an [automatic update](#updates-roll-back-and-revocation).
- **Source.** `plugins.json` records `"source": "marketplace"` for a marketplace install; a missing `source` means a file. Installing from a file over a marketplace copy makes it a file install again.

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
| `<user folder>/plugins.json` | Per id: `version`, `enabled`, the pinned `key`, the remembered folder `grants`, the `settings` a plugin or the user changed, and `source`; for [updates](#updates-roll-back-and-revocation), `autoUpdate` (only when off), `skip`, `previous`, `held` and `revoked` (`reason`, `lastGood`). |
| `<user folder>/marketplace.json` | `serial`: the highest marketplace index serial accepted. |

An update keeps the enabled state, the grants whose ids the new manifest still asks for, and the setting values that still fit the new manifest. A `plugins.json` HikariSub can't read stops installs rather than forgetting pinned keys.

## The plugin manager

Automation > Plugins… (`GLOBAL_PLUGINS`, 5231) opens one resizable window with two views, Installed and Marketplace: the plugins on the left, the chosen one's details on the right. It also opens at a plugin from About <plugin>… in that plugin's panel menu. Installed counts the updates that wait for the user, and the newer versions that won't install automatically. The tool strip holds Install from file…, Check for updates and Plugin updates, whose popover has "Check for plugin updates" and "Install updates automatically".

- **The list.** Each row shows the package icon, name, version and signer (From the marketplace for a marketplace install). It shows a state only when the plugin isn't simply running: Revoked, <version> waits for you, Turned off, Didn't start, Stopped or Full trust. The filter matches name, author and description. Space turns the chosen plugin on or off, and Delete asks to uninstall it.
- **The details.** What the plugin is and who made it, its trust, its permissions with their reasons, and what it adds (commands and panels). On turns it on or off. A marketplace plugin has its own "Install updates automatically", and Roll back to <version> sits beside the version when a replaced copy is kept. A held update shows a bar with Review…; a revoked plugin shows a red bar with Install <last good> and Uninstall….
- **Remembered folders.** Each folder the user let the plugin keep, with Forget. A forgotten folder is asked for again, and the plugin's open files in it close.
- **Settings.** A form built from `contributes.settings`: a check box for `boolean`, a field for `integer`, `number` and `string`, and a drop-down for `choice`. A change is kept at once, the same way `hikari.settings.set` keeps it, and a running plugin's `hikari.settings.get` sees it.
- **Errors.** A plugin that didn't start, crashed or hit the time limit shows the error and its traceback, with Start again and Copy.
- **Actions.** Install from file… for a new plugin, then per plugin Update to <version> (a marketplace install with a newer listed version), Update from file…, Open data folder and Uninstall…. Update from file… refuses a package with a different id.
- **Move to the marketplace.** A file install whose id the marketplace lists, signed with the listing's author key and not newer than the listed version, offers "Move to the marketplace", which installs the listed version as a marketplace install, or "Not now".
- **Marketplace.** Search matches name, summary and author, and the box narrows to one category. A listing shows the newest version this HikariSub can run, its description, screenshots, permissions, what it needs ("HikariSub <version> or later"), licence, last update and source. Install or Update to <version> downloads it and opens the install question, which says it was signed by the author and checked against the marketplace. A plugin with full access warns that it gets full access to the computer and asks for Install with full access.

## Running a plugin

Each enabled plugin runs in its own `hikari-plugin-helper` process, started when HikariSub starts or when the plugin is turned on, installed or updated. The helper uses the same framing and endpoint as the automation helper. A plugin's crash ends only its own helper.

- **Restricted (the default).** A fresh LuaJIT state with `string` (without `dump`), `table`, `math`, `bit`, `coroutine` and the base functions, minus `dofile`, `loadfile` and `module`. `load` and `loadstring` take text only, so bytecode never loads. There's no `ffi`, `io`, `os`, `debug`, `jit` or `package`, so no `package.loadlib` either. `require "a.b"` reads only `lua/a/b.lua` or `lua/a/b/init.lua` in the package, and only as text.
- **Unrestricted.** The full LuaJIT libraries, with `package.path` pointed at the package's `lua/` folder and `package.cpath` at its `lib/`. Installing one shows the full-trust warning.
- **Memory.** A counting allocator caps the state at 256 MB. A command or event that hits the cap fails with "the plugin ran out of memory: it may hold 256 MB", and the plugin keeps running.
- **Time.** The load, each command and each event runs one at a time. One that runs longer than 10 seconds is force stopped: HikariSub ends the helper and says so in a toast. Time HikariSub spends answering the plugin (an API call, a setting) doesn't count. The plugin's next command starts a new helper.
- **The sandbox.** A restricted plugin's helper starts in an operating-system sandbox, `hikari_sandbox` ([ADR 0029](adr/0029-the-ai-sidecar-runs-in-an-os-sandbox.md)): Landlock and seccomp on Linux, an AppContainer on Windows. Its profile is `HikariSub.Plugin.<the first 32 hex digits of the id's SHA-256>`: the helper's folder to read and execute, the plugin's package folder to read, nothing else, and no network. The helper needs no more, because HikariSub itself opens the files a plugin was granted (`hikari.files`) and makes its requests (`hikari.net.fetch`).
- **Full trust plugins run outside the sandbox,** with the same access as HikariSub.
- **Where the sandbox can't apply,** for example on a kernel without Landlock or seccomp, restricted plugins still run under the limits above. HikariSub logs what is missing under `hikari.plugins.sandbox`, and the plugin's details say "HikariSub keeps it to these permissions, but it isn’t sandboxed on this system."

### The helper protocol

The helper framing ([`helper_protocol.h`](../src/backends/include/hikari/backends/helper_protocol.h)) has an `Event` frame (17) in which the host pushes application events to a helper. It waits behind the helper's running work and is answered like a request, so an event handler can call back into HikariSub. A helper without an event handler answers `Unsupported`. The plugin protocol ([`plugin_protocol.h`](../src/backends/include/hikari/backends/plugin_protocol.h), version 4) has two requests, `Load` and `Run`, plus events and nine services: `Call`, `Notify`, `SetSetting`, `Subscribe`, `Lock`, `Unlock`, `Ui`, `Files` and `Net`. `Ui` carries the panel and dialog operations (`panels.render`, `panels.show`, `dialogs.show`, `dialogs.ask`, `dialogs.close`), `Files` the file operations (`files.pick`, `files.folder`, `files.open`, `files.entries`) and `Net` `net.fetch`. Each takes JSON parameters and is answered like `Call`. When several events of one name wait, only the newest is delivered. Panel and dialog events merge more finely: only text edits, slider drags and pointer moves of one widget replace each other.

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
| `hikari.document.list()`, `hikari.document.read(document)`, `hikari.document.edit(name, fn, options)` | The open Documents, a snapshot of one, and one edit applied as one undo step. See [Documents and selections](#documents-and-selections). |
| `hikari.selection.get(document)`, `hikari.selection.set(selected, active, document)` | The selected Lines and the active Line, as `selection.get` and `selection.set` give and take them. |
| `hikari.panels.render(panel, tree)`, `hikari.panels.show(panel)`, `hikari.panels.on(panel, fn)` | Draws a panel from `contributes.panels`, opens it, and calls `fn(event)` for its events. See [Panels and dialogs](#panels-and-dialogs). |
| `hikari.files.open(place, path)`, `hikari.files.entries(place, path)`, `hikari.files.pick(options)`, `hikari.files.folder(id)`, `hikari.files.list()`, `hikari.files.read(file, encoding)`, `hikari.files.write(file, data, encoding)` | Files in the plugin's own folders, in folders the user chose, and that the user picked or dropped. See [Files](#files). |
| `hikari.net.fetch(request)` | An HTTPS request that HikariSub makes to a host from the manifest. See [Network](#network). |
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

A command `greet` of `com.example.hello` is `plugin.com.example.hello.greet`. While the plugin runs, its commands sit in the Automation menu after Plugins…, in a submenu named after the plugin. In Settings › Shortcuts, the **Plugin shortcuts** filter lists them. A plugin command's shortcut is global, and it is kept with the macro shortcuts in the hotkeys file. A shortcut whose plugin isn't installed or turned on stays listed with a note, ready for when it comes back.

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

## Files

A restricted plugin never names a path on disk. It reaches a file only through a handle, and HikariSub hands one out only for a file the plugin may use:

- **Its own folders.** `data` is `<user folder>/plugin-data/<id>/` and `cache` is `<cache>/plugins/<id>/`. Both are writable, and HikariSub makes them when first used.
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

`hikari.files.open(place, path)` returns `{file, name, access}`: the handle, the path asked for, and `"read"` or `"write"`. `place` is `"data"`, `"cache"` or a folder id. A writable place makes the missing folders on the way. `hikari.files.entries(place, path)` lists a folder in name order as `{name, folder}`, at most 4096 entries. `hikari.files.read(file, encoding)` and `hikari.files.write(file, data, encoding)` take a handle or the table that holds one, as [`files.read` and `files.write`](api.md) do, up to 16 MiB; `encoding` is `"utf8"` (the default) or `"base64"`. `hikari.files.list()` lists the handles the plugin holds.

A `path` is relative, with `/` between its parts, at most 1024 bytes of UTF-8. A part that is empty, `.` or `..`, that holds a control character or one of `\ : * ? " < > |`, that ends with a dot or a space, or that Windows reserves (`CON`, `NUL`, `COM1` and so on) is refused with `not_granted`, as is a path whose links lead outside the place. An empty path is `invalid_params` for `open` and the place itself for `entries`.

`hikari.files.folder(id)` asks the user for the folder the first time and returns whether the plugin has it. The question shows the plugin's name, the request's `title` and `reason`, and **Remember this folder for <plugin>**, checked. A remembered folder is kept in `plugins.json` `grants` and is used without asking from then on, until the user forgets it in the plugin manager or uninstalls the plugin. Otherwise the folder lasts until the plugin stops. A request with `writable = false` is read-only whatever the folder allows. Opening a place the plugin doesn't have yet asks for it the same way, and declining gives `not_granted` with `reason = "declined"`.

`hikari.files.pick{title, save, name, filters}` opens the file dialog and returns `{file, name, access}`, or `nil` when the user cancels. `save = true` asks for a file to write, with `name` as the suggestion. `filters` is a list of up to 16 `{name, patterns}`, where a pattern is `*` or `*.ext`. A file dropped on a panel with `drop = true` sends `{kind = "drop", files = {{file, name, access}}}`, read-only. A pick or drop grants that one file only.

A plugin that stops, crashes or is turned off loses its handles, its folders for this run, and any file dialog still open for it.

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
