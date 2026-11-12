# Explorer

The Project folder as a tree, like VS Code's Explorer. The owner settled it in the Projects grilling rounds Q87 and Q107 ([#766](https://github.com/altqx/hikari/issues/766)); the card is PJ4 [#772](https://github.com/altqx/hikari/issues/772). It is core, not a plugin: it is the main surface of opening a folder, and every Project user needs it ([research §4.6](../research/project-folders.md#46-the-explorer-panel-decided), ADR 0022). The panel is `src/ui/ExplorerPanel.qml` over `src/ui/explorer_model.h`; the type filter, globs, ordering and grouping are UI-free in `src/application/explorer.h`.

## Where it opens

- View › Explorer (5337 `GLOBAL_SHOW_EXPLORER`, no default key) and View › Panels › Explorer. The dock id is `Explorer` (panel registry 18).
- In the Editing layout it sits left of the Grid. It opens there when a Project opens in the window and closes when the Project closes. Other layouts keep it where the user put it.
- While the window shows Home (no Document open), Home starts at the Explorer's right edge when the Explorer is docked at the left, so the Project's files stay in reach.
- Without a Project it says "Open a folder as a project to see its files here." with an **Open folder…** button.

## What it lists

- The Project folder, folders first, then files in natural order ("ep2" before "ep10", letters without case).
- By default only the files HikariSub uses: subtitles (`ass ssa srt sub txt mpl`), video, audio, keyframes (`.pass`, `.stats`, or a `.txt`/`.log` named for keyframes), timecodes (a `.txt` named for timecodes or ending `_tc`), fonts (`ttf otf ttc pfb`) and chapters (an `.xml` or `.txt` named for chapters). **Show all files** in the header lists the rest too.
- Hidden files and folders (a leading `.`, or the system's hidden flag) never show.
- `[explorer] exclude` hides names or paths matching its globs; it defaults to `["*.lwi", "*.ffindex"]`. A Project sets it in `.hikari/project.toml`, over the user's. A pattern without `/` matches a name; one with `/` matches the path from the Project folder. `*` and `?` stay inside one path part, `**` crosses them, `[...]` is a set.
- The Project's `.hikari/` shows as one **Project settings** row at the top; double-click opens Settings on the Project tab.

## Group by name

**Group by name** in the header gathers each episode's files under its subtitle file, using `findSameNamedFile`'s rule: a video's or subtitle file's stem takes the files named `<stem>.*`, and keyframes, timecodes and chapters named `<stem>_*`. A file joins the shortest stem that takes it, so `ep01.en.ass` goes with `ep01.mkv`. The first subtitle file leads the group, else the video; a group of one file is no group. Case matters, as it does for `findSameNamedFile`.

Show all files and Group by name are the user's (`explorer.allFiles`, `explorer.groupByName`), not the Project's.

## Actions

- Double-click or Enter: a folder or group opens or closes; a subtitle file opens in a new tab (or goes to its tab if one has it); a video, audio, keyframes or timecodes file becomes the current tab's.
- Right-click, or the Menu key: Open in a new tab, Open in this tab, New subtitle file here, Rename (F2), Move to the trash (Del, after a question), Show in folder, Copy path.
- New subtitle file writes the Untitled file's default script under the folder and opens it.
- Keys: ↑ ↓ Home End move, → opens or goes in, ← closes or goes to the folder above, Esc goes back to the Grid.

## Live

The tree follows the disk: each listed folder is watched, and changes land 100 ms later without losing the open folders, the open groups or the selected row. Open Documents keep using the "File changed on disk" bar.
