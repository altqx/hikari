---
status: accepted
amends: 0022, 0028, 0029
---

# Plugins grow into an extension system

On 2026-11-17 altqx decided that HikariSub plugins grow toward VS Code and Zed extensions: language servers, themes, more places for commands, background work, outside programs and more. The decisions are Q589–Q610 on tracking [#1209](https://github.com/altqx/hikari/issues/1209), in two rounds after the [extension systems research](../research/extension-systems.md). The design card is [#1221](https://github.com/altqx/hikari/issues/1221). The code cards are [#1222](https://github.com/altqx/hikari/issues/1222)–[#1231](https://github.com/altqx/hikari/issues/1231), all on beta.5 (Q610).

The contract is in [plugins.md](../plugins.md#extensions), [api.md](../api.md#extension-operations), the [manifest schema](../schemas/hikari-plugin.schema.json) and the [theme schema](../schemas/hikari-theme.schema.json). Where this record and those pages differ, the pages win on detail and this record on intent.

## Everything ships in API 1

- **No new versioning machinery** (Q589–Q592). Before 0.1.0 stable everything is API 1, and `kApiVersion`, the plugin, Lua and media protocol versions and every `since` stay 1. After stable, growing the API bumps it to 2 the normal way, as [api.md](../api.md#versioning) and [ADR 0022](0022-hikarisub-plugins.md) already say.
- **No app-version gate,** no tolerance for unknown manifest keys and no runtime capability check. Nobody develops for pre-release builds.
- **No experimental tier** (Q593). Language servers, themes and the rest are part of API 1 and land before 0.1.0 stable, so they are designed right the first time.
- **Strict manifests stay strict.** Every new key is in the schema, which still refuses unknown keys at every level.
- **Every operation the official plugins use stays open to every plugin.** No operation is reserved for `hikarisub.*` plugins.

## What a package can hold

A package's code is now one of three kinds:

- **A Lua plugin.** `main` names a `.lua` file and HikariSub runs it in a helper, as before.
- **A program plugin** (Q609). `main` is `{"program": "<id>"}`, a declared program that speaks the plugin protocol as JSON-RPC on its stdio. It has the same operations, events and permissions as Lua, so a plugin can be written in TypeScript, Python, Rust or anything else. There is no second built-in runtime, and Lua stays the default.
- **No code** (Q594). A package without `main` and without `programs` is data-only. It never starts a helper, never asks for full trust and can't ask for permissions. It is still signed, still goes through the marketplace and installs, updates, rolls back and is revoked like any plugin.

A package may also have programs without `main`, such as a language server alone: Zed's model.

## Themes and icons

- **Colour themes** (Q595). `contributes.themes` gives `light` or `dark` themes that set every colour role: the 15 interface roles, the Grid, the line editor's syntax, node ports, the audio display and the visual tools' handles. Each theme file is checked against the published theme schema. A contrast check against K2's floors runs at install and in registry CI, and a theme that fails isn't installed. They join the built-ins in Settings › Appearance. The two high-contrast themes stay built-in and can't be replaced; a plugin theme can't be high contrast. This reverses "no per-colour editing" only for whole themes: the user still edits no single colour.
- **Icon themes** (Q607). `contributes.iconThemes` replaces HikariSub's interface icons. A theme must cover every icon role the installing HikariSub has. Each SVG keeps the package SVG rules and the 16-unit icon grid, uses only `currentColor` and the `accent` layer, and is reviewed against the grid at listing. A role added in a later HikariSub falls back to the built-in icon.
- **Plugins' own icons.** `contributes.icons` gives a plugin icons for its commands, panels and status items, named `own:<id>`.

## Content packs

Q608: data-only plugins ship Styles (`.sty` catalogues), karaoke presets (`.ass`), node-effect graphs (`.json`), ASS tag snippets for the line editor, Hunspell dictionaries (`.aff` and `.dic`) and saved layouts. Each lands where its built-in counterpart lives and is marked with the plugin it came from. Plugin content is read-only: the user copies it to change it.

## Where commands appear

- **Placements** (Q596). `contributes.menus` places a command in the Grid's right-click menu, the line editor's right-click menu, or the plugin section of the menu bar. `contributes.statusItems` adds up to four status bar items each, and `contributes.keybindings` adds default shortcuts.
- **Overrides.** The user can override every default shortcut. A conflict keeps the user's binding and HikariSub's own, drops the plugin's default and tells the user in the plugin manager and Settings › Shortcuts. Between two plugins, the one installed first keeps the key.
- **Conditions.** A command's `when` is a small fixed list of conditions joined by *and*, each optionally negated with `!`. There is no expression language. A command whose conditions fail is disabled everywhere it appears, its keys do nothing, and `commands.run` refuses it with reason `disabled`.
- **Menu tests.** Menu items stay `ShellMenuItem`s, so `everyMenuIsAShellMenu` holds. `menuBarIsTheListedOne` and `contextMenusAreTheListedOnes` skip plugin rows as they skip macro rows.
- **The video surface stays native.** No plugin draws on it or takes its input.

## Limits and background work

Q597 and Q604:

- **No 10-second kill.** A load, command, event or timer that runs longer than 10 seconds shows a "still working" card with Stop. Jobs keep their cards, with progress and Cancel.
- **Background plugins.** A plugin with the `background` permission starts with HikariSub (a program plugin without it may call HikariSub only while answering it), can use timers (`hikari.timer.after`, `hikari.timer.every`) and can work while no command runs. There is no CPU budget. A background plugin's timer callback can begin a job.
- **Memory.** The Lua heap cap per plugin rises from 256 MB to 2 GB.
- **What stays.** Each plugin keeps its own process, so a crash or hang never freezes HikariSub. While a background plugin works, the status bar shows it with Stop. The restricted tier's rules don't change.

## Outside programs

- **Declared programs** (Q598). A plugin declares programs in `contributes.programs`, and HikariSub starts them in `hikari_sandbox`. Each needs the new **Run programs** permission, which the install question shows. An update that adds a program or an MCP server, gives a program the network or moves one to low integrity holds for approval like other permission increases. Programs always run sandboxed, whatever the plugin's tier.
- **Where a program comes from.** A program is bundled per platform in the signed package, or downloaded by HikariSub from an `https://` URL and checked against the SHA-256 and size in the signed manifest. Either way the signed manifest pins the bytes that run.
- **Sandbox.** On Linux a program gets Landlock and seccomp. Its profile reads its own files and reads and writes the plugin's `data` and `cache` folders and the files a caller hands it. It has no network unless it declares `network`, and the global plugin network switch turns that off too.
- **Windows** (ADR 0029's findings). A program runs in an AppContainer by default. A program declared `lowIntegrity` runs at low integrity through `hikarisub-confine.exe` instead, for the Node and Rust runtimes that an AppContainer stops. A low-integrity process always has the network, so such a program must declare `network`, and it doesn't start while plugin network access is off. The install question says that it can read your files.
- **Packages per platform.** The marketplace serves packages per platform (Windows x64, Linux x64). The index lists each platform's package, and every platform's package has the same manifest.
- **Uses.** Language servers, plugin MCP servers and program plugins build on programs. A Lua plugin can also run one with `programs.run`, or keep one running with `programs.start`.

## Language features and one problems list

- **One built-in provider system** (Q605) for the line editor: problems (squiggles), completions, hover text and quick fixes.
- **Who answers.** A Lua or program plugin answers through `problems.set` and the `complete` and `hover` calls. A declared LSP server is run by HikariSub itself, which owns the LSP client.
- **LSP mapping.** HikariSub maps a Document to one virtual text document per field, with one LSP line per Line. A server can ask for the active Line alone instead. Override blocks are blanked to spaces by default, so prose checkers see prose and the offsets still match.
- **First targets.** Each is proven by a sample or official plugin: grammar and prose checking, ASS override tag completion and checks, and a team glossary check.
- **One problems list** (Q606), grown from the QC notes panel. It holds language-server findings, plugin problems and the open QC notes. Each finding has a severity and jumps to its Line, and a quick fix is one undoable edit. The same findings show as squiggles in the line editor and as marks in the Grid. QC notes keep their own editor, times and done state.

## AI tools and MCP servers

Q599:

- **AI tools.** `contributes.aiTools` turns a plugin command into a typed AI tool, with a JSON Schema for its arguments and a read or write mark. It goes through the approval rules of HikariSub's own tools, through a gate of its own. As with an MCP server, the marks count once the user trusts the plugin's tool marks; until then every tool counts as a write. HikariSub enforces a `read` mark: while a read tool runs, the plugin's writes, network requests and setting changes are refused. `plugins.run` stays on the always-ask list.
- **MCP servers.** `contributes.mcpServers` lists a plugin program that speaks MCP in Settings › AI, off until the user turns it on. Its tools then follow the rules for any MCP server. Unlike a server the user adds, it runs sandboxed.

## Formats

Q600: `contributes.formats` adds subtitle formats, export first. For an export, HikariSub gives the plugin the Document and gets bytes back. For an import, it gives the file's bytes and gets Lines, Styles and Script Info back. The closed `SubtitleFormat` and `ExportFormat` enums become registries. Plugin formats show in the Export dialog, Save as, Open subtitles and drag and drop, each named after its plugin. The EBU STL plugin ([#1218](https://github.com/altqx/hikari/issues/1218)) is the first.

## Settings

**Provisional.** The owner is deciding one unified Settings window for the app's, Projects' and plugins' settings. Until then, the setting fields below, `scope`, grouping and where plugin settings show may change. `secret` settings and `${setting:<id>}` in programs stand.

Q601: plugin settings gain:

- a `description`, a `group` and an `order`;
- `list` settings (an array of strings);
- `secret` settings, kept in the system keychain the way AI provider keys are, never in `plugins.json` or a Project;
- a `scope`: `user` settings can't be overridden by a Project.

Each plugin's settings also get a page under Settings › Plugins. Settings may fill a program's arguments and environment, so a token reaches an MCP server without being written down.

## Quick picks, input boxes and the command palette

The owner added a VS Code-style command palette (Ctrl+Shift+P), Quick Open (Ctrl+P) and one quick-pick list and input box, on [#1236](https://github.com/altqx/hikari/issues/1236). Plugins open the same popups:

- **Services, not operations.** `quick_pick.ask`, `input_box.ask`, `quick_pick.open`, `quick_pick.update` and `quick_pick.close` are plugin services like `dialogs.*`, modelled on VS Code's `showQuickPick`, `showInputBox` and `createQuickPick`. They need no permission, and AI and MCP callers can't reach them.
- **Only when the user asked.** A plugin opens one only while it answers a command, a click in its own panel or dialog, or a status item. A timer, an event or an AI tool can't take the focus with one.
- **The palette** lists every plugin command as "<plugin>: <title>" with its `description`, unless the command sets `palette: false`, and runs it with `via: "palette"`. Its look and Quick Open's prefixes are #1236's UX spec. Plugins add no Quick Open prefixes in API 1.

## Panels

Q602: panels stay the widget tree, plus QML panels for full trust. There are no web views.

## Developer tools

Q603:

- **Install dev plugin.** The plugin manager loads a plugin from a folder and reloads it when a file changes. A dev plugin is marked Dev and never auto-updates. It overrides an installed plugin with the same id while it is loaded.
- **Test harness.** A headless test harness runs a plugin's commands, events and providers against fixture Documents.
- **Type hints.** `--export-docs` writes LuaLS annotations and TypeScript types for the whole Hikari API.

## What this amends

- **[ADR 0022](0022-hikarisub-plugins.md).**
  - "What a plugin reaches" adds declared programs, timers for background plugins, problems, status items and formats.
  - "Plugin first" counts these too, so a feature that needs only them is a plugin.
  - The package section gains data-only and program packages.
  - QC notes still stay native; they become one source in the problems list.
- **[ADR 0028](0028-the-plugin-marketplace.md).** More permissions go back to us: Run programs, background, a program's network, low integrity, a program added and an MCP server added. Registry CI also:
  - checks every platform's package;
  - downloads each declared program and checks its hash and size;
  - runs the theme contrast check;
  - checks that icon themes cover every role.
- **[ADR 0029](0029-the-ai-sidecar-runs-in-an-os-sandbox.md).** `hikari_sandbox` also runs plugin programs, with a profile per program: `HikariSub.Program.<the first 32 hex digits of the SHA-256 of "<plugin id>/<program id>">`.

## Cards and order

The split on #1209 stands. Its order changes in two places: formats move up because #1218 waits on them, and settings come before programs because programs read secret settings.

1. #1221, this design.
2. #1222, data-only packages, themes and icons.
3. #1223, content packs. It runs beside the next cards once #1222 lands.
4. #1224, menus, status items and shortcuts.
5. #1225, limits and background plugins.
6. #1229, formats.
7. #1230, settings.
8. #1226, programs and program plugins.
9. #1227, language features and the problems list. Its LSP half needs #1226.
10. #1228, AI tools and MCP servers. MCP servers need #1226.
11. #1236, the command palette and popups, after its UX spec. Its plugin services follow #1224.
12. #1231, developer tools. Each earlier card adds its own guide page; this card adds the harness, dev plugins and type hints.

Each card updates `tests/api/reference-v1.json`, `api.json`, `docs/plugins.md`, `docs/api-reference.md` and the manifest schema as it lands. The manifest schema already holds every key, so a card that finds a key wrong changes the schema and this record together.
