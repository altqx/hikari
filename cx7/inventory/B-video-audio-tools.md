# CX7 inventory B: video, audio and visual-tool menus

Read from `origin/main` 8dbf88f41. Notation as `docs/ux/context-menus.md` lines 1-16: `|` separator, ▸ submenu, [x] check, (•) radio (`MenuRadioItem`), {dyn} data (counts as 1), "bound" = key column from `command`, "hard-coded" = key in a string or `shortcutText` literal. Icon column: role, "—" = none (exempt when check/radio/data row). Line numbers are `src/ui/…` unless given.

Baseline: CX2 approved menus 10–15 (built by CX5 #715 `1daae5047`, follow-up `2690dc1a5`; CX6 #721 `52fb5c3c2` for the rail). `git log 1d122c203..origin/main` per file is cited per menu.

---

## 1. Video: right-click the picture (panel)

Opens: right-release on the picture with the default tool (`Main.qml:5352`), or Menu key / Shift+F10 (`Main.qml:5302`, at the video cursor). Defined: `VideoContextMenu.qml:6-363`, instance `Main.qml:5391-5401` (prefix `videoMenu`). Listing: `tests/ui/context_menus.txt:192-226` ("Video").

| # | Item | Command | Enabled when / notes | Icon |
|---|---|---|---|---|
| 1 | Copy video position | VIDEO_COPY_COORDS | panel only (hidden in full screen); bound | copy-position |
| 2 | Play / Pause (label flips) | VIDEO_PLAY_PAUSE | has video; bound | media-play / media-pause |
| 3 | Stop | VIDEO_STOP | playing; bound | media-stop |
| 4 | [x] Full screen | VIDEO_FULL_SCREEN | has video; bound, falls back to fixed "F" (`:85`) | fullscreen |
| 5 | Full screen on ▸ | — | shown only with 2+ monitors (`:101`) | monitor |
| 5.1 | {dyn} Monitor N (N = 2…) | — | one per non-primary screen | monitor |
| | `|` | | (`:180`) | |
| 6 | Export frame ▸ | — | | frame-snapshot |
| 6.1 | Save as PNG… | VIDEO_SAVE_FRAME_TO_PNG | `canSnapshot`; bound | frame-snapshot |
| 6.2 | Copy | VIDEO_COPY_FRAME_TO_CLIPBOARD | bound | frame-copy |
| | `|` | | | |
| 6.3 | Save with subtitles as PNG… | VIDEO_SAVE_SUBBED_FRAME_TO_PNG | bound | frame-snapshot-subtitles |
| 6.4 | Copy with subtitles | VIDEO_COPY_SUBBED_FRAME_TO_CLIPBOARD | bound | frame-copy-subtitles |
| | `|` | | | |
| 6.5 | Save subtitles only as PNG… | VIDEO_SAVE_SUBTITLES_TO_PNG | bound | subtitles-snapshot |
| 6.6 | Copy subtitles only | VIDEO_COPY_SUBTITLES_TO_CLIPBOARD | bound | subtitles-copy |
| 7 | Audio track ▸ | — | shown when the video has audio tracks | video-streams |
| 7.1 | {dyn} (•) track | — | | — (data) |
| 8 | Chapters ▸ | — | shown when chapters exist | chapters |
| 8.1 | {dyn} (•) chapter, detail "[h:mm:ss.cc]" | — | | — (data) |
| | `|` | | (`:271`) | |
| 9 | [x] Render cost | GLOBAL_VIDEO_RENDER_COST | has video; bound | render-cost |
| 10 | Change aspect ratio | VIDEO_ASPECT_RATIO | bound; opens AspectRatioDialog | aspect-ratio |
| 11 | Reset zoom and pan | GLOBAL_RESET_VIDEO_ZOOM | zoomed; bound | zoom-reset |
| 12 | Filters ▸ | — | only when the player has filters (DirectShow), inserted after 11 (`:324-353`) | video-filters |
| 12.1 | {dyn} filter | — | per filter `enabled` | — (data) |
| | `|` | | (`:354`) | |
| 13 | Unload video | VIDEO_DELETE_FILE | video loaded; bound | close-video |

Items: 12 top level (13 with Filters), 21 in all (23 with Filters); 5 submenus (Full screen on, Export frame, Audio track, Chapters, Filters); 3 top separators + 2 in Export frame. Full-screen-only rows (Open editor, Open video…, Open subtitles…, Recently opened ▸, Progress bar, Full screen bar ▸) are hidden, not greyed.

## 2. Video: right-click in full screen

Opens: right-click on the full-screen picture, or Menu key / Shift+F10 at the video cursor (`VideoFullscreen.qml:89`, `openMenuAt` `:44-47`). Defined: same component, instance `VideoFullscreen.qml:496-505` (prefix `fullscreenMenu`). Listing: `context_menus.txt:227-261` ("Full-screen video").

| # | Item | Command | Enabled when / notes | Icon |
|---|---|---|---|---|
| 1 | Play / Pause | VIDEO_PLAY_PAUSE | bound | media-play / media-pause |
| 2 | Stop | VIDEO_STOP | playing; bound | media-stop |
| 3 | [x] Full screen (checked) | VIDEO_FULL_SCREEN | bound, falls back to fixed "Esc" (`:85`) | fullscreen |
| 4 | Full screen on ▸ {dyn} Monitor 2…N | — | 2+ monitors | monitor |
| 5 | Open editor | — | full screen only | editor |
| | `|` | | full screen only (`:123`) | |
| 6 | Open video… | GLOBAL_OPEN_VIDEO | bound | open-video |
| 7 | Open subtitles… | GLOBAL_OPEN_SUBS | bound | open-subtitles |
| 8 | Recently opened ▸ | — | full screen only | recent-subtitles |
| 8.1 | heading "Subtitles", {dyn} files | — | heading always shown | recent-subtitles |
| 8.2 | `|`, heading "Videos", {dyn} files | — | heading always shown | recent-video |
| | `|` | | | |
| 9–11 | Export frame ▸ (6 rows), Audio track ▸, Chapters ▸ | as panel | | as panel |
| | `|` | | | |
| 12 | [x] Progress bar | VIDEO_HIDE_PROGRESS_BAR | full screen only; bound | progress-bar |
| 13 | [x] Render cost | GLOBAL_VIDEO_RENDER_COST | bound | render-cost |
| 14 | Full screen bar ▸ (menu 3) | (VIDEO_FULLSCREEN_BAR key on the row) | full screen only; bound (Tab) | show-toolbar |
| 15 | Change aspect ratio | VIDEO_ASPECT_RATIO | bound | aspect-ratio |
| 16 | Reset zoom and pan | GLOBAL_RESET_VIDEO_ZOOM | zoomed; bound | zoom-reset |
| 17 | Filters ▸ {dyn} | — | DirectShow only | video-filters |
| | `|` | | | |
| 18 | Unload video | VIDEO_DELETE_FILE | bound | close-video |

Items: 17 top level (18 with Filters), 31 in all (33 with Filters); 7 submenus; 4 top separators + 1 in Recently opened + 2 in Export frame.

## 3. Full screen bar ▸ (FullscreenBarMenu)

Opens: submenu of menu 2, and the bar's mode button (`VideoFullscreen.qml:469-480`, instance `:513-518`). Defined: `FullscreenBarMenu.qml:6-29`.

| # | Item | Command | Notes | Icon |
|---|---|---|---|---|
| 1 | (•) Show when the pointer moves | — | help "Show on pointer movement, hide when idle" | — (radio) |
| 2 | (•) Always show | — | | — |
| 3 | (•) Never show | — | | — |

Items: 3, no submenus, no separators.

## 4. Full-screen speaker: audio output (AudioOutputMenu, `audio === null`)

Opens: right-click on the full-screen speaker (`VideoFullscreen.qml:456-459`), Menu key on the focused speaker (`:448`). Defined: `AudioOutputMenu.qml:7-192`, instance `VideoFullscreen.qml:506-512`.

| # | Item | Command | Notes | Icon |
|---|---|---|---|---|
| — | heading "Output device" (custom Item with audio-output icon, `:143-168`) | | always | |
| 1 | {dyn} (•) device | — | inserted at hand-counted `firstDevice: 5` (`:15`) | — (data) |
| | `|` | | | |
| 2 | Audio settings… | — | opens Settings › Audio | settings-audio |

Items: 2, no submenus, 1 separator.

## 5. Audio: Volume and output ▾ (same component, `audio` set)

Opens: the audio bar's speaker button (`Main.qml:6852-6866`), instance `:6893-6900`. Adds above menu 4: Volume slider, "Linked to vertical stretch" note, Speed segmented row (25–100 %), hint "Pitch is kept. %1 and %2 change the speed." (keys via KeycapSentence from bindings). Items: 2 (sliders and the speed row not counted).

## 6. Audio: overflow » ("More audio options")

Opens: » button at the bar's tail (`Main.qml:6544-6552`), always shown. Defined: `Main.qml:6553-6770`. Rows other than 19–20 show only while the bar folds them out.

| # | Item | Command | Enabled when / notes | Icon |
|---|---|---|---|---|
| 1–7 | Play before the mark, Play after the mark, Play 500 ms before the start, Play the first 500 ms, Play the last 500 ms, Play 500 ms after the end, Play to the end | AUDIO_PLAY_BEFORE_MARK … AUDIO_PLAY_TO_END | each when shown and folded (`:6590-6596`); bound | play-before-mark, play-after-mark, play-before-start, play-after-start, play-before-end, play-after-end, play-to-end |
| | `|` | | plays and commit both folded | |
| 8 | [x] Apply changes automatically | — | folded | auto-commit |
| 9 | [x] Go to the next line after applying | — | folded | next-after-commit |
| 10 | Go to selection | AUDIO_GOTO | folded; bound | go-to-selection |
| | `|` | | | |
| 11 | (•) Line timing | — | bar folded | — (radio) |
| 12 | (•) Karaoke | — | | — (radio) |
| 13 | [x] Auto split | — | karaoke | karaoke-split |
| 14 | Split syllable at the cursor | AUDIO_KARAOKE_SPLIT | karaoke, split arrow folded; bound | karaoke-split |
| 15 | Switch long syllables between \k and \kf | AUDIO_KARAOKE_KF | same; bound | karaoke |
| 16 | [x] Tap to time | AUDIO_TAP | karaoke; bound | karaoke-tap |
| 17 | Tap options ▸ (menu 8 without its Tap to time row) | — | karaoke | karaoke-tap |
| | `|` | | bar folded | |
| 18a | (•) Waveform | — | bar folded | — |
| 18b | (•) Spectrum | — | | — |
| 18c | [x] Speech spectrum | — | toggles folded | spectrum-nonlinear |
| | `|` | | toggles folded | |
| 19 | Go to selection start | AUDIO_GOTO_START | always; bound | video-to-start-time |
| 20 | Go to selection end | AUDIO_GOTO_END | always; bound | video-to-end-time |
| | `|` | | toggles folded | |
| 21 | [x] Auto-scroll | — | toggles folded | auto-scroll |
| 22 | [x] Sync with video | — | toggles folded | audio-video-sync |

Items: up to 24 top level, 34 in all (Tap options ▸ holds 10); 2 submenus (Tap options, its Speed); up to 5 top separators. Wide bar: 2 items.

## 7. Audio: Commit ▾ (tooltip "Options for applying changes")

`Main.qml:6336-6372`, arrow of the Apply changes split button (`:6312-6334`).

| # | Item | Command | Notes | Icon |
|---|---|---|---|---|
| 1 | Apply changes | — (keys via `shortcutText` = AUDIO_COMMIT or _ALT) | | commit |
| | `|` | | | |
| 2 | [x] Apply changes automatically | — | | auto-commit |
| 3 | [x] Go to the next line after applying | — | | next-after-commit |

Items: 3, 1 separator.

## 8. Audio: Tap ▾ (TapOptionsMenu) and overflow › Tap options ▸

Component `Main.qml:6090-6171`; Tap ▾ instance `:6484-6487` (split button `:6473-6483`, tooltip "Tap options").

| # | Item | Command | Notes | Icon |
|---|---|---|---|---|
| 1 | [x] Tap to time | AUDIO_TAP | Tap ▾ only (`tapRow`); bound | karaoke-tap |
| | `|` | | Tap ▾ only | |
| — | heading "Each syllable" | | | |
| 2 | (•) Hold the key: releasing it leaves a gap | — | | — |
| 3 | (•) Press the key: no gaps | — | | — |
| | `|` | | | |
| 4 | [x] Snap to vocal onsets | — | | — |
| | `|` | | (`:6145`) | |
| 5 | Speed ▸, detail "N%" | — | | playback-speed |
| 5.1 | {5} (•) 25% … 100% | — | | — |
| 6 | Calibrate latency…, detail "N ms" | — | opens Tap calibration window | tap-calibrate |

Items: 7 top level in Tap ▾ (6 in overflow), 11 in all (10); 1 submenu; 3 separators (2).

## 9. Audio: Play around ▾ (tooltip "Play around")

`Main.qml:6240-6271`. Check items that set the split button's default and play.

Rows: [x] Play 500 ms before the start, [x] Play the first 500 ms, [x] Play the last 500 ms, [x] Play 500 ms after the end, [x] Play to the end `|` [x] Play before the mark, [x] Play after the mark. All bound (`command: symbol`); icons play-before-start, play-after-start, play-before-end, play-after-end, play-to-end, play-before-mark, play-after-mark. Items: 7, 1 separator.

## 10. Audio: Karaoke split ▾ (tooltip "Syllable commands")

`Main.qml:6454-6471`: Split syllable at the cursor (AUDIO_KARAOKE_SPLIT, bound, karaoke-split), Switch long syllables between \k and \kf (AUDIO_KARAOKE_KF, bound, karaoke). Items: 2.

## 11. Audio: Zoom

`Main.qml:6801-6849`: Horizontal slider, Vertical slider, [x] Link the volume and stretch sliders (link) `|` Reset zoom (zoom-reset, no command), hint "Shift+wheel and Ctrl+wheel on the waveform zoom too". Items: 2, 1 separator.

## 12. Audio: Customise the audio panel

`Main.qml:6911-6991` (button `:6902-6910`).

| # | Item | Command | Notes | Icon |
|---|---|---|---|---|
| — | heading "Show on the panel" (hand-built Label, `:6931-6939`) | | | |
| 1 | [x] Play around button | — | | current play-around icon |
| | `|` | | | |
| 2–8 | [x] the 7 plays (labels as menu 6 1–7) | AUDIO_PLAY_* (key shown) | toggle the button only | play-* |
| | `|` | | | |
| 9 | [x] Apply changes automatically | — | | auto-commit |
| 10 | [x] Go to the next line after applying | — | | next-after-commit |
| 11 | [x] Go to selection | AUDIO_GOTO (key shown) | toggles the button only | go-to-selection |
| | `|` | | | |
| 12 | [x] Zoom and volume sliders | — | | link |
| | `|` | | | |
| 13 | Classic arrangement | — | | layout-restore |
| 14 | Default arrangement | — | | layout-reset |

Items: 14, 4 separators.

## 13. Visual tool rail » ("More tools")

Opens: » at the rail's foot when tools fold (`VisualToolRail.qml:68-87`; TabFocus, so keyboard-reachable). Defined: `VisualToolRail.qml:89-115`. Rows: {dyn} [x] folded tools with tool icons, active one checked, help = tooltip. Items: 1.

## 14. Full screen bar: Visual tools

Opens: the bar's tool button (`VideoFullscreen.qml:426-435`). Defined: `VideoFullscreen.qml:519-540`. Rows: {dyn} [x] every rail tool (14 families incl. Gradient, Text on path), active checked. Items: 1.

## 15. Visual tool option choice menu

Opens: a choice button on the visual tool bar (`VisualToolOption.qml:153-158`). Defined: `VisualToolOption.qml:160-194`. Rows: {dyn} [x] choices (shape-unite/subtract/intersect/exclude icons on Pathfinder choices, else none), and for list-end options `|` Edit shapes… (edit). First entry "Free drawing". Items: 2, 1 separator.

## 16. Align and distribute ▸

Opens: Grid row menu (`Main.qml:7658`), between Change alignment ▸ and Position to margins. Defined: `AlignDistributeMenu.qml:6-95`. Listing `context_menus.txt:144-157`.

| # | Item | Command | Notes | Icon |
|---|---|---|---|---|
| — | heading "Relative to" | | | |
| 1 | (•) Active line | — | setting grid.alignToVideoFrame | — |
| 2 | (•) Video frame | — | | — |
| | `|` | | | |
| 3 | Align left edges | GLOBAL_ALIGN_LEFT_EDGES | bound | align-left |
| 4 | Align centres | GLOBAL_ALIGN_CENTRES | bound | align-center |
| 5 | Align right edges | GLOBAL_ALIGN_RIGHT_EDGES | bound | align-right |
| | `|` | | | |
| 6 | Align top edges | GLOBAL_ALIGN_TOP_EDGES | bound | align-top |
| 7 | Align middles | GLOBAL_ALIGN_MIDDLES | bound | align-middle |
| 8 | Align bottom edges | GLOBAL_ALIGN_BOTTOM_EDGES | bound | align-bottom |
| | `|` | | | |
| 9 | Distribute horizontally | GLOBAL_DISTRIBUTE_HORIZONTALLY | bound | distribute-horizontal |
| 10 | Distribute vertically | GLOBAL_DISTRIBUTE_VERTICALLY | bound | distribute-vertical |

Submenu row icon: align-center. Items: 10, 3 separators.

## 17. Phantom ▸

Opens: Line editor text menu (`Main.qml:4864-4868`), enabled when the field is editable and has a line. Defined: `PhantomMenu.qml:6-27`. Listing `context_menus.txt:36-38`. Rows: Keep the start aligned (GLOBAL_PHANTOM_KEEP_START, bound, align-left), Keep the end aligned (GLOBAL_PHANTOM_KEEP_END, bound, align-right). Submenu icon: phantom. Items: 2.

## 18. Node effects: Graph menu (») and From a preset ▸ / From the library… ▸

Opens: » button "Graph menu" on the node mode bar (`NodeModeBar.qml:70-79`). Defined: `NodeModeBar.qml:261-336`; submenus `NodePresetMenu.qml:7-50`, `NodeLibraryMenu.qml:6-26` (also opened alone from the empty canvas buttons, `NodeGraphCanvas.qml:783-815`).

| # | Item | Command | Enabled when / notes | Icon |
|---|---|---|---|---|
| 1 | New graph | — | editable | add |
| 2 | From a preset ▸ | — | editable | node-editor |
| 2.1 | {dyn} karaoke presets (no heading) | — | tooltip + status help | node-editor |
| | `|` | | | |
| 2.2 | heading "For selected lines", {dyn} typesetting graphs | — | | graph-* |
| 3 | From the library… ▸ {dyn} | — | library not empty | node-library |
| | `|` | | | |
| 4 | Rename… | — | | edit |
| 5 | Duplicate | — | | duplicate |
| 6 | Save to the library | — | has graph | save-as |
| 7 | Export as template lines ▸ | — | has graph, editable | export |
| 7.1 | [x] karaOK templates | — | check = last format, row also runs the export | notice-info |
| 7.2 | [x] Aegisub 3.2.2 templates | — | same | — |
| | `|` | | | |
| 8 | Delete graph | — | not graph 0, editable | delete |

Items: 8 top level, 14 in all; 3 submenus; 2 top separators + 1 in From a preset.

## 19. Node effects: Apply ⌄ ("Apply options")

`NodeModeBar.qml:338-358`, button `:191-203` (text glyph "⌄"). Rows: Apply to every karaoke line (karaoke; hidden for typesetting graphs), Apply to the selected lines (select-lines; help explains why disabled). Items: 2.

## Not menus in this area

Perspective tracking, Motion data and Extend tracking are windows with ComboBoxes (`PerspectiveTrackingWindow.qml:102`, `ExtendTrackingWindow.qml:85`); their only menu rows are the menu bar's Video › Tracking ▸ (out of scope). The visual tool bar's » opens a popover, not a ShellMenu (`VisualToolBar.qml:248-258`). `NodeAddPopup.qml` is a searchable list popup. The visual overlay's right button goes to the active tool, not a menu, except through menu 1.

---

## Changed since CX2 (1d122c203)

- **Video panel / full screen (menus 1–2):** built as approved menus 10–11 by CX5 #715 (`1daae5047`, `2690dc1a5`), including FA9 #566's subtitles-only rows (`6109dc837`, via #762). No row added, removed, renamed or moved since. Not in the drawing: Filters ▸ (pre-existing, DirectShow only) now sits after Reset zoom and pan inside the view group instead of after Unload video. `context_menus.txt` matches both listings.
- **Full screen bar ▸ (3):** as approved 12 (CX5 #715). No change since.
- **Speaker (4):** as approved 13 (CX5 #715). No change since.
- **Full-screen Tools (14):** rows are data; AU7 #893 (`101773d92`) added Gradient, AU14 #994 (`a1fa72560`) Text on path.
- **Audio overflow (6):** as approved 14 except renames by the text-style pass #926 (`8247382fc`): "Auto-commit" → "Apply changes automatically", "Go to the next line after commit" → "Go to the next line after applying" (text-style owner decision 3; same in 7, 12 and the toolbar). Go to selection start/end from FA10 #684 (`922b9e345`) as drawn. Tap options ▸ has a separator between Snap to vocal onsets and Speed ▸ that the drawing does not (`Main.qml:6145`, from CX5 itself).
- **Commit ▾ (7):** approved 15 "Commit" row is "Apply changes"; toggles renamed as above (#926).
- **Tap ▾ (8):** as approved 15 (Tap to time check item).
- **Customise (12):** toggles renamed as above (#926).
- **Rail » (13):** as approved/CX6 #721 (`52fb5c3c2`, check items). Data rows grew with AU7 #893 and AU14 #994.
- **Visual tool option choices (15):** not in CX2 (left as is). Pathfinder icons on choices added by AU6 #1004 (`8239abdcc`); new options with AU7 #893, AU14 #994.
- **Align and distribute ▸ (16), Phantom ▸ (17):** new, AU21 #1011 (`067f0b859` rows, `7e5f72096` icons and wording).
- **Node effects (18–19):** new. NE2 #826 (`8adb1acc9`: graph menu, From a preset, From the library…), NE4 #940 (`0e2f749e9`: Export as template lines), TH5d #981 (`c401f1eee`: karaOK / Aegisub 3.2.2 rows), AU9 #1025 (`34a2af941`, `8e988fcd3`: "For selected lines" heading and typesetting graphs, Apply to the selected lines).

---

## Findings

**B1 inconsistent-name: the audio play commands have up to three names.** Menu/Customise label, toolbar tooltip and Settings › Shortcuts differ for the same command: "Play the first 500 ms" / tooltip "Play 500 ms after the start time" / shortcut "Play first 500 ms"; "Play 500 ms before the start" / "Play 500 ms before the start time" / "Play 500 ms before"; "Play before the mark" / same / "Play before the marker". Evidence `Main.qml:5773-5794`, `src/ui/command_names.cpp:30-35`. Recommend one name per command everywhere, the menu's: Play 500 ms before the start, Play the first 500 ms, Play the last 500 ms, Play 500 ms after the end, Play to the end, Play before the mark, Play after the mark (tooltip = label; rename the 6 shortcut names). Visible change: yes.

**B2 inconsistent-name: Video › Streams ▸ (menu bar) and the video menu's Audio track ▸ list the same rows under two names and two controls.** `VideoStreamsMenu.qml:11` "Streams" with square check items (`:16-23`); `VideoContextMenu.qml:228-242` "Audio track" with radios. The data is audio tracks only (`src/ui/video_controller.cpp:151-165`). Recommend menu bar title "Audio track" and `MenuRadioItem` rows in `VideoStreamsMenu.qml` and `VideoChaptersMenu.qml`. Visible change: yes (menu bar; outside the right-click scope, raise with the menu-bar owner).

**B3 wrong-target: Full screen on ▸ never offers the primary monitor and shows no current monitor.** Rows are screens 1…N−1 labelled "Monitor 2…N" (`VideoContextMenu.qml:103-111`, `src/application/video_fullscreen.cpp:24-25`); in full screen on Monitor 2 the menu offers only Monitor 2 again and no way to move to Monitor 1. Recommend radio rows Monitor 1…N, the current one checked in full screen; in the panel keep all monitors. Visible change: yes.

**B4 headings: Recently opened ▸ shows empty headings.** "Subtitles", the separator and "Videos" are unconditional (`VideoContextMenu.qml:144-152`); with no recent videos the menu ends on a bare "Videos" heading, with none at all it holds two headings and a separator. Recommend hide each heading (and the separator) when its list is empty, and `shown: fullscreen && (subtitles.length + videos.length) > 0` on the submenu. Visible change: yes.

**B5 inconsistent-name: overflow and options tooltips.** text-style.md "One name per thing" names the ⋯ tooltip "More options" and lists "More audio options" under "Not". Evidence `Main.qml:6549` "More audio options", `VisualToolRail.qml:75` "More tools", `NodeModeBar.qml:74` "Graph menu"; the visual tool bar already says "More options" (`VisualToolBar.qml:255`). Recommend "More options" for the audio overflow and the graph menu button. "More tools" on the rail says what the overflow holds; keep it unless the owner wants one word for every overflow (uncertain). Visible change: yes.

**B6 wrong icon: the node graph menu button uses » for an object's options menu.** `NodeModeBar.qml:73` `menu-more`; CX6 rule and `docs/ux/icons.md` (D3): » only for overflow, `panel-menu` (⋯) for options of a panel or object. Recommend `iconRole: "panel-menu"`. Visible change: yes.

**B7 text-style: "From the library…" has an ellipsis but opens a submenu.** `NodeLibraryMenu.qml:10`, button `NodeGraphCanvas.qml:796`; its sibling "From a preset" (`NodePresetMenu.qml:11`) has none. Recommend "From the library" in both places. Visible change: yes.

**B8 toggle-hides-state: Export as template lines ▸ rows are check items that also run the export, and only one has an icon.** `NodeModeBar.qml:307-325`, `exportAs` `:18-23` re-binds `checked` then exports; "karaOK templates" carries `notice-info` (a notice icon, not an export), "Aegisub 3.2.2 templates" none. Recommend plain action rows "karaOK templates" and "Aegisub 3.2.2 templates", no check, both `iconRole: "export"` (or a new `export-template` role), keeping the help text. Visible change: yes.

**B9 inconsistent-name: audio bar tooltips differ from the menu rows they fold into.** Approved 15 says tooltips give the menus' names. Evidence `Main.qml:6492` "Waveform mode" / 6501 "Spectrum mode" vs menu "Waveform"/"Spectrum" (`:6713-6730`); `:6516` "Auto-scroll to the active line" vs "Auto-scroll"; `:6526` "Sync the video and audio cursors" vs "Sync with video"; `:6444` Auto split "Enable / Disable automatic splitting of syllables" (the slash toggle form text-style.md forbids) vs "Auto split"; `:6208` "Play the current syllable / line". Recommend tooltips Waveform, Spectrum, Auto-scroll, Sync with video, Auto split; Play: "Play the syllable or line" (uncertain wording; owner pick). Visible change: yes.

**B10 keys: Customise rows show shortcuts for commands they don't run.** `ShownPlay` sets `command: play.symbol` (`Main.qml:6929`) and Go to selection sets `command: "AUDIO_GOTO"` (`:6972`) on check rows that only show or hide a toolbar button; the key column advertises a key that plays audio instead. Recommend drop `command` from both (keep the icons). Visible change: yes (key column empties).

**B11 text-style: Change aspect ratio opens a dialog that asks for a value but has no "…".** `VideoContextMenu.qml:312`; AspectRatioDialog has OK/Cancel (`AspectRatioDialog.qml:95-122`). Shortcut names also lack it: "Change aspect ratio", "Open video", "Open subtitles" (`command_names.cpp:224, 187-188`) while the menus say "Open video…". Approved menu 10 drew it without "…", before text-style's ellipsis rule; flag for the owner. Recommend "Change aspect ratio…", and "Open video…", "Open subtitles…" in Shortcuts. Visible change: yes.

**B12 icons: one icon, two meanings.** Align and distribute ▸ uses `align-center` (`AlignDistributeMenu.qml:12`), the same as its own Align centres row (`:46`); Phantom's rows use `align-left`/`align-right` (`PhantomMenu.qml:16,23`), the icons of Align left/right edges; Customise › Zoom and volume sliders uses `link` (`Main.qml:6978`), the icon of Link the volume and stretch sliders (`:6825`). Recommend new roles `align-distribute` (submenu), `phantom-keep-start`/`phantom-keep-end`, and `audio-sliders` (or `audio-zoom`) for the Customise row. Visible change: yes. (Auto split `karaoke-split` = Split syllable at the cursor, `:6661` vs `:6674`, is the same pattern but was drawn that way in approved 14; lower priority.)

**B13 headings: From a preset ▸ heads only its second group.** `NodePresetMenu.qml:29-33` adds "For selected lines" above the typesetting graphs; the karaoke presets above have no heading. Recommend heading "For karaoke lines" above the first group (matches "Apply to every karaoke line"). Visible change: yes.

**B14 inconsistent-name: chapter rows in the menu bar glue the time to the label, and both menus bracket it.** `VideoChaptersMenu.qml:52` `label + "  " + time` (a string built from pieces, not `detailText`); the time is `"[%1]"` (`src/ui/video_controller.cpp:191`), drawn as "[0:01:30.00]" where approved 10 drew a dimmed "0:01:30". Recommend `detailText: modelData.time` in the menu bar and no brackets. Visible change: yes.

**B15 grouping: Tap options ▸ puts Snap to vocal onsets alone between two separators.** `Main.qml:6145`; approved 14 drew Snap, Speed ▸ and Calibrate latency… as one group. Recommend remove that separator. Visible change: yes. Low.

**B16 flat-list / submenu rows repeat the parent's words.** Align and distribute ▸ rows all start "Align …"/"Distribute …" (`AlignDistributeMenu.qml:41-92`); Phantom ▸ drops its word. Optional: headings "Align" and "Distribute" with rows Left edges, Centres, Right edges, Top edges, Middles, Bottom edges, Horizontally, Vertically (shortcut names keep the full names). Judgement call, since the parent names two verbs. Visible change: yes. Low.

**B17 keys: the Exit full screen tooltip writes the key by hand.** `VideoFullscreen.qml:484-488` builds `"Exit full screen" + " (" + key + ")"` with a translated "Escape", while the menu shows "Esc" (`VideoContextMenu.qml:85`, text-style "Esc") and the mode button uses `shell.bitmapTip` (`:475`). Recommend `tip: fs.shell.bitmapTip(qsTr("Exit full screen"), "VIDEO_FULL_SCREEN", 3)` with the fixed Esc fallback inside the helper. Visible change: yes (tooltip only).

**B18 text-style (uncertain): "Audio settings…" carries an ellipsis but opens a window you look at.** `AudioOutputMenu.qml:189`; text-style.md says no "…" on Settings and other window names. Approved 13 drew "Audio settings…" and Grammar settings… (`SpellCheckerPanel.qml:138`) does the same, so this needs an owner rule for "<area> settings" rows. Recommend "Audio settings" if the rule applies. Visible change: yes.

**B19 non-visible cleanups.** (a) Customise's heading is a hand-built `Label` copying `MenuHeading` (`Main.qml:6931-6939`); use `MenuHeading`. (b) Filters ▸ and its rows use fixed `objectName`s "videoMenuFilters"/"videoMenuFilter<i>" in both instances (`VideoContextMenu.qml:329,336`); use `itemPrefix`. (c) Filters ▸ is a conditional fixed row but is missing from `context_menus.txt` (Instantiator-built; whether it counts as a data row is uncertain). Visible change: no.

Clean: Full screen bar ▸ (3), speaker menu (4), Commit ▾ (7), Play around ▾ / Karaoke split ▾ / Zoom (9–11), rail » and full-screen Tools (13–14, active tool checked), Apply ⌄ (19). Panel-only versus full-screen-only rows are hidden, not greyed, in menus 1–2; Unload video and Delete graph sit last after a separator; toggles are check items; MenuKey reaches menus 1, 2, 4 and the rail's » (audio bar buttons are `NoFocus`, so menus 5–12 have no keyboard route; CX2 did not ask for one).
