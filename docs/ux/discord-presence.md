# Discord presence settings

More control over what HikariSub shows in a Discord status ([#1245](https://github.com/altqx/hikari/issues/1245)). [Discord presence](../discord-presence.md) describes what ships today (#599): two switches, a fixed text, five modes. This document adds a Settings section with a preview, separate switches, the user's own text, per-project choices, idle handling, time and button choices, more modes, and a narrow plugin hook. It is drawn in [Quiet](visual-language.md) on the [Settings](settings.md) layout.

The owner chose this design from a board on 2026-11-17 ("lgtm", Q684–Q697; see [Decisions](#decisions)). The plugin API's names are fixed in [ADR 0034](../adr/0034-plugins-grow-into-an-extension-system.md) (#1221).

The app doesn't change until the code card lands after the beta.4 tag.

## Where it lives

Settings › Updates and privacy › **Discord**, a page of its own next to Checking, Installing, Plugins and Privacy. Today's two rows move here from Privacy, and `hikarisub://settings/privacy.discordPresence` lands on the first row.

The page has one switch, then the rows in five groups, with the preview beside them (Q684):

| Group | Rows |
| --- | --- |
| — | **Show what I am doing in Discord** (`privacy.discordPresence`, off) |
| What shows | The mode; the mode's icon; the project's name; the file name (Q685) |
| Text | Standard, or My own with Details and State lines (Q686); Text from plugin (Q693) |
| Time | Time shown: Since HikariSub started, Since the file opened, Hidden (Q690) |
| Idle and away | After no input for *n* min: Show "Idle", Clear the status, Keep it; Do the same while HikariSub is in the background (Q689) |
| Buttons | "Get HikariSub"; A link of your own, with Label and Address (Q691) |

- **The preview** is a rough drawing of Discord's activity card, 300 wide, beside the rows and staying in view while they scroll. It shows what the people who can see the status would see, buttons included, and updates as rows change. It is not Discord's code or art. Under it: "Roughly how Discord shows it to people who can see your status. Buttons show to them, not to you."
- **With presence off** every row keeps its value and is disabled, and the preview reads "Nothing is shown in Discord. Presence is off."
- Below the window's narrowest width for two columns, the preview moves above the rows.

## What shows

| Row | Setting | Default | Off means |
| --- | --- | --- | --- |
| The mode | `privacy.discordMode` | on | The standard State line leaves out the mode, and `{mode}` is empty |
| The mode's icon | `privacy.discordModeIcon` | on | No small image |
| The project's name | `privacy.discordProjectName` | off | `{project}` is empty and the standard State line leaves it out |
| The file name | `privacy.discordFileName` (today's) | off | `{file}` and `{episode}` are empty and the standard State line leaves it out |

Ticking The mode shows the list of [modes](#modes) under it.

**Never sent** (Q692): folders or paths, subtitle text, video and audio names, line counts, progress, or anything about the file other than what the rows above allow.

## Your own text

Text is **Standard** (today's: "Editing subtitles", then the State line built from What shows) or **My own** (Q686). My own shows two fields:

- **Details (first line)** and **State (second line)**, each a field 420 wide with placeholders drawn as chips, a counter to Discord's 128 characters, and an Insert row of chips under them.
- A placeholder whose switch is off is drawn struck through, with "{file} stays empty while The file name is off." under the field.
- A line that expands to fewer than 2 characters (Discord's minimum) is left out. Each line is cut at 128 characters after expansion.

The settings are `privacy.discordText` (`standard` or `own`), `privacy.discordDetails` and `privacy.discordState`.

### Placeholders

Q687:

| Placeholder | Fills with | Empty when |
| --- | --- | --- |
| `{mode}` | The [mode](#modes) | The mode is off |
| `{file}` | The current tab's file name, no folder | The file name is off, or the tab is untitled |
| `{project}` | The Project's folder name | The project's name is off, or there's no Project |
| `{show}` | The Project tab's Show name, else the file's Script Info Title | Neither is set |
| `{episode}` | An episode number found in the file name (`- 03`, `E03`, `S01E03`) | None found, or the file name is off |
| `{title}` | The file's Script Info Title | Not set |
| `{plugin}` | The text of the plugin picked under Text from plugin | No plugin is picked, or it set nothing; see [Plugins](#plugins) |

An unknown `{name}` is sent as typed.

## What is sent

| Discord field | Standard text | My own text | Row |
| --- | --- | --- | --- |
| Details | "Editing subtitles" | The Details line | — |
| State | The mode, then the file name, then the project's name, each if on, joined with " · " | The State line | What shows |
| Elapsed time | Since HikariSub started, since the current tab's file opened, or none | same | Time shown |
| Small image | The mode's icon, titled with the mode | same | The mode's icon |
| Large image | The HikariSub icon (`hikari`) | same | — |
| Buttons | "Get HikariSub" and one link of your own, each if on | same | Buttons |

Updates still go at most once every 4 seconds, and only the last change waits. Idle and the project's choice apply on the next update.

## Time

Q690. **Since HikariSub started** is the default and keeps today's meaning: counting from when presence was last turned on in this run. **Since the file opened** restarts when the current tab changes. **Hidden** sends no time.

## Idle and away

Q689. After 10 minutes (1–120, `privacy.discordIdleMinutes`) without keyboard or pointer input in HikariSub:

| Choice (`privacy.discordIdle`) | Sent |
| --- | --- |
| Show "Idle" (default) | Details "Idle", no State, no time, no small image |
| Clear the status | Nothing, as when presence is off |
| Keep it | No change |

The preview draws idle with the large image greyed; Discord gets the plain image. **Do the same while HikariSub is in the background** (`privacy.discordIdleBackground`, off) also counts time with no HikariSub window active. The first input ends idle and the time restarts.

## Buttons

Q691. Discord allows two:

- **"Get HikariSub"** (`privacy.discordGetButton`, on) opens https://hikarisub.org. It can be unticked.
- **A link of your own** (`privacy.discordLink`, off), with **Label** (at most 32 characters, `privacy.discordLinkLabel`) and **Address** (`privacy.discordLinkUrl`). An address that isn't `https://` gets a danger boundary and "Needs an https:// address." under it; the preview keeps the last good buttons and nothing is sent for it.

## Per project

On the Project tab (Q688), Updates and privacy holds only Discord, with one choice, **In this project** (`privacy.discordProject`):

| Choice | Effect | Trust |
| --- | --- | --- |
| Use yours (default) | The Yours tab's rows | — |
| Hide presence while this project is open | Clears the status while a tab from this Project is current | Applies at once: it only narrows |
| Use this project's text | The project's **Show name** (`{show}`, `privacy.discordShow`), Details and State lines; an empty line uses yours | Waits for trust ([ADR 0030](../adr/0030-open-a-folder-as-a-project.md)): until the folder is trusted, yours shows |

Rows set here show "Set in this project" and a Reset to "Use yours", as in [Settings](settings.md#set-elsewhere). The switches under What shows stay the user's: a project can't turn on the file or project name.

## Modes

The mode follows the workspace layout. While an activity is going, its mode shows instead, and the layout's mode comes back when it ends (Q694, Q695).

| Mode | Key | Shows while | Can be unticked |
| --- | --- | --- | --- |
| Editing | `editing` | The Editing layout, or no other mode applies | no |
| Timing | `timing` | The Timing layout; kanji timing too | no |
| Translation | `translation` | The Translation layout | no |
| Typesetting | `typesetting` | The Typesetting layout; full-screen typesetting ([#1208](full-screen-typesetting.md)) too | no |
| Karaoke | `karaoke` | The Karaoke layout | no |
| Watching | `watching` | Simple mode ([#1207](https://github.com/altqx/hikari/issues/1207)) is open | yes, on |
| Effects | `effects` | The node-effects mode or the Template editor is open | yes, on |
| Styling | `styling` | The Style manager is open | yes, on |
| Spell-checking | `spelling` | The Spellchecker panel is checking | yes, on |
| Reviewing | `reviewing` | QC notes has focus, or a QC note was added in the last 5 minutes | yes, on |
| Exporting | `exporting` | An export, Collect fonts or a muxing run is going | yes, on |
| With the AI agent | `agent` | An agent turn is running in the current tab | yes, **off** |
| Home | none | No tab is open: Details "In HikariSub", State "Home", no small image | no |

- **Order.** When activities overlap, the first in this order shows: Watching, Effects, Styling, Spell-checking, Reviewing, Exporting, With the AI agent, then the layout's mode.
- **Unticked** activities show the layout's mode. The list is `privacy.discordActivities`, the activity keys that are on.
- A window or dialog counts only while it's open in the current HikariSub window.
- **Saved layouts** show the mode of the built-in layout they came from, never their own name (Q696).
- Mode names are in HikariSub's interface language, as today.

The list under The mode has one row per mode: a check box (disabled and ticked for the five layout modes), the mode's disk, its name, and when it shows in `muted`. Above it: "The mode follows your layout. While one of the activities below is going, it shows instead." Below it: "Unticked activities show the layout's mode. A layout you saved shows the mode of the layout it came from."

## Plugins

Q693. A plugin can set one short text that shows in presence, and nothing else. The names are ADR 0034's ([Discord status text](../plugins.md#discord-status-text)):

- **Permission** `permissions.presence`, shown at install and update as "Set text in your Discord status: *reason*".
- **Operation** [`presence.set_text {text}`](../api.md#presenceset_text), in Lua `hikari.presence.set_text(text)`. `nil` or an empty string clears it. Plain text of at most 64 characters, with no line breaks or other control characters; anything else is `invalid_params`.
- **Where it shows.** Only where the user puts `{plugin}` in My own text, and only from the plugin picked in the row **Text from plugin** (`privacy.discordPlugin`, a plugin id, empty by default). Its list offers the installed plugins that hold the permission.
- **When it's sent.** Only while presence is on, within the 4-second limit. A call from a plugin that isn't picked, or made while presence is off, succeeds but isn't sent.
- **How long it lasts.** Until the plugin sets or clears it, or is turned off, uninstalled or revoked. It isn't kept after HikariSub quits.
- Since API 1, like every operation before the first release.

## Art

Q697. Each new mode gets an image in `packaging/discord/`, made like the first five ([art notes](../../packaging/discord/README.md)): 1024×1024 opaque PNG with an SVG master, one navy (#1E2A6B) glyph on a full-bleed pastel disk, inside the inscribed circle, strokes about 1.5 px at 20 px. The owner uploads them to the Discord application under these keys.

| Key | Glyph follows `src/ui/icons/` | Disk |
| --- | --- | --- |
| `watching` | `media-play.svg` | #C9E7A4 |
| `effects` | `node-editor.svg` | #A6E3E9 |
| `styling` | `styles.svg` | #FFD1A8 |
| `spelling` | `spellchecker.svg` | #E6D6B8 |
| `reviewing` | `qc-notes.svg` | #FFC6E0 |
| `exporting` | `export.svg` | #C5CAE9 |
| `agent` | `ai-assistant.svg` | #F6B8F0 |

Home uses no small image. Until an image is uploaded, its mode sends no small image rather than a missing key.

## Decisions

The owner said "lgtm" to the #1245 board on 2026-11-17, taking the recommended option for every question ([#1209](https://github.com/altqx/hikari/issues/1209#issuecomment-6939040614)):

| Q | Decision |
| --- | --- |
| Q684 | The preview sits beside the rows and stays in view while they scroll |
| Q685 | Separate switches for the mode, the mode's icon, the project's name and the file name; the last two off by default |
| Q686 | Standard text, or My own: two lines with placeholders and a 128-character counter |
| Q687 | `{mode}`, `{file}`, `{project}`, `{show}`, `{episode}`, `{title}`; `{file}`, `{episode}` and `{project}` stay empty while their switch is off |
| Q688 | On the Project tab: Use yours, Hide presence, or the project's own text with a Show name; hiding applies at once, the project's text waits for trust |
| Q689 | After 10 minutes without input show "Idle" (or clear, or keep); the same in the background is off by default |
| Q690 | Time since HikariSub started (as today), since the file opened, or hidden; the first by default |
| Q691 | "Get HikariSub" on by default and hideable, plus one link of the user's own: a label of at most 32 characters and an https:// address |
| Q692 | Presence, the file name and the project name off by default; paths, subtitle text, video names and line counts never sent |
| Q693 | Plugins may set one short text through `permissions.presence` and `presence.set_text`, shown only in `{plugin}` from the one plugin the user picks |
| Q694 | Seven activity modes (Watching, Effects, Styling, Spell-checking, Reviewing, Exporting, With the AI agent off by default) plus Home with no icon |
| Q695 | An activity shows over the layout's mode while it's going, in a fixed order, and each can be unticked |
| Q696 | A saved layout shows the mode of the built-in layout it came from |
| Q697 | New art in the packaging/discord style for each new mode, made with the code card |
