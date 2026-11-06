# Opening a folder as a Project

Research for the Project grilling session (2026-11-04). The owner wants HikariSub to open a folder the way Zed and VS Code do: the folder becomes the window's context, Hikari AI works inside it, and an optional TOML file holds per-project settings. This note gathers what those tools, the agent CLIs and subtitle tooling do, maps it onto what HikariSub already has, and sets out the remaining choices. Repo paths are at main `05a69206f`. External claims link to the source that owns them; the links are in [Sources](#7-sources).

**Owner decisions, round 1 (fixed inputs here):**

1. The name is **Project**.
2. The project file is **optional** and is **TOML**.
3. **One Project per window.**
4. An **Explorer panel**, like VS Code's.
5. **Open folder** on Home and from the command line.
6. **New:** the Style manager, and anything else that can be shared, saves to the Project as well as to the user. [§5](#5-shared-things-with-a-project-tier) inventories these assets.

Not covered: DaVinci Resolve, Premiere, EZTitles and Ooona. I found no primary source worth citing in the time available. For Subtitle Edit, the search found only settings files (`Settings.xml`/`Settings.json`) and a `Dictionaries` folder, and no project format ([SE settings discussion], [SE help]). Treat its project support as unknown, not as absent.

## 1. Summary

- **In Zed,** a worktree is "either a directory or a single file that Zed opens as a standalone project" ([Zed trust]). Each project "has its own file tree, git state, and search scope", and the workspace layout "is preserved per project" ([Zed windows]). `.zed/settings.json` in the project overrides the user settings, and in list settings `"..."` extends the inherited list instead of replacing it ([Zed all settings]). Every worktree starts in Restricted Mode, which stops project settings, language servers and MCP servers until the user trusts it ([Zed trust]).
- **Zed's agent** works inside the project. Its file tools resolve each path to a project worktree, ask before following a symlink out of it, and treat `.zed/` as a sensitive location ([zed tool_permissions.rs]). Reads matching `private_files` or `file_scan_exclusions` are refused ([zed read_file_tool.rs]). Per-tool `always_allow`/`always_confirm`/`always_deny` regex rules sit on top, and "paths inside Zed settings directories still prompt or block" even under global auto-approve ([Zed tool permissions]). Project instructions come from the first match of `.rules` … `AGENTS.md`, `CLAUDE.md`, `GEMINI.md` ([Zed instructions]).
- **In VS Code,** a workspace is "the collection of one or more folders that are opened in a VS Code window". Opening a folder is enough to make one ([VS Code workspaces]). Settings layer from User to Workspace to Workspace Folder, later scopes override earlier ones, and objects merge while arrays are overridden ([VS Code settings]). A multi-root `.code-workspace` file lists folders and holds the workspace settings ([VS Code multi-root]).
- **VS Code's Workspace Trust** opens an unfamiliar folder in Restricted Mode, which limits "AI agents, terminal, tasks, debugging, workspace settings, and extensions" ([VS Code trust]). Built-in agent tools "can only read and write files within the current workspace folder" ([VS Code agent security]).
- **The common shape:** a folder is the unit of context; a hidden dot folder holds the project config; project config overrides user config; anything that can run code waits for a trust decision; the agent's file reach is the folder.

## 2. Comparison

| | Zed | VS Code | Claude Code | Codex |
| --- | --- | --- | --- | --- |
| **Project file** | Optional `.zed/settings.json` (JSONC, `"**/.zed/**/*.json"` maps to JSONC), plus `.zed/tasks.json` ([Zed all settings], [Zed tasks], [Zed VS Code migration]) | Optional `.vscode/settings.json`. Multi-root uses `<name>.code-workspace`, a JSON file that allows comments ([VS Code workspaces], [VS Code multi-root]) | Optional `.claude/settings.json` (shared) and `.claude/settings.local.json` (personal, kept out of git), strict JSON; instructions in `CLAUDE.md` or `AGENTS.md` ([Claude settings], [Claude memory]) | Optional `.codex/config.toml`, **TOML**, layered from the repo root down to the working directory; instructions in `AGENTS.md` ([Codex config], [Codex AGENTS.md]) |
| **Settings layering** | Project over user; list settings extend with `"..."`, `[]` clears ([Zed all settings]) | Default < User < Remote < Workspace < Workspace Folder, then language-specific, then Policy; "Values with primitive types and Array types are overridden", "values with Object types are merged" ([VS Code settings]) | Managed > `--settings` > project local > shared project > user; "Lists merge instead of overriding" ([Claude settings]) | CLI > project `.codex/config.toml` (closest wins, trusted only) > profile > user `~/.codex/config.toml` > cloud > system > defaults ([Codex config]) |
| **Not settable per project** | Not documented | "application-wide settings related to updates and security can not be overridden by Workspace settings"; executable paths are user-only ([VS Code settings]) | Keys scoped `User`, `Managed` or `Global config` "never apply from the shared file"; `defaultMode` `auto`/`bypassPermissions` not from project ([Claude settings]) | Not documented on the basics page |
| **Multi-root** | "Add Folder to Project" adds "an additional root", compared to VS Code's multi-root ([Zed windows]) | `.code-workspace` `folders` array, relative or absolute paths; only resource-scoped settings apply per folder ([VS Code multi-root]) | `--add-dir`, `/add-dir`, `permissions.additionalDirectories` ([Claude permissions]) | `sandbox_workspace_write.writable_roots` ([Codex sandbox]) |
| **Trust** | Restricted Mode per worktree; trusting a parent folder trusts its subfolders; persists; `trust_all_worktrees` ([Zed trust]) | Restricted Mode for new folders; trust a parent folder; Trusted Folders & Workspaces list; empty windows trusted ([VS Code trust]) | Trust dialog gates project `allow` rules and `additionalDirectories`; "`deny` and `ask` rules apply right away"; trust keyed on the git root ([Claude permissions]) | Untrusted projects skip `.codex/` layers, "including project-local config, hooks, and rules" ([Codex config]) |
| **Agent scope** | File tools resolve inside project worktrees and ask before a symlink escape; `.zed/` flagged; terminal sandbox writes "inside open project directories" except `.git` ([zed tool_permissions.rs], [Zed sandboxing]) | Built-in tools read and write in the workspace folder; `chat.additionalReadAccessFolders` adds read-only folders; "Restricted mode disables agents" ([VS Code agent security]) | Reads within "working directory and additional directories" without prompts; `blockReadsOutsideWorkingDirectories`/`blockWritesOutsideWorkingDirectories` ([Claude permissions]) | `read-only`, `workspace-write` ("edit within the workspace"), `danger-full-access` ([Codex sandbox]) |
| **Instructions** | First of `.rules`, `.cursorrules`, …, `AGENTS.md`, `CLAUDE.md`, `GEMINI.md`; personal `~/.config/zed/AGENTS.md` ([Zed instructions]) | `.github/copilot-instructions.md`, root `AGENTS.md` on by default, nested `AGENTS.md` experimental ([VS Code instructions]) | `CLAUDE.md` from the working directory and every directory above, concatenated root first; subdirectory files load on demand; `AGENTS.md` read when no `CLAUDE.md` ([Claude memory]) | `AGENTS.override.md`/`AGENTS.md` from the project root down to cwd, at most one per directory, 32 KiB cap ([Codex AGENTS.md]) |
| **Recent list** | File › Open Recent (Ctrl+R); Enter opens in the current window, Ctrl+Enter in a new one ([Zed windows]) | File › Open Recent lists "recently opened folders and workspaces followed by files" ([VS Code tips]) | `--resume` finds sessions by directory ([Claude permissions]) | Not researched |
| **CLI** | `zed <dir>`; `-n` new window, `-a` add, `-r` replace; `cli_default_open_behavior` ([Zed CLI], [Zed windows]) | `code <folder>` (several folders make a multi-root workspace), `-r`, `--add <dir>`, `-n` ([VS Code CLI]) | `claude` in the folder; `/cd` moves the session ([Claude permissions]) | `codex` in the folder ([Codex config]) |

Two points matter most for HikariSub:

- **Codex is the TOML precedent.** Its project file is TOML, it layers by directory, and it loads only for trusted projects ([Codex config]).
- **Everyone splits restricting from granting.** Claude Code applies project `deny`/`ask` at once and waits for trust before applying `allow` ([Claude permissions]). VS Code ignores project values for update and security settings ([VS Code settings]).

## 3. What HikariSub has today

### Glossary collisions

`CONTEXT.md` lists "Project" under _Avoid_ for **Document** (`CONTEXT.md:19`), and "Project file" under _Avoid_ for **Session** (`CONTEXT.md:27`). **Workspace** means a tab's panel arrangement (`CONTEXT.md:43-45`). ADR 0024 already uses "project folder" for the Honya glossary store: "The glossary, the characters and the style live in a project folder in Honya's format, shared with Honya. Script Info points to it" (`docs/adr/0024-hikari-ai.md:80`; options in `docs/research/hikari-ai-integration.md:750-752`). Honya's own project is a folder of `PROJECT.md`, `CHARACTERS.md`, `GLOSSARY.md`, `STYLE.md` and `Vol_NN/` (honya `README.md:281-301` at `83b6209`).

The owner's choice of "Project" means `CONTEXT.md` changes. A proposal is in [§4.1](#41-the-name-decided-project).

### Session

- The legacy line format (`LastSession.txt`, `.kls`) is read only. Each tab holds video, position, subtitles, active Line, scroll, audio, keyframes and the `Layout:` arrangement (`src/application/include/hikari/application/session_file.h:15-33`).
- HikariSub writes JSON: `last-session.json` in the user folder and `.hksession` files. Paths are absolute (`docs/distribution.md:136`; `src/app/include/hikari/app/session_json.h`).
- MW4 #628 (open) adds windows to the Session: for each window, its geometry and its tabs (ADR 0025 "Restore and the Session", `docs/adr/0025-multiple-windows.md:42-48`). A per-window Project path belongs in that same schema change.

### Associated files

- Media links come from the Document's Script Info. Relative paths resolve against the subtitle file's folder (`src/application/include/hikari/application/media_association.h:22-26`).
- `findSameNamedFile` finds `<stem>.*` beside the file, with no subfolders and no hidden files (`src/application/include/hikari/application/associated_files.h:11-14`).
- Aegisub writes these links into `[Aegisub Project Garbage]` as `Audio File`, `Video File`, `Timecodes File`, `Keyframes File`, together with `Automation Scripts` and `Last Style Storage` ([Aegisub subtitle_format_ass.cpp] lines 92-101). Its "Automatically load linked files" option chooses ask, never or always ([Aegisub options]).
- Absolute paths in that section can leak the user's name ([Aegisub #31]). Kaleido's project template strips the section with `sed '/^\[Aegisub Project Garbage/,/^$/d'` ([Kaleido clean_project_garbage.sh]).

### User folder

- One folder: `$XDG_CONFIG_HOME/hikarisub` or `%APPDATA%\HikariSub`, overridden by `HIKARISUB_USER_DIR` (`src/app/include/hikari/app/user_folder.h:26-28`).
- It holds `settings.toml`, `dictionaries`, `catalogs`, `presets`, `recovery`, `plugins`, `plugin-data` and `plugins.json` (`user_folder.h:32-42`), plus `automation/autoload` and `include` (`docs/distribution.md:111`).
- The composition root passes these folders to the app one by one: `options.dictionaryDir`, `catalogDir`, `fontCatalogDir`, `presetsDir` and `plugins` (`src/app/composition.cpp:237-246`). A Project tier can hook in at these points.

### Settings in TOML (CF1 #488, CF2 #489)

- toml++ is already a dependency (`vcpkg.json:47`), wrapped by `src/ui/toml_file.h` (`parseTomlFile`, `formatTomlFile`). Its own note says "toml++ does not keep comments, so a file HikariSub writes loses them" (`toml_file.h:3`).
- `SettingsFiles` splits the settings into `settings.toml` (non-default values only), `hotkeys.toml` and `state.json`, each with `version = 1` (`src/ui/settings_files.h:18-72`; `docs/distribution.md:113-122`).
- A broken file is renamed `<name>.broken` and never overwritten (`docs/distribution.md:121`; helper `src/application/include/hikari/application/owned_file.h`).
- **The port.** Storage sits behind `application::SettingsStorage`: `read`, `write` and `remove` per `SettingDefinition` (`src/application/include/hikari/application/settings.h:42-48`). A Project layer can be a second `SettingsStorage` consulted before the user one, with no caller changes. That is the same move CF1 made when it replaced the INI backend (#488 design note §1).
- The list files are TOML too: `shapes.toml`, `tags.toml` and `catalogs/fonts.toml` (`docs/distribution.md:124-129`).

### Home, recent files, first run

- **Home** shows only while no Document is open. It has New, Open…, Open video…, Restore and a Recent files list of up to 20, each with a Missing mark and Show in folder (`docs/ux/workspaces.md:66-74`; `src/ui/HomePage.qml`).
- **Recent files** is one list, `RecentFiles::kCapacity = 20` (`src/application/include/hikari/application/recent_files.h:13-15`). ADR 0025 keeps recent files app-wide (`docs/adr/0025-multiple-windows.md:32`).
- **Getting started** (FR1 #363) and the user folder (UD1 #362) are closed. Neither has a Project page.

### Command line and file associations

- **Arguments.** There is no `QCommandLineParser`. `launchFiles` drops the program's own switches and resolves every argument to a path (`src/app/include/hikari/app/single_instance.h:15-17`; called at `src/app/composition.cpp:207`). `SingleInstance` forwards those paths to the running instance (`single_instance.h:24-81`).
- **A folder argument opens as video today.** `openKindOf` sorts paths by extension, and a path with no known extension falls through to `OpenKind::Video` (`src/application/recent_files.cpp:107-126`).
- **Linux** registers file types through `MimeType=` in the desktop file, with `Exec=… %f` (`packaging/linux/io.github.altqx.HikariSub.desktop.in:9,14`), and `.hksession` through `packaging/linux/mime/hikarisub.xml`.
- **Windows** associates `.ass`, `.ssa`, `.srt` and `.sub` in an optional installer section (`packaging/windows/hikarisub.nsi:1028-1044`). There is no folder verb.

### How the AI and plugins reach files today

- **Documents only through HA1.** The sidecar "never opens a subtitle, a video or a user file itself" (`docs/adr/0026-hikari-ai-runs-in-a-typescript-sidecar-on-bun.md:27`).
- **The sidecar's working directory** is "a private folder in the user folder, never a subtitle's folder" (ADR 0026:43). This rule must change, or be kept on purpose, for "the agent works inside the folder".
- **Files go by grant handle.** `Api::grantFile(caller, path, writable)` returns a handle such as `f1`. `files.list` shows handles with a display name, "never the path" (`docs/api.md:49-51`). `files.read` and `files.write` act on those grants (`docs/api.md:32,39`). There are no folder handles yet.
- **Always prompt, in every mode:** "deleting or overwriting files outside the subtitle's folder and the user folder", running external programs, "sending local files to the network" (`docs/adr/0024-hikari-ai.md:51-58`).
- **SB1 (PR #759, open; ADR 0029 on `w8-sb1`).** The sidecar gets its install folder and its work folder, and "Nothing else in the home folder is reachable". `claude` may write only `~/.claude`, `~/.claude.json` and its work folder. Codex threads are read-only, and Qt declines every file change. In code, `claudeAgent()` sets `readWrite = {~/.claude, ~/.claude.json, work}` and `workingDirectory = work` (`src/backends/ai_agent_broker.cpp` on `w8-sb1`, `claudeAgent`/`codexAgent`).
- **Claude's own instructions are switched off.** The agent runs with `settingSources: []` and env vars that turn off `CLAUDE.md` (`docs/research/agent-sdk-hosting.md:60`). A project's `AGENTS.md` therefore reaches it only if Hikari passes it in.
- **Plugins (PL5).** Plugins get handles from places (`data`, `cache`, manifest folder ids), the picker and drops. `plugins::pathInside` refuses `..`, absolute paths and links out. Remembered folder grants go to `plugins.json` (brain page `decisions/hikari-pl5-plugin-files-network`; `FileGrant{path, writable}` in `src/plugins/include/hikari/plugins/registry.h:14-23`).
- **Trust precedent.** Scripts a file names are loaded only after a question that offers "**Always trust scripts in this folder**". Trusted folders join Settings › Automation › Trusted script folders (`docs/automation.md:95`), stored as `automation.trustedFolders` (`src/application/settings.cpp:315`). This is HikariSub's existing folder-trust list.

### Fansub folder conventions

- **Kaleido's project template** is one folder per show. It holds one folder per episode (`01/`) or creditless (`NCOP1/`, `NCED1/`), with split files (`… - Dialogue.ass`, `… - TS.ass`, `… - Lyrics.ass`), shared `common/fonts/`, `common/warning.ass` and a `sub.properties` read by SubKt ([Kaleido template]).
- **Its `.gitignore`** ignores every file and then allows `*.ass`, `*.srt`, `*.xml`, `*.log`, `*.md`, `*.otf`, `*.ttf`, `*.ttc`, … by extension ([Kaleido .gitignore]). A `.toml` or `.sty` in a Project would need its own `!` rule in such a repository.
- **SubKt** is "a highly configurable toolkit for fansubbing automation written in Kotlin for Gradle". It reads `sub.properties`, with episode lists such as `episodes={01..24}|12.5` and `release.entry.property=value` lookups ([SubKt]).

## 4. Design options

### 4.1 The name (decided: Project)

`CONTEXT.md` needs three entries changed. A proposal:

- **Project:** a folder opened in a shell window as its context. It gives the window's Explorer, the Project tier of shared assets, and the AI agent's file scope. There is one per window, and a window may have none.
  _Avoid:_ Workspace (a tab's arrangement), Session (open tabs), or Document.
- **Project file:** the optional `.hikari/project.toml` ([§4.3](#43-file-name-and-location)). A Project exists without one.
  _Avoid:_ session file.
- **Project tier / User tier:** where a shared asset is stored, in the Project or in the user folder ([§5](#5-shared-things-with-a-project-tier)).
- **Edits to existing entries:**
  - **Document:** _Avoid_ keeps "Project", since a Project is not the content.
  - **Session:** _Avoid_ "Project file" becomes "Project (the folder a window opened)".
- **Honya's "project folder"** (ADR 0024:80): either the Project folder itself, or a folder inside it. See [Q7](#6-open-questions-for-the-owner).

### 4.2 Folder only vs folder plus file (decided: optional file)

- **What the decision means:**
  - Any folder opens as a Project.
  - `.hikari/` appears only when the user saves something to the Project tier, or chooses "Create project file".
  - A Project without the file still has an Explorer, an AI scope and a session.
  - Both Zed and VS Code work this way: "You don't have to do anything for a folder to become a VS Code workspace other than open the folder" ([VS Code workspaces]), and Zed's migration guide tells the user to create `.zed/settings.json` themselves when they want project settings ([Zed VS Code migration]).
- **One more choice follows:** whether a folder that has a `.hikari/` opens as a Project when one of its files is opened alone. See [Q3](#6-open-questions-for-the-owner).

### 4.3 File name and location

| Option | For | Against |
| --- | --- | --- |
| **`.hikari/project.toml`**, with tier folders beside it (`.hikari/catalogs/`, `.hikari/layouts.json`, `.hikari/automation/autoload/`) | Every surveyed tool uses a dot folder (`.zed/`, `.vscode/`, `.claude/`, `.codex/`, cited in §2). The §5 assets need a folder anyway. It keeps the episode folder tidy. | Hidden on Linux and macOS. Fansub repositories that allowlist by extension need a `!.hikari/**` rule ([Kaleido .gitignore]). |
| **`hikari.toml` at the root**, with assets in `.hikari/` | Visible; easy to find and edit | Two places for one thing |
| **`hikari.toml` only**, assets inline | One file | `.sty` catalogues and Lua scripts aren't TOML; huge tables; comments lost on every write (`src/ui/toml_file.h:3`) |

**Recommended:** `.hikari/project.toml`.

A personal, untracked override, like Claude Code's `.claude/settings.local.json` ([Claude settings]), could be `.hikari/project.local.toml`. That is useful for team repos; see [Q4](#6-open-questions-for-the-owner).

**File rules.** Reuse CF1's rules unchanged:

- `version = 1`;
- only non-default values;
- unknown keys kept;
- a broken file becomes `.broken` with a notice (`docs/distribution.md:113-122`);
- comments are lost when HikariSub writes the file.

The last point weighs more for a shared project file than for `settings.toml`. A team's file can carry comments explaining choices. Two mitigations:

- write only on an explicit action;
- give hand-edited keys precedence by writing only the keys the app changed (CF1's `m_changed` set, `src/ui/settings_files.h:64`).

Both keep the file mostly hand-owned. TOML itself "aims to be a minimal configuration file format that's easy to read due to obvious semantics" and must be UTF-8 ([TOML 1.0]).

### 4.4 What the file holds

| Candidate | Where it goes | Core or plugin | Recommendation |
| --- | --- | --- | --- |
| Overridable settings (dictionary language, grammar, new-file Script Info defaults, translation options) | `[settings]` in `project.toml`, same dotted ids as `settings.toml` | Core | Yes ([§4.5](#45-settings-layering)) |
| Style and font catalogues | `.hikari/catalogs/` | Core | Yes ([§5](#5-shared-things-with-a-project-tier)) |
| Fonts folder | `fonts = ["fonts", "../common/fonts"]`, relative paths | Core (font provider) | Yes. Kaleido shares `common/fonts/` across episodes ([Kaleido template]) |
| Media discovery (video, keyframes, timecodes, chapters) | `[media]` globs per episode, e.g. `video = "premux/*.mkv"` | Core; extends `associationOffer` (`associated_files.h:33-34`) | Yes, later. Same-stem matching already works inside one folder |
| Episode naming | `[episodes]` pattern, like SubKt's `episodes={01..24}` ([SubKt]) | Plugin | Only if the Explorer groups by episode; otherwise a plugin |
| QC rules | — | Plugin or FA20 | No rule set exists today: `rules.toml` was retired (#608, `docs/distribution.md:130`), and FA20 QC notes (#577, open) load from files |
| Spellcheck words | `.hikari/dictionary.txt` | Core | Yes ([§5](#5-shared-things-with-a-project-tier)) |
| AI instructions | `AGENTS.md` at the Project root, not in TOML | Core (sidecar prompt) | Yes ([§4.8](#48-ai-agent-scope-and-trust)) |
| Plugin enablement | `[plugins] enabled = [...]`, `disabled = [...]` | Core (plugin manager) | Yes, behind trust ([§5](#5-shared-things-with-a-project-tier)) |
| Export presets | `[export]` default format and options | Core | Yes ([§5](#5-shared-things-with-a-project-tier)) |
| Translation pair and glossary | `[translation] source`, `target`, `glossary = "honya/"` | Core (AI10) | Yes; replaces ADR 0024's Script Info pointer with a Project key (Q7) |
| Explorer excludes | `[explorer] exclude = ["*.lwi", "*.ffindex"]` | Core | Yes, like VS Code's `files.exclude` ([VS Code UI]) |
| AI excludes | `[ai] exclude = [...]` | Core | Yes, like Zed's `private_files` ([zed read_file_tool.rs]) |

### 4.5 Settings layering

**Recommended:** Default < User < Project, per key, with three limits.

1. **An allowlist of Project-settable ids.** VS Code ignores project values for "updates and security" and executable paths ([VS Code settings]). Claude Code refuses `User`/`Managed`/`Global config` keys from project files ([Claude settings]). For HikariSub, never from a Project:
   - privacy and analytics;
   - updates;
   - `ai.*` providers and permissions;
   - plugin network;
   - automation trusted folders;
   - the UI theme and fonts;
   - hotkeys (ADR 0025 keeps settings, hotkeys and the theme app-wide, `docs/adr/0025-multiple-windows.md:29`).
2. **Lists are replaced, not merged, for plain settings.** That matches VS Code's "Array types are overridden" ([VS Code settings]). Named collections are different; [§5](#5-shared-things-with-a-project-tier) merges them by name.
3. **Restricting values apply without trust; widening values wait for trust.** This follows Claude Code's deny-now, allow-after-trust split ([Claude permissions]).

**Implementation.** Add a `LayeredSettingsStorage : SettingsStorage` that reads Project, then User. A write goes to the tier the UI chose (`settings.h:42-48`). `SettingDefinition` gains a `projectable` flag beside `scope` (`settings.h:24-32`).

**Settings UI.** Add User and Project tabs, as VS Code does ([VS Code settings]). The Project tab shows only the allowlisted keys, and the User tab marks a key the open Project overrides.

**Trade-off with ADR 0025.** Effective settings become per window, because each window has its own Project. ADR 0025's "a change applies to every window at once" still holds for the User tier. It needs an amendment line for the Project tier.

### 4.6 The Explorer panel (decided)

- **Placement.** A new dock in `kPanelRegistry`, which is `11` on main (`src/ui/workspace_layout.h:66`). Coordinate the id with the panel cards in flight.
- **Defaults.** Left of the Grid in the Editing preset when a Project is open, and closed when there isn't one.
- **Tree:**
  - the Project folder;
  - a type filter: subtitles, video, audio, keyframes, timecodes, fonts, chapters;
  - `[explorer] exclude` globs;
  - hidden files off;
  - `.hikari/` shown as "Project settings".
- **Grouping.** An optional "group by stem" view uses `findSameNamedFile`'s rule (`associated_files.h:11-14`), so `ep01.ass` shows its `ep01.mkv` and `ep01_keyframes.txt`.
- **Actions:**
  - open in a new tab, or in the current one;
  - open the video or keyframes into the current tab;
  - Show in folder, Copy path;
  - rename;
  - delete to the OS trash;
  - new subtitle file here.
- **Disk changes.** The tree updates when files change on disk. The existing "File changed on disk" bar handles open Documents (brain page `decisions/hikari-qt-wave6c-popup-redesign`).
- **Plugin-first check (ADR 0022:61-69).** It fits the API surface (a panel and files), but it is the main surface of a default way to open the app, and every Project user needs it. So it is core. The card says why, as ADR 0022 asks.

### 4.7 A Session per Project

Options:

- **(a)** One Session for the app, with the Project path per window. This is MW4's schema plus one field.
- **(b)** Also keep each Project's last session, so reopening a Project restores its tabs. Zed preserves layout per project ([Zed windows]). VS Code "automatically keeps track of configuration, such as your open files or editor layout" without a file in `.vscode/` ([VS Code workspaces]).
- **(c)** Store the Project session in `.hikari/session.json`.

**Recommended:** (a) plus (b), with (b) stored in the user folder and keyed by the Project's canonical path. Sessions hold absolute paths and machine state (`docs/distribution.md:136`), which don't belong in a folder that may be shared or committed. (c) would leak usernames, as Aegisub's Project Garbage does ([Aegisub #31]).

**Home.** "Restore last session" stays app-wide. Opening a Project offers its own last tabs.

### 4.8 AI agent scope and trust

Today the agent sees Documents through HA1, files only by grant, and a private work folder (§3). Three options:

| Option | What changes | For | Against |
| --- | --- | --- | --- |
| **A. Project as an HA1 folder grant** | HA1 gains folder handles (`files.list` over a granted root with relative names; `files.read`/`files.write` inside it, through PL5's `pathInside`). The window's Project root is granted to that window's AI threads: read by default, write staged. | Keeps ADR 0026's "only through the Hikari API" and Qt deciding every permission. Same code serves plugins (PL5 folder grants). Writes stay reviewable. | HA1 change and an API version bump (ADR 0022:20-22) |
| **B. Project root as the agent CLI's work folder** | `claudeAgent(…, work = project)`, so the sandbox profile's `readWrite` includes the Project | Claude's own file tools would work | Contradicts ADR 0026:43 and ADR 0029's profile. Built-in tools are off anyway (`--tools=`, ADR 0029). Writes would bypass the review. |
| **C. No folder scope** | The agent sees open Documents only | No change | Doesn't meet the goal |

**Recommended:** A. It is the only option that keeps the review (ADR 0024 "Every mode shows each edit as a diff") and the HA1 boundary. Details:

- **Reads** inside the Project are tool calls like any read: no prompt (ADR 0024:47).
- **Writes** to non-Document files inside the Project stage in the review. ADR 0024's always-prompt rule ("outside the subtitle's folder and the user folder") becomes "outside the Project, the subtitle's folder and the user folder".
- **Never sent:**
  - `[ai] exclude` globs, with defaults for `.hikari/project.local.toml`, `*.key`, `.env*` and `.git/`, after Zed's `private_files` ([zed read_file_tool.rs]) and VS Code's `"**/.env": false` example ([VS Code agent security]);
  - media files as bytes (video goes through `video.frame`, audio through `audio.peaks`, `docs/api.md`);
  - anything outside the Project root after symlink resolution, as Zed's `SymlinkEscape` ([zed tool_permissions.rs]).
- **`.hikari/` is sensitive.** An AI write there always prompts, as Zed flags `.zed/` ([zed tool_permissions.rs]) and VS Code suggests for `.vscode/*.json` ([VS Code agent security]).
- **Instructions.** Read `AGENTS.md` at the Project root, the file that Zed, VS Code, Claude Code and Codex all read ([agents.md], [Zed instructions], [VS Code instructions], [Claude memory], [Codex AGENTS.md]). Concatenate root-first like Claude Code and Codex. Hikari passes the text into its own agent and into the Claude and Codex sessions, because `settingSources: []` keeps Claude from finding it (`docs/research/agent-sdk-hosting.md:60`). Instructions are not a boundary: "Instructions guide the model and are not an enforcement boundary" ([VS Code agent security]).
- **Trust.** Reuse one folder-trust list: `automation.trustedFolders` (`src/application/settings.cpp:315`, `docs/automation.md:95`), renamed in the UI to "Trusted folders". An untrusted Project behaves like this:
  - data tiers (catalogues, dictionaries, layouts, settings that only restrict) apply;
  - `AGENTS.md` loads, but a banner shows it;
  - Project autoload scripts don't load;
  - Project plugin enablement doesn't apply;
  - AI permission widening doesn't apply.

  VS Code's Restricted Mode "disables agents in that workspace" outright ([VS Code agent security]). That is stricter, and Hikari's agent can't run code. See [Q9](#6-open-questions-for-the-owner).

### 4.9 Opening: CLI, file manager, Home (decided: Home and CLI)

- **CLI.** `hikarisub <dir>` opens the folder as a Project. Work needed:
  - `openKindOf` must test for a directory before the extension, or folders keep opening as video (`recent_files.cpp:107-126`);
  - `launchFiles` and `SingleInstance` already forward paths (`single_instance.h:15-61`).
- **Flags,** following Zed's names ([Zed CLI]):
  - `-n`/`--new-window`;
  - `-r`/`--reuse-window` (replace the current window's Project);
  - default: a folder opens in a new window unless the current window has no Project and no Documents.
  - A folder already open focuses its window. Zed's `new_window` rule is similar: "a non-ignored file inside an already-open project opens in that project's window" ([Zed windows]).
  - `hikarisub <dir> <file>` opens both.
- **A file with no Project.** Opening a file alone leaves the window without a Project. Q3 asks whether a file inside a trusted folder with `.hikari/` should open that Project.
- **File manager:**
  - **Linux:** adding `inode/directory` to `MimeType=` (`packaging/linux/…desktop.in:14`) lists HikariSub under "Open with" for folders. Some file managers may then offer it as a default folder handler, which surprises users.
  - **Windows:** a `Directory\shell` and `Directory\Background\shell` "Open with HikariSub" verb in the existing optional association section (`hikarisub.nsi:1028-1044`).

  **Recommended:** the Windows verb, and on Linux a separate desktop action rather than the MIME type ([Q10](#6-open-questions-for-the-owner)).
- **Home:**
  - an **Open folder…** button beside Open…;
  - a **Recent projects** list above Recent files, with the same row model (Missing, Remove, Show in folder; `docs/ux/workspaces.md:70`) and a separate `RecentFiles` list of 20;
  - a window with a Project shows the Project's name in Home and the title bar.

  To make Projects "a default way to open", Home could lead with Recent projects once the user has one.

### 4.10 Plugin first (ADR 0022)

The rule: a feature ships as a plugin "when it fits the plugin API and most users won't need it" (`docs/adr/0022-hikarisub-plugins.md:61-69`).

| Part | Core or plugin | Why |
| --- | --- | --- |
| Project model, `project.toml`, settings layer, trust | Core | Settings and permissions are the core; plugins can't own them |
| Explorer | Core | §4.6 |
| Project tiers for catalogues, dictionaries, layouts, scripts | Core | They extend core stores ([§5](#5-shared-things-with-a-project-tier)) |
| AI folder scope | Core (HA1 and sidecar) | ADR 0026 boundary |
| Episode batch rename, release naming, mux/build (SubKt-like) | Plugin | Commands plus files; few users |
| Project templates ("new show from template") | Plugin | Files only |
| QC rule packs | Plugin | No core rules exist (#608) |

**What plugins need.** HA1 needs `project.info` (root display name, a handle for the root folder) and a `project.changed` event. Plugins then get a `project` place, beside `data` and `cache` in PL5. Both are add-only API changes (ADR 0022:20-22).

### 4.11 Multiple windows (decided: one Project per window)

- **Where the Project lives.** It is per-window state, a new row in ADR 0025's table (`docs/adr/0025-multiple-windows.md:27-34`). Recent projects is app-wide, as Recent files is.
- **Moving a tab into another window** puts it under that window's Project tier: catalogues, dictionary words, AI scope. Options:
  - **(a)** move freely, and the tab follows the destination window's Project;
  - **(b)** ask when the Projects differ;
  - **(c)** refuse.

  **Recommended:** (a) with a toast naming the new Project. ADR 0025 says "Moving a tab never saves, reloads or reviews its Document" (`:23`), and (a) keeps that.
- **The AI panel** "belongs to the window, not to a tab" (ADR 0024:34). Its thread scope is that window's Project, which fits one Project per window.
- **Two windows on the same Project.** Refused: focus the existing window, as in §4.9.

### 4.12 Legacy and Aegisub compatibility

- **Nothing new goes into subtitle files.** Project data stays in `.hikari/`. Aegisub and HikariSub legacy ignore it.
- **Media links.** Keep relative Script Info resolution against the subtitle's folder (`media_association.h:22-26`).
- **`Last Style Storage`** names a catalogue. HikariSub follows it in `followedCatalog()` (`src/app/style_manager_controller.cpp:325-334`), and Aegisub writes it ([Aegisub subtitle_format_ass.cpp] line 97). Resolve it Project tier first, then user.
- **`Automation Scripts`.**
  - Aegisub writes `~` for paths relative to the subtitle's folder, `$` for paths under the automation base, and `/` for absolute paths ([Aegisub auto4_base.cpp] lines 386-397, 455-474). HikariSub reads the same key (`src/application/include/hikari/application/document_scripts.h:11-33`).
  - A script in `.hikari/automation/` named by a file should be written with `~` and a relative path, so Aegisub still finds it.
- **Aegisub's own autoload folders** are `?user/automation/autoload/` and `?data/automation/autoload/` ([Aegisub default_config.json] line 303). It has no project tier, and `?script` (the subtitle's folder) is its nearest equivalent ([Aegisub path.cpp] line 32).
- **The `[Hikari]` section** (WS2) keeps per-file arrangements and is unaffected.

## 5. Shared things with a project tier

### 5.1 How tiers combine

Three rules cover every asset below. Pick one per asset:

- **Shadow by name.** A Project item with the same name hides the user item. Both stay on disk, and the UI shows the hidden one. Use it where items are whole units: catalogues, layouts, presets.
- **Merge (union).** Both tiers apply together. Use it where items add up: dictionary words, font folders, instructions.
- **Override (per key).** The Project value replaces the user value. Use it for single settings.

The existing seeding rule already shadows by name: "a script replaces a shipped one of the same name" (`docs/distribution.md:132`). The Project becomes one more layer: shipped < user < Project.

### 5.2 Inventory

| # | Asset | Today: path, format, code | Project tier? | Combine | Notes |
| --- | --- | --- | --- | --- | --- |
| 1 | **Style catalogues** (Style manager) | `<user>/catalogs/<name>.sty`, "Style: " lines, UTF-8 with BOM, CRLF (`src/application/include/hikari/application/style_manager.h:94-115`); folder from `options.catalogDir` (`src/app/composition.cpp:244`); the followed catalogue comes from Script Info `Last Style Storage` (`src/app/style_manager_controller.cpp:325-334`); last one in `styles.lastCatalog` (`src/application/settings.cpp:256`) | **Yes**, `.hikari/catalogs/<name>.sty` | Shadow by catalogue name. Not per-style merge: a catalogue is one unit in Aegisub and legacy, and mixing two tiers' styles in one list would make Save ambiguous | `Last Style Storage` resolves Project first. `convert.styleCatalog` (`settings.cpp:93`) names a catalogue by the same rule |
| 2 | **Font catalogues** | `<user>/catalogs/fonts.toml`, `[[catalog]]` `name`, `fonts` (`docs/distribution.md:127`); `options.fontCatalogDir` (`composition.cpp:245`) | **Yes**, `.hikari/catalogs/fonts.toml` | Shadow by catalogue name | Same file format, so Copy is a table move |
| 3 | **Font files** | No user font folder; `fonts.externalDirectory` names one folder (`settings.cpp:110`); `fontCollector.directory` (`settings.cpp:107`) | **Yes**, `project.toml` `fonts = [...]`, relative folders | Merge: Project folders join the font sources for that window's Documents | Which source wins on a duplicate family is open: `docs/fonts.md:25` forbids inventing "an attachment-always-wins rule" (Q11). Font collector defaults to the Project's first font folder |
| 4 | **Spelling dictionaries** (Hunspell) | `<user>/dictionaries/`: `.dic`/`.aff` pairs and `UserDic.udic` (`docs/distribution.md:111`; `composition.cpp:242-243`); language `editor.dictionaryLanguage` (`settings.cpp:96`) | **Words: yes**, `.hikari/dictionary.txt`, one word a line. **Pairs: no** | Words merge (union); language overrides | "Add to dictionary" gets a tier choice (§5.3) |
| 5 | **Grammar (Harper)** | `<settings dir>/Harper/` with `dictionary.txt`, `workspace-dictionary.txt`, `file-dictionaries`, `ignored-lints` (`src/app/application_grammar.cpp:102-104,165-168`); `grammar.language` and groups (`settings.cpp:338`) | **Yes** | Merge. Harper already has a workspace dictionary slot: point `workspaceDictPath` at `.hikari/dictionary.txt`, shared with #4. Language and groups override | Today `workspace-dictionary.txt` sits in the user folder, so nothing per-project uses it |
| 6 | **Hotkeys** | `<user>/hotkeys.toml`, per-window-context tables (`docs/distribution.md:115-118`) | **No** | — | ADR 0025: hotkeys apply to every window at once (`:29`). VS Code documents keybindings only as `User/keybindings.json` ([VS Code keybindings]). Per-Project keys would make the same key do different things in two windows |
| 7 | **Saved layouts** (WS3) | `<user>/presets/layouts.json` (`src/app/application.cpp:703`; `composition.cpp:246`); per-file arrangement in the ASS `[Hikari]` section (`docs/ux/workspaces.md:44`) | **Yes**, `.hikari/layouts.json` | Shadow by layout name | A team's "Typesetting (ours)" layout. Plugin panel placeholders already survive missing plugins (ADR 0022:41) |
| 8 | **Automation autoload and include** | Shipped `Automation/autoload` → `<user>/automation/autoload` (same-name replaces); include searched user then shipped (`docs/distribution.md:132`); `?user`/`?data` resolve there | **Yes**, `.hikari/automation/autoload/` and `include/`, **trust-gated** | Shadow by file name, continuing shipped < user < Project | Runs code, so it loads only in a trusted Project (§4.8). DependencyControl modules stay user-tier; a Project copy of a DependencyControl-managed module would fight its updater (`docs/automation.md:102`) |
| 9 | **Per-file scripts** | Script Info `Automation Scripts` (`document_scripts.h:11-33`); asked before loading, "Always trust scripts in this folder" (`docs/automation.md:95`; `automation.trustedFolders`, `settings.cpp:315`) | Already per file | — | A trusted Project trusts its own subtree, so scripts named by its files stop asking. One trust list for both |
| 10 | **Script dialog presets** | `[automation.dialogPresets]` in `settings.toml`, keyed by dialog (`settings.cpp:336`; `docs/distribution.md:114`) | **Yes** | Shadow by dialog key and preset name | Their last answers (`automation.dialogAnswers`) stay in `state.json` |
| 11 | **Plugins** | `<user>/plugins/<id>/`, `plugin-data/<id>/`, `plugins.json` with `version`, `enabled`, pinned key, grants, settings (`user_folder.h:37-40`; `docs/plugins.md:58-65`); installed per user (ADR 0022:40) | **Enablement and settings: yes. Install: no** | `[plugins] enabled`/`disabled` override per id; `[plugins."<id>"]` settings override per key | Signatures, pinned keys and marketplace updates (ADR 0022:46-53) assume one install per user. A Project naming a plugin that isn't installed offers "Install from the marketplace". Enabling one is trust-gated. Folder grants stay per user |
| 12 | **Settings** | `<user>/settings.toml` (`docs/distribution.md:114`) | **Allowlisted ids** (§4.5) | Override per key | E.g. `editor.dictionaryLanguage` (`settings.cpp:96`), `grammar.*` (`:338`), `scriptProperties.translation`/`timing` defaults for new files (`:61-63`), `translation.*` |
| 13 | **Shift times presets and profiles** | `shiftTimes.presets` (`settings.cpp:304`), `shiftTimes.profiles` (`:167`) | **Yes** | Shadow by name | |
| 14 | **Timing postprocessor presets** | `postprocessor.presets` (`settings.cpp:306`) | **Yes** | Shadow by name | A group's lead-in and keyframe snaps are a house rule, which suits a Project |
| 15 | **Tag buttons** | `[[editor.tagButton]]` slots after `editor.tagButtons` (`settings.cpp:209`; `docs/distribution.md:114`) | **Maybe** | Override the whole list: slots are positional, so merging by name has no meaning | Q12 |
| 16 | **Drawing shapes and all-tags tags** | `<user>/shapes.toml` `[[shape]]`, `<user>/tags.toml` `[[tag]]` (`docs/distribution.md:125-126`) | **Yes** | Shadow by `name` | A show's logo shape |
| 17 | **Karaoke effect presets** | Bundled only: `src/application/karaoke-presets/*.ass` (`docs/karaoke-presets.md`, "The format") | **Later** | Would shadow by title | There is no user tier yet; a Project tier would come with one |
| 18 | **Karaoke templates** | Template and code Lines inside the Document (`docs/karaoke-presets.md`) | **No** | — | Already travel with the file. A shared song template is a Project `.ass` file the Explorer opens |
| 19 | **Export choices** | `export.choices`: last format and options per document file, at most 100 rows (`docs/export.md:60`; `settings.cpp:357`) | **Project default: yes** | Order: per-file memory > Project default > last export | `[export] format`, options |
| 20 | **QC / lint rules** | None: Fix minor errors' `rules.toml` was retired (#608; `docs/distribution.md:130`). FA20 QC notes (#577, open) load notes from a file | **No tier needed** | — | QC notes files live in the episode folder and show in the Explorer |
| 21 | **Translation** | `translation.*` settings; `scriptProperties.translation` (`settings.cpp:61`); the Honya glossary folder (ADR 0024:80, not built) | **Yes** | Language pair overrides; glossary is Project-only | Q7 places the Honya folder |
| 22 | **AI providers, keys, budgets, permission defaults** | `<user>/ai/providers.json` (`src/app/ai_features.cpp:41`); keys in the OS keychain (ADR 0024:25) | **Never** | — | VS Code and Claude Code keep security keys out of project files ([VS Code settings], [Claude settings]). A Project must not widen AI permissions (§4.5) |
| 23 | **AI instructions** | None today | **Yes**, `AGENTS.md` at the Project root | Merge: a user `AGENTS.md` in the user folder, then the Project's, concatenated root-first | §4.8. The agents.md spec: "The closest AGENTS.md to the edited file wins" ([agents.md]) |
| 24 | **AI threads** | Saved in the user folder (ADR 0024:37) | **No** (stay in the user folder) | — | Tag each thread with its Project path, so the panel lists that Project's threads first |
| 25 | **Recent colours** | `colourPicker.recentColours` (`settings.cpp:83`) | **Optional palette** | Merge: Project palette shown above recents | Low priority; a show's sign colours |
| 26 | **Recent files, window state, last session** | `state.json`, `last-session.json` (`docs/distribution.md:119,136`) | **No** | — | Machine state. Per-Project session in the user folder (§4.7) |

### 5.3 How the UI shows and moves tiers

**Without a Project,** nothing changes: there is only the user tier, and no badges.

**With a Project:**

- **Tier mark.** Each row in a tiered list (Style manager catalogues, font catalogues, layouts, presets, shapes, tags, script dialog presets) carries a small chip, **Project** or **Mine**, with an icon. Colour is never the only cue, as PL4's Q5 decided for plugin panels (brain page `decisions/hikari-pl4-plugin-panels-design`).
- **Grouping.** Lists group under two headings, "This project" first, then "Mine".
- **Shadowed items.** A user item hidden by a Project item of the same name stays visible, muted, with "Hidden by the project's copy" as its tooltip. Choosing it opens it read-only, with Copy.
- **Actions,** in the row's ⋯ menu and context menu:
  - **Copy to project** and **Copy to mine**, which leave the source as it is;
  - **Move to project** and **Move to mine**;
  - a name clash goes through the existing clash review: Replace, Rename or Skip, with apply-to-all (`ClashAction`, `style_manager.h:44`; brain page `decisions/hikari-qt-wave6c-popup-redesign`, "Chained questions become one review window").
- **Saving a new item.** New, Save as and Add to dictionary ask once per list where to save, as a split button: **Save to project** (the default while a Project is open) or **Save to mine**. The choice is remembered per list in `state.json`.
- **Settings.** Settings gets **User** and **Project** tabs, as VS Code's Settings editor does ([VS Code settings]). The Project tab lists only allowlisted ids. A User row that the Project overrides shows "Overridden in this project", with a link to the Project tab.
- **Untrusted Project.** Trust-gated tiers (autoload scripts, plugin enablement) show their rows with a lock and "Trust this folder to use them", which opens the same trust question as `docs/automation.md:95`.
- **Explorer.** `.hikari/` shows as one "Project settings" node, with entries for Catalogues, Layouts, Dictionary, Scripts and `project.toml`.

## 6. Open questions for the owner

1. **File location.** Is `.hikari/project.toml`, with tier folders beside it, right? Or should it be a visible `hikari.toml` at the root (§4.3)?
2. **Comments in `project.toml`.** toml++ drops them when HikariSub writes (`src/ui/toml_file.h:3`). Should the app write only keys it changed, as CF1 does? Or should it never write the file, with Project-tier settings stored apart from the file the user edits?
3. **A file inside a Project folder.** When a user opens `…/show/01/ep01.ass` alone, and a trusted ancestor folder has `.hikari/`, does the window open that Project? If yes, which ancestor counts (nearest `.hikari/`, or a git root, as Codex and Claude Code use)?
4. **A personal override file.** Should there be a `.hikari/project.local.toml`, like Claude Code's `settings.local.json`, for teams that commit `.hikari/`?
5. **Nested folders.** Does a show folder with an `.hikari/` in each episode folder layer the files (Codex's root-to-cwd layering), or does only the opened folder count?
6. **Settings allowlist.** Which ids can a Project set? §4.5 proposes a list. Do any UI preferences (Grid font, video zoom) belong per Project?
7. **Honya's folder.** Is ADR 0024's Honya "project folder" the Project itself (`PROJECT.md`, `GLOSSARY.md` at the root), or a subfolder named in `[translation] glossary`? Does the Script Info pointer stay?
8. **The AI work folder.** Is option A (HA1 folder grants, ADR 0026's boundary kept) acceptable? It means Claude's and Codex's own file tools stay off, and every file access goes through Hikari's tools.
9. **Untrusted Projects and the AI.** Does Hikari AI run in an untrusted Project with `AGENTS.md` loaded, or is it off there, as VS Code's Restricted Mode turns agents off?
10. **Folder handler on Linux.** Should HikariSub list itself as an `inode/directory` handler? That puts it in "Open with" for every folder.
11. **Project fonts versus system and attached fonts.** Which source wins when the family names match (`docs/fonts.md:25`)?
12. **Tag buttons per Project.** Should they have a Project tier, which replaces the whole set?
13. **Moving a tab between Projects.** Is §4.11's "follow the destination window" right?
14. **Recent projects on Home.** Should Home lead with them once the user has opened one?
15. **One trust list.** Should Automation's "Trusted script folders" become the app's single "Trusted folders" list?

## 7. Sources

### External

- Zed trust: <https://zed.dev/docs/worktree-trust>
- Zed windows: <https://zed.dev/docs/windows-and-projects>
- Zed CLI: <https://zed.dev/docs/reference/cli>
- Zed all settings: <https://zed.dev/docs/reference/all-settings>
- Zed tasks: <https://zed.dev/docs/tasks>
- Zed VS Code migration: <https://zed.dev/docs/migrate/vs-code>
- Zed instructions: <https://zed.dev/docs/ai/instructions>
- Zed sandboxing: <https://zed.dev/docs/ai/sandboxing>
- Zed tool permissions: <https://zed.dev/docs/ai/tool-permissions>
- zed tool_permissions.rs: <https://github.com/zed-industries/zed/blob/ba36af2050bd0c0a5b406bad3d329ea8a4d1eb62/crates/agent/src/tools/tool_permissions.rs> (`SensitiveSettingsKind`, `ResolvedProjectPath::SymlinkEscape`, `sensitive_settings_kind`)
- zed read_file_tool.rs: <https://github.com/zed-industries/zed/blob/ba36af2050bd0c0a5b406bad3d329ea8a4d1eb62/crates/agent/src/tools/read_file_tool.rs> (`file_scan_exclusions` and `private_files` refusals, around lines 351-383)
- VS Code workspaces: <https://code.visualstudio.com/docs/editing/workspaces/workspaces>
- VS Code multi-root: <https://code.visualstudio.com/docs/editing/workspaces/multi-root-workspaces>
- VS Code trust: <https://code.visualstudio.com/docs/editing/workspaces/workspace-trust>
- VS Code settings: <https://code.visualstudio.com/docs/configure/settings>
- VS Code CLI: <https://code.visualstudio.com/docs/configure/command-line>
- VS Code keybindings: <https://code.visualstudio.com/docs/configure/keybindings>
- VS Code UI: <https://code.visualstudio.com/docs/getstarted/userinterface>
- VS Code tips: <https://code.visualstudio.com/docs/getstarted/tips-and-tricks>
- VS Code agent security: <https://code.visualstudio.com/docs/agents/security>
- VS Code instructions: <https://code.visualstudio.com/docs/agent-customization/custom-instructions>
- Claude settings: <https://code.claude.com/docs/en/settings>
- Claude memory: <https://code.claude.com/docs/en/memory>
- Claude permissions: <https://code.claude.com/docs/en/permissions>
- Codex config: <https://learn.chatgpt.com/docs/config-file/config-basic> (redirected from developers.openai.com/codex/config-basic)
- Codex AGENTS.md: <https://learn.chatgpt.com/docs/agent-configuration/agents-md> (redirected from developers.openai.com/codex/guides/agents-md)
- Codex sandbox: <https://learn.chatgpt.com/docs/sandboxing> (redirected from developers.openai.com/codex/concepts/sandboxing)
- agents.md: <https://agents.md/>
- TOML 1.0: <https://toml.io/en/v1.0.0>
- Aegisub options: <https://aegisub.org/docs/latest/options/>
- Aegisub subtitle_format_ass.cpp: <https://github.com/TypesettingTools/Aegisub/blob/4107d864ced92ae09d1c1a1ef2423eba34c17aa2/src/subtitle_format_ass.cpp#L92-L101>
- Aegisub auto4_base.cpp: <https://github.com/TypesettingTools/Aegisub/blob/4107d864ced92ae09d1c1a1ef2423eba34c17aa2/src/auto4_base.cpp#L380-L474>
- Aegisub default_config.json: <https://github.com/TypesettingTools/Aegisub/blob/4107d864ced92ae09d1c1a1ef2423eba34c17aa2/src/libresrc/default_config.json#L303-L307>
- Aegisub path.cpp: <https://github.com/TypesettingTools/Aegisub/blob/4107d864ced92ae09d1c1a1ef2423eba34c17aa2/libaegisub/common/path.cpp#L28-L36>
- Aegisub #31: <https://github.com/TypesettingTools/Aegisub/issues/31>
- Kaleido template: <https://github.com/Kaleido-subs/project-template/tree/93c594eb9ef6d6227efe7b1ddae44289dd562d7c>
- Kaleido .gitignore: <https://github.com/Kaleido-subs/project-template/blob/93c594eb9ef6d6227efe7b1ddae44289dd562d7c/.gitignore>
- Kaleido clean_project_garbage.sh: <https://github.com/Kaleido-subs/project-template/blob/93c594eb9ef6d6227efe7b1ddae44289dd562d7c/scripts/clean_project_garbage.sh>
- SubKt: <https://github.com/Myaamori/SubKt>
- SE help: <https://www.nikse.dk/subtitleedit/help>
- SE settings discussion: <https://github.com/SubtitleEdit/subtitleedit/discussions/10355>

### Repository (main `05a69206f` unless noted)

- `CONTEXT.md`
- `docs/adr/0020-per-document-tab-layouts.md`, `0022-hikarisub-plugins.md`, `0024-hikari-ai.md`, `0025-multiple-windows.md`, `0026-hikari-ai-runs-in-a-typescript-sidecar-on-bun.md`; `0029-the-ai-sidecar-runs-in-an-os-sandbox.md` on branch `w8-sb1` (`6626a5b1b`, PR #759)
- `docs/distribution.md`, `docs/automation.md`, `docs/api.md`, `docs/plugins.md`, `docs/export.md`, `docs/fonts.md`, `docs/karaoke-presets.md`, `docs/ux/workspaces.md`
- `docs/research/hikari-ai-integration.md`, `docs/research/agent-sdk-hosting.md`, `docs/research/hikari-plugins.md`
- `src/app/include/hikari/app/user_folder.h`, `src/app/composition.cpp`, `src/app/include/hikari/app/single_instance.h`, `src/app/application.cpp`, `src/app/application_grammar.cpp`, `src/app/ai_features.cpp`, `src/app/style_manager_controller.cpp`
- `src/application/include/hikari/application/settings.h`, `src/application/settings.cpp`, `session_file.h`, `associated_files.h`, `media_association.h`, `recent_files.h`, `src/application/recent_files.cpp`, `style_manager.h`, `document_scripts.h`, `owned_file.h`
- `src/ui/settings_files.h`, `src/ui/toml_file.h`, `src/ui/workspace_layout.h`, `src/ui/HomePage.qml`
- `src/plugins/include/hikari/plugins/registry.h`
- `src/backends/ai_agent_broker.cpp` and `src/backends/include/hikari/backends/ai_agent_broker.h` on `w8-sb1`
- `packaging/linux/io.github.altqx.HikariSub.desktop.in`, `packaging/linux/mime/hikarisub.xml`, `packaging/windows/hikarisub.nsi`
- `vcpkg.json`
- Cards: CF1 #488 (design note), CF3 #490, MW4 #628, #601, SB1 #651, FA20 #577, SH2 #221, UD1 #362, FR1 #363
- Honya `README.md` at `83b6209` (`/home/altq/Work/honya-cli`)
- Brain pages: `decisions/hikari-pl5-plugin-files-network`, `decisions/hikari-pl4-plugin-panels-design`, `decisions/hikari-qt-wave6c-popup-redesign`, `inbox/2026-10-28-9e5f7327` (CF1 config decisions)

[Zed trust]: https://zed.dev/docs/worktree-trust
[Zed windows]: https://zed.dev/docs/windows-and-projects
[Zed CLI]: https://zed.dev/docs/reference/cli
[Zed all settings]: https://zed.dev/docs/reference/all-settings
[Zed tasks]: https://zed.dev/docs/tasks
[Zed VS Code migration]: https://zed.dev/docs/migrate/vs-code
[Zed instructions]: https://zed.dev/docs/ai/instructions
[Zed sandboxing]: https://zed.dev/docs/ai/sandboxing
[Zed tool permissions]: https://zed.dev/docs/ai/tool-permissions
[zed tool_permissions.rs]: https://github.com/zed-industries/zed/blob/ba36af2050bd0c0a5b406bad3d329ea8a4d1eb62/crates/agent/src/tools/tool_permissions.rs
[zed read_file_tool.rs]: https://github.com/zed-industries/zed/blob/ba36af2050bd0c0a5b406bad3d329ea8a4d1eb62/crates/agent/src/tools/read_file_tool.rs
[VS Code workspaces]: https://code.visualstudio.com/docs/editing/workspaces/workspaces
[VS Code multi-root]: https://code.visualstudio.com/docs/editing/workspaces/multi-root-workspaces
[VS Code trust]: https://code.visualstudio.com/docs/editing/workspaces/workspace-trust
[VS Code settings]: https://code.visualstudio.com/docs/configure/settings
[VS Code CLI]: https://code.visualstudio.com/docs/configure/command-line
[VS Code keybindings]: https://code.visualstudio.com/docs/configure/keybindings
[VS Code UI]: https://code.visualstudio.com/docs/getstarted/userinterface
[VS Code tips]: https://code.visualstudio.com/docs/getstarted/tips-and-tricks
[VS Code agent security]: https://code.visualstudio.com/docs/agents/security
[VS Code instructions]: https://code.visualstudio.com/docs/agent-customization/custom-instructions
[Claude settings]: https://code.claude.com/docs/en/settings
[Claude memory]: https://code.claude.com/docs/en/memory
[Claude permissions]: https://code.claude.com/docs/en/permissions
[Codex config]: https://learn.chatgpt.com/docs/config-file/config-basic
[Codex AGENTS.md]: https://learn.chatgpt.com/docs/agent-configuration/agents-md
[Codex sandbox]: https://learn.chatgpt.com/docs/sandboxing
[agents.md]: https://agents.md/
[TOML 1.0]: https://toml.io/en/v1.0.0
[Aegisub options]: https://aegisub.org/docs/latest/options/
[Aegisub subtitle_format_ass.cpp]: https://github.com/TypesettingTools/Aegisub/blob/4107d864ced92ae09d1c1a1ef2423eba34c17aa2/src/subtitle_format_ass.cpp#L92-L101
[Aegisub auto4_base.cpp]: https://github.com/TypesettingTools/Aegisub/blob/4107d864ced92ae09d1c1a1ef2423eba34c17aa2/src/auto4_base.cpp#L380-L474
[Aegisub default_config.json]: https://github.com/TypesettingTools/Aegisub/blob/4107d864ced92ae09d1c1a1ef2423eba34c17aa2/src/libresrc/default_config.json#L303-L307
[Aegisub path.cpp]: https://github.com/TypesettingTools/Aegisub/blob/4107d864ced92ae09d1c1a1ef2423eba34c17aa2/libaegisub/common/path.cpp#L28-L36
[Aegisub #31]: https://github.com/TypesettingTools/Aegisub/issues/31
[Kaleido template]: https://github.com/Kaleido-subs/project-template/tree/93c594eb9ef6d6227efe7b1ddae44289dd562d7c
[Kaleido .gitignore]: https://github.com/Kaleido-subs/project-template/blob/93c594eb9ef6d6227efe7b1ddae44289dd562d7c/.gitignore
[Kaleido clean_project_garbage.sh]: https://github.com/Kaleido-subs/project-template/blob/93c594eb9ef6d6227efe7b1ddae44289dd562d7c/scripts/clean_project_garbage.sh
[SubKt]: https://github.com/Myaamori/SubKt
[SE help]: https://www.nikse.dk/subtitleedit/help
[SE settings discussion]: https://github.com/SubtitleEdit/subtitleedit/discussions/10355
