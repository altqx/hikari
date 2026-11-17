# Discord presence

HikariSub can show what you are doing in your Discord status (Rich Presence, [#599](https://github.com/altqx/hikari/issues/599)). It talks to the Discord app on your computer only; nothing goes to a HikariSub server. The design and its decisions are in [Discord presence settings](ux/discord-presence.md) (#1245).

## The basics

**It is off by default**, and so are the file name and the project's name. Turn it on in **Settings > Updates and privacy > Show what I am doing in Discord**. The rows under it choose what shows; a preview beside them draws roughly what the people who can see your status see.

**Turning it off clears your status at once** and closes the connection to Discord. Quitting HikariSub clears it too.

**Without Discord** running, HikariSub does nothing visible and writes nothing to the Log. It looks for Discord again every 20 seconds and shows the status once Discord starts.

## What shows

- **What shows:** the mode, the mode's icon, the project's name and the file name, each with its own switch. The last two are off by default.
- **Text:** Standard ("Editing subtitles", then the mode, the file name and the project's name, each if on), or My own: a Details and a State line with the placeholders `{mode}`, `{file}`, `{project}`, `{show}`, `{episode}`, `{title}` and `{plugin}`. A placeholder whose switch is off stays empty. Each line is cut at 128 characters, and a line shorter than 2 is left out.
- **Text from plugin:** the one plugin whose text fills `{plugin}` ([Discord status text](plugins.md#discord-status-text)).
- **Time:** since HikariSub started (presence turned on in this run), since the current tab's file opened, or hidden.
- **Idle and away:** after 10 minutes (1–120) without keyboard or pointer input, show "Idle", clear the status or keep it. Time in the background counts only with **Do the same while HikariSub is in the background**. The first input ends idle and restarts the time.
- **Buttons:** "Get HikariSub" (https://hikarisub.org), and one link of your own with a label of at most 32 characters and an `https://` address.

On a Project's tab, **Updates and privacy › In this project** can hide presence while a tab from the Project is current, or use the Project's own Show name, Details and State; an empty line uses yours, and the Project's text waits until the folder is trusted.

## Modes

The mode follows the workspace layout: Editing, Timing (kanji timing too), Translation, Typesetting or Karaoke. A saved layout shows the mode of the layout it came from. While an activity is going, it shows instead, the first in this order: Watching, Effects (node effects or the Template editor), Styling (the Style manager), Spell-checking, Reviewing (QC notes has focus, or a note was added in the last 5 minutes), Exporting (Collect fonts) and With the AI agent. Each can be unticked; With the AI agent is off by default. With no tab open, the status reads "In HikariSub", "Home", with no small image.

## What is sent

| Field | Value |
|---|---|
| Details | "Editing subtitles", your Details line, or "Idle" |
| State | The standard State line, or your State line |
| Elapsed time | As **Time** says |
| Large image | The HikariSub icon (`hikari`), titled "HikariSub" |
| Small image | The mode's icon (`editing`, `timing`, `translation`, `typesetting`, `karaoke`, `watching`, `effects`, `styling`, `spelling`, `reviewing`, `exporting` or `agent`), titled with the mode |
| Buttons | "Get HikariSub" and your own link, each if on |
| Process id | HikariSub's, which Discord uses to clear the status when HikariSub ends |

The texts are in HikariSub's interface language.

**Never sent:** the video or audio name, folders or paths, subtitle text, line counts, progress, or anything about the file other than its name when you allow it.

## How it is sent

- Discord's local IPC, with HikariSub's Discord application id (`1566681222510411967`):
  - Linux: the Unix socket `discord-ipc-0` to `discord-ipc-9` in `$XDG_RUNTIME_DIR`, `$TMPDIR`, `$TMP`, `$TEMP` and `/tmp`, and in their `app/com.discordapp.Discord`, `app/com.discordapp.DiscordCanary` (Flatpak), `snap.discord` and `snap.discord-canary` (Snap) folders;
  - Windows: the named pipe `\\.\pipe\discord-ipc-0` to `\\.\pipe\discord-ipc-9`.
- A handshake, then `SET_ACTIVITY` each time what shows changes: at most one every 4 seconds (Discord takes 5 every 20 seconds). Only the last change waits.
- When Discord closes the connection or quits, HikariSub tries again every 20 seconds while presence is on. A handshake Discord does not answer within 5 seconds counts as closed.

## For developers

- `src/app/discord_ipc.cpp`: the IPC client (frames, candidate paths, handshake, ping).
- `src/app/discord_activity.cpp`: what is sent, from the `privacy.discord*` settings and the current scene; the Settings preview uses it too.
- `src/app/discord_presence.cpp`: the connection, idle, the activities going, plugin texts and the rate limit. `src/app/application_presence.cpp` feeds it from the windows; a window that knows of an activity calls `presenceActivity(shellWindow, key, going)`. The key `typesetting` shows the Typesetting mode whatever the layout, for full-screen typesetting.
- `presence.set_text` is in `src/api/presence_operations.cpp`.
- The application id is the CMake cache variable `HIKARI_DISCORD_CLIENT_ID`; empty turns presence off in that build.
- `hikari_ui_discord_presence_tests` runs against a fake Discord (`QLocalServer`) on both platforms.
- The art and its asset keys are in `packaging/discord/`; the owner uploads it to the Discord application.
