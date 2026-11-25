# Getting started

The first-run window for 0.1.0-beta.6 ([#1205](https://github.com/altqx/hikari/issues/1205)). It opens before [Home](home-page.md) when `workspace.gettingStarted` is due, and from Help › Getting started (`GLOBAL_GETTING_STARTED`, 5176). This document says which steps it has, what each one sets, and how it looks in [Quiet](visual-language.md).

The owner chose the **Steps on the side** direction from the gate board on 2026-11-24 (Q811), took every recommendation on it (Q812), and asked for the full redesign in beta.6 (Q813); see [Decisions](#decisions). The app doesn't change until the code card lands.

## What stays

These rules stay:

- **Nothing is saved until Finish.** Choices are staged in `values` and applied by `app.finishGettingStarted()`. Look and language preview live, as today.
- **Skip setup** keeps HikariSub's defaults and marks the window done (`app.skipGettingStarted()`), and Escape and the close button do the same.
- **The import step** shows only when old settings are found.
- **Each step ends with a Later line** that says where its settings live. The line is now a link: it opens Settings at the step's first setting ([setting links](settings.md#setting-links)) after the window closes, and the window's choices are kept as if you had pressed Finish first. While the window is open, the link only names the place in its tooltip.

## The window

A modal window titled "Getting started", 860 × 600, radius 6 and resizable down to 760 × 560. The title bar has Help (the user guide's Getting started page) and Close.

| Part | Size | Holds |
| --- | --- | --- |
| Steps | 210 wide, on `panel`, a 1-unit `line` at the trailing edge, 14 × 10 inside | The step list, then a `muted` note at the bottom: "Skip setup keeps HikariSub's defaults." |
| Page | The rest, 22 × 26 inside | The step's title (19 semibold), its explanation (`muted`), its controls, and the Later line at the bottom (12 `muted`) |
| Footer | 1-unit `line` above, 10 × 14 inside | **Back** (from the second step), then at the trailing end **Skip setup** (quiet) and **Next**, or **Finish** on the last step (primary, the default button) |

**The step list** replaces the progress dots. Each step is a row 30 high, radius `r-ctl`, with an 18-unit number disc and the step's short name:

| State | Disc | Name |
| --- | --- | --- |
| Done (visited) | ✓ on `raised` | `text` |
| Current | The number on `accent`, `onAccent` text | `text`, semibold, on the `select` fill |
| Not yet | The number in a `line` ring | `muted` |

A click on any row goes to that step, and Ctrl+PgDn / Ctrl+PgUp go to the next or previous step. Leaving the import step by any route applies the import as **Next** does today (`fillFromImport()`). The list is an `Accessible.List` named "Steps", and each row's name is "Step 3 of 9, Look, current" (or "done").

## Steps

The steps, in order. A step's id is its `pages` entry.

| # | Id | Name in the list | Title | Change |
| --- | --- | --- | --- | --- |
| 1 | `language` | Welcome | Welcome to HikariSub | Kept |
| 2 | `import` | Bring over | Choose what to bring over / Bring over your old settings? | Kept; only when old settings are found |
| 3 | `look` | Look | Look | Plugin themes, icon themes, More themes… |
| 4 | `layout` | How you work | How do you mostly work? | Six cards |
| 5 | `media` | Video and audio | Video and audio | Kept |
| 6 | `editing` | Editing | Editing | Kept |
| 7 | `ai` | AI | AI | New |
| 8 | `updates` | Updates and privacy | Updates and privacy | Adds Discord |
| 9 | `ready` | Ready | You're set | New; takes in Your files |

With no old settings there are eight steps, numbered 1–8.

### 1 · Welcome

Today's language list and its live preview (`program.language`). The list is in a box with a 1-unit `line` boundary, and the chosen row has the `select` fill and is semibold, with the language's English name in `muted` at the trailing end. The explanation and "This window switches to the language you pick." stay. Later: Settings › General › Language (`program.language`).

### 2 · Bring over

Unchanged in what it does: Import my old settings (Recommended) or Set up from scratch, the review table, Read again, and the encoding choice. Aegisub shortcuts are part of the import ([#1338](https://github.com/altqx/hikari/pull/1338)), so the card's description reads "Shortcuts, fonts, audio and video options and your dictionary words." as today. The explanation names where the settings came from: "Aegisub’s settings were found on this computer. Nothing in them is changed." for one source, "Settings from Aegisub and Kainote were found on this computer. Nothing in them is changed." for two, and "Settings from %1, %2 and %3 were found on this computer. Nothing in them is changed." for three. An old HikariSub is named "HikariSub 0.0.x". Later: Settings › General › Import settings… (`GLOBAL_IMPORT_LEGACY_SETTINGS`).

### 3 · Look

Explanation: "This window changes as you choose." The rows are:

1. **Follow system theme** and **High contrast** (`appearance.followSystem`; high contrast as today), check boxes side by side.
2. **Built in**, as a 11.5 `muted` group label, then the built-in themes as cards in a four-column grid. Each card has a 74-high preview of the theme's colours (radius 6), its name under it, and a 2-unit `accent` ring when chosen.
3. **From plugins**, the themes from installed plugins (`Theme.themes()` entries with a `plugin`). Each card adds the plugin's name in 11 `muted`. The last card is **More themes…**, with the `marketplace` glyph on `raised`, which opens Plugins at Marketplace filtered to Themes after the window closes, keeping the choices so far. With no plugin themes, the group shows only More themes….
4. **Accent colour**: the chosen theme's swatches (`appearance.accents`), as today.
5. **Icons**: a combo with the icon themes (`IconTheme.themes()`), "HikariSub (built in)" first (`appearance.iconTheme`, `hikarisub`). It previews live like the colour theme.
6. **Title bar**: HikariSub's | System (`appearance.titleBar`), with today's hint.

A plugin theme of any type is put in the setting its type maps to (`pairSettings`). Plugin themes add the types the four built-in ones don't cover, so the map must cover every type `Theme.themes()` returns, and a theme of a type with no pair setting goes in `appearance.theme`.

Later: Settings › Appearance (`appearance.theme`).

### 4 · How you work

Explanation: "This sets the layout your first tab opens with. Leave it on Editing for a bit of everything." There are six cards in a three-column grid. Each has a 58-high drawing of the layout (as today's `thumb`), the name in semibold and one `muted` sentence:

| Card | Sentence | Sets |
| --- | --- | --- |
| **Editing** (default) | Video, audio, the line editor and the grid. | No preset (`layout` empty) |
| **Translate** | The original above your translation, with Translation mode on. | `Translation` |
| **Time** | Audio across the top for timing lines by ear. | `Timing` |
| **Typeset** | A large video with the visual tools and the Styles panel. | `Typesetting` |
| **Karaoke** | The Template editor beside the video, the audio in Karaoke mode. | `Karaoke` |
| **Full screen** | Just the picture and a line box. View › Full screen typesetting any time. | `Typesetting` now; the first video opened afterwards enters [full-screen typesetting](full-screen-typesetting.md) once. The flag lasts this session only. The Ready Layout row reads "Full screen, once a video is open". |

The chosen card has a 2-unit `accent` boundary and the `select` fill. Later: the layout switcher in the title bar.

### 5 · Video and audio

Unchanged: Subtitles drawn by (`video.subtitleProvider`) with its sample, and Sound plays on (`audio.outputDevice`) with **Test sound**. Later: Settings › Video and Settings › Audio (`video.subtitleProvider`).

### 6 · Editing

Unchanged: Autosave files kept (`autosave.maxFiles`), Check spelling (`editor.spellchecker`) and Primary language (`editor.dictionaryLanguage`), Grammar (`grammar.language`), and the example sentence. Later: Settings › General › Saving (`autosave.maxFiles`).

### 7 · AI

Explanation: "HikariSub can help with translation, review and an agent that edits with you. It stays off unless you turn it on." Two choice cards, radio-like, each with the `ai-assistant` glyph, a semibold name and a `muted` description:

| Card | Description | Sets |
| --- | --- | --- |
| **Keep AI off** (default) | No AI menus, panels or downloads. Nothing about your subtitles leaves this computer. | `ai.off` = true |
| **Turn AI on** | HikariSub downloads the AI component once, after Finish. You add a provider or sign in to an agent in Settings › AI. | `ai.off` = false |

Turning AI on at Finish starts the same download as Edit › Turn on AI… ([#1373](https://github.com/altqx/hikari/pull/1373)), with its progress where that command shows it. Later: Settings › AI (`ai.off`).

### 8 · Updates and privacy

Today's rows are kept:

- Stable versions only | Pre-releases too (`updater.checkForStable`, with today's Recommended badge and descriptions);
- Check for updates automatically (`updater.autoCheck`);
- Send anonymous usage data (`privacy.usageData`) and What is sent.

Two changes:

- The hint under Check for updates automatically drops "What's New on Home needs it too." What's New no longer depends on it ([home-page.md](home-page.md#whats-new)).
- A **Discord** row is added: **Show what I am doing in Discord** (`privacy.discordPresence`, off), with "Your Discord status shows the mode you're in. Never folders, your subtitles or the video." in `muted` under it. The other Discord settings keep their defaults ([discord-presence.md](discord-presence.md)).

Later: Settings › Updates and privacy (`updater.checkForStable`).

### 9 · Ready

Title "You're set", with the explanation "Finish saves these choices. Everything is in Settings." The page then has:

- **A summary**, one row per choice: a glyph in `muted`, the name in `muted` and the value. A click on a row goes to that step.

  | Row | Glyph | Value |
  | --- | --- | --- |
  | Language | `language` | The language's own name |
  | Look | `appearance` | "Dark · teal accent · HikariSub icons" (theme, accent, icon theme) |
  | Layout | `layout` | The card's name |
  | AI | `ai-assistant` | Off / On, downloads after Finish |
  | Updates | `check-updates` | "Pre-releases, automatically" / "Stable only, not automatically", and so on |
  | Your files | `folder-open` | The user folder in monospace, with **Open folder** (quiet, small) |

- **Good to know**: two `raised` rows, radius 6. Each has a glyph, a sentence and a key cap at the trailing end. They aren't buttons.
  - `search` · "Open anything, or run any command" · **Ctrl+P**;
  - `plugins` · "Formats, presets and themes from the marketplace" · **Automation › Plugins**.

Your files' page is gone. Its explanation ("Everything of yours is in one folder. Copy it to move your setup to another computer.") becomes the Your files row's tooltip.

**Finish failing.** When `app.finishGettingStarted()` fails, the window goes to the import step if there is one, as today. Without one it stays on Ready and shows the problem above the summary: the `notice-warning` glyph in `danger` and the message. This fixes today's `pages.indexOf("import")` of −1.

## Keyboard

- Enter is Next / Finish. Escape is Skip setup. Alt+← is Back.
- Ctrl+PgDn and Ctrl+PgUp go to the next or previous step. Tab moves through the page, then the footer, then the step list.
- On card pages, ←/→/↑/↓ move between cards and Space chooses. Each card group is one Tab stop.

## Strings

In the `HikariSub.GettingStarted` context. New strings:

| String | Where |
| --- | --- |
| Steps · Step %1 of %2, %3 · current · done | Step list (accessible) |
| Welcome · Bring over · How you work · Ready | Short step names (the others reuse page titles) |
| Skip setup keeps HikariSub's defaults. | Step list note |
| %1’s settings were found on this computer. Nothing in them is changed. · Settings from %1 and %2 were found on this computer. Nothing in them is changed. · Settings from %1, %2 and %3 were found on this computer. Nothing in them is changed. | Bring over (replaces today's "HikariSub 0.0.x is installed on this computer. Nothing in it is changed.") |
| This window changes as you choose. · Built in · From plugins · More themes… · Marketplace · Icons · HikariSub (built in) | Look |
| How do you mostly work? · This sets the layout your first tab opens with. Leave it on Editing for a bit of everything. · Editing · Full screen · Video, audio, the line editor and the grid. · Just the picture and a line box. View › Full screen typesetting any time. | How you work |
| AI · HikariSub can help with translation, review and an agent that edits with you. It stays off unless you turn it on. · Keep AI off · No AI menus, panels or downloads. Nothing about your subtitles leaves this computer. · Turn AI on · HikariSub downloads the AI component once, after Finish. You add a provider or sign in to an agent in Settings › AI. · Later: Settings › AI | AI |
| At most once a day, when HikariSub starts. | Updates hint (replaces today's) |
| Your Discord status shows the mode you're in. Never folders, your subtitles or the video. | Updates and privacy |
| You're set · Finish saves these choices. Everything is in Settings. · Language · Look · Layout · Updates · Your files · Good to know · Open anything, or run any command · Formats, presets and themes from the marketplace · Off · On · On, downloads after Finish · Full screen, once a video is open · %1 accent · %1 icons · HikariSub icons · Stable only, automatically · Stable only, not automatically · Pre-releases, automatically · Pre-releases, not automatically · Automation › Plugins | Ready |

Changed: "What do you mostly do?" becomes "How do you mostly work?", and its explanation changes as above. The Translate and Karaoke descriptions are kept. Removed: "Your files" as a page title (it stays as a row name), and "At most once a day, when HikariSub starts. What’s New on Home needs it too." Discord's row reuses `HikariSub.Options`' "Show what I am doing in Discord".

## Settings written at Finish

`program.language`, the import, `appearance.followSystem`, `appearance.theme` and its pair settings, `appearance.accents`, `appearance.iconTheme`, `appearance.titleBar`, `video.subtitleProvider`, `audio.outputDevice`, `autosave.maxFiles`, `editor.spellchecker`, `editor.dictionaryLanguage`, `grammar.language`, `ai.off`, `updater.checkForStable`, `updater.autoCheck`, `privacy.usageData`, `privacy.discordPresence`, and `workspace.gettingStarted` (done). Only the settings you changed are written, as today (`touched`). No new settings keys.

## Implementing

Getting started is one PR, in [GettingStartedDialog.qml](../../src/ui/GettingStartedDialog.qml) and `app.openGettingStarted()` / `finishGettingStarted()`, which add the icon themes and `ai.off` / `privacy.discordPresence` to the values they read and write, and start the AI download after Finish. The existing `gettingStarted_*` object names stay. New ones: `gettingStartedSteps`, `gettingStartedStep_<id>`, `gettingStartedPluginTheme_<code>`, `gettingStartedMoreThemes`, `gettingStartedIcons`, `gettingStartedAi_off`, `gettingStartedAi_on`, `gettingStartedDiscord`, `gettingStartedSummary_<row>`.

## Decisions

The owner picked from the Getting started gate board on 2026-11-24 ([#1205](https://github.com/altqx/hikari/issues/1205)):

| Q | Decision |
| --- | --- |
| Q811 | Direction G-A: every step kept, with a step list on the side you can jump in |
| Q812 | Every recommendation on the board: plugin and icon themes on Look (gs-2); an AI step, off by default (gs-3); no plugins step, a pointer on Ready (gs-4); Discord on Updates and privacy, off (gs-5); six layout cards (gs-6); a Ready step that takes in Your files (gs-7) |
| Q813 | The full redesign ships in beta.6, not a restyle only |
