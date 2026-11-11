# Write your first plugin

This guide makes a small plugin in three steps: a command, then a panel, then an edit to the subtitles. Then it explains the manifest, permissions and tiers, and how to sign and publish a plugin.

[Plugins](plugins.md) is the full contract, and the [Hikari API reference](api-reference.md) lists every operation and event. Each step below is a complete plugin in [`examples/first-plugin`](examples/first-plugin/), and a test runs all three.

## Before you start

- **Software.** You need HikariSub, a text editor and a zip tool. Plugins are written in Lua 5.1 and run on LuaJIT.
- **Folders.** A plugin is a folder with two files: `hikari-plugin.json`, the manifest, and the Lua file that the manifest names as `main`.
- **Packages.** To install it, you zip the folder's contents into a `.hikariplugin` file.

## Step 1: a command

Make a folder `first-plugin` with this manifest:

```json step-1/hikari-plugin.json
{
  "$schema": "../../../schemas/hikari-plugin.schema.json",
  "id": "com.example.first-plugin",
  "name": "First plugin",
  "version": "0.1.0",
  "api": 1,
  "main": "main.lua",
  "authors": ["Your name"],
  "license": "MIT",
  "contributes": {
    "commands": [{"id": "count", "title": "Count lines"}]
  }
}
```

- **`id`** is reverse-DNS and lowercase. It names the plugin's folders, so don't change it later.
- **`api`** is the lowest Hikari API version the plugin needs. A HikariSub with an older API refuses the plugin, and the message names both numbers.
- **`$schema`** lets an editor check the file against [the published schema](schemas/hikari-plugin.schema.json). Point it at your own copy of the schema, or remove it.
- **`contributes.commands`** declares each command that the plugin adds.

Then add `main.lua`:

```lua step-1/main.lua
hikari.commands.register("count", function()
    local info = hikari.call("documents.get", {})
    hikari.notify(info.title .. " has " .. info.lineCount .. " lines.")
end)
```

