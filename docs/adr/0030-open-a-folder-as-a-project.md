---
status: accepted
amends: 0024, 0025, 0026
---

# Open a folder as a Project

On 2026-11-04 altqx settled Projects in two grilling rounds, answering "yes to all" to Q82–Q109 ([#766](https://github.com/altqx/hikari/issues/766), card [PJ0 #769](https://github.com/altqx/hikari/issues/769)). HikariSub opens a folder the way Zed and VS Code do: the folder becomes the window's context, the Explorer shows it, Hikari AI works inside it, and shared things such as Style catalogues can be saved to it as well as to the user folder. The prior art and the options are in the [project folders research note](../research/project-folders.md); where it differs from this record, this record wins.

## What a Project is

- **A Project is a folder opened in a window,** with an optional project file. Any folder can be one, a whole show or a single episode, and both work the same way.
- **The names stay apart.** A Workspace is still a tab's panel arrangement ([ADR 0020](0020-per-document-tab-layouts.md)), and the Session is still the set of open windows and tabs. A Project is neither.
- **A subtitle file opened by itself has no Project,** as today.
- **One Project per window.** The title bar and Home show its name. A window's tabs may still hold files from outside its folder.

## The `.hikari/` folder

- **The project file is `.hikari/project.toml`, and it is optional.** HikariSub writes it the first time a Project-level setting changes. Opening a folder without one adds no files.
- **Beside it** are `catalogs/`, `layouts.json`, `dictionary.txt` and `automation/`.
- **Its tables and keys are `settings.toml`'s,** plus `version = 1`, so a line can move between the two files. This replaces the research note's `[settings]` table (coordinator, 2026-11-10, PJ3 #771).
- **Hand edits survive.** HikariSub writes only the keys it changed, so comments elsewhere in the file stay.
- **`.hikari/project.local.toml`** is an optional file for personal overrides that a team doesn't share. HikariSub never writes it.
- **One `.hikari/` applies.** Nested folders don't layer in v1.
- **Nothing machine-specific goes in `.hikari/`:** no absolute paths, sessions or window state.

## Opening a Project

- **Home** gets **Open folder** next to Open subtitles. Once the user has opened a Project, Home leads with Recent projects, up to 20, above Recent files.
- **The command line:**
  - `hikarisub <folder>` opens the folder as a Project. A folder must never open as video, as it does today (`src/application/recent_files.cpp:107-126`).
  - `hikarisub <folder> <file>` opens both.
  - `-n`/`--new-window` and `-r`/`--reuse-window` choose the window.
- **Which window.** A folder opens in a new window unless the current window is empty. A Project that is already open focuses its window; two windows on one Project aren't allowed.
- **A file inside a Project folder.** When a file is opened by itself, the nearest folder above it with a `.hikari/` counts. If that folder is trusted, it opens as the window's Project. If not, a bar offers **Open project**.
- **The file manager.** On Windows, "Open with HikariSub" is a folder verb among the installer's optional associations. On Linux it is a desktop action; HikariSub doesn't register as an `inode/directory` handler.

## Windows and the Session

- **The Project is per-window state,** a new row on the per-window side of [ADR 0025](0025-multiple-windows.md)'s table.
- **A tab moved to another window** follows that window's Project, and a toast names it. Moving still never saves, reloads or reviews the Document.
- **One app Session** records each window's Project with its geometry and tabs.
- **Each Project's last tabs** are kept in the user folder, keyed by the Project, so reopening the Project offers them.

## The Explorer

- **A dockable panel shows the Project's folder tree.** It docks left of the Grid in the Editing preset when a Project is open.
- **What it lists.** By default, the files HikariSub uses: subtitles, video, audio, fonts, keyframes, timecodes and chapters. A toggle shows all files. Hidden files are off. `[explorer] exclude` defaults to `*.lwi` and `*.ffindex`.
- **Views.** A "group by name" view shows files that share a name together. `.hikari/` shows as one **Project settings** node.
- **Double-click** opens a subtitle file in a tab; a video or audio file becomes the current tab's media.
- **Right-click:** New subtitle file, Rename, Move to trash, and Reveal in the file manager.
- **It follows changes on disk live.**

The Explorer is core, not a plugin ([ADR 0022](0022-hikarisub-plugins.md), "Plugin first"): it is the main surface of a default way to open the app, and every Project user needs it.

## Settings layering

- **Default < User < Project, per key,** for an allowlist of settings.
- **A Project can never set:** privacy, analytics, updates, AI providers and permissions, plugin network, trusted folders, the theme and UI fonts, or hotkeys. Display preferences stay the user's.
- **Settings gets two tabs, Yours and Project,** in a Project window ([#771](https://github.com/altqx/hikari/issues/771)).
  - The Project tab lists only the pages and rows a Project can set. Each row says "Uses yours", "Set in this project" or "Set in project.local.toml"; a row from project.local.toml is read-only.
  - "Use yours" removes the key from project.toml. Apply writes only the keys that changed, and only to project.toml.
  - A Yours row the Project sets says "Overridden in this project".
- **The allowlist** is the dictionary language and spelling switch, grammar, the Script Info values written on save ([#892](https://github.com/altqx/hikari/issues/892)), and moving tags from the original in translation mode. Lists are replaced, not merged.
- **A setting that widens** what a Project may do applies only once its folder is trusted. None of today's allowlisted settings widens.

## Shared things with a Project tier

**With a Project tier:**

- Style catalogues, font catalogues and font folders;
- dictionary words, one list shared by spelling and Harper;
- saved layouts;
- autoload scripts, and plugin on/off with plugin settings, both trust-gated (plugins are still installed per user);
- script dialog presets;
- Shift times and postprocessor presets;
- drawing shapes and tags;
- the default export format;
- the translation pair and glossary;
- `AGENTS.md`.

**User-only:** hotkeys; AI providers, keys and budgets; AI threads, which are tagged with their Project; recent files and window state; Hunspell dictionary files. **Later:** karaoke presets and a colour palette. Tag buttons have no Project tier in v1.

**How the tiers combine:**

- Catalogues, layouts and presets are hidden by name: a Project item hides the user's item of the same name, which shows muted.
- Dictionary words, font folders and `AGENTS.md` merge.
- Settings override per key.

**In the UI:**

- Each row has a **Project** or **Mine** chip with an icon. Lists group "This project" first, then "Mine".
- Rows offer Copy to and Move to either tier. A name clash goes through the Replace / Rename / Skip review.
- While a Project is open, New and Save as default to **Save to project**. They are split buttons, and the choice is remembered per list.

**Fonts.** `fonts = ["fonts", "../common/fonts"]` in `project.toml` names folders relative to the Project folder; `project.local.toml`'s list replaces it. Their fonts join that window's Documents after the fonts attached to the subtitle file and before the Fonts folder in Settings, so a Project font wins over a system or Fonts folder font of the same family. Attached fonts keep today's rules. The Font collector starts in the Project's first font folder (#774).

## Trust

- **One Trusted folders list** replaces Automation's Trusted script folders. It sits on the General page and is kept as `trust.folders`; the old `automation.trustedFolders` moves there once (owner, 2026-11-10).
- **An untrusted Project** still applies its styles, dictionaries, layouts and settings that only restrict. It loads no scripts, changes no plugin on/off, and doesn't widen AI permissions.
- **The trust question** comes on first open only when `.hikari/` holds scripts or plugin settings. It is a bar across the top of that window, with Trust folder (which adds the folder to Trusted folders) and Not now, which isn't remembered ([#771](https://github.com/altqx/hikari/issues/771), owner Q224).

## Hikari AI in a Project

- **The window's Project folder is granted to its AI** through Hikari's file tools, as a Hikari API folder grant.
  - Reads need no prompt.
  - Writes to anything but open Documents stage in the review. Writes into `.hikari/` always ask.
- **Never sent:** paths matching `[ai] exclude` (by default `.env*`, `*.key`, `.git/` and `project.local.toml`), the bytes of media files, and anything a symlink reaches outside the Project.
- **Claude's and Codex's built-in file tools stay off.** The sidecar's working directory stays its private folder ([ADR 0026](0026-hikari-ai-runs-in-a-typescript-sidecar-on-bun.md)).
- **Instructions.** The user's `AGENTS.md` and then the Project root's are joined in that order and given to every agent.
- **In an untrusted Project** the AI still runs. A banner shows when the Project's `AGENTS.md` is loaded.
- **Honya's project folder is the Project** ([ADR 0024](0024-hikari-ai.md), "Translation"): `PROJECT.md` and `GLOSSARY.md` at its root, or the subfolder `[translation] glossary` names. The Script Info pointer stays only for files outside a Project.

## Core and plugins

- **Core:** the Project model and file, settings layering, trust, the Explorer, the tiers and the AI folder scope.
- **Plugins:** episode rename, release naming and muxing, templates and QC packs.
- **The Hikari API** gains `project.info`, a `project.changed` event and a `project` file place for plugins. These are add-only changes ([ADR 0022](0022-hikarisub-plugins.md)).

## What changes

- `CONTEXT.md` gains Project, Project file and Project tier. The Session no longer lists "Project file" under _Avoid_.
- ADRs 0024, 0025 and 0026 each get an amendment line.
- [automation.md](../automation.md)'s Trusted script folders becomes Trusted folders when [PJ3 #771](https://github.com/altqx/hikari/issues/771) lands.

## Order

The cards run one at a time, each after the one before it merges:

1. [PJ2 #770](https://github.com/altqx/hikari/issues/770): open a folder as a Project, starting with the folder-as-video fix.
2. [PJ3 #771](https://github.com/altqx/hikari/issues/771): the project file, settings layering and trusted folders.
3. [PJ4 #772](https://github.com/altqx/hikari/issues/772): the Explorer panel.
4. [PJ5 #773](https://github.com/altqx/hikari/issues/773): Project tiers for shared things.
5. [PJ6 #774](https://github.com/altqx/hikari/issues/774): Project font folders.
6. [PJ7 #775](https://github.com/altqx/hikari/issues/775): Hikari AI in a Project.
7. [PJ8 #776](https://github.com/altqx/hikari/issues/776): Project scripts and plugins.
8. [PJ9 #777](https://github.com/altqx/hikari/issues/777): Open with HikariSub on folders.
