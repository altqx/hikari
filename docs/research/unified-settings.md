# One Settings for everything: VS Code, Zed and HikariSub

Research note, 2026-11-17. The owner answered the decisions at the end on #1209 (Q638–Q649, yes to all); the result is [docs/ux/settings.md](../ux/settings.md).

The question: how do VS Code and Zed build one Settings experience, and how could HikariSub merge its own settings, Project settings and per-plugin settings into one page "and more"? Card [#1230](https://github.com/altqx/hikari/issues/1230) already plans per-plugin pages under Settings › Plugins. Card [#1236](https://github.com/altqx/hikari/issues/1236) plans the command palette.

## Source pins

| Tag | Source | Pin |
| --- | --- | --- |
| **VS** | microsoft/vscode, shallow clone | `04b33e0a3a8eff7ab51b6c847f65d9fc8ff07a6b` (2026-11-17) |
| **Z** | zed-industries/zed, shallow clone | `482e76524bd33d8cbdfc352f1fef3eefbf2fad92` (2026-11-16) |
| **H** | altqx/hikari `origin/main` | `aac6e73ddd2e9f96b22bbe2e946e05496da25751` (2026-11-17) |
| **PR** | altqx/hikari PR #1235 head (ADR 0034, docs only) | `0d2b3307173d339b14418f42ae39d4959e945dd8` |

Citations are `TAG path:line`. VS paths are under `src/vs/` unless they start with `build/`. Z paths are under `crates/` unless they start with `assets/`.

---

## VS Code

### The model

- **Layers.** Values merge as default < application < user < workspace < memory. Folder settings merge on top for a resource (VS `platform/configuration/common/configurationModels.ts:1078-1097`). Policy values are written over the result last (`:1063-1072`).
- **Files.** User settings live in `settings.json`. Workspace settings live in `.vscode/settings.json` or the `.code-workspace` file. The UI editor and the JSON editor read and write the same files (VS `workbench/contrib/preferences/browser/settingsEditor2.ts:1042-1060`).

### The Settings editor

- **Search and filters.** One box searches every setting. Tag filters are parsed out of the query first (VS `workbench/contrib/preferences/browser/settingsTreeModels.ts:1262-1323`). The tags are (VS `workbench/contrib/preferences/common/preferences.ts:101-112`):
  - `@modified`: only settings set in the current scope (`settingsEditor2.ts:1348-1360`, `settingsTreeModels.ts:411`);
  - `@ext:<id>`: settings of one extension (`settingsTreeModels.ts:499-508`);
  - `@id:<key>`: one setting by its key (`:550-570`);
  - `@tag:<tag>`: settings with that tag (`:434-464`);
  - `@feature:<toc id>`: one branch of the contents tree (`:511-547`);
  - `@lang:<language>`: show and edit the language override (`:573-590`);
  - plus `@hasPolicy`, `@stable` and others.
- **Suggestions.** The box offers a fixed list of tags (`settingsEditor2.ts:135-150`). It also has an AI results toggle (`settingsEditor2.ts:943`, setting `workbench.settings.showAISearchToggle` at VS `workbench/browser/workbench.contribution.ts:768`).
- **Contents tree.** A tree on the left (`settingsEditor2.ts:1086-1189`, `tocTree.ts`). Its layout is a fixed table: Commonly Used, Text Editor, Workbench, Window, Features, Application, Security (`settingsLayout.ts:64-433`). Extensions get one node under it (`settingsLayout.ts:328`). The tree follows the scroll position and filters with the search.
- **Scope tabs.** User, Remote, Workspace and Folder are tabs over the list (VS `workbench/contrib/preferences/browser/preferencesWidgets.ts:213-296`, created at `settingsEditor2.ts:892-894`). The Workspace tab carries the workspace name (`preferencesWidgets.ts:255`).
- **Row indicators** (VS `workbench/contrib/preferences/browser/settingsEditorSettingIndicators.ts`):
  - "Modified in: <scope>" and "Also modified in", with a link to that scope (`:404-405`);
  - "Modified elsewhere" for language overrides (`:435-443`);
  - "Managed by your organization" when a policy sets it (`:356-367`);
  - "Applies to all profiles" (`:388-390`);
  - "Requires workspace trust" (`:118-125`);
  - "Not synced" (`:153-157`);
  - "Default value changed" (`:172-174`);
  - "Preview" and "Experimental" from tags (`:307-312`).
- **The gear menu** on each row (VS `workbench/contrib/preferences/browser/settingsTree.ts`):
  - Reset Setting (`:2239`);
  - Copy Setting ID (`:2753`);
  - Copy Setting as JSON (`:2772`);
  - Copy Setting as URL (`:2792-2807`);
  - Sync This Setting (`:2815`);
  - Apply Setting to all Profiles (`:2859`).
- **Links between settings.** `#editor.fontSize#` in a markdown description becomes a link that jumps to that setting (`settingsTree.ts:1100-1106`, `:2426-2427`).
- **Sync.** A header control shows the sync state and a Turn on button (`settingsEditor2.ts:903-905`, `:2297-2315`).
- **Commit.** There is no Apply button. Each change is written at once, through the `onDidChangeSetting` event (`settingsTree.ts:2239-2249`).

### The deep link

- **The scheme.** `<product>://settings/<key>`, for example `vscode://settings/files.autoSave`. The handler checks the `settings` authority and opens the editor at that key (VS `workbench/services/preferences/browser/preferencesService.ts:674-700`, registered at `:108`, authority at VS `workbench/services/preferences/common/preferences.ts:340`).
- **It waits for extensions.** When the key isn't known yet, it waits for extensions to register, then looks again (`preferencesService.ts:694-698`).
- **Copy as URL** builds the same link from a row (`settingsTree.ts:2805-2806`).
- **Inside the app**, the open call takes `target`, `folderUri`, `query`, `revealSetting` and `focusSearch` (VS `workbench/services/preferences/common/preferences.ts:180-191`).

### UI and JSON

- **The default editor** is the setting `workbench.settings.editor`, `ui` or `json` (VS `workbench/browser/workbench.contribution.ts:757-766`, read at `preferencesService.ts:214`).
- **Switching.** The editor's title bar has an Open Settings (JSON) button (`settings.switchToJSON`, VS `workbench/contrib/preferences/browser/preferences.contribution.ts:67`, `:1437-1443`). It opens the file of the current scope tab (`settingsEditor2.ts:1042-1060`).
- **Separate commands** open each form: Open Settings (UI) and Open User Settings (JSON) (`preferences.contribution.ts:260-278`).

### How extensions contribute settings

- **The manifest key.** `contributes.configuration` is one object or an array of objects (VS `workbench/api/common/configurationExtensionPoint.ts:244-258`). Each object is one category:
  - `title`: the category heading; a title equal to the extension's name folds into the extension's own heading (`:31-33`);
  - `order`: the category's place (`:35-37`);
  - `properties`: the settings, keyed by their full id (`:39-46`).
- **Per setting** (`:53-160`):
  - `scope`: `application`, `machine`, `window`, `resource`, `language-overridable` or `machine-overridable` (`:55-67`);
  - `enumDescriptions`, `markdownEnumDescriptions` and `enumItemLabels` (`:69-89`);
  - `markdownDescription` (`:90-93`);
  - `deprecationMessage`, `markdownDeprecationMessage` and their severity (`:94-110`);
  - `editPresentation`: single-line or multi-line text (`:112-120`);
  - `order` inside the category (`:122-125`);
  - `ignoreSync` (`:126-129`);
  - `keywords`: hidden words that help search (`:130-136`);
  - `tags`: `accessibility`, `advanced`, `experimental`, `telemetry`, `usesOnlineServices` and free tags, found with `@tag:` (`:137-158`).
- **Strictness.** A bad property is dropped with a warning. A key that another extension already registered is dropped too (`:302-317`).
- **Policies for extensions** come from the product file, not from the extension (`:296`, `:320-328`).
- **Untrusted workspaces.** `restrictedConfigurations` keeps chosen settings from applying in an untrusted workspace (`:289`).
- **Grouping.** Extension settings sit under one node per extension. One category is flattened. Several categories become children, sorted by `order` (VS `workbench/contrib/preferences/browser/settingsTree.ts:480-530`).
- **Localisation.** Manifest strings may be `%key%`, filled from `package.nls.json` (VS `platform/extensionManagement/common/extensionNls.ts:16-36`).

### Keyboard Shortcuts editor

- **A separate editor** (VS `workbench/contrib/preferences/browser/keybindingsEditor.ts:76`), not a Settings page.
- **Columns:** Command, Keybinding, System-wide, When, Source (`:540-562`).
- **Actions:** Record Keys and Sort by Precedence (`:148-152`).
- **Search filters:** `@source:system`, `@source:user`, `@source:extension`, `@command:<id>`, `@ext:<id>` and `@keybinding:` (VS `workbench/services/preferences/browser/keybindingsEditorModel.ts:27-43`, `:120-127`).

### The extension page

- **The Features tab** lists what an extension contributes (VS `workbench/contrib/extensions/browser/extensionEditor.ts:607`).
- **Its Settings table** has ID, Description and Default (`configurationExtensionPoint.ts:486-528`).

### Profiles

- **Named profiles** hold their own settings, keybindings, snippets, tasks, extensions, global state and MCP (VS `platform/userDataProfile/common/userDataProfile.ts:37-45`).
- **Shared settings.** An `application`-scoped setting is shared by all profiles, and the row says so (`settingsEditorSettingIndicators.ts:388-390`).

### Policies and managed settings

- **The policy key.** A setting may declare `policy` (it owns a policy name) or `policyReference` (VS `platform/configuration/common/configurationRegistry.ts:285-301`).
- **The policy shape.** A policy has a name, a category, a minimum version and localization (VS `base/common/policy.ts:68-91`).
- **Sources.** Values come from the OS (registry or plist), from a file, or from `managed-settings.json` (VS `platform/policy/node/nativePolicyService.ts:13`, `platform/policy/common/filePolicyService.ts:28`, `platform/policy/common/fileManagedSettingsService.ts:19-94`).
- **Admin templates.** The build writes ADMX/ADML and macOS profiles from the policy list (VS `build/lib/policies/render.ts:189`, `:295-296`).
- **In the UI,** a row with a policy shows "Managed by your organization" and can't be changed (`settingsEditorSettingIndicators.ts:356-367`).

---

## Zed

### The model

- **Layers.** Values merge as default < extensions < global < user (with its release-channel and OS overrides) < the active settings profile < server < project (Z `settings/src/settings_store.rs:1369-1397`). The precedence order is also written on `SettingsFile` (`settings_store.rs:180-216`).
- **Files.**
  - Defaults are `assets/settings/default.json`.
  - The project file is `.zed/settings.json` (Z `paths/src/paths.rs:497-499`). A subfolder can have its own; the store keys local settings by worktree and path (`settings_store.rs:186`, `:700`).
- **Language overrides.** A `languages` table holds per-language settings (Z `assets/settings/default.json:2470`), with `file_types` beside it (`:2375`).
- **Profiles.** Named settings profiles live in the user file (Z `settings_content/src/settings_content.rs:503-515`; default `{}` at `assets/settings/default.json:3079`). A profile's `base` says whether it starts from the user settings or from the defaults (`settings_store.rs:1383-1395`).
- **Organization overrides.** A field can be overridden by the signed-in organization (Z `settings_ui/src/settings_ui.rs:117-121`, `:192`, `:269-272`).
- **Importing VS Code and Cursor settings** (Z `settings/src/vscode_import.rs:30-48`).

### The Settings window (`settings_ui`)

- **A window of its own.** It has a nav bar of pages:
  - General, Appearance, Keymap, Editor, Languages & Tools, Search & Files, Window & Layout, Panels, Debugger, Terminal, Version Control, Collaboration, AI, Network and Developer (Z `settings_ui/src/page_data.rs:85-9786`).
- **Search.** The window builds a search index: one document per item, plus fuzzy candidates per word (`settings_ui.rs:1027-1038`, `:2403-2425`). Exact hits come first, then fuzzy hits scoring 0.5 or more (`:2364-2384`).
- **Files as tabs.** User, Project (per worktree) and Server (`settings_ui.rs:1732-1780`). Opening can pick the file (`:689-735`).
- **Each row:**
  - "— Modified in <file>" when another file sets it (`settings_ui.rs:1456-1460`, `:1491-1502`);
  - a Reset to Default button (`:1444-1452`, `:215-235`);
  - a Copy Link button that copies `zed://settings/<json path>` (`:1577-1590`).
- **Stable link ids.** Every field has a URL-safe `json_path`, such as `theme.mode`. It exists for links (`settings_ui.rs:123-141`).
- **The deep link.** `zed://settings/<path>` is parsed in Z `zed/src/zed/open_listener.rs:185-187` and Z `zed/src/main.rs:1645-1656`. It becomes the `OpenSettingsAt { path, target }` action (Z `zed_actions/src/lib.rs:145-183`), which searches the index and scrolls to the item (`settings_ui.rs:778-830`). Language paths aren't supported yet (`:792`). An `OpenSettingsPage { page, target }` action opens a whole page (`zed_actions/src/lib.rs:160-170`).
- **JSON.** There is no full toggle. A field that the UI can't edit shows "Edit in settings.json" (`settings_ui.rs:526-545`, `:2960`). Hard sections link to sub-pages or to their own forms (`:62`, `:818-821`).
- **Commit.** Changes are written to the file at once. Project files go through an update queue (`settings_ui.rs:451-453`).

### How Zed extensions contribute settings

- **No settings key.** The extension manifest has no settings key (Z `extension/src/extension_manifest.rs:89-126`).
- **Language defaults.** Extensions add per-language defaults only, merged as the `extension_settings` layer (`settings_store.rs:1167-1176`, fed from Z `languages/src/lib.rs:305-320`).
- **Language servers.** An extension reads the user's `lsp.<server>` settings. It may give JSON Schemas for a server's initialization options and workspace configuration (Z `extension_api/src/extension_api.rs:104-124`).
- **MCP servers.** An extension may return a configuration with installation instructions, a settings schema and default settings (`extension_api.rs:188-196`; Z `extension_api/wit/since_v0.8.0/context-server.wit:3-9`).

### Keymap editor

- **A separate editor**, also reachable from the Keymap settings page (Z `settings_ui/src/page_data.rs:1691-1710`).
- **Columns:** Action, Arguments, Keystrokes, Context, Source (Z `keymap_editor/src/keymap_editor.rs:2152`).
- **Filters:** a Conflicts filter (`:217-227`, `:1649-1651`) and keystroke search by recording keys (`:1556-1571`).
- **Sources:** User, Vim, Base and Default (`:242-244`).

---

## HikariSub today

### Where values live

- **Three user files:** `settings.toml`, `hotkeys.toml` and `state.json` (H `src/ui/settings_files.cpp:657-678`).
  - Before a write, the file on disk is merged back in (`:1041`, `:1061`). There is no live file watch.
- **368 setting definitions** in one table (H `src/application/settings.cpp:62`):
  - 328 are scope `Profile`, 21 `Collection` and 19 `Workspace` (scope enum at H `src/application/include/hikari/application/settings.h:18`).
  - A definition has an id, a type, a default and flags. It has **no label, description or page** (`settings.h:25-35`).
- **Labels live only in QML.** A C++ `OptionsBinding` gives a setting's page, control and bounds (H `src/application/include/hikari/application/options_dialog.h:33-50`). The label and the note exist only in the QML rows.
- **Project values.** A Project may set 25 ids or prefixes (H `src/application/settings.cpp:31-42`). They live in `.hikari/project.toml`, with personal overrides in `project.local.toml` (H `docs/adr/0030-open-a-folder-as-a-project.md:19-23`). The order is Default < User < Project, per key (ADR 0030 `:56-62`).
- **Plugin values** live in `plugins.json` per plugin, with `[plugins."<id>"]` overrides in `project.toml` (H `docs/plugins.md:110`, `:117-129`).
- **The host API** has `settings.list`, `settings.get` and `settings.set` in API 1 (H `docs/api-reference.md:20-26`, `:342-380`).
  - `settings.list` returns `scope` as `profile`, `collections` or `workspace`.
  - It returns no title and no description.
- **A setting catalogue already exists, at docs time.** `--export-docs` walks the live Settings dialog. It writes `settings.json` with each setting's key, page, section, label, hint, control, type and default (H `src/app/docs_export.cpp:186-240`, `:279-305`).
  - It finds rows and sections by their QML property names. That is flagged `MUST KILL` (`:199`).

### Inventory

| Surface | File | Tier | Searchable? |
| --- | --- | --- | --- |
| Settings window, Yours tab: 14 pages in a flat list (General, Appearance, Line editor, Editing, Spelling and grammar, Grid, Video, Audio, Conversion, Script properties, Automation, Shortcuts, AI, Updates and privacy) | H `src/ui/SettingsDialog.qml:50-65`, list `:534-590`, stack `:594-1860` | User (`Profile`) | No |
| Yours / Project tabs, shown only in a Project window | `SettingsDialog.qml:494-511` | — | — |
| "Overridden in this project" note on Yours rows | `SettingsDialog.qml:338-342`, `:360-370`, `:1020-1025` | User, showing Project | No |
| Project tab: 5 pages (Spelling and grammar, Script properties, Translation, Export, AI) | H `src/ui/ProjectSettingsPages.qml:23-29` | Project (`project.toml`, `project.local.toml`) | No |
| Project row state "Uses yours / Set in this project / Set in project.local.toml" and Use yours | `ProjectSettingsPages.qml:102-125`; logic `:53-98` | Project | No |
| Project AI keys | H `src/ui/ProjectAiKeysPage.qml`, used at `ProjectSettingsPages.qml:419-423` | Project (`.hikari/credentials.toml`) | No |
| Explorer's Project settings node | H `src/ui/ExplorerPanel.qml:16`, `:36`; H `src/ui/Main.qml:10217` | Project | — |
| Script properties dialog (Script Info of one file) | H `src/ui/ScriptPropertiesDialog.qml:10-12` | This file (Document) | No |
| Defaults written into Script Info on save | `SettingsDialog.qml:1477-1503` (Yours); `ProjectSettingsPages.qml:343-357` (Project) | User / Project | No |
| Appearance: theme cards, Follow system, accent, high-contrast picks, font, window, AI side bar | H `src/ui/AppearancePage.qml:26-330` | User | No |
| Shortcuts page: filter combo (All, Set, Global, Grid, Line editor, Video, Audio, Automation, Plugin) and search box | `SettingsDialog.qml:1538-1765`; filter `:1550-1560`; search `:1563-1569`; H `src/app/include/hikari/app/hotkeys_controller.h:86-88` | User (`hotkeys.toml`) | **Yes, shortcuts only** |
| AI page: AI features, Providers, Models, Web search, External agents, MCP, Agent, Skills, Inline actions, AI components, Privacy | H `src/ui/SettingsAiPage.qml:1178-2931`; mounted `SettingsDialog.qml:1766` | User (+ keychain) | No |
| Updates and privacy, including "Let plugins connect to the internet" | `SettingsDialog.qml:1768-1858`; plugin network `:1854` | User | No |
| Trusted folders | `SettingsDialog.qml:692-730` | User | No |
| Notices, "Ask again" | `SettingsDialog.qml:640-656` | User | No |
| Tag buttons (own store) | `SettingsDialog.qml:795-915` | User | No |
| Reset page, Reset all (shortcuts and appearance excluded) | `SettingsDialog.qml:164-195`, `:268-302` | User | — |
| OK / Cancel / Apply (changes are staged) | `SettingsDialog.qml:151-157`, `:255-261`, `:303-322` | User and Project | — |
| Plugin manager › details › Settings form (boolean, integer, number, string, choice), **kept at once** | H `src/ui/PluginManagerDialog.qml:1164-1270`; H `docs/plugins.md:141` | User (`plugins.json`) + Project `[plugins."<id>"]` | No (the plugin list has a Filter, `:331`) |
| Plugin manager › Check for plugin updates, Install updates automatically | `PluginManagerDialog.qml:1294-1317` | User settings **outside** the Settings window | No |
| Plugin manager notice "This project turns plugins on or off, or changes their settings." | `PluginManagerDialog.qml:348` | Project | — |
| Legacy settings import and its review | H `src/ui/SettingsImportDialog.qml:12`; review H `src/ui/SettingsImportReview.qml:60-131`; opened from `SettingsDialog.qml:731-770` | User | **Yes**, "Search settings" over import rows (`SettingsImportReview.qml:71`) |
| Openers: `openSettings(page, filter)`, `openProjectSettingsPage(page)`, about 15 callers | `Main.qml:10216-10217`; for example `:1450`, `:4945`, `:5118`, `:9931` | — | — |
| Host API `settings.list/get/set` | H `docs/api-reference.md:342-380` | User | `prefix` only |

**Search today.** Only the Shortcuts page and the legacy import review can be searched. There is no search over settings.

**Deep links today.** `openSettings` opens a page and, for Shortcuts, a filter index (`SettingsDialog.qml:69-110`). Nothing opens one setting. The `hikarisub://` scheme exists and is registered by every installer (H `docs/distribution.md:23`, `:35`). Its only use is `hikarisub://plugin/install?id=<id>` (H `docs/plugins.md:87`).

### What PR #1235 (ADR 0034) and #1230 add

- **New fields.** Every plugin setting may have `description` (500 characters), `group`, `order` (0–9999) and `scope` (`project` by default, or `user`). The new types are `list` and `secret` (PR `docs/plugins.md:839-857`, PR `docs/adr/0034-plugins-grow-into-an-extension-system.md:85-94`).
- **Secrets** go to the system keychain as `plugin/<plugin id>/<setting id>`. They are always `user` (PR `docs/plugins.md:851-855`).
- **Pages in Settings.** "Each plugin with settings gets a page under Settings › Plugins, named after it, with the same form as the plugin manager's. The plugin manager's Settings opens that page." (PR `docs/plugins.md:856`).
- **An event.** `plugin_settings.changed` fires for a change from either place or from a Project (PR `docs/plugins.md:857`, PR `docs/api.md:159`).
- **Choice options are titles.** `options` is a list of `Title` strings, and `default` is one of them (PR `docs/schemas/hikari-plugin.schema.json`, `Setting` at `:786`, choice branch about 95 lines below). The stored value is the visible text. There is no separate value and label, and no manifest localisation.
- **Themes.** Plugin themes become `plugin:<plugin id>/<theme id>` in `appearance.theme`. The Appearance page lists them (PR `docs/plugins.md:567`).
- **Shortcuts.** Plugin default shortcuts lose to the user's and HikariSub's own bindings. Settings › Shortcuts says "taken by <owner>" (PR `docs/plugins.md:628-629`; ADR 0034 `:43`).
- **MCP servers** that plugins contribute appear in Settings › AI (ADR 0034 `:79`).

### Quiet constraints (ADR 0033)

- **Rows lose their icons.** The page area takes the height (H `docs/ux/visual-language.md:461-462`; ADR 0033 `:26`).
- **The accent is reserved** for selection, the default button, checked marks, the editing field's border, the current tab and focus marks. Hover never uses it (`visual-language.md:473`). A "modified" mark therefore can't be accent.
- **Dialogs use the DG1 frame** (`visual-language.md:89`). The Style manager's sidebar-and-editor shape is the nearest model (`:353`).

---

## Gap list

| VS Code / Zed feature | HikariSub today | What it would take |
| --- | --- | --- |
| One search over every setting (VS filters; Z fuzzy index) | None. Shortcuts and the import review only | A runtime settings catalogue: id → page, section, label, note, keywords, control. Labels must move out of QML-only rows, or be gathered the way `docs_export.cpp` does. Then a results view across pages. |
| Filter tags `@modified`, `@ext:`, `@id:`, `@tag:`, `@feature:`, `@lang:` | None | A small query parser in the UI. Suggested facets: `@modified`, `@project`, `@plugin:<id>`, `@id:<id>`. |
| Contents tree with sections and an Extensions node | Flat list of 14 pages; Project pages in a second list | Sections become tree children. A Plugins node with one child per plugin. The tree follows the scroll position. |
| Scope tabs, same tree in every scope (VS User/Workspace/Folder; Z User/Project/Server) | Yours / Project tabs. Project has its own 5-page list. | One tree for both tabs. The Project tab shows only projectable rows (as today), with the same search. |
| "Modified in …" / "Also modified elsewhere" | "Overridden in this project" on Yours; state label on Project | Per-row marker in `muted` text: "Changed", "Set in this project", "Set in project.local.toml". `SettingsStore::isSet` exists (H `src/ui/settings_store.h:34`). |
| Per-row reset (VS gear Reset Setting; Z reset button) | Reset page, Reset all; Project "Use yours" | A per-row ⋯ menu: Reset, Use yours (Project), Copy setting id, Copy link. |
| Deep link (`vscode://settings/<id>`, `zed://settings/<path>`, `OpenSettingsAt`) | Page-level `openSettings(page, filter)`, internal only | `hikarisub://settings/<id>` on the existing scheme. Reveal and highlight the row. Never change a value from a link. |
| Copy Setting ID / as URL | None | The ⋯ menu above. |
| UI ↔ JSON toggle | None. The file is TOML. | "Open settings file" in the ⋯ menu (Zed's "Edit in settings.json" level), with no in-app TOML editor. |
| Extension settings inside Settings (VS `contributes.configuration`) | Plugin manager only | #1230 as planned. |
| Rich per-setting metadata: enum labels and descriptions, deprecation, keywords, tags, markdown, edit presentation | #1230: description, group, order, scope, list, secret. Choice options are display titles. | Decide before 0.1.0 (see decisions). |
| Localised manifest strings (`package.nls.json`) | None | A separate design. Without it, plugin pages show English in a translated window. |
| Instant commit (both apps) | Staged OK/Cancel/Apply; plugin settings instant | One commit model for the whole window (decision). |
| Keybindings editor: Source column, `@source:`, record keys, conflicts filter (Z) | Filter combo by area, text search | A Source filter (Built-in, Yours, Plugin, Macro) and a Conflicts filter. |
| Extension page's Settings table (VS Features tab) | Plugin details list commands and panels (H `docs/plugins.md:139`) | Details list the plugin's settings and link to its Settings page. |
| Settings profiles (VS, Z) | None. "Profile" in code means the user tier. | Not needed for 0.1.0. |
| Policies and managed settings (VS policy; Z organization) | None | Not needed for 0.1.0. Don't add a manifest `policy` key. |
| Settings Sync (VS) | None | Out of scope. |
| Language overrides (VS `[lang]`, Z `languages`) | None | No HikariSub counterpart worth adding now. |
| Trust indicator (VS "Requires workspace trust") | Trust bar. Two settings widen, `translation.translator` and `translation.reviewer` (H `src/application/settings.cpp:43-44`), though ADR 0030 `:65` still says none does. | A "Waits for trust" chip on Project rows whose setting widens. Fix the ADR line. |
| AI search over settings (VS AI toggle) | The AI reads `settings.list` without titles | Add `title` (and maybe `description`) to `settings.list` once the catalogue exists. |

---

## Options for a HikariSub unified Settings

Every plugin-facing name below ships in API 1 and can't change after 0.1.0 stable (ADR 0034, PR `docs/adr/0034-plugins-grow-into-an-extension-system.md:12-18`).

### Option 1: one window with search and a contents tree

**What.** Keep one Settings window on the DG1 frame:

- a search box at the top;
- a contents tree on the left: pages, then their sections, then Plugins › <name>;
- one scrolling list on the right.

Typing filters both the tree and the list, as in VS and Z. Custom sections stay bespoke, such as AI providers, theme cards, tag buttons and the shortcut table. They still register a title and keywords, so search finds the section. Zed mixes generic rows with custom renderers and sub-page links the same way (Z `settings_ui/src/settings_ui.rs:526-680`, `:818-821`).

**How.** Add a C++ settings catalogue: id, page, section, label, note, keywords and control. Simple rows (check, number, text, choice, font) render from it. It replaces the QML walk in `docs_export.cpp` (and its `MUST KILL`). It also feeds `settings.list` and the AI.

**API 1 surface.** None new for plugins. Plugin `title`, `description` and `group` become search text. They are already frozen by #1230. If plugins get `keywords`, that key freezes (decision 4).

### Option 2: scopes as tabs (Yours / Project / this file)

**2a, recommended.** Two tabs, Yours and Project, over the same tree and search. The Project tab shows only projectable rows, as today. "This file" is not a tab. Script Info is Document content, not a setting. Settings links to it with a row "Script properties of the open file…" that opens `ScriptPropertiesDialog`.

**2b.** Three tabs, the third editing the Script Info fields of the editing target. This mixes undoable Document edits with settings, and it only works for ASS (H `src/ui/ScriptPropertiesDialog.qml:28`).

**API 1 surface.**
- The manifest `scope` enum, `user | project`. It is frozen by #1230 either way.
- 2b would tempt a third value, `document`. That would freeze a per-Document plugin settings store. Not recommended.

### Option 3: plugin sections generated from the manifest

**What.** As #1230 says, one page per plugin under Settings › Plugins:

- `group` gives the section headings, sorted by `order`;
- `description` sits under the title;
- secrets show dots with Show and Clear;
- the page header has the plugin's icon, name and a "Manage plugin…" link to its details.

Plugins never place rows inside built-in pages. That keeps HikariSub's page ids out of the API. VS Code groups extension settings under their own node too (VS `settingsTree.ts:480-530`).

**API 1 surface.**
- Already frozen by #1230: `contributes.settings[]` with `id`, `title`, `type` (`boolean`, `integer`, `number`, `string`, `choice`, `list`, `secret`), `default`, `minimum`, `maximum`, `options`, `maxItems`, `description`, `group`, `order` and `scope`.
- Also frozen: `hikari.settings.get` / `set`, QML `api.settings` / `api.setSetting`, and the event `plugin_settings.changed`.
- Candidates to decide now, because adding them later means API 2:
  - **Choice options as `{value, title}`.** Today the stored value is the visible text, so a plugin can never reword or translate an option without breaking stored values.
  - **`keywords`**: hidden search words (VS `configurationExtensionPoint.ts:130-136`).
  - **`deprecated`**: a message that marks the setting as going (VS `:94-110`).
  - **`multiline`** for long strings (VS `editPresentation`, `:112-120`).
  - **Not recommended:** markdown descriptions, free `tags`, `policy`, `when` conditions on settings.

### Option 4: hotkeys, themes, AI and plugins: folded in or linked

| Area | Recommendation |
| --- | --- |
| **Shortcuts** | Folded in, as today: a page in the window with its own table. Global search also matches command names and shows "N shortcuts match", which opens the page with the search filled in. Add a Source filter and a Conflicts filter (Z `keymap_editor.rs:217-227`). VS and Z keep it a separate editor. HikariSub's page already works and avoids a second window. |
| **Themes** | Folded in. The Appearance page lists built-in and plugin themes (PR `docs/plugins.md:567`). |
| **AI** | Folded in. The page stays bespoke. Its sections are indexed by heading and keywords. Plugin MCP servers land there (ADR 0034 `:79`). |
| **Plugins** | Split by job. The plugin manager keeps install, enable, permissions, updates and marketplace. Settings gets each plugin's settings. "Check for plugin updates" and "Install updates automatically" also appear in Settings, under Updates and privacy, beside the app's own update rows. Both places link to each other. |
| **Legacy import review** | Linked. It stays its own flow from General › Legacy. |

**API 1 surface.** None, apart from the link scheme in Option 6.

### Option 5: a JSON/text view or not

- **5a, recommended.** No in-app text editor. The ⋯ menu offers "Open settings file" (`settings.toml`, or `project.toml` on the Project tab), which reveals or opens it in the system editor. This is about Zed's level ("Edit in settings.json", Z `settings_ui.rs:534`).
  - One gap: the app merges disk changes only before its own writes (H `src/ui/settings_files.cpp:1041`). Hand edits made while HikariSub runs need a file watch to show up.
- **5b.** A VS Code-style UI/text toggle with a schema-aware editor. That is a TOML editor with completion and validation. It costs a lot and serves few subtitle editors.

**API 1 surface.** None. The file layouts become something users rely on, but they aren't plugin API. `[plugins."<id>"]` in `project.toml` is already documented (H `docs/plugins.md:117-129`).

### Option 6: "modified" filters, reset, and a settings link scheme

- **Modified marker.** `muted` text on the row, because Quiet keeps the accent for other things. Options: "Changed" (Yours differs from default), "Set in this project" or "Set in project.local.toml" (Project), and "Overridden in this project" (Yours, as today).
- **Filters.** `@modified`, `@project`, `@plugin:<id>` and `@id:<id>`. These are UI-only and not API.
- **Per-row ⋯ menu.** Reset to default, Use yours (Project), Copy setting id, Copy link, Open settings file.
- **Link scheme.** `hikarisub://settings/<id>` on the existing `hikarisub://` scheme.
  - Built-in ids are the existing setting ids, such as `grid.autoHideColumns`.
  - Plugin ids are `plugin.<plugin id>.<setting id>`, matching plugin command ids (H `docs/plugins.md:215`).
  - `?scope=project` picks the Project tab. `hikarisub://settings` alone opens the window.
  - Like VS Code, the handler only opens and reveals. It waits for plugins to load before giving up on an id (VS `preferencesService.ts:694-698`). It never changes a value.
- **Users of the link:**
  - the palette (#1236): commands "Open settings", "Open project settings" and "Open shortcuts";
  - docs and the website;
  - toasts and notices that say "Settings › …";
  - the Explorer's Project settings node;
  - plugins.

**API 1 surface if plugins can jump to a setting.**
- **An operation**, for example `settings.open {setting?: string, scope?: "user" | "project"}`. With no `setting`, it opens the calling plugin's own page.
- **Don't take `page` or `query`.** `page` would freeze HikariSub's page ids. `query` would freeze the filter grammar.
- **The id format** `plugin.<plugin id>.<setting id>` and the URL grammar `hikarisub://settings/<id>` freeze too.
- **Whether `settings.list` gains** `title`, `description`, `source` (`default | user | project | projectLocal`) and `projectable`. These are host-API result fields, so they must be settled in API 1.

---

## Decisions for the owner

1. **Commit model.** Should the whole window apply at once, like VS Code, Zed and today's plugin settings, or keep OK / Cancel / Apply?
   *Recommended:* apply at once, with per-row reset and a Close button. The Theme preview code already does live preview.
2. **Shape.** Keep one Settings window (DG1, resizable) with search, a contents tree with sections, and a Plugins node? Or make Settings a Document-area tab like VS Code?
   *Recommended:* one window.
3. **Scopes.** Yours / Project tabs over one tree, with no "This file" tab, and Script properties linked from Settings?
   *Recommended:* yes. Use one word for the user tier in the UI ("Yours"; CONTEXT.md still says "Mine" for tier chips, H `CONTEXT.md:192`) and keep `user` in the manifest.
4. **Plugin setting keys to add before API 1 freezes.**
   *Recommended:* choice options as `{value, title}`, `keywords`, `deprecated` and `multiline`. No markdown, `tags`, `policy` or `when`.
5. **Manifest localisation.** Should plugin titles, descriptions and options be translatable in API 1, like `package.nls.json`?
   *Recommended:* yes, as its own card before stable. Otherwise every plugin page is English-only in a translated window, for good.
6. **Plugin placement.** Plugin settings only under Settings › Plugins › <name>, never inside built-in pages?
   *Recommended:* yes. This keeps page ids out of the API.
7. **Link scheme.** `hikarisub://settings/<id>`, with `plugin.<plugin id>.<setting id>` for plugins and `?scope=project`, that only opens and reveals?
   *Recommended:* yes.
8. **Plugin operation.** Add `settings.open {setting?, scope?}` to API 1, with no `page` and no `query`?
   *Recommended:* yes. The palette (#1236) uses the same path through built-in commands.
9. **`settings.list` fields.** Add `title`, `source` and `projectable` (and maybe `description`) to API 1, for the AI and for plugins?
   *Recommended:* yes, once the C++ catalogue exists. Decide now, because these are result fields of an API 1 operation.
10. **Text view.** Settle for "Open settings file" only, with no in-app TOML editor?
    *Recommended:* yes, and add a file watch so hand edits show up.
11. **Shortcuts.** Keep them as a page in the window, adding Source and Conflicts filters, and let global search match command names?
    *Recommended:* yes.
12. **Not in 0.1.0:** named settings profiles, policies and managed settings, settings sync and language overrides.
    *Recommended:* confirm, so no manifest key for them freezes by accident.
