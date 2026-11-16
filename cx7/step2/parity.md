# Parity: Aegisub and HikariSub (CX7 #810, reconciled)

For the homepage, the README and *Coming from Aegisub* ("Everything you do in Aegisub, plus …"; owner Q486, Q487). Not published, not reviewed. Read-only research; nothing was built or run.

## Sources and method

| Source | Revision | Use |
| --- | --- | --- |
| hikari-site draft `parity.md` and `additions.md` | 2026-11-13, read-only copies in `hikari-wt/wave8/cx7-site-beta4/` | The starting point. Every row, its wording and its structure are kept; each was checked again. |
| HikariSub app | `main` at `8dbf88f41` (2026-11-16) | Every row's status. The draft checked the guide first and `main` `14939fe` second; here every row cites the app at `8dbf88f41`. |
| Aegisub 3.5 source | `/home/altq/Work/hikari-wt/ref/aegisub-3.5` (read as data) | Every `CMD_NAME` in `src/command/*.cpp`, the `dialog_*`, `subtitle_format_*`, provider, `auto4_*` and `export_*` files, `preferences.cpp`, `automation/autoload`. Line numbers are from this tree. The draft read TT master `d8ad830`. |
| FA1 audit | `docs/research/aegisub-feature-audit.md` | The fork and TypesettingTools 3.4–3.5.1 rows. Its status column is from 2026-11-02 and was not trusted. |
| Legacy feature ledger | `src-coverage-ledger.md` (the removed `docs/qt/coverage.md`) and `docs/compatibility-decisions.md` | The legacy 0.0.x rows and the named departures (anchors as `#FA9-clip-places`). |

