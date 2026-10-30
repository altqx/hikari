# Menu icon gaps (IC1)

[IC1](https://github.com/altqx/hikari/issues/226) gives an icon of the set ([icons.md](icons.md)) to every main-menu and context-menu item that runs a command. This is the gap list it started from, at `qt` 4a4b0f45: each item that had no `iconRole`, the role it takes now, and the items left without one and why. `hikari_ui_shell_tests` everyMenuItemHasAnIcon keeps it closed: it reads every QML file and fails on a menu item or a submenu without an `iconRole` that is no check item and no exemption below.

**New** marks a role drawn for IC1; the others were in the set and keep their meaning (one command, one icon; one icon, one meaning).

## Items that take an icon

### File

| Item | Role |
| --- | --- |
| Open auto save | autosave (new: the floppy with the recent clock) |
| Remove temporary files | remove-temporary (new: the bin with the clock) |
| Log (legacy "Show / Hide log window") | log (new) |
| Last session › Load session from file | session-open (new: the session's windows with the open plus) |
| Last session › Save session to file | session-save (new: the floppy with the session's windows) |
| Import legacy settings... | import |

### Edit, Automation, Subtitles

| Item | Role |
| --- | --- |
| Automation › Rerun last macro | rerun-macro (new: the script with a repeat arrow) |
| Automation › Repeat last run (AD2) | repeat-run (new: the script with a repeat loop; also the Automation panel's tool strip) |
| Automation › Show panel | automation-manager (new: the scripts, two pages; AM1 made the window the Automation panel) |
| Subtitles › Shift times / run timing postprocessor | shift-times-run (new: Shift times' clock with run-script's play mark) |

### Video and Audio

| Item | Role |
| --- | --- |
| Video › Open dummy video | dummy-video (new: the film with a blank colour) |
| Video › Streams (submenu) | video-streams (new) |
| Video › Chapters (submenu) | chapters (new) |

### View

| Item | Role |
| --- | --- |
| Panels (submenu) | panels (new) |
| Panels › *panel* › Show | show (new: the eye) |
| Panels › *panel* › Hide | hide (new: the eye struck through) |
| Panels › *panel* › Float | panel-float (new) |
| Panels › *panel* › Dock | panel-dock (new) |
| Move panel… | panel-move (new) |
| Layout preset (submenu) | layout-preset (new) |
| Reset layout | layout-reset (new) |
| Restore the previous layout | layout-restore (new) |

### A panel's "⋯" menu and a floating panel's menu

| Item | Role |
| --- | --- |
| Compare with reference (the reference tray's own item) | compare |
| Move panel… | panel-move |
| Move tab left / Move tab right | tab-move-left / tab-move-right (new) |
| Undock / Dock | panel-float / panel-dock, by the panel's state |
| Close / Close reference | tab-close (the close's ×) |
| Floating panel's menu: Dock, Close | panel-dock, tab-close |

### Document tab menu

| Item | Role |
| --- | --- |
| Close all tabs | tab-close-all (new) |
| Subtitle comparison › Compare by selected styles (submenu) | styles |
| Subtitle comparison › Compare | compare |
| Subtitle comparison › Turn off comparison | compare-off (new: the two lists with the close cross) |

### Text fields' menu (Line editor)

| Item | Role |
| --- | --- |
| Installed languages (submenu) | dictionary (new: a book) |
| Add word "…" to dictionary | dictionary-add (new) |

### Grid menu

| Item | Role |
| --- | --- |
| Insert (submenu) | add |
| Insert before; before with video time; before with video frame time | insert-before (new; the variants share the verb's icon) |
| Insert after; after with video time; after with video frame time | insert-after (new) |
| Swap | swap-lines (new) |
| Join lines | join-lines (new) |
| Join lines and keep first / last | join-keep-first / join-keep-last (new) |
| Make times continuous with the previous line / with the next line | continuous-previous / continuous-next (new) |
| Split lines (submenu) | split-lines (new) |
| Split line at video time | split-at-video (new) |
| Split lines into frames / characters / words | split-frames / split-characters / split-words (new) |
| Split lines by wraps | split-wraps (new) |
| Select all lines visible on video | select-visible (new) |
| Make line group | make-tree (new) |
| Paste translation text | paste-translation (new: Paste with Save translation's T) |
| Dialogue shifting window | translation-shift (new) |
| Hide selected lines | hide |
| Filtering › Hide lines with styles (submenu) | styles |
| Filtering › Filter | filter |
| Filtering › Turn off filtering | filter-off (new) |
| Hide columns (submenu) | columns (new) |
| Hide columns › Reset column widths | restore-default |
| Set new FPS | fps (new) |
| Set FPS from video | fps-from-video (new) |
| Copy columns / Paste columns | columns-copy / columns-paste (new: Copy and Paste with columns) |
| Delete text | clear |

### The tree's menu (Grid)

| Item | Role |
| --- | --- |
| Add lines | tree-add-lines (new) |
| Copy line group | edit-copy (it copies the line group to the clipboard) |
| Change description | edit |
| Select the group's lines | tree-select (new) |
| Delete | delete |

### Video context menu (and full screen's)

| Item | Role |
| --- | --- |
| Copy video position | copy-position (new) |
| Open in full screen on monitor *n* / Switch full screen to monitor *n* | monitor (new) |
| Open editor | editor |
| Show / hide progress bar | progress-bar (new) |
| Change aspect ratio | aspect-ratio (new) |
| Filters (submenu, DirectShow) | video-filters (new) |

### A tag button's menu (Line editor)

LE5 ([#230](https://github.com/altqx/hikari/issues/230)) replaced the Manage tag buttons menu with the strip's "+" popover and this menu, merged into IC1 from `qt` f621d55a.

| Item | Role |
| --- | --- |
| Edit... | edit (LE5's) |
| Move left / Move right | move-left / move-right (new: move-up's arrow turned) |
| Remove | remove (LE5's) |

### Font catalogs, visual tools

| Item | Role |
| --- | --- |
| Font catalog's add menu: Add fonts from subtitles | fonts-from-subtitles (new) |
| The font picker's catalog menu: Manage catalogs… (PU3) | styles |
| The font + menus: New catalog… (PU3) | add |
| A visual tool's list, its last entry (the shape list's Edit) | edit |

## Items left without an icon

- **Check items** (checkable): Last session's two startup choices, the Grid's filtering switches, filters and hidden columns, the comparison criteria, the Spellchecker's languages, the tag list's options, the layout presets, streams and chapters, the font catalogs, the reference tray's occurrences, a visual tool's choices, the document tabs in the tab menu. Their check mark stands in the icon's place, as in MuseScore 4's menus. Some check items that had an icon keep it (Save subtitles using the video name, Spellchecker).
- **Entries named by their data**, under a submenu that has the icon: recent subtitles, videos, keyframes and audio, and the empty list's "None"; the font + menus' "No catalogs yet" (PU3); the macros of the loaded scripts and the submenus a "/" in their names opens (AS1); the sort keys under Sort all lines and Sort selected lines; the panels under View › Panels; the spelling suggestions; the DirectShow filters under Filters.
- **Menus themselves**: the menu bar's titles and the context menus that open on their own.

## Notes for other cards

- **WS3 [#217](https://github.com/altqx/hikari/issues/217)** removes the View arrangements (GLOBAL_VIEW_*, roles view-all, view-video-subs, view-audio-subs, view-only-video, view-only-subs) and Enable / Disable editor. IC1 draws nothing for them. WS3 drops the view-* roles or marks them as not placed. The editor role stays: the Settings Editor page and the video menu's Open editor use it. WS3's new Save layout as… and Manage layouts… items need icons. layout-preset, layout-reset and layout-restore are there for its menu.
- **LE2 [#227](https://github.com/altqx/hikari/issues/227)** adds a checkable Times/Frames item to the video panel's menu, and **GR3 [#236](https://github.com/altqx/hikari/issues/236)** adds a checkable auto-hide toggle under Hide columns. Check items need no icon. **VF1 [#288](https://github.com/altqx/hikari/issues/288)** adds the Seek on and Then play submenus after it, with video-to-start-time and play-line; their choices are check items.
- **AV1 [#238](https://github.com/altqx/hikari/issues/238)**'s speaker button and device menu can take audio-device (a loudspeaker, drawn for Settings › Audio output). Its "Audio settings…" item needs an icon: settings-audio.
- **PF1 [#225](https://github.com/altqx/hikari/issues/225)** places the Options icons below. The manifest marks them `pending: "PF1 #225"`.

## Options: the primary options of each page

IC1 also draws icons for each Settings page's primary options, which PF1 places beside their labels (MS1's research, docs/research/musescore-shell.md, PF1 "Per-option icons"). An option that matches a menu command uses that command's icon.

| Page | Option | Role |
| --- | --- | --- |
| Editor | Language | language (new: a globe) |
| Editor | Spell checker language | dictionary |
| Editor › Conversion | Choose catalog / Choose style | styles |
| Editor › Conversion | Choose FPS | fps |
| Editor › Conversion | Resolution when converting to ASS | resample |
| Editor › Advanced | Number of edits to save, Maximum number of autosave files | autosave |
| Editor › Advanced | Subtitle grid font, Program font | ui-font (new: Aa) |
| Editor › Advanced | Folder with external fonts | folder-open |
| Editor › Advanced | Autoload loading method, LUA scripts tracking level | automation |
| Video | Subtitle display filter | renderer (new: subtitles on the picture) |
| Video | FFMS2 video seeking method | video-seek (new) |
| Video | Playback | media-play |
| Video | Start video zoom in percent | zoom |
| Video | Preferred audio | video-streams |
| Audio | Audio output (device, API) | audio-device (new: a loudspeaker) |
| Audio | Audio delay in milliseconds | audio-delay (new: the waveform with the clock) |
| Audio | Lead-in / Lead-out | lead-in |
| Appearance | Theme | theme (new: the moon and a star) |
| Appearance | Accent color | accent-colour (new: a drop of the accent) |
| Appearance | UI colors (high contrast) | ui-colours (new) |
| Hotkeys | the filter | filter |
| Hotkeys, every page | Restore default hotkey, Set default, Reset to default | restore-default (new) |
| Subtitle properties | Subtitle information | script-properties |
