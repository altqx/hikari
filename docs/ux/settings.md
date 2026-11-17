# Settings

One Settings window for everything, as in VS Code ([#1238](https://github.com/altqx/hikari/issues/1238)). It has a search box, a contents tree, Yours and Project tabs over the same tree, and a Plugins node. Changes apply at once. It is drawn in [Quiet](visual-language.md).

**Where the decisions come from.**

- The owner said yes to Q638–Q649 on 2026-11-17, on tracking [#1209](https://github.com/altqx/hikari/issues/1209).
- The research with sources is [unified-settings.md](../research/unified-settings.md).
- The plugin side (manifest keys, `settings.open` and the `settings.list` fields) is fixed in [ADR 0034](../adr/0034-plugins-grow-into-an-extension-system.md) and built in #1230. This document only says how it looks and behaves.

The app doesn't change until the code card lands after the beta.4 tag. Parts marked **Proposed** wait for the owner.

## The window

One DG1 window ([visual-language.md](visual-language.md#hikari-theme-layer-k2)), titled "Settings", resizable and modeless. It is per shell window, because the Project tab is that window's Project. Opening it again raises it. It first opens at 960 × 680 and can't shrink below 720 × 480.

| Region | Size | Holds |
| --- | --- | --- |
| Search row | 40 high, on `panel`, 12 inside | The search field across the width, the Filter button (a tool button with a menu), then **Open settings file** at the trailing end |
| Tabs | 30 high, only in a Project window | **Yours** and **Project: <folder name>**, as a tab strip |
| Contents | 220 wide, on `bg` | The tree (see [The contents tree](#the-contents-tree)) |
| Rows | The rest, on `panel`, 24 inside | Every page's rows as one scrolling list, under page and section headings |
| Footer | DG1's | **Reset all…** at the leading edge, **Close** at the trailing edge, the default button |

Splits are 1-unit `line`. There's no OK, Cancel or Apply (Q638).

## Changes apply at once

- **Check boxes, choices, sliders and pickers** apply as they change.
- **A text or number field** applies on Enter, when it loses focus, or after the user stops typing for 500 ms. Esc while typing puts back the value the field had when it took focus.
- **A value it can't take** shows a `danger` boundary and one `danger` line under the field, with the warning icon. The setting keeps its last good value.
- **Appearance** previews and applies as one step. There's no preview to take back.
- **Destructive actions** (Remove key, Delete provider, Delete MCP server, Reset all) keep their message boxes.
- **A setting that takes effect only after a restart** says "Applies after a restart" in `muted` under the row. Its value is saved at once.

Nothing is staged, so closing the window loses nothing. Each change is written to its file at once.

## Rows

A row is the setting's label in `text`, its description under it in 12-unit `muted` (wrapping), and its control, at the label's end for a check box or under the description otherwise. Rows are 12 apart and sections 24, as in the Style manager.

- **Reset.** While a row's value differs from where it would otherwise come from, a reset tool button (K1's `restore-default`, 16 units) shows at the label's end, as in the Style manager. On the Yours tab it goes back to the default. On the Project tab it removes the key from `project.toml`, so the row uses yours again; its tooltip says "Use yours".
- **The row's ⋯ menu,** shown on hover and on focus within the row: Reset · Copy setting id · Copy link · Open settings file.
- **Hover** is the `hover` fill across the row. The accent is never used for a changed row.
- **Changed** shows in `muted` text after the label, so it isn't told by the reset button alone.

### Set elsewhere

Each row says where else it is set, as one `muted` line under the description with a link:

| Tab | The row is | It says |
| --- | --- | --- |
| Yours | Set by this window's Project | "Set in this project" (links to the row on the Project tab) |
| Project | Not set in the Project | "Uses yours" (links to the row on the Yours tab) |
| Project | Set in `project.toml` | "Set in this project" |
| Project | Set in `project.local.toml` | "Set in project.local.toml". The row is read-only, and its ⋯ menu opens that file |
| Project | A setting that widens, in an untrusted Project | "Waits for trust", with the warning icon, linking to Trusted folders. The value is shown but doesn't apply yet ([ADR 0030](../adr/0030-open-a-folder-as-a-project.md)) |
| Either | A plugin setting the Project overrides in `[plugins."<id>"]` | The same lines as a built-in setting |

## Yours and Project

The two tabs (Q640) show the same tree and the same search.

- **Yours** shows every setting.
- **Project** shows only the settings a Project can set, and the tree shows only the pages and sections that have them. The tab is there only in a window with a Project.
- Switching tabs keeps the search and the scroll position as near as it can.
- "Yours" is the word for the user's tier everywhere: here, the tier chips ("Yours"), their menus ("Copy to yours", "Save to yours") and [CONTEXT.md](../../CONTEXT.md). Stored values and icon roles keep `mine`.

**Script properties isn't a tab.** The Script properties page holds the defaults written into Script Info on save. It starts with one row: "To change the open file's own values, use **Script properties…**". The button opens the Script properties dialog for the editing target. With no ASS Document open, it is disabled, and its tooltip says why.

## The contents tree

The tree lists the pages, each page's sections as its children, then **Plugins** with one child per plugin with settings.

- Pages start folded, except the one in view. A click on a node scrolls the rows to its heading. The tree follows the scroll: the node whose heading is at the top of the rows is `select`.
- Rows are 24 high, indented 12 per level, with K1's page icons on the pages only.
- While searching, the tree shows only the nodes with matches, unfolded, each with its count in `muted` at the trailing end.
- The tree's right-click menu on a page has **Reset page…** (today's Reset page).

### How today's pages map into the tree

The 14 Settings pages keep their names and order. Their sections become tree children. The Project tab's 5 pages fold into the same nodes.

| Node | Children (sections) | From today | On the Project tab |
| --- | --- | --- | --- |
| General | Language, Tabs, Saving, Notices, Folders, Trusted folders, Legacy | General | — |
| Appearance | Theme, Accent colour, UI colours, Font, Window | Appearance | — |
| Line editor | Display, Editing, Translation mode, Tag buttons | Line editor | Translation mode: source and target language, the glossary, tags from original (from Project › Translation) |
| Editing | Cycle values | Editing | — |
| Spelling and grammar | Spelling, Grammar, Rule groups | Spelling and grammar | Spelling, Grammar, Rule groups (from Project › Spelling and grammar) |
| Grid | Active line, Columns, Counting, Splitting, New lines | Grid | — |
| Video | Playback, Subtitles on video, Decoding, View, Visual tools, Script resolution | Video | — |
| Audio | Output, Playback, Display, Snapping, Karaoke, Cache | Audio | — |
| Conversion | Style, Timing, ASS, **Export** (new) | Conversion | Export: the export format (from Project › Export) |
| Script properties | Credits | Script properties | Credits (from Project › Script properties) |
| Automation | Files, Debugging, Script dialogs, Kept answers and presets | Automation | — |
| Shortcuts | — (one table, see [Shortcuts](#shortcuts)) | Shortcuts | — |
| AI | AI features, Providers, Models, Web search, External agents, MCP, Agent, Skills, Inline actions, AI components, Privacy | AI | Models: the translator and reviewer models; Privacy: the paths never sent to AI; **Keys** (from Project › AI) |
| Updates and privacy | Checking, Installing, **Plugins** (new), Privacy | Updates and privacy | — |
| Plugins | One child per plugin | The plugin manager's Settings forms | Plugins whose settings a Project can set |

- **Translation and Export** leave the Project tab as pages of their own. Their rows join the sections above, and the Yours tab shows them too, as the user's defaults.
- **Updates and privacy › Plugins** holds "Check for plugin updates" and "Install updates automatically", which today are only in the plugin manager. The plugin manager keeps them and links here.
- **Bespoke sections stay bespoke.** That covers the theme cards, the tag buttons, the cycle values, the AI providers and the Shortcuts table. Each registers a title and keywords, so search finds the section even where it can't find a single row.
- **A setting with no control** yet is a row showing its value read-only, with **Open settings file**.

### Plugins

Plugin settings live only under Settings › Plugins › *plugin name* (Q643). Plugins can't add rows to the app's own pages.

- **The plugin's page** is headed by its icon and name, with **Manage plugin…** at the trailing end, which opens its details in the plugin manager. The plugin manager's Settings opens this page.
- **Its sections** are the plugin's groups, in its order.
- **The rows** use the same controls as the app's:
  - a choice shows its options' titles;
  - a multiline string is a 3-line field;
  - a list is an editable list;
  - a secret shows dots, with Show and Clear.
- **A deprecated setting** shows only while it is set. It has the warning icon and its message in `muted`.
- **A plugin that is turned off** keeps its page, with one `muted` line at the top: "Line tools is turned off. Its settings apply when it's on."
- **No plugins with settings:** with none, the Plugins node says "No plugin has settings." with **Plugins…**.

## Search

The search field's placeholder is "Search settings". Typing filters the rows and the tree as it goes. Matched words are semibold, never colour alone.

**What it matches.** Labels, descriptions, keywords, setting ids (`grid.autoHideColumns`), and the names of sections, pages and plugins. Letters match at word starts first, then anywhere.

**Shortcuts in the results.** Search also matches command names (Q648). When commands match, the results end with a **Shortcuts** group: up to 5 commands with their keys, then "Show all 12 in Shortcuts". That opens the Shortcuts page with the search filled in.

**Filters.** The Filter button's menu puts a token in the field. Tokens can also be typed, and Backspace removes a token whole.

- **Modified** (`@modified`): only rows that are changed on this tab.
- **Plugin ›** *name* (`@plugin:<plugin id>`): only that plugin's settings.

Tokens combine with each other and with the words typed.

**No results.** With no match, the rows show one `muted` line: "No settings match "kara"." With **Modified** on, it says "Nothing is changed here."

## Shortcuts

Shortcuts stay a page in the window (Q648), with today's table: command, key, area, and the "taken by <owner>" line from ADR 0034. Its tool row gains two filters beside the area filter and the page's own search:

- **Source**: All · Built-in · Yours · Plugin · Macro. "Yours" is a key the user set or cleared. It replaces today's "Set".
- **Conflicts**: a toggle that keeps only keys bound to more than one command where their contexts overlap, plus plugin defaults that were dropped because the key is taken. Each such row says what it clashes with, in `muted`, with a link to the other row.

Open settings file on this page opens `hotkeys.toml`.

## Open settings file

**Open settings file** (Q647) opens, in the system's editor, the file that holds what the window shows:

- the Yours tab: `settings.toml`;
- the Shortcuts page: `hotkeys.toml`;
- a plugin's page: `plugins.json`;
- the Project tab: `.hikari/project.toml`.

There's no text editor in the app.

- **Hand edits show up while HikariSub runs.** HikariSub watches the file. A saved edit lands in the window within a second, with no reload.
- **A file it can't read** keeps the last good values. An inline bar across the top of the rows says "settings.toml has an error on line 12. HikariSub keeps the last values it could read.", with **Open settings file**. HikariSub doesn't write that file until it reads again, so a change in the window waits; the bar says so.

## Setting links

`hikarisub://settings/<id>` (Q644) opens the window and shows one setting. A link never changes a value.

- **Ids:** a built-in setting's id (`grid.autoHideColumns`), or `plugin.<plugin id>.<setting id>` for a plugin's.
- **The tab:** `?scope=project` picks the Project tab.
- **No id:** `hikarisub://settings` alone opens the window where it was.

**How a link lands.**

1. The window opens for the active shell window, or is raised. The search is cleared, and the tab is picked.
2. The row's page and section unfold in the tree, and the node is `select`.
3. The row scrolls to the top third of the list.
4. The row is highlighted: the `select` fill, fading to none over 2 seconds. In high contrast, a 1-unit `accent` boundary that goes with it.
5. Keyboard focus goes to the row's control, with its focus ring.

**When it can't land.**

- **An unknown id** waits for plugins to finish loading, then opens the window with an inline note: "No setting "grid.foo"."
- **`scope=project` in a window without a Project**, or for a setting a Project can't set, opens the Yours tab with an inline note saying why.

**Where links come from.**

- Copy link in a row's ⋯ menu.
- The command palette (#1236): **Settings**, **Project settings** and **Shortcuts** are commands, and any setting link can be run from it.
- Notices that name a setting ("Settings › Video › Decoding").
- The Explorer's Project settings row.
- The docs, the AI and plugins (`settings.open`).

## Keyboard and accessibility

- **Opening:** the window opens with focus in the search field.
- **Moving between regions:** F6 cycles the search row, the tabs, the tree, the rows and the footer. Tab moves through controls in order.
- **Ctrl+F** goes to the search field from anywhere in the window.
- **Esc** clears the search field first, then closes the window.
- **The tree** is a tree: arrows move, Right unfolds, Left folds, and Enter scrolls the rows to the node.
- **Accessible names:**
  - Each row's control is named by its label and described by its description and its set-elsewhere line.
  - The reset button is named "Reset <label>".
  - The ⋯ button is named "<label> options".
- **Announcements:**
  - After typing settles, the result count is announced: "14 settings match".
  - A link's landing announces the row.
  - A value the field can't take is announced as an alert.
- **Themes:** everything holds in all four themes. "Changed", the set-elsewhere lines and the highlight never rely on colour alone.

## Not in 0.1.0

Q649 keeps these out; each has its own future card:

- named settings profiles (#1240);
- managed and policy settings (#1241);
- settings sync (#1242);
- per-language settings (#1243).

## Open questions for the owner

1. **Translation and Export.** Should they fold into Line editor › Translation mode and a new Conversion › Export section, with the Yours tab showing those rows too as the user's defaults?
2. **When text applies.** Should a text field apply after 500 ms without typing, as well as on Enter and on leaving it, or only on Enter and on leaving it?
3. **Reset all…** Should it stay in the footer, keeping today's rule that shortcuts and appearance aren't touched?
4. **Plugin update settings.** Should "Check for plugin updates" and "Install updates automatically" appear under Updates and privacy › Plugins as well as in the plugin manager?