**Statuses** (the draft's words):

- **same:** HikariSub does what Aegisub does, perhaps differently.
- **better:** the same, plus a concrete difference you can check.
- **not yet:** missing, or only partly there. The row says in one sentence what is missing.
- **different on purpose:** a named departure in `docs/compatibility-decisions.md`, linked.
- **unverified:** not settled by the source; listed again at the end.

**Evidence abbreviations.** `hk:N` is `src/application/hotkeys.cpp` line N (the command id follows). `mb:N` is `tests/ui/menu_bar.txt` line N and `cm:N` is `tests/ui/context_menus.txt` line N: the test-pinned listings of the real menus. A bare path is in altqx/hikari at `8dbf88f41`. `guide:` is a hikari-site page under `/en/docs/`. In **From**, `A:` is the Aegisub 3.5 tree's `src/`.

**Change column.** Empty when the draft's status still holds. Otherwise `old → new`. **New** marks a row the draft lacked, **split** a row cut in two, **moved** a row that changed section, and **evidence** a row whose status holds but whose "what is missing" sentence changed.

## Aegisub features

### Files and formats

| Feature | From | HikariSub | Where in HikariSub | Evidence | Change |
| --- | --- | --- | --- | --- | --- |
| New, open, save, save as, close subtitles | A:command/subtitle.cpp:236, 265, 365, 382, 253 | better: a failed save never leaves half a file, and lines other programs added are kept byte for byte | File › Open…, Save, Save as…, Close | mb:2, 7, 12, 14; hk:154, 156, 210, 262; `src/application/document_files.cpp:233`; guide: basics/files-and-formats#saving | |
| Recent subtitles, video, audio, keyframes, timecodes | A:command/recent.cpp:50–99 | same | File › Recently opened subtitles; Video › Recently opened videos, keyframes, timecodes; Audio › Recently opened audio | mb:4, 111, 114, 117, 161 | |
| Open SRT, MicroDVD, SSA, plain text | A:subtitle_format_srt.cpp:269, _microdvd.cpp:53, _ass.h:23, _txt.cpp:48 | better: also opens MPL2, TMPlayer, LRC, TTML, KRC/QRC lyrics and YouTube srv3, and reads the format from the content, not the extension | File › Open…; File › Import lyrics… | `src/core/subtitle_load.cpp:21–39`, `src/core/line_formats.cpp`, `src/core/lyrics_import.cpp`, `src/core/srv3_import.cpp`; mb:11 | |
| Open MPEG-4 Streaming Text (TTXT) | A:subtitle_format_ttxt.cpp:48 | not yet: TTXT files don't open. | — | No `ttxt` in `src/` | |
| Plain text import options (actor separator, comment starter, blank lines) | A:dialog_text_import.cpp:54–61 | not yet: plain text opens one line per row, with no options. | — | `src/core/line_formats.cpp`; no import options in `src/ui` | |
| Open subtitles from the video (MKV tracks), compressed tracks included | A:command/subtitle.cpp:315; FA1 §2b "Compressed subtitles read from MKV" | same | File › Import subtitles from MKV… | mb:10; hk:147 GRID_SUBS_FROM_MKV; `vcpkg.json:16` (FFmpeg `zlib` on) | |
| Open with a chosen charset, and legacy charsets | A:command/subtitle.cpp:295 | not yet: subtitles open as UTF-8 only, and bytes that aren't UTF-8 are kept as they are; only Import lyrics and Paste translation detect other charsets. | — | `src/core/ass_load.cpp:920` (InvalidUtf8 kept opaque); `src/backends/legacy_text_file.cpp:322` (uchardet, used by `src/app/application.cpp:483, 4473` only) | |
| Script properties (credits, resolution, LayoutRes, YCbCr matrix, wrap style, scale border) | A:command/subtitle.cpp:332; FA1 §3 "Edit LayoutRes" | better: also sets colliding lines, and can write your credits into every file on save | Subtitles › Script properties | mb:98; `src/ui/ScriptPropertiesDialog.qml`; [892-script-info-on-save](../../../docs/compatibility-decisions.md#892-script-info-on-save) | |
| Script properties: Synch point and Update details fields | A:dialog_properties.cpp:121, 123 | not yet: the window has no Synch point or Update details field. | — | `src/ui/ScriptPropertiesDialog.qml:107–118` (Title, Author, Translator, Proofreading, Timer, Editing) | |
| Attachments manager (attach and extract fonts and graphics) | A:command/subtitle.cpp:77; A:dialog_attachments.cpp | not yet: there's no window to attach or extract files; attached fonts are only read for drawing. | — | `src/core/ass_attachments.cpp`; no attachment window in `src/ui` | |
| Prompt before loading scripts a subtitle file names | FA1 §3 "CVE-2026-92705" | same | A bar asks before loading the file's scripts | `src/app/automation_shell.cpp:926` | |
| Linked media: load associated files, report missing ones | FA1 §3 "No prompt for missing linked media" | same | The association bar: "Linked files not found:" | `src/app/application_tab_menu.cpp:248` | |
| Save UI state in the subtitle file | A:preferences.cpp:103 | same | The `[Hikari]` section of the file | `src/core/include/hikari/core/document.h:159` | |
| Keep `[Aegisub Extradata]` (folds, perspective plane) through load and save | FA1 §2a "Extradata storage" | same | Automatic | `src/core/extradata.cpp`; [XD1-karaoke-marks](../../../docs/compatibility-decisions.md#xd1-karaoke-marks) | |

### Subtitle grid

| Feature | From | HikariSub | Where in HikariSub | Evidence | Change |
| --- | --- | --- | --- | --- | --- |
| Columns, CPS, show or hide columns | A:preferences.cpp (Grid); legacy ui-inventory §SubsGrid | better: empty columns hide themselves, and a Wraps column | Grid header right-click; Grid ⋯ › Columns | cm:170–183, 276–290; hk:98–108 | |
| CPS warning and line-length thresholds you can set | A:preferences.cpp:283 | not yet: the CPS warning is fixed at 15, and there's no line-length threshold. | — | `src/ui/line_table_model.cpp:185` (`cps > 15`) | |
| Next and previous line, next line creating one at the end | A:command/grid.cpp:49, 60, 88 | same | Line editor and Grid keys | hk:225–226 GLOBAL_PREVIOUS_LINE/NEXT_LINE, hk:69 EDITBOX_COMMIT_GO_NEXT_LINE | |
| Select all, select lines visible on the video | A:command/subtitle.cpp:394, 407 | same | Ctrl+A in the Grid; Grid › Select lines visible on video | `src/ui/line_grid.cpp:509`; cm:161; hk:115 | |
| Insert before or after, also at video time | A:command/subtitle.cpp:133, 167, 178, 209 | better: also at the video frame's time | Grid › Insert › Before, After, at video time, at video frame time | cm:63–70; hk:109–114 | |
| Copy, cut, paste, delete, duplicate lines | A:command/edit.cpp:604, 627, 851, 644, 722 | same | Grid › Cut, Copy, Paste, Duplicate, Delete | cm:71, 75–77, 166; hk:124, 128–130, 231 | |
| Paste lines over (choose which fields) | A:command/edit.cpp:883 | same | Grid › Paste special › Copy columns, Paste columns | cm:79–80; hk:150–151 | |
| Swap two lines | A:command/grid.cpp:380 | same | Grid › Swap lines | cm:110; hk:123 | |
| Move lines up or down one row | A:command/grid.cpp:346, 363 | not yet: there's no command to move lines up or down a row. | — | No such id in `src/application/hotkeys.cpp` | |
| Sort all or selected lines by start, end, style, actor, effect, layer | A:command/grid.cpp:99–239 | better: also by text and by text without tags (from the fork, FA1 §2a) | Edit › Sort all lines, Sort selected lines | mb:32–49; hk:244–259 | |
| Tag hiding: show, hide, simplify, cycle | A:command/grid.cpp:251, 276, 292, 308 | not yet: there's no command that cycles between the modes; View › Hide tags swaps each block for ☀, and tags vanish only if that replacement is set to nothing in Settings. | View › Hide tags | mb:230; hk:183; `src/ui/line_table_model.cpp:200–207`; `src/application/settings.cpp:166` | evidence |
| Grid highlights the lines on the current frame | A:preferences.cpp:290 | not yet: the Grid doesn't mark the lines shown at the video frame. | — | `src/ui/line_table_model.h:56–92` (no role for the video frame) | unverified → not yet |
| Line folding (arch1t3cht's fork) | FA1 §2a "Line folding" | better: folds convert to line groups, which nest, and plain folds are kept on save | Grid › Convert Aegisub folds to line groups | cm:115, 189; hk:336; `src/core/aegisub_folds.cpp` | |
| Smooth touchpad scrolling | FA1 §2a "Smooth touchpad scrolling" | same | The Grid | `src/ui/line_grid.cpp` `wheelEvent` | |

### Editing

| Feature | From | HikariSub | Where in HikariSub | Evidence | Change |
| --- | --- | --- | --- | --- | --- |
| Bold, italic, underline, strikeout, font, the four colours | A:command/edit.cpp:389–485 | better: works on the selected text and restores the style after it; up to 20 tag buttons of your own | Line editor tag buttons | hk:58–60, 64–67, 70–71; `src/application/settings.cpp:250–269` | |
| Colour picker with screen dropper, RGB, HSL, HSV, recent colours | A:dialog_colorpicker.cpp | same | Line editor colour buttons | `src/ui/ColourPopover.qml`; `src/ui/screen_sampler.cpp:36` | |
| Syntax highlighting, tag help while typing | A:preferences.cpp:274 | same | Line editor; type `\` for the tag list | `src/core/editor_syntax.cpp`; `src/ui/tag_list_controller.h` | |
| Drawing and clip coordinates in alternating colours; mixin lines as templates | FA1 §2a | same | Line editor | `src/core/include/hikari/core/editor_syntax.h:28, 40`; `src/core/editor_syntax.cpp:14` | |
| Insert hard space `\h`; Shift+Enter inserts `\n` (setting) | FA1 §3 "Insert hard space"; A:preferences.cpp:276 | same | Edit › Insert hard space; Settings › Editing | mb:68; hk:322; `src/application/settings.cpp:333` (`editor.softLineBreak`) | |
| Times past 10 hours | FA1 §2a "Timestamps past 10 h" | same | Time fields go to 99:59:59.99 | `src/application/include/hikari/application/editor_fields.h:89` | |
| Show times or frames in the fields | Aegisub edit box | same | Line editor time fields | `src/application/settings.cpp` `editor.timesToFramesSwitch` | |
| Split lines before or after the current frame | A:command/edit.cpp:733, 745 | same | Grid › Split › At video time | cm:98; hk:116 | |
| Split at the text cursor (estimate times, keep times, at the video frame) | A:command/edit.cpp:1113, 1129, 1140 | not yet: nothing splits a line at the text cursor. | — | No such id in `src/application/hotkeys.cpp` (EDITBOX_SPLIT_LINE inserts `\N`) | |
| Join lines: concatenate, keep first | A:command/edit.cpp:798, 809 | better: also keep last, and F4/F5 join with the line before or after | Grid › Join › Join, Keep first, Keep last | cm:94–96; hk:125–127, 227–228 | |
| Join lines as karaoke | A:command/edit.cpp:787 | not yet: there's no join that turns each line into a `\k` syllable. | — | No such id in `src/application/hotkeys.cpp` | |
| Split lines by karaoke syllables | A:command/edit.cpp:1045 | not yet: no split by syllable; Split › At tag blocks folds `\k` syllables into one delay instead. | — | No such id; [AU10-splits-and-overlaps](../../../docs/compatibility-decisions.md#au10-splits-and-overlaps) D2 | |
| Recombine lines that were split and merged | A:command/edit.cpp:967 | not yet: there's no recombine command. | — | No `recombine` in `src/` | |
| Show Original pane, Revert, Insert Original | A:command/edit.cpp:1209, 1254 | not yet: Esc drops an unapplied draft, but there's no pane with the line as it was and no Insert Original. | — | No such id in `src/application/hotkeys.cpp` | |
| Clear text, or clear text but keep the tags | A:command/edit.cpp:1222, 1236 | not yet: Delete text empties the whole text; nothing keeps the tags. | Grid › Delete text | cm:165; hk:232 | |
| Undo and redo | A:command/edit.cpp:1157, 1183 | better: a History window jumps to any step, and Undo to last save | Edit › Undo, Redo, Undo to last save, History | mb:30–31, 50–51; hk:159–162 | |
| Thesaurus in the text's right-click menu | A:thesaurus.cpp | not yet: right-click offers spelling and grammar only. | — | cm:1–50; no thesaurus in `src/` | |

### Timing and audio

| Feature | From | HikariSub | Where in HikariSub | Evidence | Change |
| --- | --- | --- | --- | --- | --- |
| Open audio, from video, close; audio track choice | A:command/audio.cpp:75, 114, 63 | better: Reload audio keeps the place and track; decoded audio is cached on disk | Audio › Open audio, Open audio from video, Close audio, Reload audio | mb:160–165; hk:171–173, 325 | |
| Blank or noise audio for testing | A:command/audio.cpp:92, 103 | not yet: there's no noise audio and no standalone blank audio; a dummy video brings silence. | — | hk:214 GLOBAL_OPEN_DUMMY_AUDIO is retired: `src/application/settings_import.cpp:372` | |
| Waveform and spectrum display | A:command/audio.cpp:131, 147, 506 | better: a Speech spectrum mode | Audio panel buttons: Spectrum mode, Speech | `src/ui/Main.qml:6497–6535` | |
| Spectrum with log power and log frequency mapping | FA1 §2c; A:preferences.cpp:462 | not yet: Speech mode maps frequency non-linearly, but power stays linear and there's no choice of curve. | Audio panel › Speech | `src/application/audio_spectrum.cpp:345–367`; `src/ui/Main.qml:6530` | evidence |
| Play selection, line, toggle, stop; 500 ms before, after, first, last; to the end | A:command/audio.cpp:191–323, 247 | better: also play around a marker, and slower playback at 25 to 75% with the pitch kept | Audio keys and panel | hk:20–31; hk:268–269 AUDIO_SPEED_DOWN/UP | |
| Commit, commit and next, commit and stay; auto commit, auto next | A:command/audio.cpp:336, 368, 383, 472, 489 | same | Audio keys; Settings › Audio | hk:19 AUDIO_COMMIT; `src/ui/audio_controller.cpp:1100`; `src/application/settings.cpp:64, 88` | |
| Commit and reset the next line to the default timing | A:command/audio.cpp:353 | not yet: there's no commit that resets the next line's times. | — | No such id in `src/application/hotkeys.cpp` | |
| Go to selection, its start, its end; scroll left and right; auto scroll | A:command/audio.cpp:395, 407, 418, 429, 440, 455 | same | Audio keys; panel Auto-scroll | hk:32–34, 270–271; `src/application/settings.cpp:66` | |
| Horizontal and vertical zoom, volume | A:command/audio.cpp; FA1 §2c "Vertical audio zoom" | same | Audio panel sliders; Ctrl+wheel | `src/ui/audio_controller.cpp` `applyVerticalZoom` | |
| Link vertical zoom and volume sliders | A:command/audio.cpp:523 | same | Audio panel: Link the volume and stretch sliders | `src/ui/Main.qml:5726–5744, 6826`; `src/ui/audio_controller.cpp:719–733`; `src/application/settings.cpp:84` (`audio.link`) | not yet → same |
| Save an audio clip of the line | A:command/audio.cpp:163 | not yet: audio can't be saved to a file. | — | No such id in `src/application/hotkeys.cpp` | |
| Video position marker in the audio, snapping to frame middles | FA1 §3 "Reworked video-position marker" | same | Audio panel | `src/application/settings.cpp:74` (`audio.drawVideoPosition`) | |
| Audio display shows inactive lines (none, previous, previous and next, all) | A:preferences.cpp:183 | same | Settings › Audio | `src/application/settings.cpp:77`; `src/ui/SettingsDialog.qml:1363` | new |
| Mono playback | FA1 §2c "Downmix option" | different on purpose: Play in mono covers the audio panel and editing playback, not general video playback | Settings › Audio › Playback › Play in mono | `src/application/settings.cpp:312`; [FA19-mono-scope](../../../docs/compatibility-decisions.md#fa19-mono-scope) | same → different on purpose |
| Audio player and device choice | A:preferences.cpp:486; A:audio_player_*.cpp | better: playback moves to the default device when yours is unplugged, and back when it returns | Settings › Audio › Output device | `src/application/settings.cpp:305–309`; docs/media.md:101 | |
| Snap to keyframes and other lines while dragging | A:preferences.cpp (Snap markers by default) | same | Audio panel | `src/application/settings.cpp:90–91` | |
| Medusa mode: a toggle that makes the timing keys work anywhere | A:command/app.cpp:218 | not yet: there's no toggle; audio keys work over the Audio panel and the Grid, and numpad keys in text fields are a setting. | Settings › Editing › numpad shortcuts | `src/application/settings.cpp:223` | |
| Lead-in, lead-out, both, for selected lines | A:command/time.cpp:238, 251, 263 | better: Remove lead-in and lead-out too, and adding stops at the neighbour | Subtitles › Timing helpers › Add lead-in, Remove lead-in, Add lead-out, Remove lead-out | mb:78–81; hk:387–390; [AU19-timing-helpers](../../../docs/compatibility-decisions.md#au19-timing-helpers) L5 | |
| Lengthen or shorten the line or syllable by keys, and shift what follows | A:command/time.cpp:275–330 | not yet: only one-frame start and end moves (with a video) and right-drag of syllables. | Subtitles › Timing helpers › Start/End one frame earlier/later | mb:83–86; hk:391–394 | |
| Next or previous line or syllable | A:command/time.cpp:353, 365 | same | Audio keys | hk:22–23 AUDIO_PREVIOUS/NEXT | |
| Make times continuous (change start or end) | A:command/time.cpp:104, 115 | same | Grid › Make times continuous | cm:111–113; hk:148–149 | |
| Snap start or end to the video frame | A:command/time.cpp:180, 341 | better: a joined neighbour stays joined | Video › Insert start time from video, Insert end time from video | mb:126–127; hk:199–200 | |
| Shift the selection so the active line starts at the current frame | A:command/time.cpp:126 | better: also end at video, and all following lines | Video › Shift by frames to video | mb:128–132; hk:318–321 | |
| Snap to scene: start and end to the keyframes around the current frame | A:command/time.cpp:192 | not yet: keyframe snaps move one edge at a time; nothing sets both from the video frame. | Subtitles › Timing helpers (one edge) | mb:73–76 | |
| Shift times by time or frames, forward or back, chosen lines and times | A:command/time.cpp:151 | better: live preview, shift to the video or an audio mark, move tag times, centre within frames, presets | Subtitles › Shift times… | mb:70; hk:184; `src/ui/ShiftTimesPanel.qml` | |
| Shift times history (Load from history) | A:dialog_shift_times.cpp:145 | not yet: presets and "last used" replace the history list. | Shift times › presets | `src/ui/ShiftPresetBar.qml` | |
| Timing post-processor: lead-in, lead-out, continuity, keyframe snapping, styles | A:command/tool.cpp:172 | better: live preview, and a line-by-line assistant that says why each edge moved | Subtitles › Timing postprocessor…; Timing helpers › Timing assistant | mb:71, 91–92; `src/ui/TimingPostprocessorPanel.qml:189–198` (styles) | |
| Post-processor closes small overlaps (Max overlap) | A:dialog_timing_processor.cpp:199 | not yet: Close small gaps acts on gaps only. | — | `src/application/shift_times.cpp:413` (`tdiff > 0`) | |
| Keyframes: open | A:command/keyframe.cpp:64 | same | Video › Open keyframes | mb:113; hk:212 | |
| Keyframes: save, or close and go back to the video's own | A:command/keyframe.cpp:85, 47 | not yet: keyframes can be opened, not saved or closed. | — | No such id in `src/application/hotkeys.cpp` | |
| Timecodes: open, save, close | A:command/timecode.cpp:66, 81, 49 | better: also reads mkvmerge's v4 and `timestamp` headers | Video › Open timecodes, Save timecodes, Close timecodes | mb:116–120; hk:332–334; [FA21-timecodes-v4](../../../docs/compatibility-decisions.md#fa21-timecodes-v4) | same → better |
| Kanji timer | A:command/tool.cpp:160 | better: every pair in one window, and it can write furigana readings and `#` multi-highlight syllables | Subtitles › Kanji timer | mb:106; hk:311; `src/ui/KanjiTimerDialog.qml` | |
| Karaoke mode in the audio display | A:command/audio.cpp:540 | better: Auto split, tap to time, onset snap, `\k`/`\kf` switch | Audio panel karaoke mode | hk:284–285, 267 AUDIO_TAP; `src/application/vocal_onsets.cpp` | |
| Centre times within frames (arch1t3cht's script) | FA1 §1 "Center Times" | same | Video › Centre times within frames | mb:122; hk:326 | |

### Video

| Feature | From | HikariSub | Where in HikariSub | Evidence | Change |
| --- | --- | --- | --- | --- | --- |
| Open, close video; open audio with it | A:command/video.cpp:584, 196 | better: indexing runs in a strip with Cancel while the window stays usable; Reload video keeps the frame | Video › Open video…, Unload video, Reload video | mb:110, 151–152; hk:211, 324 | |
| Play, play line, stop | A:command/video.cpp:631, 643, 702 | same | Video › Play or pause; video right-click › Play, Stop | mb:147; cm:194–195; hk:37–38, 242 | |
| Step frames, keyframes, fast jumps | A:command/video.cpp:331, 398, 371, 438, 385, 460 | same | Video › Previous frame, Next frame, Go to previous/next keyframe | mb:143–149; hk:39–42, 195–196, 203–204 | |
| Next or previous subtitle boundary | A:command/video.cpp:342, 409 | not yet: no command steps the video to the next line start or end. | — | No such id in `src/application/hotkeys.cpp` | |
| Jump to a frame or time | A:command/video.cpp:544; A:dialog_jumpto.cpp | not yet: there's no Jump to window or typed seek. | — | No such id in `src/application/hotkeys.cpp` | |
| Jump to the line's start or end | A:command/video.cpp:571, 558 | same | Video › Go to start time of line, Go to end time of line | mb:145–146; hk:201–202 | |
| Video follows the selected line | A:command/video.cpp:614 | better: chooses when to move and what to play after moving | Settings › Video | `src/application/settings.cpp:184, 187, 189` | |
| Toggle focus to the video slider | A:command/video.cpp:271 | not yet: no command moves the keyboard to the seek bar. | — | No such id in `src/application/hotkeys.cpp` or `src/ui/command_names.cpp` | unverified → not yet |
| Zoom and pan as in 3.5 (wheel zoom, middle-drag and Shift+wheel pan, pinch, reset) | A:command/video.cpp:672; FA1 §2b | different on purpose: the plain wheel doesn't resize the video box, because the video fills its panel; each wheel's action is a setting and the legacy crop zoom is kept | Video › Zoom video, Reset zoom and pan; Settings › Video › View › Zoom and pan | mb:156–157; hk:197–198; `src/application/settings.cpp:404–407`; [FA12-wheel-resize](../../../docs/compatibility-decisions.md#fa12-wheel-resize) | better → different on purpose |
| Zoom to 50, 100 or 200%, zoom in and out by command | A:command/video.cpp:733, 684, 715, 750, 762 | not yet: only Zoom video and Reset zoom and pan. | Video › Zoom video | mb:156–157 | |
| Aspect ratio: default, 4:3, 16:9, 2.35, custom | A:command/video.cpp:142, 160, 178, 81, 99 | not yet: a Taller–Wider stretch and Reset; no fixed ratios and no typed ratio. | Video right-click › Change aspect ratio | cm:223; hk:52 | |
| Overscan mask | A:command/video.cpp:655 | not yet: there's no overscan or safe-area mask. | — | No `overscan` in `src/` | |
| Copy the pointer's coordinates | A:command/video.cpp:208 | same | Video right-click › Copy video position | cm:193; hk:53 | |
| Save or copy the frame: with subtitles, without, subtitles only on transparent | A:command/video.cpp:298–533 | same | Video right-click › Export frame | cm:205–213; hk:54–57, 305–306 | |
| Detach video into its own window | A:command/video.cpp:238 | better: every panel can float, and tabs can move to other windows, each with its own video | Panel ⋯ › Float; Document tab › Move tab to new window | cm:303, 329; hk:342 | |
| Video details (with colour space and matrix) | A:command/video.cpp:258; FA1 §3 | same | Video › Video details… | mb:155; hk:335 | |
| Choose or cycle the subtitles renderer | A:command/video.cpp:219 | same | Settings › Video: libass, or VSFilterMod on Windows | `src/application/settings.cpp:241`; `src/ui/SettingsDialog.qml:1144`; `src/backends/subtitle_renderer_selection.cpp` | |
| Dummy video | A:command/video.cpp:600; A:dialog_dummy_video.cpp | better: sides up to 100000 px, an exact fraction frame rate, a 1080x1920 preset | Video › Open dummy video | mb:121; hk:213; [FA9-dummy-drawn-size](../../../docs/compatibility-decisions.md#fa9-dummy-drawn-size), [FA19-dummy-fraction](../../../docs/compatibility-decisions.md#fa19-dummy-fraction) | |
| Resolution, LayoutRes and YCbCr matrix prompts on open; HDR and untagged warnings | FA1 §3 | same | Prompts on opening video; each a row in Settings › Video | `src/application/settings.cpp:470–476`; [FA9-video-property-prompts](../../../docs/compatibility-decisions.md#fa9-video-property-prompts) | |
| Subtitles drawn with the file's YCbCr matrix | FA1 §2b "YCbCr Matrix applied by all providers" | same | Automatic | `src/application/video_matrix.cpp` | new |
| AV1 video (dav1d) | FA1 §3 (TT 3.4.1 "dav1d") | same | Video › Open video… | `vcpkg.json:13`; docs/media.md:9 | new |
| YUV4MPEG (`.y4m`) video | A:video_provider_manager.cpp:46; A:video_provider_yuv4mpeg.cpp:59 | unverified: `.y4m` isn't in the Open video filter (only through All files), and whether the indexer opens it wasn't checked. | — | `src/ui/Main.qml:9001` | new |
| AviSynth scripts as video (Windows) | FA1 §2b "AviSynth provider" | not yet: video opens through FFMS2 only. | — | No `avisynth` in `src/`; ADR 0016 | |
| VapourSynth and BestSource providers (fork) | FA1 §2b "BestSource", "VapourSynth provider" | not yet: video opens through FFMS2 only. | — | No `vapoursynth`/`bestsource` in `src/`; ADR 0016 | split |
| Keyframe generation (fork, VapourSynth) | FA1 §2b; FA1 §5 "Plugin first" | same, with the official **Keyframe generator** plugin (separate install)[^kf] | — | `src/application/hotkeys.cpp:212` (Open keyframes only) | split |

### Visual typesetting tools

| Feature | From | HikariSub | Where in HikariSub | Evidence | Change |
| --- | --- | --- | --- | --- | --- |
| Standard (crosshair) tool | A:command/vis_tool.cpp:71 | same | Video tool strip: Position pointer, Text positioning | hk:286–287 | |
| Drag (`\move`) | A:command/vis_tool.cpp:79 | better: a move from one point on two frames | Video tool strip: Text moving | hk:288 | |
| Rotate Z, rotate XY | A:command/vis_tool.cpp:87, 95 | better: Set angle from 2 points | Video tool strip | hk:290–291 | |
| Scale | A:command/vis_tool.cpp:103 | better: Set scale by rectangle | Video tool strip | hk:289 | |
| Rectangular clip | A:command/vis_tool.cpp:111 | same | Video tool strip | hk:292 | |
| Vector clip: drag, line, Bézier, remove; modes on keys | A:command/vis_tool.cpp:119–165 | better: B-splines and separate points too, and a Vector drawing tool with shapes | Video tool strip; each mode bindable | hk:293–294, 298–304 | |
| Vector clip: freehand, freehand smooth, convert a segment, insert a point | A:command/vis_tool.cpp:151, 158, 172, 179 | not yet: no freehand or convert mode; a point is inserted by right-click in the Line, Bézier and B-spline modes. | Vector clip, right-click | hk:298–304; `src/application/visual_vector.cpp:475, 640–726` | evidence |
| Vector clip coordinates with two decimals | FA1 §2b | same | Automatic | [FA9-clip-places](../../../docs/compatibility-decisions.md#fa9-clip-places) | |
| Handle size setting | FA1 §2b "Shape handle size setting" | same | Settings › Video | `src/application/settings.cpp:468` | |
| Perspective tool (fork) | FA1 §2b "Perspective visual tool" | better: perspective tracking from Power-Pin data | Video tool strip: Perspective; Video › Tracking › Perspective tracking… | hk:297, 337; `src/application/visual_perspective.cpp` | |
| Align to video (fork `time/align`) | FA1 §2a "Align to video" | different on purpose: checks every frame, where Aegisub steps two | Video › Align to video… | mb:123; hk:327; [FA14-search-step](../../../docs/compatibility-decisions.md#fa14-search-step) | same → different on purpose |
| Visual typesetting help page | A:command/help.cpp:90 | same | F1 over the video opens the guide's page | guide: typesetting/visual-tools | |

### Styles and fonts

| Feature | From | HikariSub | Where in HikariSub | Evidence | Change |
| --- | --- | --- | --- | --- | --- |
| Styles manager: storages (catalogues), copy both ways, import from a script, sort, move | A:command/tool.cpp:148 | better: edit several styles at once, Add to all open files, Delete unused styles, and a Styles panel | Subtitles › Style manager; Styles panel | mb:97; hk:180; `src/ui/StyleManager.qml`; cm:336–347 | |
| Style editor with preview | A:dialog_style_editor.cpp | same | Style manager › Edit | `src/ui/StyleManager.qml` | |
| Border style 4 (opaque box per line) | FA1 §3 "Style border style 4" | better: a three-way Border type that says which choice is libass-only | Style editor › Border type | `src/ui/StyleManager.qml:1862`; [FA8-borderstyle-values](../../../docs/compatibility-decisions.md#fa8-borderstyle-values) | |
| Encoding -1 (detect text direction) | FA1 §3 "Style encoding -1" | same | Style editor › Text encoding | `src/ui/StyleManager.qml:1797` | |
| Styling assistant | A:command/tool.cpp:106 | not yet: there's no window that steps through lines applying styles. | Styles panel (apply to selected lines) | cm:337; no styling assistant in `src/` | |
| Fonts collector: check, copy to a folder or the subtitle's folder, zip | A:command/tool.cpp:67 | better: the renderer checks which font answered each name, missing faces and glyphs; runs on all tabs; takes fonts from the MKV | Subtitles › Font collector | mb:99; hk:182; `src/ui/FontCollectorDialog.qml` | |
| Fonts collector: symlink fonts to a folder | A:dialog_fonts_collector.cpp:55, 114 | not yet: the actions are Check, Copy to a folder and Zip. | — | No `symlink` in `src/` | |
| Reload the renderer's fonts (fork `video/subtitles_provider/reload`) | FA1 §2b "Reload font provider" | same: fonts reload by themselves when the catalogue changes | Automatic | `src/app/application.cpp:2721` | new |
| Resample resolution, from video, LayoutRes kept | A:command/tool.cpp:91; FA1 §3 | better: Adjust 3D rotations keeps rotated signs in shape | Subtitles › Resample subtitles | mb:100; hk:181; `src/ui/ResampleWindow.qml`; [738-resample-stretch-axis](../../../docs/compatibility-decisions.md#738-resample-stretch-axis) | |
| Resample: margin offsets, add or remove borders, manual aspect | A:dialog_resample.cpp:138, 175 | not yet: the aspect ratio is Keep it or Stretch, with no margin offsets or borders. | Resample subtitles › Aspect ratio | `src/ui/ResampleWindow.qml` | |
| Resample: change the YCbCr matrix | A:dialog_resample.cpp:200, 213 | not yet: the Resample window has no matrix choice. | — | `src/ui/ResampleWindow.qml` | |
| Default style catalogue for new files | A:preferences.cpp:122 | not yet: File › New always starts with the built-in Default style; the catalogue setting covers conversions only. | — | `src/application/document_files.cpp:131–152`; `src/application/settings.cpp:134` | unverified → not yet |

### Translation

| Feature | From | HikariSub | Where in HikariSub | Evidence | Change |
| --- | --- | --- | --- | --- | --- |
| Translation assistant (original shown, next, previous, insert original) | A:command/tool.cpp:184–254 | better: Translation mode keeps original and translation side by side in the file, with an Original text panel, Paste all and Paste the selected text | Subtitles › Translation mode; View › Panels › Original text | mb:108, 203; hk:61–63, 75–77, 272 | |

### Karaoke

| Feature | From | HikariSub | Where in HikariSub | Evidence | Change |
| --- | --- | --- | --- | --- | --- |
| Karaoke templater (kara-templater.lua, Karaoke template export filter) | Aegisub automation/autoload/kara-templater.lua | better: built in, with a Template editor and live preview; the export filter is gone, run Apply karaoke template instead | Automation › Apply karaoke template; View › Panels › Template editor | `src/helpers/lua/kara_templater.cpp`; mb:214; [TH5b-builtin-templater](../../../docs/compatibility-decisions.md#th5b-builtin-templater) | |
| Automatic karaoke lead-in macro | Aegisub automation/autoload/karaoke-auto-leadin.lua | same | Automation menu | `Automation/automation/Autoload/karaoke-auto-leadin.lua` | |

### Find, replace and checks

| Feature | From | HikariSub | Where in HikariSub | Evidence | Change |
| --- | --- | --- | --- | --- | --- |
| Find, find next | A:command/subtitle.cpp:90, 103 | better: Find previous, a live match count, Find all | Edit › Find…, Find next, Find previous | mb:63, 65–66; hk:163, 165, 314 | |
| Find and replace (fields, selected lines, regex, case) | A:command/edit.cpp:541 | better: across open tabs or a folder on disk, inside or outside tags, Replace N checked | Edit › Replace… | mb:64; hk:164 | |
| Select lines: by field, regex, case, dialogue or comments, set, add, subtract | A:command/tool.cpp:79 | better: then copy, cut, delete, move or comment them, in all tabs, and an Overlapping lines search | Edit › Select lines | mb:62; hk:167; `src/ui/SelectLinesDialog.qml:22` | |
| Select lines: intersect with the selection, exact match | A:dialog_selection.cpp:134, 152 | not yet: the modes are Select, Add to selection and Deselect, and matching is "with" or "without" the text. | Edit › Select lines | `src/ui/SelectLinesDialog.qml` | |
| Spell checker (replace, ignore, add, language, skip comments and capitals) | A:command/subtitle.cpp:439 | better: several dictionaries at once for mixed-language lines, and a grammar check beside it | Subtitles › Check spelling | mb:104; hk:168; `src/backends/harper_client.cpp` | |

### Automation

| Feature | From | HikariSub | Where in HikariSub | Evidence | Change |
| --- | --- | --- | --- | --- | --- |
| Automation manager, reload scripts, reload autoload | A:command/automation.cpp:71, 46, 59 | better: each script runs in its own process, so a crash stops only that script; Repeat last run | Automation › Show panel, Refresh autoload scripts, Repeat last run | mb:171–177; hk:190–192, 310; ADR 0006 | |
| Macros in the Automation menu, submenus from `/` | A:command/automation.cpp:83 | same | Automation menu | `src/ui/MacroMenuEntries.qml` | |
| Lua and MoonScript Automation 4 API, as in 3.5.1 | FA1 §4 | same | — | `src/helpers/lua/lua_helper.cpp`; [FA4-frame-times](../../../docs/compatibility-decisions.md#fa4-frame-times) | |
| `project_properties().timecodes_file` | FA1 §4 | same | — | `src/helpers/lua/lua_helper.cpp:2083`; [FA4-timecodes-file](../../../docs/compatibility-decisions.md#fa4-timecodes-file) | |
| Fork API: `gui.*`, `get_frame` with `getPixel` and `data()`, `get_audio_selection`, `set_status_text`, `line.extra` | FA1 §4 | same | — | `src/helpers/lua/lua_helper.cpp`; [FA4-getpixel-rgb](../../../docs/compatibility-decisions.md#fa4-getpixel-rgb) | |
| Export filters from scripts (`register_filter`) | FA1 §4; Aegisub cleantags-autoload.lua | not yet: `register_filter` is accepted and does nothing. | — | `src/helpers/lua/lua_helper.cpp:340–343` | |
| Macro descriptions, log levels 4 and 5 | A:auto4_lua.cpp; A:preferences.cpp:410 | same | Automation panel tooltips; Settings › Automation › Lua trace level (0–5) | `src/ui/AutomationManager.qml:1119–1120`; `src/ui/SettingsDialog.qml:1512`; `src/helpers/lua/lua_helper.cpp:463` | split (was part of a not yet row) |
| Greyed-out invalid macros and their own undo points | A:auto4_lua.cpp; A:auto4_lua_assfile.cpp | not yet: a macro's validation runs only when you choose it, and `set_undo_point` does nothing. | — | `src/app/automation_shell.cpp:1366`; `src/helpers/lua/lua_helper.cpp:1644–1647` | split |
| MoonScript 0.7.0 | FA1 §3 | not yet: MoonScript 0.5.0 is bundled. | — | `Automation/automation/Include/moonscript.lua:4508` | |
| 3.5 library fixes: `lfs.dir` raises the message, `include()` reports the real error | FA1 §3 | same | — | `Automation/automation/Include/aegisub/lfs.moon:74, 80`; [FA4-lfs-returns](../../../docs/compatibility-decisions.md#fa4-lfs-returns), [FA4-load-errors](../../../docs/compatibility-decisions.md#fa4-load-errors) | |
| Built-in modules always load from the installation | FA1 §3 | same | — | FA1 §3 row (`src/helpers/lua/lua_helper.cpp` `userMayReplace`); [TH5d2-ln-kara](../../../docs/compatibility-decisions.md#th5d2-ln-kara) | |
| Bundled macros: Clean Tags, Strip Tags, Add edgeblur, Make text fullwidth, Select overlaps | Aegisub automation/autoload | same | Automation menu | `Automation/automation/Autoload/` (all five) | |
| DependencyControl | FA1 §1 "Bundled" | better: bundled with its Toolbox and the usual libraries | Automation menu | `Automation/automation/Autoload/l0.DependencyControl.Toolbox.moon` | |

### Settings, hotkeys and shell

| Feature | From | HikariSub | Where in HikariSub | Evidence | Change |
| --- | --- | --- | --- | --- | --- |
| Options window | A:command/app.cpp:202 | same | File › Settings | mb:26; hk:217 | |
| Hotkeys editor | A:preferences.cpp (Hotkeys) | better: Shift+click any menu item to give it a key; conflicts shown as you press | File › Settings › Shortcuts; Shift+click | `src/app/hotkeys_controller.cpp` | |
| Interface colours for each element (audio, grid, syntax, visual tools) | A:preferences.cpp:302 | not yet: themes, accent colours and high-contrast colours only. | File › Settings › Appearance | `src/ui/theme.cpp`; `src/application/settings_import.cpp:985` | |
| Interface language | A:command/app.cpp:153 | not yet: 5 languages (English, Korean, Polish, Tamil, Thai) against Aegisub's 33. | File › Settings › Interface language | `i18n/` (5 `.ts` sets); `src/ui/SettingsDialog.qml:609` | |
| Display modes: full, video+subs, audio+subs, subs only | A:command/app.cpp:66–122 | better: layouts (Editing, Timing, Translation, Typesetting, Karaoke) plus your own, floating panels | View › Layout | mb:181–193 | |
| New window | A:command/app.cpp:190 | better: move a tab to another window; each window keeps its own video and comes back with the session | Document tab › Move tab to new window | cm:329–330; hk:342–344; ADR 0025 | |
| Main toolbar | A:command/app.cpp:236 | different on purpose: no main toolbar, by owner decision (2026-10-05) | — | `src/application/settings_import.cpp:354`; legacy ledger P10-no-main-toolbar (`src-qt-compat-decisions.md`) | not yet → different on purpose |
| Log window | A:command/app.cpp:178 | same | View › Log | mb:229 | |
| Check for updates | A:command/app.cpp:257 | better: signed updates that can install themselves | Help › Check for updates | mb:236; hk:224; ADR 0027 | |
| About, exit | A:command/app.cpp:54, 142 | same | Help › About; File › Exit | mb:239, 28; hk:218, 220 | |
| Help: contents, website, bug tracker | A:command/help.cpp:66, 102, 45 | better: Report an issue is a form inside the app | Help › User guide, Report an issue | mb:234–235; hk:222–223 | |
| IRC channel link | A:command/help.cpp:78 | not yet: the Help menu has no chat link. | — | mb:232–240 | |

### Export and conversion

| Feature | From | HikariSub | Where in HikariSub | Evidence | Change |
| --- | --- | --- | --- | --- | --- |
| Export a copy in another format | A:command/tool.cpp:54 | better: WebVTT, SBV, TTML, DFXP, srv3, MPL2 and TMPlayer, a report of what each line lost, lines by All, Selected or Shown | File › Export as… | mb:15; hk:313; `src/core/include/hikari/core/export.h:14, 42–57` | |
| Export or save as SSA | A:subtitle_format_ssa.h:22 | not yet: SSA opens but is never written as SSA. | — | `src/core/include/hikari/core/export.h:14` (no SSA); `src/core/ass_save.cpp` | |
| Export to Adobe Encore, TranStation, MPEG-4 TTXT, EBU STL | A:subtitle_format_encore.cpp:44, _transtation.cpp:41, _ttxt.cpp:48, _ebu3264.cpp:620 | not yet: none of these is an export format. | — | `src/core/include/hikari/core/export.h:14` | |
| Export filters: Transform framerate, Fix styles, Clean tags, Karaoke template | A:export_framerate.cpp:52; A:export_fixstyle.cpp:46; automation/autoload | not yet: there are no export filters; nearby are Subtitles › Set FPS, Remove tags… and Apply karaoke template. | Subtitles › Set FPS; Grid › Remove tags… | mb:101–103; cm:108; [TH5b-builtin-templater](../../../docs/compatibility-decisions.md#th5b-builtin-templater), [AU4-remove-tags](../../../docs/compatibility-decisions.md#au4-remove-tags) | |
| Export text encoding choice | A:dialog_export.cpp:144 | not yet: every format is written as UTF-8. | — | `src/core/include/hikari/core/export.h:19–39` (no encoding option) | |
| MicroDVD and SRT import settings (frame rate) | A:preferences.cpp:154 | same | Settings › Conversion | `src/application/settings.cpp:127–128` | |

### Recovery and history

| Feature | From | HikariSub | Where in HikariSub | Evidence | Change |
| --- | --- | --- | --- | --- | --- |
| Autosave and Open autosaved subtitles | A:command/subtitle.cpp:281 | better: the Home page offers the copies after a crash; a copy opens as a new "(recovered)" file | File › Open autosave… | mb:18; hk:215; `src/ui/Autosaves.qml` | |
| Autosave interval you can set | A:preferences.cpp:386 | not yet: autosave runs 20 seconds after a change; only the number of copies kept is a setting. | Settings › Recovery | `src/app/application.cpp:4080`; `src/application/settings.cpp:120` | |
| Automatic backup copy on save | A:preferences.cpp:390 | not yet: saving keeps no backup copy (only Find's folder replace does). | — | `src/app/application.cpp:486–495` (replace only) | |

### Platforms

| Feature | From | HikariSub | Where in HikariSub | Evidence | Change |
| --- | --- | --- | --- | --- | --- |
| Windows | Aegisub releases | same: Windows 10 1809+ and 11, x64 | Setup and portable zip | docs/platform-policy.md:13; docs/distribution.md | new |
| Linux | Aegisub releases | same: x86_64, X11 and Wayland | AppImage, deb, rpm, tar.gz | docs/platform-policy.md:13; docs/distribution.md | new |
| Runs on macOS (and its window menu commands) | A:command/app.cpp:269–324 | not yet: no macOS build; the policy keeps the port possible. | — | docs/platform-policy.md:19 | moved (from Files and formats) |

## Legacy HikariSub 0.0.x extras (not in Aegisub)

Kept from the draft for the *Moving from legacy* page. "From" cites the legacy ui-inventory surface or Appendix A action.

| Feature | From | HikariSub | Evidence | Change |
| --- | --- | --- | --- | --- |
| Tabs, one file each, with its own video, audio and undo | ui-inventory §Shell | better: tabs move between windows | cm:309–335; hk:229–230, 261–262, 342 | |
| Two files side by side (split view) | ui-inventory §Shell | same | View › Panels › Reference (mb:206) | |
| Subtitle comparison by times, visible lines, selections, styles, chosen styles | ui-inventory §Shell | better: also against the file on disk or the reference | cm:318–326; `src/ui/SubtitleComparisonMenu.qml` | |
| Sessions: last, from file, to file, ask or load on start | ui-inventory §Shell | better: relinking moved files, windows restored, legacy `.kls` read | mb:20–25; hk:207–209 | |
| Status bar fields | ui-inventory §Shell | same | guide: get-started/tour | |
| Open containing folders of subtitles, video, audio, keyframes | ui-inventory §Shell | same | cm:314–317 | |
| Remove subtitles from the editor; save using the video name | App. A | same | mb:6, 17; hk:158, 170 | |
| Grid filters: hide selected, by styles, comments, unconfirmed, untranslated, invert, keep, ignore in actions | ui-inventory §SubsGridFiltering | same | cm:263–275, 291; hk:132–143 | |
| Line groups (tree) | App. A `GRID_TREE_MAKE` | better: groups nest inside groups | cm:114, 184–191; hk:144 | |
| Split into frames, characters, words, wraps | App. A `GRID_SPLIT_BY_*` | better: Bake animations, At tag blocks, Every N frames or seconds, marks kept with their character | cm:97–107; hk:117–122; [FA15-bake-default](../../../docs/compatibility-decisions.md#fa15-bake-default), [TH4-grapheme-char-split](../../../docs/compatibility-decisions.md#th4-grapheme-char-split) | |
| Join keep first or last; merge with previous or next (F4, F5) | App. A | same | cm:93–96; hk:125–127, 227–228 | |
| Insert before or after with video frame time | App. A | same | cm:69–70; hk:113–114 | |
| Copy and paste columns | App. A | better: copies tags only, a clip, or named tags | cm:79–80; [AU21-position-commands](../../../docs/compatibility-decisions.md#au21-position-commands) P8 | |
| Set new FPS, set FPS from video | App. A | same | mb:101–103; hk:152–153 | |
| Subtitles preview of another file | App. A | same | cm:162; hk:131 | |
| Twenty tag buttons of your own | App. A | same | `src/application/settings.cpp:250–269` | |
| Insert difference from the start or to the end | App. A | same | hk:73–74 | |
| Translator mode: two texts per line, paste all or selected, hide original, unconfirmed flag, next unconfirmed or untranslated | ui-inventory §Translation | better: an Original text panel and tags carried from the original | mb:108, 203; hk:61–63, 75–77 | |
| Paste translation from a file; Dialogue shifting window | App. A | same | cm:82, 163; hk:145–146 | |
| Save translation | App. A | same | mb:16; hk:157 | |
| Spelling suggestions and dictionary in the text's right-click menu | ui-inventory §EditBox | same | cm:1–5, 50 | |
| Synonyms and web search in the text's right-click menu | ui-inventory §EditBox | not yet: right-click has spelling and grammar only. | cm:1–50; no synonyms in `src/` | |
| Audio: play before or after the marker, set the marker or audio position from the video | App. A | same | mb:167–168; hk:25–26, 205–206 | |
| Audio auto-focus under the pointer | ui-inventory §AudioBox | same | `src/application/settings.cpp:65` | |
| Video: previous and next file, chapters, stream choice, volume | ui-inventory §VideoBox | same | mb:153–154; cm:214–215; hk:43–48 | |
| Full screen on a chosen monitor, progress bar on or off | App. A | better: a player bar with three modes | cm:196–197, 217, 219–222; hk:49–50, 266 | |
| DirectShow player and its filters' property pages (Windows) | ui-inventory §VideoBox | same | `src/app/application_directshow.cpp`; `src/ui/VideoContextMenu.qml`; `src/application/settings.cpp:362` | |
| Delete the video file (Windows) | App. A `VIDEO_DELETE_FILE` | different on purpose: the action is now Unload video, which leaves the file | cm:226 (Unload video [VIDEO_DELETE_FILE]); legacy ledger V3-unload-video | not yet → different on purpose |
| Select the line at the video position; select lines visible on video | App. A | same | cm:161; hk:115, 241 | |
| Position shifter (move all) | ui-inventory §VideoToolbar | same | hk:295 | |
| Shapes editor and all-tags editor | ui-inventory §VideoToolbar | better: kept per Project as well as yours | hk:296 VIDEO_TOOL_HYDRA; docs/visual-tools.md §T6 | |
| Font catalogues and Add fonts from subtitles | ui-inventory §FontDialog | same | `src/ui/FontCatalogWindow.qml`; `src/ui/FontsFromSubtitlesDialog.qml` | |
| External fonts folder | ui-inventory §OptionsDialog | same | guide: typesetting/fonts#your-fonts-folder | |
| Clean styles of the file | App. A `GLOBAL_STYLE_MANAGER_CLEAN_STYLE` | same | hk:243 | |
| Find results list and file scope | ui-inventory §FindReplaceDialog | same | View › Panels › Find (mb:209) | |
| Fix minor errors (misspell rules) | App. A `GLOBAL_MISSPELLS_REPLACER` | different on purpose: removed by owner decision (2026-11-02), rules not imported | `src/application/settings_import.cpp:386` (no ledger anchor) | not yet → different on purpose |
| Shift times profiles; postprocessor in the same panel | ui-inventory §ShiftTimes | better: two panels with previews; profiles became presets | mb:70–71 | |
| Conversion to ASS, SRT, MicroDVD, MPL2, TMPlayer with options | App. A `GLOBAL_CONVERT_TO_*` | same | mb:14–15; hk:174–178 | |
| Extract subtitle tracks and fonts from MKV | ui-inventory §FontCollector, Demux | same | mb:10; `src/application/matroska.cpp` | |
| Automation shortcut window | App. A | same | cm:351–352 (Set shortcut…) | |
| Run the last loaded script | App. A | same | mb:173; hk:192 | |
| Script editor path | ui-inventory §OptionsDialog | same | `src/application/settings.cpp:117` | |
| History window, undo to last save | ui-inventory §SubsFile | same | mb:50–51; `src/ui/HistoryWindow.qml` | |
| Open autosave, remove temporary files | ui-inventory §AutoSaveOpen | same | mb:18–19; `src/ui/Autosaves.qml:295` | |
| View arrangements and the editor switch | App. A `GLOBAL_VIEW_*`, `GLOBAL_EDITOR` | better: layouts and floating panels | mb:181–193; `src/application/settings_import.cpp:356` | |
| Configurable main toolbar | ui-inventory §Shell | different on purpose: no main toolbar, by owner decision (2026-10-05) | `src/application/settings_import.cpp:354`; legacy ledger P10-no-main-toolbar | not yet → different on purpose |
| Open video with FFMS2 (indexed precision) | App. A `GLOBAL_VIDEO_INDEXING` | same | `src/application/settings.cpp:233` | |
| File associations (Windows) | ui-inventory §OptionsDialog | same | guide: get-started/install#setup-recommended | |
| Themes | ui-inventory §OptionsDialog | better: light, dark and high contrast, following the system | `src/ui/theme.cpp` | |
| Website, report issue, check for updates, about, credits | App. A | same | mb:233–240 | |
| Legacy style catalogues, font catalogues and themes carried over | data-inventory §catalogs | not yet: the settings import brings none of them; a legacy FontCatalogs.txt can be opened by hand. | `src/application/settings_import.cpp:977, 1474`; `src/app/font_catalogs_controller.cpp:563` | evidence |

## Changed rows (draft → this pass)

Status changes:

1. **Link vertical zoom and volume sliders:** not yet → same. The Audio panel has "Link the volume and stretch sliders" (`src/ui/Main.qml:5726–5744`, `src/ui/audio_controller.cpp:719–733`, `audio.link` at `src/application/settings.cpp:84`). The draft looked in `command_names.cpp`; the toggle is a panel button, not a command.
2. **Grid highlights the lines on the current frame:** unverified → not yet. No Grid model role marks the lines at the video frame (`src/ui/line_table_model.h:56–92`).
3. **Toggle focus to the video slider:** unverified → not yet. No command for it in `hotkeys.cpp` or `command_names.cpp`.
4. **Default style catalogue for new files:** unverified → not yet. File › New uses a fixed Default style (`src/application/document_files.cpp:131–152`); the only catalogue setting is `convert.styleCatalog` (`settings.cpp:134`).
5. **Mono playback:** same → different on purpose ([FA19-mono-scope](../../../docs/compatibility-decisions.md#fa19-mono-scope): video playback keeps the file's channels).
6. **Zoom and pan as in 3.5:** better → different on purpose ([FA12-wheel-resize](../../../docs/compatibility-decisions.md#fa12-wheel-resize): the plain wheel doesn't resize the video).
7. **Align to video:** same → different on purpose ([FA14-search-step](../../../docs/compatibility-decisions.md#fa14-search-step)).
8. **Timecodes: open, save, close:** same → better (v4 and mkvmerge headers, [FA21-timecodes-v4](../../../docs/compatibility-decisions.md#fa21-timecodes-v4), [FA21-timecodes-headers](../../../docs/compatibility-decisions.md#fa21-timecodes-headers)).
9. **Main toolbar** (Aegisub table): not yet → different on purpose (owner decision 2026-10-05, `settings_import.cpp:354`).
10. **Configurable main toolbar** (legacy table): not yet → different on purpose (same decision).
11. **Delete the video file** (legacy): not yet → different on purpose (VIDEO_DELETE_FILE is Unload video, cm:226).
12. **Fix minor errors** (legacy): not yet → different on purpose (owner decision 2026-11-02, `settings_import.cpp:386`).

Rows split, added or moved:

13. **"What Aegisub shows about macros"** (not yet) split into **Macro descriptions, log levels 4 and 5** (same: tooltips at `AutomationManager.qml:1119–1120`, Lua trace level 0–5 at `SettingsDialog.qml:1512`) and **Greyed-out invalid macros and their own undo points** (not yet: `set_undo_point` is a stub, `lua_helper.cpp:1644–1647`).
14. **"VapourSynth and BestSource providers, keyframe generation"** split into the providers (not yet) and **Keyframe generation** (same, with the official Keyframe generator plugin, separate install[^kf]).
15. **Runs on macOS** moved from Files and formats to a new Platforms section.
16. New rows: Audio display shows inactive lines (same); Subtitles drawn with the file's YCbCr matrix (same); AV1 video (same); YUV4MPEG video (unverified); Reload the renderer's fonts (same); Windows (same); Linux (same).

Evidence narrowed, status kept (not yet):

17. **Tag hiding:** View › Hide tags shows ☀ by default; an empty replacement in Settings hides the tags. Missing: a cycle command.
18. **Spectrum with log scales:** the Speech mode maps frequency non-linearly. Missing: log power and a curve choice.
19. **Vector clip freehand and friends:** right-click inserts a point in the Line, Bézier and B-spline modes. Missing: freehand, freehand smooth and convert.
20. **Legacy catalogues carried over:** a legacy FontCatalogs.txt can be opened by hand.

Rows rechecked against `8dbf88f41` that hold as the draft says: all other rows. None of the wave-8 merges since `14939fe` (DK3 side bars, HA2/HA3 plugin APIs, OP1 plugin undo steps, MW tab dragging, LE7 Line editor and the Windows test fixes) closes another "not yet" row.

## Not yet

From the Aegisub table (55 rows):

1. Open MPEG-4 Streaming Text (TTXT) files.
2. Plain text import options (actor separator, comment starter, blank lines).
3. Open subtitles in a chosen or legacy charset (UTF-8 only).
4. Script properties' Synch point and Update details fields.
5. Attachments manager.
6. CPS warning and line-length thresholds you can set.
7. Move lines up or down a row.
8. A command that cycles tag hiding (show, simplify, hide).
9. Grid highlight of the lines on the current frame.
10. Split at the text cursor (estimate, keep times, at the video frame).
11. Join lines as karaoke.
12. Split lines by karaoke syllables.
13. Recombine lines.
14. Show Original pane, Revert, Insert Original.
15. Clear text but keep the tags.
16. Thesaurus.
17. Blank or noise audio for testing.
18. Spectrum with log power and a frequency curve choice.
19. Commit and reset the next line to the default timing.
20. Save an audio clip.
21. Medusa mode toggle.
22. Lengthen, shorten and shift by keys.
23. Snap to scene.
24. Shift times history.
25. Post-processor's Max overlap.
26. Keyframes save and close.
27. Next or previous subtitle boundary.
28. Jump to a frame or time.
29. Toggle focus to the video slider.
30. Zoom to 50, 100, 200%; zoom in and out commands.
31. Aspect ratio presets and a typed ratio.
32. Overscan mask.
33. AviSynth scripts as video.
34. VapourSynth and BestSource providers.
36. Vector clip freehand, freehand smooth and convert modes.
37. Styling assistant.
38. Fonts collector symlink mode.
39. Resample margin offsets, add or remove borders, manual aspect.
40. Resample's YCbCr matrix change.
41. Default style catalogue for new files.
42. Select lines: intersect with the selection, exact match.
43. Export filters from scripts (`register_filter`).
44. Greyed-out invalid macros and `set_undo_point`.
45. MoonScript 0.7.0.
46. Interface colours for each element.
47. Interface language (5 against Aegisub's 33).
48. IRC channel link.
49. Export or save as SSA.
50. Export to Adobe Encore, TranStation, MPEG-4 TTXT, EBU STL.
51. Export filters: Transform framerate, Fix styles, Clean tags, Karaoke template.
52. Export text encoding choice.
53. Autosave interval you can set.
54. Automatic backup copy on save.
55. macOS.

From the legacy table (2 rows): synonyms and web search in the text's right-click menu; legacy style catalogues, font catalogues and themes carried over.

## Unverified

- **YUV4MPEG (`.y4m`) video** (new): not in the Open video filter; whether the FFMS2 build indexes it wasn't checked, and nothing was run.

## Counts

| Status | Aegisub rows, draft | Aegisub rows, this pass | Legacy rows, draft | Legacy rows, this pass |
| --- | --- | --- | --- | --- |
| same | 58 | 64 | 34 | 34 |
| better | 50 | 50 | 12 | 12 |
| not yet | 53 | 54 | 5 | 2 |
| different on purpose | — | 4 | — | 3 |
| unverified | 3 | 1 | — | — |
| **Total** | **164** | **173** | **51** | **51** |

The Aegisub total grows by nine: seven new rows and two splits (164 + 7 + 2). The macOS row only moved. Same goes up by five: one from not yet (the Link button), three down to other statuses (Mono and Align to different on purpose, Timecodes to better), and seven up from new and split rows. Not yet goes up by two: three from unverified and one split, less the Link button and the main toolbar. Keyframe generation then moved from not yet to same, with the official plugin[^kf].

## Plus: what HikariSub adds (Q488 tiles)

Reconciled with the draft's `additions.md`. Each item names a source at `8dbf88f41`. "(beta.4)" means merged after beta.3.

### 1. Projects and the Explorer (beta.4)

- Open a folder as a Project; recent Projects first on Home. File › Open folder…, Close project (mb:3, 8; hk:357–358). `src/application/project.cpp`; ADR 0030.
- The Explorer panel: open, rename, trash or add subtitle files (cm:386–395). `src/ui/ExplorerPanel.qml`, `src/application/explorer.cpp`.
- Project settings and tiers: style and font catalogues, layouts, shapes, tags, presets, dictionary words, export format, translation languages, beside yours. `src/ui/ProjectSettingsPages.qml`, `src/ui/project_file.cpp`.
- Project fonts, Project scripts and Project AI keys. `src/application/project_automation.cpp`, `src/application/ai_project_keys.cpp`, `src/ui/ProjectAiKeysPage.qml`.

### 2. Several windows (beta.4)

- Move a tab to a new window or to another window; each window plays its own video and audio (cm:329–330; hk:342–344). ADR 0025; docs/media.md:87.
- Sessions bring back every window, its size and place (mb:20–25). `src/ui/SessionWindows.qml`.
- **Side bars** (DK3, merged after the draft): an AI side bar on the left and a tools side bar on the right; View › Toggle left/right side bar, Swap side bars (mb:195–198; hk:433–435). ADR 0032; `src/ui/SideBar.qml`, `src/ui/side_bar_model.cpp`. The draft said "Nothing on the site covers side bars yet".
- Layouts: five built in, your own, Lock layout, per-tab layouts saved in the file (mb:181–193; hk:331). ADR 0020.

### 3. Hikari AI with every edit in review (beta.4)

- The Agent panel, threads, modes, cost caps, skills, sub-agents, MCP servers, agents on this computer (hk:353–354). `src/ui/AgentPanel.qml`; docs/ai-agent.md; ADR 0024, 0026, 0029.
- Every AI edit waits in Review until Keep or Undo (mb:52–61; hk:346–352). `src/ui/ReviewPanel.qml`; docs/ai-review.md.
- Inline actions: Rewrite, Shorten to 15 CPS, Fix spelling and grammar, Translate (cm:40–48, 83–91; hk:355–356). docs/ai-inline-actions.md.
- Turn off all AI features in one box. docs/ai-settings.md.

### 4. Node effects (beta.4)

- Karaoke and typesetting effects as a node graph, previewed on the video, exported as template lines (mb:226; hk:379, 382). `src/application/node_graph.cpp`, `src/ui/NodeGraphCanvas.qml`; docs/node-effects.md; ADR 0023; [TH5d1-node-effects-mode](../../../docs/compatibility-decisions.md#th5d1-node-effects-mode).

### 5. Motion and perspective tracking (beta.4)

- Video › Tracking: apply, revert, copy motion data; Copy Power-Pin track; extend tracked lines; extrapolate `\move` (mb:133–142; hk:359–364). `src/application/motion_data.cpp`, `src/ui/MotionDataWindow.qml`; [AU8-motion-data](../../../docs/compatibility-decisions.md#au8-motion-data).
- Perspective tracking from Power-Pin data (mb:135; hk:337). `src/application/perspective_tracking.cpp`, `src/ui/PerspectiveTrackingWindow.qml`.
- Track colour and Find fade (mb:124–125; hk:328–329). [AU11-video-analysis](../../../docs/compatibility-decisions.md#au11-video-analysis).

### 6. Typesetting tools

- Perspective, Hydra, Position shifter and Vector drawing (hk:294–297). docs/visual-tools.md.
- Gradient and Text on path (beta.4; hk:307–308). `src/application/visual_gradient.cpp`, `src/application/visual_text_on_path.cpp`; [AU7-gradient](../../../docs/compatibility-decisions.md#au7-gradient), [AU14-text-on-path](../../../docs/compatibility-decisions.md#au14-text-on-path).
- Blur and glow layers (beta.4; mb:93–94; hk:380–381). `src/ui/BlurAndGlowLayersDialog.qml`.
- Remove tags, Interpolate across lines, change alignment, align and distribute, position to margins, Phantom, cycle tags, line breaks and case (beta.4; cm:17–38, 108–159; hk:365–378, 399–431). `src/application/interpolate.cpp`, `src/application/position_commands.cpp`, `src/application/text_commands.cpp`; [AU4-remove-tags](../../../docs/compatibility-decisions.md#au4-remove-tags), [AU22-interpolate](../../../docs/compatibility-decisions.md#au22-interpolate), [AU21-position-commands](../../../docs/compatibility-decisions.md#au21-position-commands), [AU20-text-commands](../../../docs/compatibility-decisions.md#au20-text-commands).
- The font picker, font catalogues and a Fonts folder. `src/ui/FontPicker.qml`, `src/ui/FontCatalogWindow.qml`.

### 7. Checking

- Harper grammar check for English, offline. `src/backends/harper_client.cpp`.
- Every installed dictionary at once, each word by its script; a Thai dictionary ships. `src/core/spelling_scripts.cpp`.
- QC notes in the file, with Done boxes, sections, Previous and Next (beta.4; mb:96; hk:338–341). `src/application/qc_notes.cpp`, `src/ui/QcNotesPanel.qml`.
- Subtitle comparison with another tab, the reference or the file on disk (cm:318–326). `src/ui/SubtitleComparisonMenu.qml`.
- Check karaoke (mb:105; hk:309). `src/core/karaoke_checks.cpp`.

### 8. Plugins (beta.4)

- Plugins add commands, panels, dialogs and nodes, from the marketplace or a `.hikariplugin` file; signed, sandboxed, with folder and network permissions (mb:178; hk:323). `src/ui/PluginManagerDialog.qml`, `src/ui/plugin_api`; docs/plugins.md; ADR 0022, 0028.
- Plugins get macros, keyframes, video frames, long jobs, Grid row marks and git (HA2, HA3, merged after the draft: 657ade4ff, f73e22059). The official Macro chains, Keyframe generator and Git line marks plugins use them; their listings stay drafts until beta.4.

### Karaoke tile: the native Karaoke Templater

- Built in and run natively, with the Template editor's live preview, a karaOK mode and twenty presets (mb:214). `src/helpers/lua/kara_templater.cpp`, `src/ui/TemplateEditor.qml`, `src/application/karaoke_presets.cpp`; docs/karaoke-templater.md; [TH5b-builtin-templater](../../../docs/compatibility-decisions.md#th5b-builtin-templater), [TH5c-karaok-mode](../../../docs/compatibility-decisions.md#th5c-karaok-mode).
- Tap to time and onset snap (hk:267). `src/application/karaoke_tap.cpp`, `src/application/vocal_onsets.cpp`.
- Import word-timed lyrics from LRC, TTML, KRC and QRC (mb:11; hk:345). `src/core/lyrics_import.cpp`.

### Translation tile: Thai support line

- Marks stay on their letters in Grid splits, karaoke Auto split, templates' `char`, gradients by character and Text on path; balanced line breaks fall between Thai words. `src/core/graphemes.cpp`; [TH4-grapheme-char-split](../../../docs/compatibility-decisions.md#th4-grapheme-char-split), [TH3-thai-auto-split](../../../docs/compatibility-decisions.md#th3-thai-auto-split), [TH1-template-char-graphemes](../../../docs/compatibility-decisions.md#th1-template-char-graphemes), [AU20-text-commands](../../../docs/compatibility-decisions.md#au20-text-commands) T1.
- The Reference panel and Paste translation with line-up. `src/application/translation_project.cpp`; cm:82, 163.

### Reconciliation with additions.md

- **Added:** side bars (DK3, merged 2026-11-14 as d21432b07) under Several windows; the HA2/HA3 plugin APIs under Plugins.
- **Moved:** additions.md's "Side bars and layouts" items go under Several windows, as the Q488 tiles have no layouts tile. Its "Automation and native tools" items are split between Motion and perspective tracking, Typesetting tools and the Karaoke tile. Its "Platform, install, updates and accessibility" and "Editing, timing, audio and video" items aren't homepage tiles; they stay on the site's *What HikariSub adds* list.
- **Unchanged:** every other additions.md item still holds at `8dbf88f41`.

## Notes for the owner

- The draft's three site notes still stand: `help/faq.mdx` says the macro menu is flat (it isn't, `src/ui/MacroMenuEntries.qml`); `automation/writing-scripts.mdx` says `timecodes_file` is empty (it isn't, `lua_helper.cpp:2083`); `typesetting/fonts.mdx` says "or attach them to the file" (there's no way to attach).
- The guide's audio page should mention the Link button: the draft missed it because the guide doesn't name it.
- Seven rows moved to "different on purpose" (four Aegisub, three legacy). The public page needs a word for them; the draft's statuses didn't include one.
- Only one row leans on the beta.4 marketplace: Keyframe generation becomes "same" once the official Keyframe generator plugin is listed.

[^kf]: Keyframe generation needs the official Keyframe generator plugin, a separate install from the marketplace (live with beta.4). Whether a plugin-only feature counts as "same" in the parity claim is an open owner question in the step-2 gate.
