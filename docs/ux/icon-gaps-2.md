# Icon gaps beyond menus (IC2)

[IC2](https://github.com/altqx/hikari/issues/550) carries [IC1](menu-icon-gaps.md) past the menus, under [icons.md](icons.md)'s rules. The list started from `main` 90bc8c083. **New** marks a role drawn for IC2. The others were already in the set and keep their meaning.

Left without an icon, as IC1 decided: check items, entries named by their data, and plain text verbs in a DialogButtonBox. Primary and danger buttons (`PrimaryButton`, `DangerButton`, `FooterButton`) are a dialog's own verb, so they stay text too. Panel tabs and titles stay text-only, as in MuseScore 4, because tab width matters in narrow docks.

## One icon, two meanings

| Place | Was | Now |
| --- | --- | --- |
| Video menu: Save frame with subtitles as PNG | frame-snapshot | frame-snapshot-subtitles (new: the camera with subtitle lines) |
| Video menu: Copy frame to clipboard | edit-copy | frame-copy (new: Copy's sheets with the camera's lens) |
| Video menu: Copy frame with subtitles to clipboard | edit-copy | frame-copy-subtitles (new) |
| Report an issue: Add the screenshot | frame-snapshot | screenshot (new: a window with capture corners) |
| Info severity: notice bars, toasts, messages, the Log, run log lines, dialog notes | about | notice-info (new: a speech bubble with an i) |
| About's website link, What's new's All release notes | link | open-link (new: a box with an arrow out) |
| Manage font catalogs: the catalog bar, the font picker's menu and button | settings, styles, none | font-catalogs (new: a book with an A) |
| Help › Getting started | settings | getting-started (new: steps with an arrow) |
| Timing › Kanji timer… | karaoke-split | kanji-timer (new: two linked syllable rows) |
| Timing › Check karaoke | karaoke | karaoke-check (new: syllables with a check) |
| A script dialog's Last used; Settings' Last answers | undo | dialog-memory (new: a dialog with the recent clock) |
| Automation panel: Rerun last macro | run-script | rerun-macro (the menu's) |
| Style manager: Delete unused styles | clear | delete |
| Home page: Remove missing files | clear | remove |
| Visual tools' last change: Close | clear | tab-close |
| Settings: Reset all settings… | settings | restore-default |
| Settings import: All conflicts | commit | select-all |

## Notice bars

| Bar | Action | Role |
| --- | --- | --- |
| Layout notice | Restore the previous layout | layout-restore |
| Startup bar | Restore session / Restore | last-session |
| Startup bar | Autosaves… | autosave |
| File changed on disk | Reload | refresh |
| File changed on disk | Compare | compare |
| A session file that didn't open | Retry | refresh |
| A session file that didn't open | Relink… | folder-open |
| A session file that didn't open | Remove | remove |
| Update ready | What's new | whats-new |
| Update ready | Restart now | restart (new) |
| Update ready | Show in folder | show-in-folder |
| Update ready | Copy install command | edit-copy |
| Toasts | Details, Open the Log | log |

"Keep mine" and "Dismiss" stay text: they answer the bar and run no command.

## Home page

| Place | Role |
| --- | --- |
| Crash offer: Autosaves… | autosave |
| Last session: Restore | last-session |
| A missing recent file: Remove | remove |

## Dialog actions

| Dialog | Action | Role |
| --- | --- | --- |
| Crash report | Restore session; Autosaves… | last-session; autosave |
| Report an issue | Copy, Copy again, Copy image | edit-copy |
| Report an issue | Show file | show-in-folder |
| Report an issue | Open the form again | open-link |
| Font collector | Show folder | show-in-folder |
| Getting started | Test sound | volume |
| Getting started | Open folder | folder-open |
| Script properties | From video (twice) | resolution-from-video (new) |
| Script resolution | Use video resolution | resolution-from-video |
| Settings import | Roll back… | undo |
| Aspect ratio | Reset | restore-default |
| Automation progress | Open in panel | automation-manager |
| Automation progress | Cancel | run-cancelled |
| Autosaves | Delete older than… | delete |
| Karaoke check | Re-check | refresh |
| Tap calibration | Start again | refresh |
| Font picker | Manage catalogs… | font-catalogs |
| Font picker, hotkey capture | Clear | clear |
| Update available | Skip this version | skip-version (new) |
| Choose styles, Columns, Fix minor errors, Search results | Select all / All / Check all | select-all (new) |
| Choose styles, Columns, Fix minor errors, Search results | None / Uncheck all | clear |

## Settings

Each Settings page gives every option row (`SettingsRow`) an icon. Check boxes stay without one.

| Page | Option | Role |
| --- | --- | --- |
| General | Tab name length | tab-name-length (new) |
| General | Questions hidden: Ask again; User folder: Open; Fonts folder: Choose… | notices; folder-open; folder-open |
| Subtitle grid | Start frame offset / End frame offset | time-offset-start / time-offset-end (new) |
| Subtitle grid | ASS tag replacement | hide-tags |
| Video | Seek on / Then play | video-to-start-time / play-line |
| Video | Preview subtitles at | preview-resolution (new) |
| Video | Preferred audio | video-streams |
| Audio | Play around the marker | play-before-mark |
| Audio | Line boundaries thickness | boundary-thickness (new) |
| Audio | Inactive lines | inactive-lines (new) |
| Audio | Long syllables for \kf | karaoke |
| Audio | Cache files kept | cache (new) |
| Conversion | Style | styles |
| Conversion | Time per letter | time-per-letter (new) |
| Conversion | Tags at the start of each line | tags-insert (new) |
| Automation | Last answers; Clear last answers… | dialog-memory; clear |
| Hotkeys | Map hotkey…; Restore default; Delete | hotkeys; restore-default; delete |
| Updates and privacy | Skipped version; Offer it again | skip-version; check-updates |
| Appearance | Preview text | preview-text (new) |

## Drawn for wave 8

The manifest marks these `pending` until their card places them (#410).

| Role | Card | For |
| --- | --- | --- |
| export | EX1 #448 | File › Export as… |
| plugins | PL4 #416 | the plugin manager |
| plugin-install | PL4 #416 | Install a plugin |
| plugin-unverified | PL6 #418 | an unverified plugin's warning |
| node-editor | NE2 #424 | the node-editor mode in the toolbar and View menu |
| ai-assistant | AI7 #435 | the agent panel |
| ai-send | AI7 #435 | Send a message |
| review-keep | AI2 #430 | Keep a change (Undo takes undo) |
| checkpoint | AI2 #430 | a turn checkpoint |
