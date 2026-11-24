# Home page

The Home page for 0.1.0-beta.6 ([#1205](https://github.com/altqx/hikari/issues/1205)). Home is the tab a window shows with no document. This document says what is on it, in which states, and how it looks in [Quiet](visual-language.md). The first-run window that comes before it is [getting-started.md](getting-started.md).

The owner chose the **Desk** direction from the gate board on 2026-11-24 (Q810), took every recommendation on it (Q812), and asked for the full redesign in beta.6 (Q813); see [Decisions](#decisions). Home sits under the title-bar row, where the Document tabs are ([visual-language.md](visual-language.md), "one row of chrome"); its tab reads **Home**. The app doesn't change until the code card lands.

## Layout

The page is centred, at most 1120 wide, with 24 around it on `bg`. At 1000 wide or more it has two columns 20 apart:

| Column | Width | Blocks, top to bottom |
| --- | --- | --- |
| Work | The rest | [Header](#header), [Actions](#actions), [Continue](#continue), [Recent](#recent) |
| News | 320 | [What's New](#whats-new), [Plugins](#plugins), [AI](#ai) |

Blocks are 16 apart, and the News cards 12. Recent fills the height left and scrolls inside itself; nothing else scrolls.

Under 1000 wide the page is one column. The News cards go between Continue and Recent in one row, with What's New first and without its picture. The row is at most 200 high and its cards scroll inside themselves.

Cards are `panel` with a 1-unit `line` boundary and radius 6. Card titles are 13 semibold, and their text is 12.5 `muted`.

## Header

**HikariSub** in 20 semibold, then the version (`0.1.0-beta.6`) in an 11.5 `muted` pill with a `line` boundary. At the trailing end is the **Quick Open field**, 360 wide: a `field` box with a `control` boundary, the `search` glyph, "Search files, projects and commands" in `muted`, and **Ctrl+P** as a key cap.

The field is a button. A click, Enter or Space opens [Quick Open](command-palette.md#quick-open) (`GLOBAL_QUICK_OPEN`, 5343). Typing a character while the field has focus opens Quick Open with that character typed, so `>` opens the command palette. The key cap shows the command's current key, and the cap hides when the command has no key.

## Actions

Four tiles and **More**, in one row 8 apart. Each tile is a `panel` box 12 inside, with the glyph at 20, the name in semibold and the current key in 11.5 `muted`.

| Tile | Glyph | Command | Key today |
| --- | --- | --- | --- |
| **New** | `new-subtitle-file` | New subtitles in a new tab (`GLOBAL_ADD_PAGE`, 5301; tooltip as today) | Ctrl+T |
| **Open…** | `open-subtitles` | Open subtitles… (`GLOBAL_OPEN_SUBS`, 5100) | Ctrl+O |
| **Open folder…** | `open-project` | Open folder… (`GLOBAL_OPEN_FOLDER`, 5255) | none |
| **Open video…** | `open-video` | Open video… (`GLOBAL_OPEN_VIDEO`, 5101) | Ctrl+Shift+O |

- **New** is the default: it has a 1-unit `accent` boundary and its glyph is `accent`. In high contrast the boundary is 2 units.
- **A tile with no key** leaves the key line out; the tile keeps its height.
- **More** is a narrow tile with ⋯ and "More". It opens a menu at the tile: **Import text…** (`GLOBAL_IMPORT_TEXT`, 5375), **Import settings…** (`GLOBAL_IMPORT_LEGACY_SETTINGS`, 5223), **Load session from file…** (`GLOBAL_LOAD_EXTERNAL_SESSION`, 5053), a separator, and **New files use: *catalogue*…**. That last item opens Settings at `new.styleCatalog` and reads "New files use no catalogue…" when the setting is empty.

## Continue

One card for everything that reopens work. It replaces today's startup offer card, Last session card and Project card (home-3). It has the glyph at 22, the title, one `muted` line and its buttons at the trailing end. Only one state shows, in this order:

| State | When | Title | Line | Buttons |
| --- | --- | --- | --- | --- |
| Crash | `StartupOffer` holds a crash offer | "HikariSub did not close properly" | Today's session sentence + "Unsaved work in N documents was autosaved." | **Dismiss**, **Autosaves…** (quiet), **Restore session** (primary) |
| Ask | `session.restore` = 1 and a session is waiting | "Restore the last session?" | "N documents · names" | ☐ **Restore it at every start**, **Restore** (primary) |
| Project | The window has a Project with last tabs | The Project's name | "Last tabs: names" | **Reopen last tabs (N)** (primary); **Last session** (quiet) when a last session exists too |
| Last session | A last session exists | "Continue where you left off" | "N documents · names" | **Restore** (primary) |
| None | Nothing of the above | The card is hidden | | |

- **Crash** has the `notice-warning` glyph in `warning`, a 1-unit `warning` boundary and a 3-unit `warning` bar on the leading edge. The other states use `session-open` in `accent`. Project uses `project`.
- The names are the first three documents' file names, then "and N more".
- Restoring, dismissing or reopening hides the card until there is a new session to offer. What these buttons do is unchanged: `StartupOffer` and `app.lastSessionSummary()` stay the sources.

## Recent

A heading row: **Recent** in 13.5 semibold, the **Files | Projects** segmented control (home-4), and at the trailing end the **Filter** field (200 wide).

- **Files** lists `recent.subtitles` with their `recent.subtitlesOpened` times. **Projects** lists `recent.projects` with `recent.projectsOpened`.
- **With no recent projects** the segmented control hides and the list shows files.
- **The choice is kept** per window in `home.recentTab` (Workspace, `files` or `projects`). A new window starts on Files.
- **Rows** are today's ([RecentList.qml](../../src/ui/RecentList.qml)), redrawn:
  - the `file-subtitles` or `project` glyph in `muted`;
  - the name in semibold over the folder in 11.5 `muted` monospace;
  - the time at the trailing end in `muted` ("Today 14:05", "Yesterday 09:12", then the short date).
- **Unchanged:** the **Missing** tag (11.5 `warning`, after the name), hover **Show in folder** and Remove, the row menu (Open, Show in folder, Copy path, Remove from list, Remove missing folders), and the Filter matching name and path.
- **The first row** has the `select` fill when the list has focus.
- **Empty (first run):** the list box shows, centred, the `recent-subtitles` glyph, "Files and projects you open show here." in `muted`, and **Import text…** and **Import settings…** (quiet, small). The heading row keeps only "Recent".
- **No match** for the filter: "Nothing matches “ep03”." in `muted`.

## What's New

A card with a header row: the `whats-new` glyph, **New in 0.1.0-beta.6** (the running version) and a close button (×, quiet, "Close until the next version"). Then come:

- **the picture:** 108 high, 12 in from the sides, radius 4, with a caption in white with a dark halo at the bottom centre;
- **up to four items**, each the `accent` glyph at 16, the name in semibold and one sentence in `muted`, separated by 1-unit `line`s;
- **All that's new** (quiet, small), which opens the release notes ([WhatsNew](../../src/app/include/hikari/app/whats_new.h): the cached list when there is one, otherwise `openAllReleaseNotes()`).

When What's New and Continue are both on screen in two columns, What's New shows two items; it shows four when Continue is hidden. The order of items is the order in the content file.

**It doesn't depend on the update check** (home-2). The content is built into the app (see [What's New content](#whats-new-content)), so it shows on a first run and with update checks off.

**Closing it** writes the running version to `home.whatsNewClosed` (Profile, string). The card stays hidden while that equals the running version, and comes back with the next version. Closing doesn't touch the release-notes cache.

## Plugins

A card (home-5). The header row has the `plugins` glyph, **Plugins**, and at the trailing end a badge, "N updates" on `accent` with `onAccent` text, when updates wait. The card's states:

| State | Line | Buttons |
| --- | --- | --- |
| None installed | "Official plugins add formats, presets and themes." | **Marketplace** (quiet, `marketplace` glyph) |
| Some, nothing waiting | "N on · first three names…" | **Manage** (quiet), **Marketplace** (quiet) |
| Updates waiting | "N on · first three names…" | **Update all** (small), **Marketplace** (quiet) |
| An update asks for more | Adds "1 update asks for more permissions." | **Review…** in place of Update all |

- "Updates waiting" is what Plugins › Installed counts ([plugins.md](../plugins.md)): updates that won't install by themselves, because `plugins.autoUpdate` is off for them or the update asks for more.
- **Update all** installs those that don't ask for more.
- **Review…** opens Plugins (`GLOBAL_PLUGINS`, 5231) at Installed on the first held plugin.
- **Marketplace** opens Plugins at Marketplace. **Manage** opens it at Installed.
- With `plugins.checkUpdates` off the badge never shows, and the line ends with "Update checks are off." as a link to Settings at `plugins.checkUpdates`.

## AI

While `ai.off` is on, a card shows ([agent-panel.md](agent-panel.md)'s Q801 card, made small; home-6):

- the `ai-assistant` glyph and **AI is off**;
- "Translation help, review and the Agent panel. Nothing is sent until you turn it on." in `muted`;
- **Turn on…** (quiet). It does what Edit › Turn on AI… does: AI turns on, and HikariSub downloads the AI component once.

A close button hides the card for good: `home.aiCardClosed` (Profile, bool). The card is never shown while AI is on.

## Left out

There are no tips, no Discord and no news feed on Home (home-7). Import text… and Import settings… are under [More](#actions) and in the empty Recent list.

## Keyboard

- Tab order: Quick Open field → the tiles (←/→ move between tiles, and each tile is one Tab stop) → Continue's buttons → Files | Projects → Filter → the Recent list → What's New → Plugins → AI.
- Opening Home focuses the Recent list when it has rows, and **New** otherwise (as today's `takeFocus`).
- In the Recent list, ↑/↓ moves, Enter opens, Delete removes from the list, and the menu key or Shift+F10 opens the row menu. Ctrl+F focuses Filter, and Escape in Filter clears it, then goes back to the list.
- Ctrl+P and Ctrl+Shift+P work from anywhere on Home.
- Each card is an `Accessible.Grouping` named by its title. The Continue card's name includes its state ("Continue: HikariSub did not close properly").

## Strings

New strings, in the `HikariSub.Home` context:

| String | Where |
| --- | --- |
| Search files, projects and commands | Quick Open field |
| More | The More tile |
| New files use: %1… · New files use no catalogue… | More menu |
| Continue where you left off | Continue, Last session |
| Last tabs: %1 · Last session | Continue, Project |
| and %n more | Continue names |
| Files · Projects | Recent switch |
| Files and projects you open show here. | Empty Recent |
| Nothing matches “%1”. | Filter with no match |
| New in %1 · All that's new · Close until the next version | What's New |
| Plugins · %n updates · %n on · Official plugins add formats, presets and themes. · Update all · Review… · Manage · Marketplace · %n update(s) ask(s) for more permissions. · Update checks are off. | Plugins |
| AI is off · Translation help, review and the Agent panel. Nothing is sent until you turn it on. · Turn on… | AI |

These strings stay as today: New, Open…, Open folder…, Open video…, New subtitles in a new tab, the startup offer's strings, Restore, Restore session, Autosaves…, Dismiss, Restore it at every start, Reopen last tabs (%n), Recent, Filter, Missing, Today %1, Yesterday %1, and the row menu.

## Settings

| Key | Tier | Type, default | Use |
| --- | --- | --- | --- |
| `home.recentTab` | Workspace | string, `files` | Recent's Files or Projects |
| `home.whatsNewClosed` | Profile | string, empty | The version whose What's New was closed |
| `home.aiCardClosed` | Profile | bool, false | The AI card was closed |

They are read: `session.restore`, `recent.*`, `ai.off`, `plugins.checkUpdates`, `plugins.autoUpdate` and `new.styleCatalog`. Links to Settings use `hikarisub://settings/<id>` ([settings.md](settings.md#setting-links)).

## What's New content

- **Where.** `src/ui/whats-new/<version>.toml`, with its picture next to it (`<version>.webp`, 600 × 216, at most 150 KB). Both are compiled into the app as resources. The file has `caption`, `picture`, `credit`, and up to four `[[item]]`s, each with `icon` (an icon role), `name` and `text`.
- **Which file.** A version uses its own file, or the newest older one when it has none, so a bugfix release can ship without a new file.
- **Who writes it.** The release thread drafts the file with the release notes: the intro's new features become the items, in the same words, one sentence each. DS checks the wording and the icon roles. It lands in the release's last feature PR or a small PR before the tag.
- **The picture.** A screenshot of the first item, taken under vx11 from the app with test fixtures or Sprite Fright (CC BY 4.0). With Sprite Fright, `credit` reads "Sprite Fright © Blender Foundation, CC BY 4.0". The credit is the picture's tooltip and accessible description, and it is also listed in About › Credits.
- **Translations.** `name`, `text` and `caption` are extracted for lupdate in the `HikariSub.WhatsNew` context, so they follow [the translations rule](../localisation.md).

## Implementing

Home is one PR. It touches [HomePage.qml](../../src/ui/HomePage.qml), [RecentList.qml](../../src/ui/RecentList.qml) and [StartupOffer.qml](../../src/ui/StartupOffer.qml) (folded into the Continue card), the What's New component the window passes in, the three settings, the content file for beta.6 and its tests. The `homeNew`, `homeOpen`, `homeOpenFolder`, `homeOpenVideo` and `homeWhatsNew` object names stay. The new blocks are `homeQuickOpen`, `homeMore`, `homeContinue`, `homeRecentTab`, `homePlugins` and `homeAi`.

## Decisions

The owner picked from the Home page gate board on 2026-11-24 ([#1205](https://github.com/altqx/hikari/issues/1205)):

| Q | Decision |
| --- | --- |
| Q810 | Direction H-A Desk: your work on the left, What's New, Plugins and AI on the right |
| Q812 | Every recommendation on the board: What's New built into the app and closable per version (home-2); one Continue card (home-3); Recent with Files \| Projects (home-4); a Plugins card (home-5); the AI-off card (home-6); no tips, Discord or news, Import entries under More (home-7) |
| Q813 | The full redesign ships in beta.6, not a restyle only |
