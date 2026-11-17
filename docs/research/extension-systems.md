# Extension systems: VS Code and Zed, mapped to HikariSub plugins

> **Decided 2026-11-17.** The owner answered the open questions at the end of this note in Q589–Q610 on [#1209](https://github.com/altqx/hikari/issues/1209). The decisions are in [ADR 0034](../adr/0034-plugins-grow-into-an-extension-system.md); where they differ from an option here, the ADR wins. In particular, §6's mechanisms were all turned down: everything ships in API 1 before 0.1.0 stable.

Research note, 2026-11-17. It fed the owner interview on making HikariSub plugins more like VS Code and Zed extensions (language servers, themes, more tools, background processes, "and more"). Primary sources only.

## Pinned sources

| Short name | What | Pin |
| --- | --- | --- |
| **VSC** | microsoft/vscode | [`eb50e1bf17`](https://github.com/microsoft/vscode/tree/eb50e1bf17fc87a192079dda6ab5d2b6523ee68c) (main, 2026-11-17) |
| **VLC** | microsoft/vscode-languageserver-node | [`898f44d0fd`](https://github.com/microsoft/vscode-languageserver-node/tree/898f44d0fd2a0e9c182414060fb7084ed39f49bc) |
| **ZED** | zed-industries/zed | [`482e76524b`](https://github.com/zed-industries/zed/tree/482e76524bd33d8cbdfc352f1fef3eefbf2fad92) (main, 2026-11-17) |
| **HK** | altqx/hikari `origin/main` | `2ac50e272` (read with `git show origin/main:<path>`) |

Below, `VSC:path#Lx` means `https://github.com/microsoft/vscode/blob/eb50e1bf17fc87a192079dda6ab5d2b6523ee68c/path#Lx`, and the same goes for ZED and VLC. Zed's docs pages at zed.dev/docs/extensions are built from `docs/src/extensions/*.md` in the same repository, so ZED doc paths are cited at the pin. VS Code docs are cited by URL and were read on 2026-11-17.

---

## 1. Summary

- **VS Code** runs every extension's JS/TS in a shared **extension host** process: a Node host, a web-worker host or a remote host ([extension host](https://code.visualstudio.com/api/advanced-topics/extension-host); `VSC:src/vs/workbench/services/extensions/common/extensionHostKind.ts#L9-L12`). That host "has the same permissions as VS Code itself", and there is no runtime sandbox ([runtime security](https://code.visualstudio.com/docs/configure/extensions/extension-runtime-security)). Its strengths are a huge declarative `contributes` surface (40 contribution points) and lazy activation events.
- **Zed** runs extension code as **WASM components** (Rust → `wasm32-wasip2`) inside the editor's process under wasmtime. The interface is versioned WIT, and WASI preopens only the extension's own work dir (`ZED:crates/extension_host/src/wasm_host.rs#L557-L596`, `#L742-L760`). Most features (themes, icon themes, languages/grammars, snippets) need **no code at all**. Code exists mainly to *return a command line* for an external language server, MCP server or debug adapter, and Zed then spawns and talks to that process itself (`ZED:crates/extension_api/wit/since_v0.8.0/extension.wit#L98`, `#L161`, `#L177`).
- **HikariSub today** sits between the two, closer to Zed in spirit. It has isolated per-plugin LuaJIT helper processes, an OS sandbox for restricted plugins, a host-brokered narrow API (files by handle, HTTPS by host allow-list), declarative widget-tree panels, and a signed marketplace. Its gaps against both editors are: no declarative-only contributions (themes, icon themes, snippets/templates, dictionaries, formats); no menus, toolbar or status-bar placement beyond the Automation submenu; no long-lived background work outside a command; and no way to run an external server process (LSP/MCP-style).
- **The versioning constraint is the sharpest finding.** HikariSub's manifest schema is `additionalProperties: false` at every level (HK `docs/schemas/hikari-plugin.schema.json`). HK `docs/api.md` §Versioning and ADR 0022 also say every release that grows the API bumps the number. With the API frozen at 1, a HikariSub 0.1.0 would **refuse** any plugin that uses a contribution point added in 0.2.0, and the marketplace's `apiVersions` gate stops telling versions apart. Growing additively without a bump needs (a) app-version gating, (b) tolerated unknown keys, and (c) runtime capability detection. §6 covers how LSP, MCP, VS Code and Zed each do this.

---

## 2. What HikariSub has today (origin/main)

| Area | Today | Source |
| --- | --- | --- |
| Runtime | One `hikari-plugin-helper` process per enabled plugin, running LuaJIT. **Restricted** (default): no `ffi/io/os/debug/jit/package`, text-only `load`, `require` only inside the package. **Unrestricted**: full LuaJIT and native libs behind a full-trust prompt. A crash ends only that plugin's helper. | HK `docs/plugins.md` §Running a plugin; `docs/adr/0022-hikarisub-plugins.md` §Isolation and tiers |
| Limits | 256 MB Lua heap cap; each load, command or event has 10 s unless it becomes a **job** (`hikari.job.begin`), which only a command or a panel/dialog click can start | HK `docs/plugins.md` §Running a plugin, §Long jobs |
| OS sandbox | Restricted helpers start in `hikari_sandbox`: Landlock + seccomp on Linux, an AppContainer on Windows. The helper may read its own folder and the package only, with **no network**. Unrestricted plugins are unsandboxed. | HK `docs/plugins.md` §The sandbox; `docs/adr/0029-the-ai-sidecar-runs-in-an-os-sandbox.md` §The sandbox |
| Manifest | `hikari-plugin.json`: `id`, `name`, `version`, `api`, `main`, `authors`, `tier`, `publicKey`, `permissions` (`documents.all`, `network`, `files`, `vcs.read`), `contributes` (`commands`, `panels`, `settings`, `nodes`). The JSON Schema refuses unknown keys. | HK `docs/plugins.md` §The manifest; `docs/schemas/hikari-plugin.schema.json` |
| API | The **Hikari API** (`src/api`): typed operations with read/write marks and a JSON Schema for each, plus events. Shared by plugins, AI tools, the MCP server and nodes. `kApiVersion = 1`; each operation has `since`; a frozen `tests/api/reference-v1.json` guards against breaks. | HK `docs/api.md`; `src/api/include/hikari/api/api.h:17` |
| UI | Declarative widget tree (about 20 kinds, `canvas` draw list) rendered in HikariSub's theme; dialogs; Grid row marks. Full-trust plugins may ship **QML panels** in HikariSub's own engine, which is not isolated: a QML crash takes down HikariSub. | HK `docs/plugins.md` §Panels and dialogs, §QML panels, §Grid row marks |
| Commands | `plugin.<id>.<cmd>`, shown in an Automation submenu; shortcuts can be bound in Settings › Shortcuts. There is **no** menu, toolbar, context-menu or default-keybinding contribution. | HK `docs/plugins.md` §Commands and shortcuts |
| Files / net / git | Files only by handle: own `data/` and `cache/`, a trusted Project, remembered folder grants, pick/drop. HTTPS only to manifest hosts, made **by HikariSub**, with one global off switch. Git read-only, run by the host. "A plugin can't run programs." | HK `docs/plugins.md` §Files, §Network, §Git |
| Settings | `contributes.settings` (`boolean/integer/number/string/choice`) rendered as a form in the plugin manager; a Project can override settings and enable or disable plugins (trusted Projects only) | HK `docs/plugins.md` §The plugin manager, §Plugins in a Project |
| Node effects | `contributes.nodes`: Lua nodes run in the node sandbox, not in the helper | HK `docs/plugins.md` §Nodes; `docs/adr/0023-node-effects.md` |
| Distribution | `.hikariplugin` zip with author minisign signatures and the key pinned at first install. A marketplace index signed by the marketplace key, with rollback serials, auto-update, a hold when an update asks for more permissions, roll back one step, revocation, sideloads marked "Unverified", and a `hikarisub://plugin/install` link. Registry: Git PR for the first listing, GitHub releases for updates, CI checks. | HK `docs/plugins.md` §Signatures, §The marketplace; `docs/adr/0028-the-plugin-marketplace.md` |
| AI | The AI sidecar on Bun runs in an OS sandbox with no network or keys; Qt relays HTTP and starts agent CLIs. MCP **client**: the user adds servers in Settings › AI, and these run **unsandboxed** in v1. | HK `docs/adr/0026-…-sidecar-on-bun.md`; `docs/adr/0029-…`; `docs/ai-mcp-clients.md` |
| Automation | The legacy Aegisub Lua automation host stays separate, in one process per script, and is **not** a sandbox | HK `docs/adr/0006-isolated-lua-automation-host.md`; `docs/automation.md` |
| DX | `docs/plugin-guide.md` (three steps, a test runs them); the Agent can scaffold a plugin (`plugins.scaffold`) | HK `docs/plugin-guide.md`; `docs/api.md` table |
| Policy | "Plugin first": a feature that fits the plugin API ships as an official plugin. Core editing, video, audio, timing and anything on the video surface stay native. | HK `docs/adr/0022-…` §Plugin first |

Closed seams that matter for the mapping:

- Themes are four built-ins plus accent presets in `src/ui/theme.h`, with "no per-colour editing" (HK `docs/ux/visual-language.md` §Hikari theme layer).
- Icons come from `IconTheme` (`src/ui/icon_theme.h`).
- Formats are closed enums: `SubtitleFormat` (`src/core/include/hikari/core/document.h:190`), `ExportFormat` (`src/core/include/hikari/core/export.h:14`), `LyricsFormat` (`lyrics_import.h:49`).
- Spelling goes through an internal `SpellingBackend` interface over Hunspell dictionaries (`src/application/include/hikari/application/spell_checker.h`).

---

## 3. VS Code

### 3.1 Architecture

- **Three host kinds.** `LocalProcess`, `LocalWebWorker` and `Remote` (`VSC:src/vs/workbench/services/extensions/common/extensionHostKind.ts#L9-L12`).
  - Local: "A Node.js extension host running locally".
  - Web: a browser WebWorker that needs a `browser` entry.
  - Remote: a Node host in a container or SSH target.
  - Source: [extension host](https://code.visualstudio.com/api/advanced-topics/extension-host).
- **Where an extension runs.** `extensionKind` decides it: `["workspace"]`, `["ui"]` or a preference order ([extension host](https://code.visualstudio.com/api/advanced-topics/extension-host)).
- **Process.** On desktop the local host is an Electron **UtilityProcess** started by the main process (`VSC:src/vs/platform/extensions/electron-main/extensionHostStarter.ts#L14`, `#L77`). Extensions **share** one host: the crash log lists "The following extensions were running: …" (`VSC:src/vs/workbench/services/extensions/common/abstractExtensionService.ts#L954`).
- **Crash isolation is per host, not per extension.**
  - One bad extension takes down all the others in that host.
  - A remote host auto-restarts unless it crashes 3 times in 5 minutes (`abstractExtensionService.ts#L926-L929`, `ExtensionHostCrashTracker` `#L1549-L1552`).
  - The UI stays alive because it runs in a different process.
- **Languages.** JS/TS only (`main` for Node, `browser` for web). Anything else runs as a child process the extension spawns (for example an LSP server) ([extension manifest](https://code.visualstudio.com/api/references/extension-manifest); [LSP guide](https://code.visualstudio.com/api/language-extensions/language-server-extension-guide)).
- **Lazy activation.** `activate()` runs once, on the first matching activation event.
  - Events: `onLanguage`, `onCommand`, `onView`, `workspaceContains`, `onUri`, `onStartupFinished`, `*` and about 25 others.
  - Since 1.74, contributed commands, languages, views, custom editors and auth providers activate implicitly.
  - `*` is discouraged; `onStartupFinished` "will not slow down VS Code startup".
  - Source: [activation events](https://code.visualstudio.com/api/references/activation-events).
- **Performance budget.** There is no hard CPU or time cap per extension in these docs. The host's stated purpose is to stop extensions "impacting startup performance" and "slowing down UI operations", and lazy activation is the main tool ([extension host](https://code.visualstudio.com/api/advanced-topics/extension-host)).

### 3.2 Manifest and contributions

- **Manifest.** `package.json` with `name`, `publisher`, `version` and `engines.vscode` (required, not `*`). Also `main`/`browser`, `activationEvents`, `extensionKind`, `extensionDependencies`, `extensionPack`, `capabilities.untrustedWorkspaces/virtualWorkspaces`, `contributes`, `pricing` and `sponsor` ([manifest](https://code.visualstudio.com/api/references/extension-manifest)).
- **40 contribution points** ([contribution points](https://code.visualstudio.com/api/references/contribution-points)):
  - Commands and UI: `commands`, `menus`, `submenus`, `keybindings`, `views`, `viewsContainers`, `viewsWelcome`, `walkthroughs`.
  - Settings: `configuration`, `configurationDefaults`.
  - Themes and icons: `themes`, `iconThemes`, `productIconThemes`, `icons`, `colors`.
  - Languages: `languages`, `grammars`, `snippets`, `semanticToken*`, `jsonValidation`.
  - Tasks and debugging: `taskDefinitions`, `problemMatchers/Patterns`, `debuggers`, `breakpoints`.
  - Editors and notebooks: `customEditors`, `notebooks`, `notebookRenderer`.
  - Other: `terminal`, `authentication`, `resourceLabelFormatters`, `typescriptServerPlugins`.
  - AI: `chatAgents`, `chatInstructions`, `chatPromptFiles`, `chatSkills`, `languageModelTools`, `languageModelChatProviders`.
- **`configuration`** is JSON-Schema-like. Types are rendered in the Settings UI, and there are scopes `application`, `machine`, `machine-overridable`, `window`, `resource` and `language-overridable`, plus `markdownDescription` and `order` (same page).
- **Declarative vs programmatic.** Contributions are read without running code; behaviour is registered in `activate()` against `vscode.d.ts`. Examples:
  - `window.createStatusBarItem` (`VSC:src/vscode-dts/vscode.d.ts#L11641`)
  - `tasks.registerTaskProvider` (`#L9359`)
  - `window.registerCustomEditorProvider` (`#L11791`)
  - `lm.registerTool` (`#L21045`)
  - `lm.registerMcpServerDefinitionProvider` (`#L21109`)

### 3.3 Capabilities

| Capability | How | Source |
| --- | --- | --- |
| Colour themes | `contributes.themes` `{label, uiTheme, path}` pointing to a JSON colour theme. No code. | [contribution points](https://code.visualstudio.com/api/references/contribution-points) |
| Icon themes | `iconThemes` (file icons), `productIconThemes` (UI icons), `icons` | same |
| Language servers | The extension bundles the server and starts it with `vscode-languageclient`. `ServerOptions` is an `Executable {command, args, options}` or a `NodeModule {module, runtime}`, optionally split into `run`/`debug`. Transports: `stdio`, `ipc`, `pipe`, `socket`. The client wires LSP features to the editor once the server's `initialize` capabilities arrive. | `VLC:client/src/node/main.ts#L30-L35`, `#L62-L95`, `#L151`; [LSP guide](https://code.visualstudio.com/api/language-extensions/language-server-extension-guide) |
| Who ships the server | The extension, usually inside the VSIX ("Since VS Code already ships with a Node.js runtime, there is no need to provide your own"). Platform-specific VSIX targets (`win32-x64`, `linux-arm64`, …) carry native binaries. | [LSP guide](https://code.visualstudio.com/api/language-extensions/language-server-extension-guide); [publishing](https://code.visualstudio.com/api/working-with-extensions/publishing-extension) |
| Debuggers | `contributes.debuggers` plus `DebugAdapterDescriptorFactory` | [contribution points](https://code.visualstudio.com/api/references/contribution-points); `VSC:src/vscode-dts/vscode.d.ts#L17238` |
| MCP / AI | `languageModelTools`, `chat*` contributions; `McpServerDefinitionProvider` | `vscode.d.ts#L20784`, `#L21428` |
| Tasks | `taskDefinitions` + `TaskProvider`; blocked in Restricted Mode | `vscode.d.ts#L9227`; [workspace trust](https://code.visualstudio.com/api/extension-guides/workspace-trust) |
| Webviews / custom editors | A webview is "an `iframe` within VS Code that your extension controls" and talks by `postMessage`. Scripts are off by default, a CSP is recommended, `localResourceRoots` limits files, and `retainContextWhenHidden` "has high memory overhead". "Should … be used sparingly and only when VS Code's native API is inadequate." | [webview guide](https://code.visualstudio.com/api/extension-guides/webview) |
| Background processes | Unrestricted Node: `child_process`, sockets, timers. Nothing brokers them. | [runtime security](https://code.visualstudio.com/docs/configure/extensions/extension-runtime-security) ("run external processes") |
| Files / network | Direct Node `fs`/`net`; also `FileSystemProvider` for virtual schemes | same; `vscode.d.ts#L9603` |
| Storage / secrets | `ExtensionContext.globalState`/`workspaceState` (Memento), `storageUri`/`globalStorageUri`, `secrets: SecretStorage` | `vscode.d.ts#L8418-L8530`, `#L8640` |
| Telemetry | `env.isTelemetryEnabled`, `env.createTelemetryLogger` | `vscode.d.ts#L10811`, `#L10832` |
| Status bar | `window.createStatusBarItem(id, alignment, priority)` | `vscode.d.ts#L11641` |
| Tests / notebooks | `tests.createTestController`; `notebooks`/`notebookRenderer` contributions | `vscode.d.ts#L18551`; contribution points |

### 3.4 Security model

- **No sandbox.** "The extension host has the same permissions as VS Code itself… an extension can read and write files on your machine, make network requests, run external processes" ([runtime security](https://code.visualstudio.com/docs/configure/extensions/extension-runtime-security)). This is the known criticism, stated by VS Code itself.
- **Mitigations sit at the marketplace and install boundary** ([runtime security](https://code.visualstudio.com/docs/configure/extensions/extension-runtime-security)):
  - malware scans of every published version, plus a dynamic "clean room VM" run;
  - Marketplace signing that VS Code verifies at install;
  - a publisher-trust prompt on first install from a third-party publisher (since 1.97);
  - verified publishers (domain proof plus 6 months in good standing);
  - name-squatting protection;
  - a **block list**: a blocked extension is "automatically uninstalled by VS Code".
- **Workspace Trust is not a sandbox.** It decides whether an extension activates in Restricted Mode and which settings it sees ([workspace trust](https://code.visualstudio.com/api/extension-guides/workspace-trust)):
  - `capabilities.untrustedWorkspaces.supported` is `true`, `false` or `'limited'`;
  - `restrictedConfigurations` ignores workspace values while untrusted;
  - `workspace.isTrusted` and `onDidGrantWorkspaceTrust` are available to code;
  - Restricted Mode blocks tasks and debugging;
  - for `'limited'`, enforcement is the author's job: "A command can still be called even if it is not presented in the UI".

### 3.5 Versioning and compatibility

- **`engines.vscode`** is a semver range, for example `^1.8.0`. It ensures "the extension only gets installed for clients that contain the API you depend on", and Insiders builds can be targeted by date suffix ([publishing](https://code.visualstudio.com/api/working-with-extensions/publishing-extension)). The *app* version is the API version; there is no separate API number. `vscode.version` is exposed (`vscode.d.ts#L11`).
- **Proposed API.** Unstable APIs live in `vscode.proposed.<name>.d.ts` (186 files at the pin, under `src/vscode-dts/`).
  - An extension opts in by name in `enabledApiProposals`.
  - They work "only available in Insiders", and authors "should not publish extensions using the proposed API on the Marketplace" ([proposed API](https://code.visualstudio.com/api/advanced-topics/using-proposed-api)).
  - Effect: new API grows in a gated namespace and becomes stable only once settled.
- **Additive stable API.** New members are added to `vscode.d.ts`, and implicit activation was added without breaking explicit events ([activation events](https://code.visualstudio.com/api/references/activation-events)).

### 3.6 Distribution

- **Packaging.** `vsce package/publish` builds a `.vsix` ([publishing](https://code.visualstudio.com/api/working-with-extensions/publishing-extension)).
- **Credentials.** The Marketplace runs on Azure DevOps. Global PATs retire on **2026-12-01**, and Trusted Publishing (GitHub OIDC) replaces them.
- **Release lanes.** Pre-release and release versions must differ, and VS Code suggests odd/even minor numbers. Platform-specific packages are supported.
- **Auto-update.** "VS Code will automatically update extensions to the highest version available".
- **Sideload.** "Install from VSIX".
- **Removal.** Unpublish or remove; a removed name is reserved forever.

### 3.7 Developer experience

- **Scaffolding.** `npx --package yo --package generator-code -- yo code`. F5 opens the **Extension Development Host**, "Developer: Reload Window" picks up changes, and breakpoints work in the debugger ([first extension](https://code.visualstudio.com/api/get-started/your-first-extension)).
- **Types.** `@types/vscode`, plus `@vscode/dts` for proposed APIs ([proposed API](https://code.visualstudio.com/api/advanced-topics/using-proposed-api)).
- **Tests.** `@vscode/test-cli` (Mocha) or `@vscode/test-electron` run integration tests in a real VS Code. Useful flags: `--extensionDevelopmentPath`, `--extensionTestsPath`, `--disable-extensions`, `--profile-temp` ([testing](https://code.visualstudio.com/api/working-with-extensions/testing-extension)).
- **LSP debugging.** `autoAttachChildProcesses` attaches to the server process ([LSP guide](https://code.visualstudio.com/api/language-extensions/language-server-extension-guide)).

---

## 4. Zed

### 4.1 Architecture

- **Code is optional.** An extension is a Git repo with `extension.toml`. "Most extensions will work properly without any Rust code present… only language server, context server and debugger extensions require … custom Rust" (`ZED:docs/src/extensions/developing-extensions.md`).
- **WASM components in-process.**
  - Rust compiles to `wasm32-wasip2`.
  - One global `wasmtime::Engine` runs with the component model and **epoch interruption**: a timer bumps the epoch every 100 ms, and each `Store` yields after one tick (`ZED:crates/extension_host/src/wasm_host.rs#L557-L596`, `#L686-L687`).
  - Epochs are for fairness ("yield control back to the executor"), not a kill timeout.
  - Compiled code goes into an incremental-compilation cache capped at 32 MB (`wasm_host.rs#L1005-L1020`).
- **WASI filesystem.** Each extension preopens **only** its own work dir, read-write (`wasm_host.rs#L742-L760`). The extension directory is split into `installed/` (sources) and `work/` (downloaded servers and other files) (`ZED:docs/src/extensions/installing-extensions.md`).
- **The WIT world (v0.8.0).**
  - Imports: `context-server`, `dap`, `github`, `http-client`, `platform`, `process`, `nodejs`, `get-settings` and `download-file`, plus a `worktree` resource with `read-text-file`, `which` and `shell-env` (`ZED:crates/extension_api/wit/since_v0.8.0/extension.wit#L4-L10`, `#L55-L84`).
  - Exports are **callbacks Zed invokes on demand**: `init-extension`, `language-server-command`, LSP init/workspace config and schemas, `labels-for-completions/symbols`, `context-server-command`, slash commands, `index-docs`, and `get-dap-binary` plus the DAP locators (`extension.wit#L20`, `#L98-L183`).
- **Lifecycle.** There are no activation events. Declarative resources (themes, languages, grammars, snippets) are indexed from the manifest and loaded when the extension index is rebuilt; the installed dir is watched (`ZED:crates/extension_host/src/extension_host.rs#L512-L519`, `#L1258-L1426`). WASM is instantiated with `init-extension` and is called only through the exports above.
- **Crash isolation.** A trap returns an error from the call (`.to_wasmtime_result()` in `ZED:crates/extension_host/src/wasm_host/wit/since_v0_8_0.rs`). The real work runs in the external server processes Zed spawns, so a crashing LSP server does not take Zed down.

### 4.2 Manifest

- **`ExtensionManifest` fields** (`ZED:crates/extension/src/extension_manifest.rs#L88-L128`):
  - identity: `id`, `name`, `version`, `schema_version`, `description`, `repository`, `authors`;
  - code: `lib`;
  - declarative content: `themes`, `icon_themes`, `languages`, `grammars` (repo + rev), `snippets`;
  - process providers: `language_servers` (`languages`, `language_ids`, `code_action_kinds`, `opt_in_languages`), `context_servers`, `debug_adapters`, `debug_locators`;
  - deprecated: `slash_commands`;
  - permissions and AI: `capabilities`, `language_model_providers`.
- **Language directories** hold `config.toml` and Tree-sitter queries: `highlights.scm`, `brackets.scm`, `outline.scm`, `indents.scm`, `injections.scm`, `overrides.scm`, `textobjects.scm`, `redactions.scm`, `runnables.scm`, plus `semantic_token_rules.json` (`ZED:docs/src/extensions/languages.md`).
- **No equivalent** of commands, menus, keybindings, panels, status bar or webviews. Zed extensions cannot add UI.

### 4.3 Capabilities

| Capability | How | Source |
| --- | --- | --- |
| Themes | JSON files under `themes/`, schema `https://zed.dev/schema/themes/v0.2.0.json`: a theme family with `appearance`, `style` UI colours, `syntax`, terminal ANSI colours. The Theme Builder exports them. | `ZED:docs/src/extensions/themes.md` |
| Icon themes | `icon_themes/*.json` (schema `icon_themes/v0.3.0.json`): directory, chevron, file-stem and suffix maps to bundled SVGs | `ZED:docs/src/extensions/icon-themes.md` |
| Languages + grammars | `[grammars.x] repository, rev`: Zed builds the Tree-sitter grammar to WASM, using wasi-sdk | `ZED:docs/src/extensions/languages.md`; `developing-extensions.md` |
| Language servers | The manifest names the server and its languages. Rust implements `language_server_command(id, worktree) -> Command{command,args,env}`, usually finding the binary with `worktree.which` or downloading it (`download-file`, `github` release lookup, `npm:install`), and reporting progress with `set-language-server-installation-status`. **Zed** spawns the process and speaks LSP. Publishing rule: "Do not bundle a language server with the extension. Download it or check for it in the user's environment." | `ZED:docs/src/extensions/languages.md#L472-L500`; `extension.wit#L61`, `#L67`, `#L98`; `ZED:docs/src/extensions/publishing/prerequisites.md` |
| Debuggers | `[debug_adapters.x]` + JSON schema; `get_dap_binary`, `dap_request_kind`, `dap_config_to_scenario`; locators turn tasks into debug scenarios | `ZED:docs/src/extensions/debugger-extensions.md` |
| MCP (context servers) | `[context_servers.x]` + `context_server_command`. **Deprecated** in favour of the official MCP registry, and new submissions are not accepted. | `ZED:docs/src/extensions/mcp-extensions.md`; `publishing/prerequisites.md` |
| Slash commands / agent servers | **Removed or deprecated.** Slash commands are replaced by MCP; agent servers moved to the ACP Registry as of Zed v1.5.0. | `ZED:docs/src/extensions/slash-commands.md`; `agent-servers.md` |
| Snippets | `snippets = ["./snippets/rust.json"]`; `snippets.json` is global | `ZED:docs/src/extensions/snippets.md` |
| Processes | `process.run-command` runs a child and collects its output (synchronous, no streaming) | `ZED:crates/extension_api/wit/since_v0.8.0/process.wit#L28` |
| Network | `http-client.fetch` / `fetch-stream` are **not capability-gated** in the host. Only `download-file` is. | `ZED:crates/extension_host/src/wasm_host/wit/since_v0_8_0.rs#L633-L660` vs `#L1068-L1076` |
| Settings | `get-settings(location, category, key)` reads Zed settings (for example LSP settings for a server) | `extension.wit#L55` |
| Storage | The WASI work dir; a `key-value-store` resource for docs indexing | `wasm_host.rs#L742-L760`; `extension.wit#L86-L90` |
| UI, panels, webviews, telemetry, tests | None in the extension API | (absence across `extension.wit`) |

### 4.4 Security model

- **Two-level capability check.**
  - For `process:exec`, the manifest must list the command (`ExtensionManifest::allow_exec`, `ZED:crates/extension/src/extension_manifest.rs#L168-L187`) **and** the user's `granted_extension_capabilities` must allow it (`ZED:crates/extension_host/src/capability_granter.rs#L23-L48`).
  - `download_file` and `npm:install` are checked against the user grants only (`capability_granter.rs#L50-L80`).
- **Defaults grant everything.** `process:exec *`, `download_file *` and `npm:install *` (`ZED:assets/settings/default.json#L2413-L2417`). Users can narrow grants, for example to `host = "github.com"`, or empty them (`ZED:docs/src/extensions/capabilities.md`).
- **The real exposure** is the spawned native processes (LSP, DAP, MCP), which run with the user's permissions, and the ungated HTTP fetch. WASM itself only reaches its work dir.
- **Policy instead of enforcement.** Extensions must "not read or modify anything outside the environment Zed designates" (`ZED:docs/src/extensions/publishing/prerequisites.md`).
- **No signatures found.** The client fetches archives from Zed's API (`/extensions/{id}/download`) over HTTPS. At this pin I found no signature or hash verification in `extension_host.rs` (`#L984`, `#L1029`). Integrity rests on the API host and the reviewed `zed-industries/extensions` repo.

### 4.5 Versioning and compatibility

- **API versions are frozen WIT snapshots.** Each published `zed_extension_api` version has a frozen WIT directory: `wit/since_v0.0.1` … `since_v0.8.0` (tree at the pin). The host keeps one binding module per snapshot (`ZED:crates/extension_host/src/wasm_host/wit.rs#L1-L24`) and dispatches on the extension's declared version, newest first (`wit.rs#L93-L200`).
  - Old extensions keep working because the host still implements their WIT.
  - New features arrive only in a new snapshot.
- **The version is embedded in the binary.** Each extension's WASM carries a `zed:api-version` custom section, which the host reads before instantiating (`wasm_host.rs#L825-L846`).
- **Staged release.** `wasm_api_version_range` exposes the newest snapshot only to Dev/Nightly builds; Stable/Preview stop at v0.6.0. `authorize_access_to_unreleased_wasm_api_version` refuses unreleased versions elsewhere (`wit.rs#L60-L90`). The crate is 0.8.0 at the pin (`ZED:crates/extension_api/Cargo.toml#L3`), unreleased to Stable.
- **Server-side filtering.** Update and listing queries send `min/max_schema_version` and `min/max_wasm_api_version`, so the registry offers only extensions this Zed can run (`extension_host.rs#L668`, `#L698-L711`).
- **Compatibility table.** For example, Zed 0.192 runs API 0.0.1–0.6.0, and "Extensions created using newer versions … won't be compatible with older versions of Zed" (`ZED:crates/extension_api/README.md#L60-L77`).
- **Batched breaks.** Breaking changes are collected in `PENDING_CHANGES.md` so they ship together (`ZED:crates/extension_api/PENDING_CHANGES.md`).
- **Additive manifest fields.** On `opt_in_languages`: "Versions of Zed that do not support `opt_in_languages` ignore it and start the language server for all of its `languages`" (`ZED:docs/src/extensions/languages.md`). Older hosts tolerate unknown keys, and the feature degrades gracefully.

### 4.6 Distribution

- **Registry.** A PR to `zed-industries/extensions` adds a git submodule plus an `extensions.toml` entry (version must match), and updates are the same kind of PR. The rules: one extension per PR, at most 3 open PRs, and closure after 3 weeks without an author reply. Zed CI builds and packages the extension (`ZED:docs/src/extensions/publishing/publishing-guide.md`; `updating-and-maintenance.md`).
- **Licence.** An accepted licence has been required since 2025-10-01, "so we can distribute the resulting binary produced from your extension code" (`publishing/license-requirements.md`).
- **Review is heavy.** "Up to one or two months". One extension per use case; abandoned extensions can be forked into `zed-extensions` (`publishing/faq.md`).
- **Auto-update.** Per extension via `auto_update_extensions`; `auto_install_extensions` exists too (`ZED:crates/extension_host/src/extension_settings.rs#L16-L19`; `extension_host.rs#L698`).

### 4.7 Developer experience

- **Dev extensions.** "Install Dev Extension" picks a folder; Zed compiles it (cargo → wasm, plus grammars with wasi-sdk) and marks the upstream copy "Overridden by dev extension". There is a rebuild action (`ZED:docs/src/extensions/developing-extensions.md`; `extension_host.rs#L1119`, `#L1220`).
- **Debugging.** Print-style only: stdout and stderr are forwarded and visible with `zed --foreground`, and Zed.log shows errors (`developing-extensions.md`). There is no step debugger.
- **Types.** The `zed_extension_api` crate on docs.rs. There is no test harness for extensions in the docs.

---

## 5. Side by side

| | VS Code | Zed | HikariSub today |
| --- | --- | --- | --- |
| Code runtime | Node/JS in a shared extension host (UtilityProcess); web worker; remote | WASM components in-process (wasmtime, WASI p2) | LuaJIT, one helper process per plugin |
| Code required? | Always (`main`/`browser`), except pure-contribution packs | Only for LSP, MCP and DAP | Always (`main` required) |
| Activation | Activation events; implicit for contributions | None; host calls exports on demand | Started when HikariSub starts or the plugin is turned on |
| Crash blast radius | All extensions in that host | One call errors; external servers separate | One plugin's helper (QML panels: the whole app) |
| Time / memory caps | None documented | Epoch yield, no kill | 10 s per call unless a job; 256 MB |
| FS access | Full | Own work dir only (WASI) | Handles only (restricted); full (unrestricted) |
| Network | Full | Ungated `fetch`; gated `download_file` | Host-made HTTPS to manifest hosts |
| Spawn processes | Full | `process:exec` (manifest + user grant); LSP/DAP/MCP spawned by Zed | None (git run by host, read-only) |
| OS sandbox | None | None (WASM boundary only) | Landlock/seccomp, AppContainer for restricted plugins |
| UI | Menus, views, webviews, status bar, custom editors | None | Commands (Automation submenu), widget-tree panels, dialogs, Grid marks, QML panels (full trust) |
| Themes / icons | `themes`, `iconThemes`, `productIconThemes` | `themes`, `icon_themes` | None |
| Versioning | `engines.vscode` app semver; proposed API | Frozen WIT per crate version; custom-section tag; channel-gated | `api` integer (frozen at 1); `since` per operation |
| Signing | Marketplace-signed, verified at install | None found (HTTPS) | Author minisign + signed index + pinned keys |
| Kill switch | Block list, auto-uninstall | Registry removal | Revocation in signed index, auto-disable |
| Dev loop | F5 host, reload window, debugger, test-cli | Dev extension, rebuild, `--foreground` logs | Install from file; guide + test |

---

## 6. Growing an API without bumping its version

Owner constraint: HikariSub's API version and plugin API version stay **1** and freeze at 0.1.0 stable. The mechanisms below are seen in primary sources:

| Mechanism | Where it comes from | What HikariSub would need |
| --- | --- | --- |
| **Capability flags in a handshake.** Features are advertised, missing means unsupported, and unknown flags are ignored. | LSP: "Features in the spec are kept compatible using so called capability flags"; "A missing property should be interpreted as an absence of the capability"; servers "should ignore" unknown client capabilities ([LSP 3.17](https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/)). MCP: both sides "MUST … only use capabilities that were successfully negotiated" ([MCP lifecycle](https://modelcontextprotocol.io/specification/2025-06-18/basic/lifecycle)). | A capability query: an `api.capabilities` (or `api.operations`) read op, or `hikari.api.has("marks.set")` in Lua, answered from the registry, which already knows every operation's name and `since` (HK `docs/api.md` §Reference). |
| **Ignorable messages and fields.** | LSP: `$/` notifications may be ignored; unknown requests return `MethodNotFound` (LSP 3.17). Zed: older hosts ignore `opt_in_languages` (`ZED:docs/src/extensions/languages.md`). | Relax the manifest schema to tolerate unknown keys under `contributes` and `permissions` (warn, don't refuse), **or** add an explicit "optional contributions" rule. Today `additionalProperties: false` everywhere (HK `docs/schemas/hikari-plugin.schema.json`) makes every new contribution point a hard break for older apps. **Permissions must stay strict**: an unknown permission must refuse, or the plugin must be shown as "needs a newer HikariSub", never silently granted or dropped. |
| **Gate by app version, not API number.** | VS Code `engines.vscode` semver range ([publishing](https://code.visualstudio.com/api/working-with-extensions/publishing-extension)); Zed server-side `min/max_wasm_api_version` filtering (`ZED:crates/extension_host/src/extension_host.rs#L698-L711`). | A manifest field such as `"hikarisub": ">=0.2.0"`, checked at install and by the registry. The index already maps API → first HikariSub version (`apiVersions`, HK `docs/schemas/marketplace-index.schema.json`; used in `src/app/plugin_marketplace.cpp:520`). With `api` frozen, that map collapses to one entry, so the gate must move to app versions. |
| **`since` = app version.** | VS Code ships API by app release; HikariSub records `since` per operation (HK `docs/api.md` §Versioning). | Keep `since`, but its value becomes the first **HikariSub version**, not an API number. Same for events, widget kinds and contribution points. Generate it into `api.json` and `api-reference.md`. |
| **Proposed / unstable namespace.** | VS Code `enabledApiProposals`: Insiders only, not publishable ([proposed API](https://code.visualstudio.com/api/advanced-topics/using-proposed-api)); Zed exposes the newest WIT only to Dev/Nightly (`ZED:crates/extension_host/src/wasm_host/wit.rs#L60-L90`). | An `experimental` opt-in (manifest list of proposal names) honoured only on beta/dev builds and refused by registry CI. New surfaces (LSP, themes) can then settle before they join the frozen set. |
| **Frozen per-version interface files, multiple supported at once.** | Zed `wit/since_vX` + per-version host bindings (`wit.rs#L93-L200`). | Already partly present: `tests/api/reference-v1.json` freezes v1 (HK `docs/api.md` §Versioning). Keep it as the "never break" reference and add append-only snapshots per app release. |
| **Batch breaking changes.** | Zed `PENDING_CHANGES.md` | A `docs/api-pending-breaks.md` list for a hypothetical API 2, so nothing breaks piecemeal. |

**Conflict to raise.** HK `docs/api.md` §Versioning ("Every release that grows the API bumps it") and ADR 0022 §The Hikari API ("every release that grows the API bumps it") contradict the owner's freeze-at-1 constraint. The memory notes "API version stays 1" record only a pre-release freeze. A decision plus an ADR amendment is needed.

---

## 7. Mapping to HikariSub

Each item: **Today** → **VS Code** → **Zed** → **Options** (with trade-offs) → **Seam**.

### 7.1 Language servers (owner-named)

- **Today.** No LSP client. Spelling is native Hunspell behind `SpellingBackend` (`src/application/include/hikari/application/spell_checker.h`). Diagnostics come from native checks (QC notes, Karaoke check). A plugin can mark Grid rows (`hikari.marks.*`) but cannot underline text in the Line editor.
- **VS Code.** The extension bundles the server and starts it via `vscode-languageclient` (stdio/ipc/pipe/socket); the client maps LSP features to the editor (`VLC:client/src/node/main.ts#L30-L151`).
- **Zed.** The extension only returns `{command,args,env}`, after finding or downloading the binary. Zed spawns the server and owns the LSP client. Bundling servers is forbidden by policy (`extension.wit#L98`; publishing prerequisites).
- **What LSP means for a subtitle editor.** The "document" is the Line text, not a code file. Useful servers are prose and grammar ones, plus custom ones for ASS override tags, glossary or terminology checks, and translation memory. That covers `publishDiagnostics`, completion, hover and code actions over a virtual text document per Line or per Document (inference; no primary source covers subtitle LSP).
- **Options.**
  - **A. Host-owned LSP client, plugin declares the server (Zed model).** `contributes.languageServers: [{id, documents: "lines"|"document", command, args, download}]`. Qt spawns the server in a `hikari_sandbox` profile (no network unless declared; read-only package and work dir), maps a Document to a virtual text document, and shows diagnostics, completions and code actions natively.
    - Pros: the strongest UX; reuses ADR 0029's broker pattern.
    - Cons: real C++ work (the LSP client, the Line↔text position mapping); external binaries must be signed or pinned; Windows AppContainer may not run Node/Java servers (ADR 0029 notes Codex/ACP runtimes "can't run in an AppContainer").
  - **B. Plugin-side "providers" in Lua.** New contribution kinds (`diagnostics`, `completions`, `codeActions`) answered by the plugin's helper through events, and the host renders them.
    - Pros: no external processes; fits the restricted tier.
    - Cons: not LSP, so there is no reuse of existing servers.
  - **C. A + B.** Providers as the native seam; an LSP adapter is just one built-in provider kind fed by a declared server.
    - Most flexible; most work.
- **Seam.** The Line editor and Grid decoration (marks already exist); `SpellingBackend`; the ADR 0029 `AgentBroker`-style process start; the Hikari API events `document.changed` and `selection.changed`.

### 7.2 Themes (owner-named)

- **Today.** Four built-in themes plus seven accents per mode; roles in `src/ui/theme.h`; "no per-colour editing" chosen after MuseScore (HK `docs/ux/visual-language.md`). Plugins can't contribute themes.
- **VS Code.** `contributes.themes` `{label, uiTheme, path}` pointing to a JSON colour theme; no code.
- **Zed.** `themes/*.json` against a published, versioned JSON Schema (`themes/v0.2.0.json`) with `appearance` and `style` colours; Theme Builder; theme-only extensions required by policy.
- **Options.**
  - **A. Declarative `contributes.themes` mapping the existing role set** (`panel`, `accent`, `text`, `line`, …) plus content colours (Grid, audio display), `appearance: light|dark`, a published schema, and a contrast check at install and in registry CI.
    - Pros: no code runs; works for sideloads and restricted plugins.
    - Cons: reverses the "no per-colour editing" decision; high-contrast themes must stay built-in or be validated.
  - **B. Accent packs only.** Extra accent presets, keeping MuseScore's model.
    - Minimal risk; small user value.
  - **C. A, plus a "theme-only package" type** (Zed's policy: theme extensions contain only themes) that skips the helper process entirely.
- **Seam.** `src/ui/theme.h` role resolution; `appearance.theme` setting; the plugin manager; registry CI.

### 7.3 Icon themes

- **Today.** `IconTheme` (`src/ui/icon_theme.h`) with named icon roles; plugins choose among HikariSub's icons by name only (HK `docs/plugins.md` §Widgets).
- **VS Code.** `iconThemes` (file icons) and `productIconThemes` (UI glyphs).
- **Zed.** `icon_themes` mapping file and folder kinds to bundled SVGs.
- **Options.**
  - **A.** Product icon themes: an SVG per role, sanitised by the same rules already used for package SVG icons (HK `docs/plugins.md` §Media and README).
  - **B.** Let plugins ship **their own** icons for their commands and panels only.
  - **C.** Both.
  - Trade-off: the IC1 glyph grid and the "icon-only tool strips" polish rule (memory `ui-professional-polish`) need a review gate.
- **Seam.** `IconTheme` / `IconImageProvider`.

### 7.4 More tools: commands, menus, keybindings, toolbars, status bar (owner-named)

- **Today.** Commands only, in the Automation › <plugin> submenu; user-bound shortcuts; panels; dialogs; Grid marks. "Plugin first" excludes the video surface (HK ADR 0022).
- **VS Code.** `menus` (many locations with `when` clauses), `submenus`, `keybindings` with defaults, `viewsContainers`, `views`, `viewsWelcome`, `walkthroughs`; `createStatusBarItem`.
- **Zed.** None.
- **Options.**
  - **A. Declarative placements.** `contributes.menus: [{command, location: "grid.context"|"subtitles"|"tools"|"toolbar", when}]`, `contributes.keybindings` (defaults the user can override), and `contributes.statusItems` (text and icon, updated through the API).
    - Pros: big perceived capability.
    - Cons: needs a small `when`-clause language, and touches menu QML (memory: plain `MenuItem` in a `ShellMenu` fails `everyMenuIsAShellMenu`).
  - **B. Fixed slots only.** Grid context menu, Tools menu, status bar; no `when` clauses (the host passes enabled state via an API call).
  - **C.** Also allow **video-surface overlays** read-only (a draw list over the video, like `canvas`). This reopens ADR 0022's "stays native" line.
- **Seam.** `commands.list`/`commands.run`; the menu QML (`ShellMenu`); the hotkeys file; the status bar; the `canvas` widget's draw list.

### 7.5 Background processes and long-running work (owner-named)

- **Today.** Helpers are persistent, but every event or command has 10 s unless a **job** begins, and only a command or a click can start a job. No timers, no process spawning, no long-lived sockets (HK `docs/plugins.md` §Long jobs, §Running a plugin).
- **VS Code.** Unrestricted Node: timers, `child_process`, servers, all in the shared host.
- **Zed.** No background WASM; long-lived work is an external process Zed spawns (LSP, DAP, MCP) from a returned command. `process:exec` is for short commands with collected output (`process.wit#L28`).
- **Options.**
  - **A. "Service" mode in the helper.** A manifest flag `background: true` lifts the 10 s limit for event handlers that yield, and adds `hikari.timer.after/every`. The status bar shows that the plugin is running, and there is a per-plugin CPU/memory budget.
    - Pros: no new process types.
    - Cons: a runaway Lua loop needs a cap (an instruction-count hook).
  - **B. Declared external executables** (`contributes.processes: [{id, command, platforms, sha256}]`), started by Qt in a `hikari_sandbox` profile and talking JSON-RPC over stdio to the plugin or host.
    - This is the ADR 0029 broker generalised.
    - Pros: any language; enables 7.1 and 7.9.
    - Cons: shipping native binaries per platform in a `.hikariplugin` (64 MB cap); Windows AppContainer runtime limits; registry needs per-platform packages (VS Code has `--target`).
  - **C. Full trust only.** Unrestricted plugins may already spawn via FFI; document and support it. Cheapest; no sandbox.
- **Seam.** `hikari-plugin-helper` and `plugin_protocol.h` (`Job`); `hikari_sandbox`; `AgentBroker` (`src/backends/ai_agent.cpp`, `ai_mcp_clients.*`); the job card UI.

### 7.6 Snippets and templates

- **Today.** Built-in karaoke presets (`src/application/karaoke-presets/`), the node-effect graph library in the user folder (ADR 0023), Styles in documents. No plugin contribution for any of these.
- **VS Code.** `snippets` (per language).
- **Zed.** `snippets` (per language, or global `snippets.json`).
- **Options.**
  - **A.** Declarative content packs: `contributes.stylePresets`, `contributes.karaokePresets`, `contributes.graphs` (node-effect graphs), `contributes.tagSnippets` (ASS override tag snippets for the Line editor).
  - **B.** Only graphs and styles first.
  - All are data, so no code runs and they are safe in the restricted tier.
- **Seam.** The karaoke presets loader; the graph library; Styles import; Line editor completion.

### 7.7 Panels and webviews

- **Today.** The widget tree (restricted) and QML panels (full trust, in-process, can crash the app) (HK `docs/plugins.md` §QML panels). The plugins research note §4.4 already weighed webviews (HK `docs/research/hikari-plugins.md`).
- **VS Code.** Webviews, explicitly "sparingly", with CSP and memory cost.
- **Zed.** None.
- **Options.**
  - **A.** Keep it as is and grow widget kinds additively (each with `since`).
  - **B.** QML panels in a **separate process** (a QML runtime in the helper, sharing textures or a remote window) to remove the crash risk. Large work.
  - **C.** A webview via QtWebEngine in a sandboxed process. Heavy dependency; Chromium on both platforms; conflicts with the "professional polish" look.
- **Seam.** The `Ui` service in `plugin_protocol.h`; `Hikari.Plugin` QML module.

### 7.8 Settings pages

- **Today.** `contributes.settings` (5 types) as a form in the plugin manager; Project overrides (HK `docs/plugins.md`).
- **VS Code.** `configuration` with scopes, Markdown descriptions, arrays and objects, `order`, `restrictedConfigurations` under trust.
- **Zed.** `get-settings`; LSP settings schemas exported by the extension.
- **Options.**
  - **A.** Additive fields: `description`, `group`, `order`, `array<string>`, `secret` (keychain-backed, the ADR 0029 key rule), `scope: user|project`.
  - **B.** Also surface plugin settings inside Settings (not just the plugin manager).
- **Seam.** `plugins.json` settings; `settings.*` operations; QtKeychain / the Windows DPAPI folder for secrets.

### 7.9 AI tools and MCP

- **Today.** The MCP client (user-added servers, unsandboxed, run by Qt), HikariSub's own MCP server, and AI tools over the Hikari API. Plugins are callable by AI only via `plugins.run` (HK `docs/ai-mcp-clients.md`; `docs/api.md`).
- **VS Code.** `languageModelTools`, `chat*` contributions, `McpServerDefinitionProvider`.
- **Zed.** `context_servers` extensions, now **deprecated** for the official MCP registry; slash commands removed.
- **Options.**
  - **A.** `contributes.aiTools`: a plugin command exposed as a typed AI tool, with a JSON Schema for its arguments and a read/write mark. It goes through the same approval policy.
  - **B.** `contributes.mcpServers`: a plugin ships an MCP server definition that lands in Settings › AI, still asking the user.
    - Zed's deprecation suggests a registry is better than per-extension wrappers.
  - **C.** Both; A is cheap and safe.
- **Seam.** The sidecar tool groups (`search_tools`); `AgentThreads::gateMcp`; the `plugins.*` operations.

### 7.10 File formats: importers and exporters

- **Today.** Closed enums for subtitle, export and lyrics formats (`core/document.h:190`, `core/export.h:14`, `core/lyrics_import.h:49`).
- **VS Code.** `customEditors`, `FileSystemProvider`.
- **Zed.** No equivalent.
- **Options.**
  - **A.** `contributes.formats: [{id, extensions, import: true, export: true}]`, handled by the helper: bytes in, Hikari Lines out (and the reverse), via a new `formats.*` event pair.
    - Pros: high value (srv3-likes, studio formats); the restricted tier is fine because the host passes bytes.
    - Cons: core enums must become open registries; fidelity tests.
  - **B.** Export only first.
- **Seam.** The Open/Save dialogs; `core::export`; the `document.apply` path.

### 7.11 Video and audio providers, keyframe generators

- **Today.** Media stays native (ADR 0022 "stays native"; `docs/backends.md` ports `IndexedSource`, `GeneralPlayer`). Keyframe generators already work as plugins via `video.frames` + `video.set_keyframes` (HK `docs/plugins.md` §Macros, keyframes and frames; official keyframe generator).
- **VS Code / Zed.** No equivalent.
- **Options.**
  - **A.** Keep media native, but add analysis seams: `audio.samples` (PCM in chunks) and scene or loudness helpers.
  - **B.** Declared decoder executables via 7.5B. Conflicts with the FFMS2 isolation design; not advised.
- **Seam.** `video.frames`, `audio.peaks`, the backends ports.

### 7.12 Spell and grammar providers, dictionaries

- **Today.** Hunspell dictionaries from folders behind `SpellingBackend` (`spell_checker.h`); the Spellchecker panel.
- **VS Code / Zed.** Done through LSP servers (no primary source on specific spell extensions consulted).
- **Options.**
  - **A.** `contributes.dictionaries` (`.aff`/`.dic` data files). Declarative, safe.
  - **B.** `contributes.spellProviders` answered by the helper (`spell(word)`, `suggest`). It must be fast and batched; the 10 s limit is irrelevant, but latency matters.
  - **C.** Through 7.1 (an LSP grammar server).
- **Seam.** `SpellChecker` folders list; `SpellingBackendLoader`.

### 7.13 Tasks, checks and QC

- **Today.** Native QC notes; plugins can add marks.
- **VS Code.** `taskDefinitions`, `problemMatchers`.
- **Zed.** Tasks feed debug locators.
- **Options.**
  - **A.** `contributes.checks`: a named check that returns `{line, severity, message, fix?}` and shows in a shared Problems/QC panel with quick fixes as `document.apply` edits.
  - Overlaps 7.1 B; pick one diagnostics model.
- **Seam.** The QC notes panel; `marks.*`.

### 7.14 Localisation of plugins and the app

- **Today.** Per-plugin `i18n/<locale>.json` (HK `docs/plugins.md` §The `hikari` global). Neither VS Code nor Zed docs consulted here cover language packs beyond this; out of scope.

### 7.15 Runtime question

| Runtime | Security / sandbox | Windows / Linux | Packaging and signing | Fit |
| --- | --- | --- | --- | --- |
| **Lua only (today)** | Proven: restricted LuaJIT + `hikari_sandbox` (HK `docs/plugins.md`) | Works on both; the AppContainer already runs the helper | Unchanged | Keeps one runtime. Weak for CPU-heavy work and for reusing ecosystem servers. |
| **+ WASM components (Zed model)** | Strong by construction: WASI preopens only (`ZED:wasm_host.rs#L742-L760`); capability-gated imports (`capability_granter.rs`). Needs a fuel/epoch cap (Zed only yields). | Portable bytecode, so one package for all platforms | `.wasm` in the zip, signed as today. Authors need a Rust or other WASM toolchain (Zed requires `wasm32-wasip2`). | Good for compute-heavy, safe plugins (formats, analysis). Adds wasmtime as a C++ dependency (C API) and a second binding surface: WIT vs the Hikari API's JSON Schema. Could run inside the existing helper process. |
| **+ JS/TS host (VS Code model, reuse Bun)** | Bun already runs sandboxed with no network (ADR 0029). A per-plugin Bun process is heavier than LuaJIT. Sharing one host breaks per-plugin isolation. | Bun in an AppContainer works for the sidecar (ADR 0029), but Node-style runtimes failed for Codex/ACP (same ADR) | npm dependencies; bundle size; author keys unchanged | Biggest author pool and TypeScript types from `api.json` (the sidecar already builds types from it, HK `docs/api.md` §Reference). Duplicates the Lua story. |
| **+ Out-of-process executables over a protocol (LSP/MCP model)** | Only as strong as `hikari_sandbox` per process. ADR 0029 shows non-trivial runtimes need low integrity on Windows, not an AppContainer. | Native binaries per OS and arch | Per-platform packages (VS Code `--target`) or downloads (Zed); the signature must cover each binary, and downloads need a pinned SHA-256 | Required for LSP and MCP reuse. Best as a *capability* any runtime can declare (7.5B), not as a plugin runtime. |

Combination options to put to the owner: keep Lua as the plugin runtime; add **declarative-only packages** (themes, icons, presets, dictionaries) that run no code; add **declared executables** for LSP-style servers; defer WASM and JS until a concrete plugin needs them.

---

## 8. Open decisions for the owner

1. **Versioning model.** Given API = 1 frozen at 0.1.0, do new operations, events, widgets and contribution points still ship after 0.1.0 under the same number, gated by HikariSub version? This needs ADR 0022 and `docs/api.md` §Versioning amended.
2. **Compatibility gate.** Add a manifest `hikarisub` version range (like `engines.vscode`), checked at install and in the registry, replacing `apiVersions` as the marketplace filter?
3. **Unknown manifest keys.** Tolerate unknown `contributes` keys with a warning (Zed `opt_in_languages` style) while unknown `permissions` still refuse? Or keep strict and refuse with "needs a newer HikariSub"?
4. **Capability detection.** Add an `api.capabilities`/`hikari.api.has()` read op, and record `since` as the HikariSub version?
5. **Experimental API.** Add a proposed/experimental opt-in honoured only on beta/dev builds and refused by registry CI?
6. **Declarative-only packages.** Allow packages with no `main` (themes, icons, presets, dictionaries) that never start a helper?
7. **Themes.** Allow plugin colour themes over the role set, reversing "no per-colour editing", or accent packs only? Are high-contrast themes protected?
8. **Icon themes.** Product icon themes, per-plugin icons for their own commands, both, or neither?
9. **UI placement.** Which slots plugins may contribute to (Grid context menu, Tools, toolbar, status bar, default keybindings); whether `when` clauses exist; and whether the video surface stays off-limits?
10. **Background work.** Lift the 10 s limit for a declared `background` mode with timers and a CPU/memory budget, or keep jobs-only?
11. **External executables.** May a plugin declare native processes started by HikariSub in `hikari_sandbox`? Bundled per platform, or downloaded with pinned SHA-256? Restricted tier or full trust only?
12. **Language servers.** A host-owned LSP client for Line text (diagnostics, completion, code actions), Lua-side providers, or both? Which first use case (grammar, ASS tags, glossary)?
13. **Diagnostics model.** One shared Problems/QC model for LSP, plugin checks and native QC notes?
14. **AI.** `contributes.aiTools` (plugin commands as typed AI tools)? `contributes.mcpServers`, or leave MCP to a registry as Zed now does?
15. **Formats.** Open the subtitle import/export enums to plugin-provided formats? Export only first?
16. **Content packs.** Which first: Styles, karaoke presets, node graphs, ASS tag snippets, Hunspell dictionaries?
17. **Settings.** Additive setting types (arrays, secrets in the keychain, scopes), and plugin settings inside Settings itself?
18. **Panels.** Keep QML panels in-process for full trust, move them out of process, or add webviews (no)?
19. **Second runtime.** None for now, WASM components, or a JS/TS host on Bun? Decide after 11 and 12 show whether one is needed.
20. **Dev loop.** "Install dev plugin" from a folder with live reload, a test harness (scripted `.jsonl` calls already exist in `tests/api/scripts/`), and generated type stubs (LuaLS annotations from `api.json`)?