- **`hikari.commands.register`** gives the command from the manifest its handler.
- **`hikari.call`** calls a Hikari API operation, here [`documents.get`](api-reference.md#documentsget). It returns the result, or `nil` and an error with a `code`, a `message` and sometimes `data`.
- **`hikari.notify`** shows a toast and writes a Log panel entry under the plugin's name.

To try it:

1. Zip the folder's contents, not the folder itself. On Linux or macOS:

   ```sh
   cd first-plugin && zip -r ../first-plugin.hikariplugin .
   ```

   On Windows, in a terminal in that folder:

   ```powershell
   tar --format zip -cf ..\first-plugin.hikariplugin *
   ```

2. In HikariSub, choose Automation › Plugins…, then Install from file…, and choose the package.
3. Open a subtitle file, then choose Automation › First plugin › Count lines.

After a change, raise `version` and choose Update from file… in the plugin's details. If the plugin fails to start, its details show the error, with the file and line inside the package.

## Step 2: a panel

A panel is drawn from a tree of widgets that the plugin renders. Declare it in the manifest:

```json step-2/hikari-plugin.json
    "commands": [{"id": "count", "title": "Count lines"}],
    "panels": [{"id": "summary", "title": "Summary", "area": "right"}]
```

Then draw it, and draw it again whenever the subtitles change:

```lua step-2/main.lua
local PANEL = "summary"

local function lineCount()
    return hikari.call("documents.get", {}).lineCount
end

local function draw()
    hikari.panels.render(PANEL, {
        kind = "column",
        children = {
            { kind = "label", style = "heading", text = lineCount() .. " lines" },
        },
    })
end
```

```lua step-2/main.lua
hikari.panels.on(PANEL, draw)
hikari.events.on("document.changed", draw)
```

- **`hikari.panels.on`** gets the panel's events. The first one, `show`, comes when the user opens the panel.
- **`hikari.events.on`** listens to a Hikari API event. [`document.changed`](api-reference.md#documentchanged) comes after every edit, undo and redo.

A plugin panel stays closed until the user opens it from View › Panels. Then it docks in the manifest's `area` and behaves like HikariSub's own panels. [Widgets](plugins.md#widgets) lists every kind of widget.

## Step 3: an edit

The last step adds a command and a button that join the line breaks (`\N`) in the selected lines. Add the command to the manifest:

```json step-3/hikari-plugin.json
      {"id": "unbreak", "title": "Join line breaks in the selected lines"}
```

Make the change with `hikari.document.edit`:

```lua step-3/main.lua
local function unbreak()
    local chosen = {}
    for _, id in ipairs(hikari.selection.get().selected) do
        chosen[id] = true
    end
    return hikari.document.edit("Join line breaks", function(doc, change)
        for _, line in ipairs(doc.lines) do
            if chosen[line.id] and line.text:find("\\N", 1, true) then
                change:set_line(line.id, { text = (line.text:gsub("\\N", " ")) })
            end
        end
    end)
end
```

- **The snapshot.** The function gets a snapshot of the subtitles, `doc`, and `change`, where it stages edits.
- **One undo step.** HikariSub applies the staged edits as one undo step, named "First plugin: Join line breaks".
- **Line ids.** Lines are named by their `id`, which survives edits and moves. A row number doesn't.
- **A stale snapshot.** If the user changes the subtitles while the function runs, nothing is applied, and the error is `stale_revision`.

Give the command its handler, and say what happened:

```lua step-3/main.lua
hikari.commands.register("unbreak", function()
    local done, err = unbreak()
    if not done then
        hikari.notify("Nothing was joined: " .. err.message, "warning")
    elseif not done.changed then
        hikari.notify("The selected lines have no line breaks.")
    end
end)
```

Then add the button to the panel, and answer its click:

```lua step-3/main.lua
            { kind = "button", id = "unbreak", text = "Join line breaks", style = "primary" },
```

```lua step-3/main.lua
hikari.panels.on(PANEL, function(event)
    if event.kind == "click" and event.widget == "unbreak" then
        unbreak()
    end
    draw()
end)
```

Every widget that sends events needs an `id`. Each event says which widget sent it, in `event.widget`.

## The manifest

[The manifest](plugins.md#the-manifest) lists every field. Besides the fields above:

- **`description`, `homepage`.** Shown in the plugin manager.
- **`contributes.settings`.** Settings that the user changes in the plugin manager and that the plugin reads with `hikari.settings.get`.
- **`permissions`.** What the plugin asks for, each with a reason that the install question shows. See below.
- **`summary`, `icon`, `screenshots`.** What the marketplace shows. A `README.md` in the package is its page.
- **Translations.** `i18n/<locale>.json` files are read by `hikari.i18n.t`.
- **`contributes.nodes`.** Lua nodes for node effects. Each names its `file`, its `inputs` and `outputs`, and the node joins the Add node list under your plugin's name. Its code runs in the node sandbox, not in your plugin's helper, so it can't call `hikari`. See [Nodes](plugins.md#nodes) and the [Zigzag](examples/zigzag/) example.

## Permissions and tiers

A plugin gets only what its manifest asks for. The user sees each request in the install question, and again when an update asks for more.

- **The active document.** Every plugin can read and edit the subtitle file that's active. `documents.all` lets it see the other tabs too.
- **Files.** A plugin has its own data and cache folders. It can open files that the user picks or drops on its panel, and folders that it names in `permissions.files`, after the user chooses them. It gets handles, never paths. See [Files](plugins.md#files).
- **Network.** HikariSub makes each request for the plugin, over HTTPS, and only to the hosts in `permissions.network`. See [Network](plugins.md#network).
- **Restricted, the default tier.** The plugin's Lua has no `io`, `os`, `ffi` or `debug`. Its helper runs in the operating system's sandbox where one is available.
- **Full trust.** `"tier": "unrestricted"` gives the full LuaJIT libraries, no sandbox, and [QML panels](plugins.md#qml-panels). The user must agree to give the plugin full access to the computer. Ask for it only when the restricted tier can't do the job.

A call that needs a permission the plugin doesn't have fails with the error `not_granted`. [The Hikari API](api.md#calls-callers-and-errors) lists every error code.

## Sign it

A signature tells users and HikariSub that a package comes from you, and lets updates prove they come from the same author.

1. Make a [minisign](https://jedisct1.github.io/minisign/) key once. Keep the secret key safe: you can't change keys without asking the marketplace.

   ```sh
   minisign -G -p you.pub -s ~/.minisign/you.key
   ```

2. Put the public key, the base64 line of `you.pub`, in the manifest as `publicKey`.
3. Sign each package. Ship the `.minisig` file beside the package, with the same name plus `.minisig`.

   ```sh
   minisign -S -s ~/.minisign/you.key -m first-plugin.hikariplugin
   ```

HikariSub checks a signed package against its `publicKey` at the first install, then pins that key. A later update signed by another key needs the user to trust the new key.

## Publish it

The marketplace lists plugins from [altqx/hikari-plugins](https://github.com/altqx/hikari-plugins). Its README is the full process.

1. **The id.** It starts with your GitHub handle, for example `alice.line-tools`.
2. **What a listing needs.** A `summary`, an SPDX `license` and a `README.md` at the package root.
3. **The listing.** Open a pull request there that adds `plugins/<id>.json`, with your public key. The first listing is reviewed by hand.
4. **Releases.** Publish a GitHub release with `<id>-<version>.hikariplugin` and its `.minisig`. The registry checks it, publishes it within half an hour, and uses the release notes as its changelog. Users who installed it from the marketplace get the update automatically.
5. **More permissions.** An update that asks for more permissions waits for review. The users who installed the plugin then confirm the new permissions.

## Next

- **[Plugins](plugins.md).** Dialogs, events, files, the network, QML panels and the plugin manager.
- **[Hikari API reference](api-reference.md).** Every operation, event and type, with its parameters and results. It is generated from HikariSub itself, so it matches the API version that HikariSub reports in `hikari.api_version`.
- **[Line tools](examples/line-tools/).** A larger example with a chart, a list, a menu and a dialog.
- **[Zigzag](examples/zigzag/).** A plugin that adds one node-effects node.
