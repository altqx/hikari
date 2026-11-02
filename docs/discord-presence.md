# Discord presence

HikariSub can show what you are doing in your Discord status (Rich Presence, [#599](https://github.com/altqx/hikari/issues/599)). It talks to the Discord app on your computer only; nothing goes to a HikariSub server.

## The basics

**It is off by default**, and so is the file name. Turn it on in **Settings > Updates and privacy > Show what I am doing in Discord**. **Include the file name** below it adds the file name.

**Turning it off clears your status at once** and closes the connection to Discord. Quitting HikariSub clears it too.

**Without Discord** running, HikariSub does nothing visible and writes nothing to the Log. It looks for Discord again every 20 seconds and shows the status once Discord starts.

## What is sent

To the Discord app, which shows it to the people who can see your Discord status:

| Field | Value |
|---|---|
| Details | "Editing subtitles" |
| State | The mode: Editing, Timing, Translation, Typesetting or Karaoke. With **Include the file name**, the mode, then the current tab's file name (e.g. `Timing · episode01.ass`), at most 128 characters |
| Elapsed time | Since presence was turned on in this run |
| Large image | The HikariSub icon (`hikari`), titled "HikariSub" |
| Small image | The mode's icon (`editing`, `timing`, `translation`, `typesetting` or `karaoke`), titled with the mode |
| Button | "Get HikariSub", opening https://hikarisub.org |
| Process id | HikariSub's, which Discord uses to clear the status when HikariSub ends |

The texts are in HikariSub's interface language. The mode is the workspace layout you chose last (a saved layout keeps the layout it was made from).

**Never sent:** the video or audio name, folders or paths, subtitle text, line counts, progress, or anything about the file other than its name when you allow it.

## How it is sent

- Discord's local IPC, with HikariSub's Discord application id (`1566681222510411967`):
  - Linux: the Unix socket `discord-ipc-0` to `discord-ipc-9` in `$XDG_RUNTIME_DIR`, `$TMPDIR`, `$TMP`, `$TEMP` and `/tmp`, and in their `app/com.discordapp.Discord`, `app/com.discordapp.DiscordCanary` (Flatpak), `snap.discord` and `snap.discord-canary` (Snap) folders;
  - Windows: the named pipe `\\.\pipe\discord-ipc-0` to `\\.\pipe\discord-ipc-9`.
- A handshake, then `SET_ACTIVITY` each time the mode or the file name changes: at most one every 4 seconds (Discord takes 5 every 20 seconds). Only the last change waits.
- When Discord closes the connection or quits, HikariSub tries again every 20 seconds while presence is on. A handshake Discord does not answer within 5 seconds counts as closed.

## For developers

- `src/app/discord_ipc.cpp`: the IPC client (frames, candidate paths, handshake, ping).
- `src/app/discord_presence.cpp`: the settings `privacy.discordPresence` and `privacy.discordFileName`, the activity and the rate limit.
- The application id is the CMake cache variable `HIKARI_DISCORD_CLIENT_ID`; empty turns presence off in that build.
- `hikari_ui_discord_presence_tests` runs against a fake Discord (`QLocalServer`) on both platforms.
- The art and its asset keys are in `packaging/discord/`; the owner uploads it to the Discord application.
