# HikariSub wx UI parity inventory

Research for [#3](https://github.com/altqx/hikari/issues/3). Source audited at [20d647c4c769ab7f5d383cf3c1c33f03876a94e9](https://github.com/altqx/hikari/tree/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub), 2026-09-27. This is a static source inventory, not a claim that every platform/backend was run. The capability checklist and exhaustive symbol/reference appendices together define the baseline; labels describe present behavior, not approved future naming. Preserve capabilities unless a later ticket explicitly merges or drops one.

## Shell, tabs and layouts

- [ ] Main window: File, Edit, Video, Audio, View, Subtitles, Automation and Help menus; configurable main toolbar; recent-file submenus; drag/drop open; title/modified state; save/discard/cancel prompts; log/progress/error presentation.
- [ ] Five View arrangements: **All**, **Video and subs**, **Audio and subs**, **Only video**, **Only subtitles**, declared by `GLOBAL_VIEW_ALL`, `GLOBAL_VIEW_VIDEO`, `GLOBAL_VIEW_AUDIO`, `GLOBAL_VIEW_ONLY_VIDEO`, `GLOBAL_VIEW_SUBS`. These are visibility arrangements, not a general workspace system.
- [ ] Each `TabPanel` owns a subtitle grid/document, text editor, video box and shift-times panel; audio is attached through the edit box. Resizing changes video/editor/grid allocation. Preserve per-tab media association, active-line/selection state and unsaved state rather than treating tabs as filenames alone.
- [ ] Notebook: add/close/select/next/previous, drag/reorder and split comparison; save one/all, close all, open containing folders for subtitles/video/audio/keyframes; compare by times, visible lines, selections, styles and selected styles; disable comparison; modified tabs prompt before loss.
- [ ] Session menu: load last, load external, save external, ask on startup, load automatically. Restore associated media and tabs; missing files and external changes require intelligible recovery paths.
- [ ] Nine status fields: 0 general help/progress (including autosave/script messages), 1 video scale, 2 video zoom, 3 duration, 4 FPS, 5 video resolution, 6 aspect ratio, 7 subtitle resolution, 8 video filename. Fields can be empty when no media is loaded.

Sources: [main shell](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp), [tab composition and accelerator routing](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/TabPanel.cpp), [notebook](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Notebook.cpp).

## Capability checklist by surface

The source appendix below supplies exact local command IDs, control labels, menu declarations and every referenced configuration symbol for each source file. This avoids silently excluding context-only commands, dynamic script/stream/chapter entries or options declared through arrays. Global menu actions are also eligible toolbar entry points where `AppendTool` is used.

| Surface / source stem | Capabilities to preserve | Entry points / command families |
|---|---|---|
| File operations / HikariSubFrame | Open/save/save all/save as/save translation; remove document from editor; name from video; recent subtitles; encoding/file dialogs | File; `GLOBAL_OPEN_SUBS`, `GLOBAL_SAVE_*`, `GLOBAL_REMOVE_SUBS`, `GLOBAL_RECENT_SUBS`; drag/drop |
| SubsGrid, SubsGridWindow, SubsGridBase | Multi-selection, active line, column visibility/widths, numbering, layer/start/end/style/actor/margins/effect/text/CPS/wraps; tag hiding; comments; clipboard rows and columns; paste special; insert before/after using video/frame; split time/frame/chars/words/wraps; swap/duplicate/join/continuous timing; sort all or selected; preview | Grid context/header menus, `GRID_*`, `GLOBAL_SORT_*`, `GLOBAL_REMOVE_*`; `SubsGridPreview` |
| SubsGridFiltering, SubsGridDialogs | Hide selected; filter by styles/selection/dialogues/doubtful/untranslated; inverse/persist/ignore-filtered-in-actions; tree grouping/editing; FPS change/from video; choose columns/properties | Grid context menu; FPSDialog, TreeDialog, SwapPropertiesDialog; exact menu index below |
| EditBox, DialogueTextEditor, TextEditorTagList | Original and translated text, syntax/tag completion, spell highlighting, caret/selection/clipboard, line metadata, start/end/duration, font/bold/italic/underline/strikeout, four colors, alignment, configurable 20 tag buttons, apply/apply-next; inline style editing | Edit panel; `EDITBOX_*`; tag popup, TagButtonDialog; editor context menu also has dictionary/synonym/web search and spelling language/suggestions |
| Translation / TLDialog | Translator mode, copy all/selection original to translation, hide original, unconfirmed flag, tag movement, next doubtful/untranslated; paste translation and configure translation workflow | Edit panel translator controls, `GRID_TRANSLATION_DIALOG`, `GRID_PASTE_TRANSLATION`, `EDITBOX_*`; comparison via tab context menu |
| AudioBox, AudioDisplay | Open/close/from-video/dummy audio; waveform/spectrum; time ruler, line/keyframe/video/cursor marks; selection timing/commit/advance; playback selected line/selection/marker windows/first-last 500ms/to end; stop, scroll, zoom, gain/volume/linking, lead-in/out, snap, auto-scroll/commit/focus; karaoke timing/splitting | Audio menu; audio toolbar/context; `AUDIO_*`, `GLOBAL_AUDIO_*`, `GLOBAL_OPEN_AUDIO`; KaraokeSplitting |
| VideoBox, VideoSlider, VideoFullscreen | Open/recent/dummy video, indexed precision mode, play/pause/stop, seek/time/frame/keyframe, line preview, zoom/scale/aspect, fullscreen/monitor choice, hide progress bar, volume, previous/next file/chapter, stream selection, filters when supported, copy coordinates, image capture with/without subtitles, delete file | Video menu; `VIDEO_*`, `GLOBAL_*FRAME`, `GLOBAL_SET_*`; video context and fullscreen controls; dynamic streams/chapters/monitors are source-dependent |
| VideoToolbar, Visual* | Crosshair, position, move, move-all, scale, Z/XY rotation, rectangular/vector clip and drawing, shape presets/editing, arbitrary tag controls; seek/play behavior when selecting a line; numeric/direct-manipulation paths | Video visual toolbar and per-tool controls; AllTagsEdition, ShapesEdition; contextual lists and preset editors |
| StyleStore, StyleChange, StyleList, StylePreview, NewCatalog | Current document styles and external catalogs; create/copy/edit/rename/delete/import/transfer/reorder/clean styles; detailed ASS style properties and preview; catalog naming/selection | Subtitles > Style manager, `GLOBAL_OPEN_STYLE_MANAGER`, edit-box style Edit; NewCatalog/Stylelistbox/HikariListBox/CustomCheckListBox chooser dialogs |
| FontDialog, FontCatalogList, ColorPicker | Font search/sample/select; font catalog/profile management and add fonts from subtitles; color spectrum/recent/screen dropper/alpha and simpler picker variant | Style/edit-box font/color buttons; font catalog menus; profile dialog and Add fonts from subtitles dialog |
| FindReplaceDialog, findreplace, FindReplaceResultsDialog | Find, next, replace, replace all, expression/scope controls and history, results navigation and applicable file/tab scope | Edit > Find / Find and replace; `GLOBAL_SEARCH`, `GLOBAL_FIND_REPLACE`, `GLOBAL_FIND_NEXT`; results dialog |
| SelectLines | Select by expression/options and selection operation, scoped subtitle fields | Edit > Select lines; `GLOBAL_OPEN_SELECT_LINES` |
| MisspellReplacer, MispellReplacerDialog | Rule-based minor corrections, rule configuration and result review | Edit > Fix minor errors (experimental); `GLOBAL_MISSPELLS_REPLACER`; FindResultDialog |
| SpellCheckerDialog | Word-by-word suggestions, replace/ignore and dictionary operations; current language | Subtitles > Check spelling; `GLOBAL_OPEN_SPELLCHECKER`; inline editor spelling context |
| ShiftTimes | Offset by time/frames; direction; line/style/time subset; align to video/audio marker; preserve relative timed tags; end correction; named profiles; postprocessor lead-in/out, continuity and keyframe thresholds | Subtitles > Shift times, `GLOBAL_SHOW_SHIFT_TIMES`, `GLOBAL_SHIFT_TIMES`; panel switches between shift and postprocessor; ProfileEdition |
| ScriptInfo | Script title/credits/comment metadata, resolution/layout resolution and ASS properties | Subtitles > ASS file properties; `GLOBAL_OPEN_ASS_PROPERTIES` |
| SubsResampleDialog | Subtitle coordinate/resolution resampling and resolution mismatch choices | Subtitles > Resample; `GLOBAL_OPEN_SUBS_RESAMPLE`; SubsMismatchResolutionDialog during media/document resolution mismatch |
| Conversion / OptionsPanels | ASS/SRT/MicroDVD/MPL2/TMPlayer conversion with FPS, style/catalog, duration-per-letter, resolution and tag insertion settings | Subtitles > Conversion; `GLOBAL_CONVERT_TO_*`; Settings conversion page |
| FontCollector, Demux | Collect/check fonts from subtitle use, destination/output and progress/log; extract subtitle tracks/attachments from MKV, choose tracks/output | Subtitles > Font collector; `GLOBAL_OPEN_FONT_COLLECTOR`; grid `GRID_SUBS_FROM_MKV`; backend/platform availability must be carried as a capability flag |
| Automation, AutomationHotkeysDialog, AutomationDialog, AutomationProgress | Load scripts, reload autoload, rerun last, dynamic macro menu; map automation shortcuts; compatible script-created labels/edit/textbox/color/int/float/dropdown/checkbox/button dialogs; progress/cancel/errors | Automation menu `GLOBAL_AUTOMATION_*`, dynamic script action IDs; script dialog schema defines arbitrary user-facing surfaces |
| OptionsDialog / HkeysDialog | Program/editor/advanced/conversion/video/audio/theme/shortcut/file-association preferences; shortcut filter/map/reset/conflicts and choose scope; external-font and script-editor paths | File > Settings `GLOBAL_SETTINGS`, mapped-button/menu mapping gesture, Automation shortcut window; platform-specific associations |
| SubsFile HistoryDialog | Inspect history, undo/redo, undo to last save, select revision | Edit > History; `GLOBAL_HISTORY`, `GLOBAL_UNDO`, `GLOBAL_REDO`, `GLOBAL_UNDO_TO_LAST_SAVE` |
| AutoSaveOpen, AutoSavesRemoving | Browse/open recovery snapshots; select/remove temporary autosave/cache files | File > Open auto save / Remove temporary files; `GLOBAL_OPEN_AUTO_SAVE`, `GLOBAL_DELETE_TEMPORARY_FILES` |
| LogHandler, ProgressDialog, HikariMessageBox, UpdateChecker | Log display, progress/cancellation, confirmation/error/result notices; website/report issue/check updates/about/credits | File log window; Help menu; background/indexing/automation operations |

## Custom controls and their purpose

These are interaction obligations, not instructions to port wx classes. Sources are linked in the class census below.

- `HikariFrame`, `HikariDialog`, `HikariPanel`, `HikariNavigation`: themed top-level/container behavior, navigation and dialog sizing.
- `HikariCheckBox`, `HikariRadioButton`, `HikariRadioBox`, `MappedButton`, `ToggleButton`, `MenuButton`, `BitmapButton`: boolean/exclusive/action controls, hotkey mapping, button menus and icon activation.
- `HikariTextCtrl`, `HikariTextValidator`, `NumCtrl`, `TimeCtrl`, `TextEditor`: text input, validation, numeric/time editing and rich ASS-aware dialogue editing.
- `HikariChoice`, `PopupList`, `HikariListCtrl`, its ItemText/ItemColor/ItemCheckBox, `StyleList`, `FontList`: editable/selectable popup/list variants, owner-drawn rows, check/color cells and style/font previews.
- `HikariScrollbar`, `HikariScrolledWindow`, `HikariSlider`, `VideoSlider`, `VolSlider`: scrolling, continuous settings, timeline/chapter seeking and volume.
- `HikariTabBar`, `Notebook`, `HikariTreebook`: tabbed document/panel and hierarchical settings navigation.
- `HikariStaticText`, `HikariStaticBox`, `HikariStaticBoxSizer`, `HikariStatusBar`, `HikariGauge`, `HikariWindowResizer`: themed labels/groups/status/progress and draggable region sizing.
- `Menu`, `MenuBar`, `MenuDialog`, `Mnemonics`, `HikariToolbar`, `ToolbarMenu`: custom menus/mnemonics, command mapping, toolbar selection/order.
- Color spectrum/recent/dropper, style/font samples, grid preview and visual-tool subcontrols are specialized presentation/input surfaces; replacement must preserve their operations and keyboard alternatives.

## Hotkey scope and discovery risks

`Hotkeys.h` declares five scopes in order: **GLOBAL_HOTKEY (G), GRID_HOTKEY (S), EDITBOX_HOTKEY (E), VIDEO_HOTKEY (V), AUDIO_HOTKEY (A)**. The saved key is an action plus scope, not just a key sequence. `TabPanel::SetAccels` routes some actions across panels, includes fixed Ctrl-X/C/V grid bindings and numpad Enter variants, excludes hidden tag-button bindings, and maps alternate audio actions. Dynamic automation IDs begin at 30100. Do not infer actual availability solely from ID prefixes. Exact declared IDs, numeric values, labels and default bindings appear below. Sources: [Hotkeys](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Hotkeys.cpp), [TabPanel](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/TabPanel.cpp).

Candidates for merging or making more discoverable (recommendations, not approved removals): search/replace/select-lines/misspell results can share scope controls; style/font/catalog choosers can share selection patterns; time shifting and postprocessor share one panel but have distinct operations; global/video/audio playback shortcuts overlap by focus and must be explained; comparison is buried in tab context menus; translation controls mix text copying with review flags; shape/tag preset editing is hidden within visual tools; many action mappings depend on custom control gestures. Preserve command aliases during redesign. Keep Windows-only filters/associations/delete-file actions explicit rather than letting a cross-platform UI silently promise unavailable backend functions.

## Method, completeness boundary and downstream use

The appendices mechanically enumerate the current macro action registry, concrete default assignments, all Hikari/wx-derived user-control declarations, per-file config/color symbols and menu/control/tooltip declarations across the top-level HikariSub source. Dynamic entries (Lua macros/dialogs, installed dictionaries/fonts, tracks, streams, chapters, monitors, recents, catalogs) are represented by their generating source, not by a fabricated finite list. The source census includes some helper controls/internal IDs; presence alone is not proof that a control is reachable on every platform. No runtime behavior or accessibility has been measured.

Use the checklist and source indexes to give each redesign ticket an explicit set of capabilities. QML/HTML prototype tickets should get human reaction before UX sign-off, with Figma Starter reserved for occasional hand-off. Final implementation parity requires executable tests and human workflow checks; this inventory supplies their scope.

## Appendix A: every declared hotkey action

Names are from HotkeysNaming; blank names mean no literal name assignment was found. Default assignments are reproduced in Appendix B.

| Value | Symbol | User-facing name |
|---|---|---|
| 1000 | `AUDIO_COMMIT_ALT` | Commit alt |
| 1001 | `AUDIO_PLAY_ALT` | Play alt |
| 1002 | `AUDIO_PLAY_LINE_ALT` | Play line alt |
| 1003 | `AUDIO_PREVIOUS_ALT` | Previous line alt |
| 1004 | `AUDIO_NEXT_ALT` | Next line alt |
| 1010 | `AUDIO_COMMIT` | Commit |
| 1011 | `AUDIO_PLAY` | Play |
| 1012 | `AUDIO_PLAY_LINE` | Play line |
| 1013 | `AUDIO_PREVIOUS` | Previous line |
| 1014 | `AUDIO_NEXT` | Next line |
| 1015 | `AUDIO_STOP` | Stop |
| 1016 | `AUDIO_PLAY_BEFORE_MARK` | Play before the marker |
| 1017 | `AUDIO_PLAY_AFTER_MARK` | Play after the marker |
| 1018 | `AUDIO_PLAY_500MS_BEFORE` | Play 500ms before |
| 1019 | `AUDIO_PLAY_500MS_AFTER` | Play 500ms after |
| 1020 | `AUDIO_PLAY_500MS_FIRST` | Play first 500ms |
| 1021 | `AUDIO_PLAY_500MS_LAST` | Play last 500ms |
| 1022 | `AUDIO_PLAY_TO_END` | Play to the end |
| 1023 | `AUDIO_SCROLL_LEFT` | Scroll right |
| 1024 | `AUDIO_SCROLL_RIGHT` | Scroll left |
| 1025 | `AUDIO_GOTO` | Go to selection |
| 1026 | `AUDIO_LEAD_IN` | Add lead-in |
| 1027 | `AUDIO_LEAD_OUT` | Add lead-out |
| 2000 | `VIDEO_PLAY_PAUSE` | Play / Pause |
| 2001 | `VIDEO_STOP` | Stop |
| 2002 | `VIDEO_5_SECONDS_FORWARD` | 5 seconds forward |
| 2003 | `VIDEO_5_SECONDS_BACKWARD` | 5 seconds backward |
| 2004 | `VIDEO_MINUTE_BACKWARD` | 1 minute backward |
| 2005 | `VIDEO_MINUTE_FORWARD` | 1 minute forward |
| 2006 | `VIDEO_VOLUME_PLUS` | Volume up |
| 2007 | `VIDEO_VOLUME_MINUS` | Volume down |
| 2008 | `VIDEO_PREVIOUS_FILE` | Previous file |
| 2009 | `VIDEO_NEXT_FILE` | Next file |
| 2010 | `VIDEO_PREVIOUS_CHAPTER` | Previous chapter |
| 2011 | `VIDEO_NEXT_CHAPTER` | Next chapter |
| 2012 | `VIDEO_FULL_SCREEN` | Full screen |
| 2013 | `VIDEO_HIDE_PROGRESS_BAR` | Show / hide progress bar |
| 2014 | `VIDEO_DELETE_FILE` | Remove video |
| 2015 | `VIDEO_ASPECT_RATIO` | Change aspect ratio |
| 2016 | `VIDEO_COPY_COORDS` |  |
| 2017 | `VIDEO_SAVE_FRAME_TO_PNG` | Save frame as PNG |
| 2018 | `VIDEO_COPY_FRAME_TO_CLIPBOARD` | Copy frame to clipboard |
| 2019 | `VIDEO_SAVE_SUBBED_FRAME_TO_PNG` | Save frame with subtitles as PNG |
| 2020 | `VIDEO_COPY_SUBBED_FRAME_TO_CLIPBOARD` | Copy frame with subtitles to clipboard |
| 3000 | `EDITBOX_CHANGE_FONT` | Font selection |
| 3001 | `EDITBOX_CHANGE_UNDERLINE` | Underline |
| 3002 | `EDITBOX_CHANGE_STRIKEOUT` | Strikethrough |
| 3003 | `EDITBOX_PASTE_ALL_TO_TRANSLATION` | Paste all |
| 3004 | `EDITBOX_PASTE_SELECTION_TO_TRANSLATION` | Paste the selected |
| 3005 | `EDITBOX_HIDE_ORIGINAL` | Hide original |
| 3006 | `EDITBOX_CHANGE_COLOR_PRIMARY` | Primary color |
| 3007 | `EDITBOX_CHANGE_COLOR_SECONDARY` | Secondary color for karaoke |
| 3008 | `EDITBOX_CHANGE_COLOR_OUTLINE` | Border color |
| 3009 | `EDITBOX_CHANGE_COLOR_SHADOW` | Shadow color |
| 3010 | `EDITBOX_COMMIT` | Apply changes |
| 3011 | `EDITBOX_COMMIT_GO_NEXT_LINE` | Apply the changes and go to the next line |
| 3012 | `EDITBOX_INSERT_BOLD` | Add bold |
| 3013 | `EDITBOX_INSERT_ITALIC` | Add italic |
| 3014 | `EDITBOX_SPLIT_LINE` | Add line wrap |
| 3015 | `EDITBOX_START_DIFFERENCE` | Insert difference from the start |
| 3016 | `EDITBOX_END_DIFFERENCE` | Insert difference to the end |
| 3017 | `EDITBOX_FIND_NEXT_DOUBTFUL` | Next unconfirmed line |
| 3018 | `EDITBOX_FIND_NEXT_UNTRANSLATED` | Next untranslated line |
| 3019 | `EDITBOX_SET_DOUBTFUL` | Mark as unconfirmed and go to the next line |
| 3100 | `EDITBOX_TAG_BUTTON1` | First tag button |
| 3101 | `EDITBOX_TAG_BUTTON2` | Second tag button |
| 3102 | `EDITBOX_TAG_BUTTON3` | Third tag button |
| 3103 | `EDITBOX_TAG_BUTTON4` | 4th tag button |
| 3104 | `EDITBOX_TAG_BUTTON5` | 5th tag button |
| 3105 | `EDITBOX_TAG_BUTTON6` | 6th tag button |
| 3106 | `EDITBOX_TAG_BUTTON7` | 7th tag button |
| 3107 | `EDITBOX_TAG_BUTTON8` | 8th tag button |
| 3108 | `EDITBOX_TAG_BUTTON9` | 9th tag button |
| 3109 | `EDITBOX_TAG_BUTTON10` | 10th tag button |
| 3110 | `EDITBOX_TAG_BUTTON11` | 11th tag button |
| 3111 | `EDITBOX_TAG_BUTTON12` | 12th tag button |
| 3112 | `EDITBOX_TAG_BUTTON13` | 13th tag button |
| 3113 | `EDITBOX_TAG_BUTTON14` | 14th tag button |
| 3114 | `EDITBOX_TAG_BUTTON15` | 15th tag button |
| 3115 | `EDITBOX_TAG_BUTTON16` | 16th tag button |
| 3116 | `EDITBOX_TAG_BUTTON17` | 17th tag button |
| 3117 | `EDITBOX_TAG_BUTTON18` | 18th tag button |
| 3118 | `EDITBOX_TAG_BUTTON19` | 19th tag button |
| 3119 | `EDITBOX_TAG_BUTTON20` | 20th tag button |
| 4001 | `GRID_HIDE_LAYER` | Hide layer |
| 4002 | `GRID_HIDE_START` | Hide start time |
| 4004 | `GRID_HIDE_END` | Hide end time |
| 4016 | `GRID_HIDE_ACTOR` | Hide actor |
| 4008 | `GRID_HIDE_STYLE` | Hide style |
| 4032 | `GRID_HIDE_MARGINL` | Hide left margin |
| 4064 | `GRID_HIDE_MARGINR` | Hide right margin |
| 4128 | `GRID_HIDE_MARGINV` | Hide vertical margin |
| 4256 | `GRID_HIDE_EFFECT` | Hide effect |
| 4512 | `GRID_HIDE_CPS` | Hide characters per second |
| 4513 | `GRID_HIDE_WRAPS` |  |
| 4514 | `GRID_INSERT_BEFORE` | Insert before |
| 4515 | `GRID_INSERT_AFTER` | Insert after |
| 4516 | `GRID_INSERT_BEFORE_VIDEO` | Insert before with video time |
| 4517 | `GRID_INSERT_AFTER_VIDEO` | Insert after with video time |
| 4518 | `GRID_INSERT_BEFORE_WITH_VIDEO_FRAME` | Insert before with video frame time |
| 4519 | `GRID_INSERT_AFTER_WITH_VIDEO_FRAME` | Insert after with video frame time |
| 4520 | `GRID_SELECT_VISIBLE_LINES` | Select all lines visible on video |
| 4521 | `GRID_SPLIT_BY_VIDEO_TIME` | Split line at video time |
| 4522 | `GRID_SPLIT_BY_FRAME` | Split lines into frames |
| 4523 | `GRID_SPLIT_BY_CHARS` | Split lines into characters |
| 4524 | `GRID_SPLIT_BY_WORDS` | Split lines into words |
| 4525 | `GRID_SPLIT_BY_WRAPS` | Split lines by wraps |
| 4526 | `GRID_SWAP_LINES` | Swap lines |
| 4527 | `GRID_DUPLICATE_LINES` | Duplicate lines |
| 4528 | `GRID_JOIN_LINES` | Join lines |
| 4529 | `GRID_JOIN_TO_FIRST_LINE` | Join lines and keep first |
| 4530 | `GRID_JOIN_TO_LAST_LINE` | Join lines and keep last |
| 4531 | `GRID_COPY` |  |
| 4532 | `GRID_PASTE` |  |
| 4533 | `GRID_CUT` |  |
| 4534 | `GRID_SHOW_PREVIEW` | Show subtitles preview |
| 4535 | `GRID_HIDE_SELECTED` | Hide selected lines |
| 4536 | `GRID_FILTER_BY_NOTHING` | Turn off filtering |
| 4537 | `GRID_FILTER_BY_STYLES` | Hide lines with styles |
| 4538 | `GRID_FILTER_BY_SELECTIONS` | Hide selected lines |
| 4539 | `GRID_FILTER_BY_DIALOGUES` | Hide comments |
| 4540 | `GRID_FILTER_BY_DOUBTFUL` | Show unconfirmed |
| 4541 | `GRID_FILTER_BY_UNTRANSLATED` | Show untranslated |
| 4542 | `GRID_FILTER` | Filter |
| 4543 | `GRID_FILTER_AFTER_SUBS_LOAD` | Filter after loading subtitles |
| 4544 | `GRID_FILTER_INVERT` | Reverse filtering |
| 4545 | `GRID_FILTER_DO_NOT_RESET` | Do not reset previous filtering |
| 4546 | `GRID_FILTER_IGNORE_IN_ACTIONS` | Ignore filtering in some actions |
| 4547 | `GRID_TREE_MAKE` | Make tree |
| 4548 | `GRID_PASTE_TRANSLATION` | Paste translation text |
| 4549 | `GRID_TRANSLATION_DIALOG` | Dialogue shifting window |
| 4550 | `GRID_SUBS_FROM_MKV` | Load subtitles from an MKV file |
| 4551 | `GRID_MAKE_CONTINOUS_PREVIOUS_LINE` | Set times as a continuous (previous line) |
| 4552 | `GRID_MAKE_CONTINOUS_NEXT_LINE` | Set times as a continuous (next line) |
| 4553 | `GRID_PASTE_COLUMNS` | Paste columns |
| 4554 | `GRID_COPY_COLUMNS` | Copy columns |
| 4555 | `GRID_SET_FPS_FROM_VIDEO` | Set FPS from video |
| 4556 | `GRID_SET_NEW_FPS` | Set new FPS |
| 5000 | `GLOBAL_SAVE_SUBS` | Save |
| 5001 | `GLOBAL_SAVE_ALL_SUBS` | Save all subtitles |
| 5002 | `GLOBAL_SAVE_SUBS_AS` | Save as... |
| 5003 | `GLOBAL_SAVE_TRANSLATION` | Save translation |
| 5004 | `GLOBAL_REMOVE_SUBS` | Remove subtitles from the editor |
| 5005 | `GLOBAL_REDO` | Redo |
| 5006 | `GLOBAL_UNDO` | Undo |
| 5007 | `GLOBAL_UNDO_TO_LAST_SAVE` | Undo to last save |
| 5008 | `GLOBAL_HISTORY` | History |
| 5009 | `GLOBAL_SEARCH` | Find |
| 5010 | `GLOBAL_FIND_REPLACE` | Find and replace |
| 5011 | `GLOBAL_FIND_NEXT` | Find next |
| 5012 | `GLOBAL_MISSPELLS_REPLACER` | Fix minor errors (experimental) |
| 5013 | `GLOBAL_OPEN_SELECT_LINES` | Select lines |
| 5014 | `GLOBAL_OPEN_SPELLCHECKER` | Check spelling |
| 5015 | `GLOBAL_VIDEO_INDEXING` | Open video with FFMS2 |
| 5016 | `GLOBAL_SAVE_WITH_VIDEO_NAME` | Save subtitles using the video name |
| 5017 | `GLOBAL_OPEN_AUDIO` | Open audio |
| 5018 | `GLOBAL_AUDIO_FROM_VIDEO` | Open audio from video |
| 5019 | `GLOBAL_CLOSE_AUDIO` | Close audio |
| 5020 | `GLOBAL_CONVERT_TO_ASS` | Convert to ASS |
| 5021 | `GLOBAL_CONVERT_TO_SRT` | Convert to SRT |
| 5022 | `GLOBAL_CONVERT_TO_TMP` | Convert to TMP |
| 5023 | `GLOBAL_CONVERT_TO_MDVD` | Convert to MDVD |
| 5024 | `GLOBAL_CONVERT_TO_MPL2` | Convert to MPL2 |
| 5025 | `GLOBAL_OPEN_ASS_PROPERTIES` | ASS file properties |
| 5026 | `GLOBAL_OPEN_STYLE_MANAGER` | Style manager |
| 5027 | `GLOBAL_OPEN_SUBS_RESAMPLE` | Resample subtitles |
| 5028 | `GLOBAL_OPEN_FONT_COLLECTOR` | Font collector |
| 5029 | `GLOBAL_HIDE_TAGS` | Hide tags |
| 5030 | `GLOBAL_SHOW_SHIFT_TIMES` | Time shift window |
| 5031 | `GLOBAL_VIEW_ALL` | View all |
| 5032 | `GLOBAL_VIEW_AUDIO` | View audio and subtitles |
| 5033 | `GLOBAL_VIEW_VIDEO` | View video and subtitles |
| 5034 | `GLOBAL_VIEW_ONLY_VIDEO` | View only video |
| 5035 | `GLOBAL_VIEW_SUBS` | View only subtitles |
| 5036 | `GLOBAL_AUTOMATION_LOAD_SCRIPT` | Load script |
| 5037 | `GLOBAL_AUTOMATION_RELOAD_AUTOLOAD` | Refresh autoload scripts |
| 5038 | `GLOBAL_AUTOMATION_LOAD_LAST_SCRIPT` | Run the last loaded script |
| 5039 | `GLOBAL_AUTOMATION_OPEN_HOTKEYS_WINDOW` | Open shortcut mapping window |
| 5040 | `GLOBAL_PLAY_PAUSE` | Play / Pause |
| 5041 | `GLOBAL_PREVIOUS_FRAME` | Previous frame |
| 5042 | `GLOBAL_NEXT_FRAME` | Next frame |
| 5043 | `GLOBAL_VIDEO_ZOOM` | Zoom video |
| 5044 | `GLOBAL_RESET_VIDEO_ZOOM` | Turn off video zoom |
| 5045 | `GLOBAL_SET_START_TIME` | Insert start time from video |
| 5046 | `GLOBAL_SET_END_TIME` | Insert end time from video |
| 5047 | `GLOBAL_SET_VIDEO_AT_START_TIME` | Go to start time |
| 5048 | `GLOBAL_SET_VIDEO_AT_END_TIME` | Go to end time of line |
| 5049 | `GLOBAL_GO_TO_NEXT_KEYFRAME` | Go to next keyframe |
| 5050 | `GLOBAL_GO_TO_PREVIOUS_KEYFRAME` | Go to previous keyframe |
| 5051 | `GLOBAL_SET_AUDIO_FROM_VIDEO` | Set audio position to video time |
| 5052 | `GLOBAL_SET_AUDIO_MARK_FROM_VIDEO` | Set audio marker to video time |
| 5053 | `GLOBAL_LOAD_EXTERNAL_SESSION` |  |
| 5054 | `GLOBAL_SAVE_EXTERNAL_SESSION` |  |
| 5055 | `GLOBAL_LOAD_LAST_SESSION` | Load last session |
| 5100 | `GLOBAL_OPEN_SUBS` | Open subtitles |
| 5101 | `GLOBAL_OPEN_VIDEO` | Open video |
| 5102 | `GLOBAL_OPEN_KEYFRAMES` | Open keyframes |
| 5103 | `GLOBAL_OPEN_DUMMY_VIDEO` |  |
| 5104 | `GLOBAL_OPEN_DUMMY_AUDIO` |  |
| 5105 | `GLOBAL_OPEN_AUTO_SAVE` | Open auto save |
| 5106 | `GLOBAL_DELETE_TEMPORARY_FILES` |  |
| 5107 | `GLOBAL_SETTINGS` | Settings |
| 5108 | `GLOBAL_QUIT` |  |
| 5109 | `GLOBAL_EDITOR` | Enable / Disable editor |
| 5110 | `GLOBAL_ABOUT` | About |
| 5111 | `GLOBAL_HELPERS` | Credits |
| 5112 | `GLOBAL_HELP` | HikariSub website |
| 5113 | `GLOBAL_ANSI` | Report an issue |
| 5114 | `GLOBAL_CHECK_FOR_UPDATES` | Check for updates |
| 5150 | `GLOBAL_PREVIOUS_LINE` | Previous line |
| 5151 | `GLOBAL_NEXT_LINE` | Next line |
| 5152 | `GLOBAL_JOIN_WITH_PREVIOUS` | Merge with previous line |
| 5153 | `GLOBAL_JOIN_WITH_NEXT` | Merge with next line |
| 5154 | `GLOBAL_NEXT_TAB` | Next tab |
| 5155 | `GLOBAL_PREVIOUS_TAB` | Previous tab |
| 5156 | `GLOBAL_REMOVE_LINES` | Delete line |
| 5157 | `GLOBAL_REMOVE_TEXT` | Delete text |
| 5158 | `GLOBAL_SNAP_WITH_START` | Change start time to nearest keyframe |
| 5159 | `GLOBAL_SNAP_WITH_END` | Change end time to nearest keyframe |
| 5160 | `GLOBAL_SORT_LINES` |  |
| 5161 | `GLOBAL_SORT_SELECTED_LINES` |  |
| 5162 | `GLOBAL_RECENT_AUDIO` |  |
| 5163 | `GLOBAL_RECENT_VIDEO` |  |
| 5164 | `GLOBAL_RECENT_SUBS` |  |
| 5165 | `GLOBAL_RECENT_KEYFRAMES` |  |
| 5166 | `GLOBAL_SELECT_FROM_VIDEO` | Select line at current video position |
| 5167 | `GLOBAL_PLAY_ACTUAL_LINE` | Play active line |
| 5168 | `GLOBAL_STYLE_MANAGER_CLEAN_STYLE` | Clean styles of ASS file |
| 5200 | `GLOBAL_SORT_ALL_BY_START_TIMES` | Sort all lines by start time |
| 5201 | `GLOBAL_SORT_ALL_BY_END_TIMES` | Sort all lines by end time |
| 5202 | `GLOBAL_SORT_ALL_BY_STYLE` | Sort all lines by styles |
| 5203 | `GLOBAL_SORT_ALL_BY_ACTOR` | Sort all lines by actor |
| 5204 | `GLOBAL_SORT_ALL_BY_EFFECT` | Sort all lines by effect |
| 5205 | `GLOBAL_SORT_ALL_BY_LAYER` | Sort all lines by layer |
| 5206 | `GLOBAL_SORT_SELECTED_BY_START_TIMES` | Sort selected lines by start time |
| 5207 | `GLOBAL_SORT_SELECTED_BY_END_TIMES` | Sort selected lines by end time |
| 5208 | `GLOBAL_SORT_SELECTED_BY_STYLE` | Sort selected lines by styles |
| 5209 | `GLOBAL_SORT_SELECTED_BY_ACTOR` | Sort selected lines by actor |
| 5210 | `GLOBAL_SORT_SELECTED_BY_EFFECT` | Sort selected lines by effect |
| 5211 | `GLOBAL_SORT_SELECTED_BY_LAYER` | Sort selected lines by layer |
| 5300 | `GLOBAL_SHIFT_TIMES` | Shift times / run time post processor |
| 5301 | `GLOBAL_ADD_PAGE` | Open new tab |
| 5302 | `GLOBAL_CLOSE_PAGE` | Close current tab |

Source: [Hotkeys.h](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Hotkeys.h), [HotkeysNaming.cpp](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HotkeysNaming.cpp).

## Appendix B: concrete default bindings

These are source defaults; user config and fixed accelerators can override/extend them.

```cpp
_hkeys[idAndType(GLOBAL_QUIT, GLOBAL_HOTKEY)] = hdata(_("Exit"), L"Alt-F4");
_hkeys[idAndType(GLOBAL_ADD_PAGE, GLOBAL_HOTKEY)] = hdata(_("Open new tab"), L"Ctrl-T");
_hkeys[idAndType(GLOBAL_CLOSE_PAGE, GLOBAL_HOTKEY)] = hdata(_("Close current tab"), L"Ctrl-W");
_hkeys[idAndType(GLOBAL_SHOW_SHIFT_TIMES, GLOBAL_HOTKEY)] = hdata(_("Time shift window"), L"Ctrl-I");
_hkeys[idAndType(GLOBAL_CONVERT_TO_ASS, GLOBAL_HOTKEY)] = hdata(_("Convert to ASS"), L"F9");
_hkeys[idAndType(GLOBAL_CONVERT_TO_SRT, GLOBAL_HOTKEY)] = hdata(_("Convert to SRT"), L"F8");
_hkeys[idAndType(GLOBAL_CONVERT_TO_MDVD, GLOBAL_HOTKEY)] = hdata(_("Convert to MDVD"), L"F10");
_hkeys[idAndType(GLOBAL_CONVERT_TO_MPL2, GLOBAL_HOTKEY)] = hdata(_("Convert to MPL2"), L"F11");
_hkeys[idAndType(GLOBAL_CONVERT_TO_TMP, GLOBAL_HOTKEY)] = hdata(_("Convert to TMP"), L"Ctrl-F12");
_hkeys[idAndType(GLOBAL_OPEN_STYLE_MANAGER, GLOBAL_HOTKEY)] = hdata(_("Style manager"), L"Ctrl-M");
_hkeys[idAndType(GLOBAL_EDITOR, GLOBAL_HOTKEY)] = hdata(_("Enable / Disable editor"), L"Ctrl-E");
_hkeys[idAndType(GLOBAL_OPEN_VIDEO, GLOBAL_HOTKEY)] = hdata(_("Open video"), L"Ctrl-Shift-O");
_hkeys[idAndType(GLOBAL_SEARCH, GLOBAL_HOTKEY)] = hdata(_("Find"), L"Ctrl-F");
_hkeys[idAndType(GLOBAL_FIND_REPLACE, GLOBAL_HOTKEY)] = hdata(_("Find and replace"), L"Ctrl-H");
_hkeys[idAndType(GLOBAL_UNDO, GLOBAL_HOTKEY)] = hdata(_("Undo"), L"Ctrl-Z");
_hkeys[idAndType(GLOBAL_REDO, GLOBAL_HOTKEY)] = hdata(_("Redo"), L"Ctrl-Y");
_hkeys[idAndType(GLOBAL_HISTORY, GLOBAL_HOTKEY)] = hdata(_("History"), L"Ctrl-Shift-H");
_hkeys[idAndType(GLOBAL_OPEN_SUBS, GLOBAL_HOTKEY)] = hdata(_("Open subtitles"), L"Ctrl-O");
_hkeys[idAndType(GLOBAL_SAVE_SUBS, GLOBAL_HOTKEY)] = hdata(_("Save"), L"Ctrl-S");
_hkeys[idAndType(GLOBAL_SAVE_SUBS_AS, GLOBAL_HOTKEY)] = hdata(_("Save as..."), L"Ctrl-Shift-S");
_hkeys[idAndType(GLOBAL_REMOVE_TEXT, GLOBAL_HOTKEY)] = hdata(_("Delete text"), L"Alt-Delete");
_hkeys[idAndType(GLOBAL_REMOVE_LINES, GLOBAL_HOTKEY)] = hdata(_("Delete line"), L"Shift-Delete");
_hkeys[idAndType(GLOBAL_SET_START_TIME, GLOBAL_HOTKEY)] = hdata(_("Insert start time from video"), L"Ctrl-Left");
_hkeys[idAndType(GLOBAL_SET_END_TIME, GLOBAL_HOTKEY)] = hdata(_("Insert end time from video"), L"Ctrl-Right");
_hkeys[idAndType(GLOBAL_PLAY_PAUSE, GLOBAL_HOTKEY)] = hdata(_("Play / Pause"), L"Alt-Space");
_hkeys[idAndType(GLOBAL_PREVIOUS_FRAME, GLOBAL_HOTKEY)] = hdata(_("Previous frame"), L"Left");
_hkeys[idAndType(GLOBAL_NEXT_FRAME, GLOBAL_HOTKEY)] = hdata(_("Next frame"), L"Right");
_hkeys[idAndType(GLOBAL_PREVIOUS_LINE, GLOBAL_HOTKEY)] = hdata(_("Previous line"), L"Ctrl-Up");//góra
_hkeys[idAndType(GLOBAL_NEXT_LINE, GLOBAL_HOTKEY)] = hdata(_("Next line"), L"Ctrl-Down");//dół
_hkeys[idAndType(GLOBAL_FIND_NEXT, GLOBAL_HOTKEY)] = hdata(_("Find next"), L"F3");
_hkeys[idAndType(GLOBAL_JOIN_WITH_PREVIOUS, GLOBAL_HOTKEY)] = hdata(_("Merge with previous line"), L"F4");
_hkeys[idAndType(GLOBAL_JOIN_WITH_NEXT, GLOBAL_HOTKEY)] = hdata(_("Merge with next line"), L"F5");
_hkeys[idAndType(GLOBAL_SNAP_WITH_START, GLOBAL_HOTKEY)] = hdata(_("Change start time to nearest keyframe"), L"Shift-Left");//lewo
_hkeys[idAndType(GLOBAL_SNAP_WITH_END, GLOBAL_HOTKEY)] = hdata(_("Change end time to nearest keyframe"), L"Shift-Right");//prawo
_hkeys[idAndType(GLOBAL_NEXT_TAB, GLOBAL_HOTKEY)] = hdata(_("Next tab"), L"Ctrl-PgDn");
_hkeys[idAndType(GLOBAL_PREVIOUS_TAB, GLOBAL_HOTKEY)] = hdata(_("Previous tab"), L"Ctrl-PgUp");
_hkeys[idAndType(GLOBAL_SELECT_FROM_VIDEO, GLOBAL_HOTKEY)] = hdata(_("Select line at current video position"), L"F2");
_hkeys[idAndType(GLOBAL_HELP, GLOBAL_HOTKEY)] = hdata(_("HikariSub website"), L"F1");
_hkeys[idAndType(GRID_DUPLICATE_LINES, GRID_HOTKEY)] = hdata(_("Duplicate lines"), L"Ctrl-D");
_hkeys[idAndType(GRID_COPY_COLUMNS, GRID_HOTKEY)] = hdata(_("Copy columns"), L"Ctrl-Shift-C");
_hkeys[idAndType(GRID_PASTE_COLUMNS, GRID_HOTKEY)] = hdata(_("Paste columns"), L"Ctrl-Shift-V");
_hkeys[idAndType(GRID_SHOW_PREVIEW, GRID_HOTKEY)] = hdata(_("Show subtitles preview"), L"Ctrl-Q");
_hkeys[idAndType(VIDEO_PLAY_PAUSE, VIDEO_HOTKEY)] = hdata(_("Play / Pause"), L"Space");
_hkeys[idAndType(VIDEO_5_SECONDS_FORWARD, VIDEO_HOTKEY)] = hdata(_("5 seconds forward"), L"L");
_hkeys[idAndType(VIDEO_5_SECONDS_BACKWARD, VIDEO_HOTKEY)] = hdata(_("5 seconds backward"), L";");
_hkeys[idAndType(VIDEO_MINUTE_FORWARD, VIDEO_HOTKEY)] = hdata(_("1 minute forward"), L"Up");
_hkeys[idAndType(VIDEO_MINUTE_BACKWARD, VIDEO_HOTKEY)] = hdata(_("1 minute backward"), L"Down");
_hkeys[idAndType(VIDEO_NEXT_FILE, VIDEO_HOTKEY)] = hdata(_("Next file"), L".");
_hkeys[idAndType(VIDEO_PREVIOUS_FILE, VIDEO_HOTKEY)] = hdata(_("Previous file"), L",");
_hkeys[idAndType(VIDEO_VOLUME_PLUS, VIDEO_HOTKEY)] = hdata(_("Volume up"), L"Num .");
_hkeys[idAndType(VIDEO_VOLUME_MINUS, VIDEO_HOTKEY)] = hdata(_("Volume down"), L"Num 0");
_hkeys[idAndType(VIDEO_NEXT_CHAPTER, VIDEO_HOTKEY)] = hdata(_("Next chapter"), L"M");
_hkeys[idAndType(VIDEO_PREVIOUS_CHAPTER, VIDEO_HOTKEY)] = hdata(_("Previous chapter"), L"N");
_hkeys[idAndType(EDITBOX_INSERT_BOLD, EDITBOX_HOTKEY)] = hdata(_("Add bold"), L"Ctrl-B");
_hkeys[idAndType(EDITBOX_INSERT_ITALIC, EDITBOX_HOTKEY)] = hdata(_("Add italic"), L"Ctrl-I");
_hkeys[idAndType(EDITBOX_SPLIT_LINE, EDITBOX_HOTKEY)] = hdata(_("Add line wrap"), L"Shift-Enter");
_hkeys[idAndType(EDITBOX_START_DIFFERENCE, EDITBOX_HOTKEY)] = hdata(_("Insert difference from the start"), L"Ctrl-,");
_hkeys[idAndType(EDITBOX_END_DIFFERENCE, EDITBOX_HOTKEY)] = hdata(_("Insert difference to the end"), L"Ctrl-.");
_hkeys[idAndType(EDITBOX_FIND_NEXT_DOUBTFUL, EDITBOX_HOTKEY)] = hdata(_("Next unconfirmed line"), L"Ctrl-D");
_hkeys[idAndType(EDITBOX_FIND_NEXT_UNTRANSLATED, EDITBOX_HOTKEY)] = hdata(_("Next untranslated line"), L"Ctrl-R");
_hkeys[idAndType(EDITBOX_SET_DOUBTFUL, EDITBOX_HOTKEY)] = hdata(_("Mark as unconfirmed and go to the next line"), L"Alt-Down");
_hkeys[idAndType(EDITBOX_COMMIT, EDITBOX_HOTKEY)] = hdata(_("Apply changes"), L"Ctrl-Enter");
_hkeys[idAndType(EDITBOX_COMMIT_GO_NEXT_LINE, EDITBOX_HOTKEY)] = hdata(_("Apply the changes and go to the next line"), L"Enter");
_hkeys[idAndType(AUDIO_COMMIT, AUDIO_HOTKEY)] = hdata(_("Commit"), L"Enter");
_hkeys[idAndType(AUDIO_COMMIT_ALT, AUDIO_HOTKEY)] = hdata(_("Commit alt"), L"G");
_hkeys[idAndType(AUDIO_PREVIOUS, AUDIO_HOTKEY)] = hdata(_("Previous line"), L"Left");
_hkeys[idAndType(AUDIO_PREVIOUS_ALT, AUDIO_HOTKEY)] = hdata(_("Previous line alt"), L"Z");
_hkeys[idAndType(AUDIO_NEXT, AUDIO_HOTKEY)] = hdata(_("Next line"), L"Right");
_hkeys[idAndType(AUDIO_NEXT_ALT, AUDIO_HOTKEY)] = hdata(_("Next line alt"), L"X");
_hkeys[idAndType(AUDIO_PLAY, AUDIO_HOTKEY)] = hdata(_("Play"), L"Down");
_hkeys[idAndType(AUDIO_PLAY_ALT, AUDIO_HOTKEY)] = hdata(_("Play alt"), L"S");
_hkeys[idAndType(AUDIO_PLAY_LINE, AUDIO_HOTKEY)] = hdata(_("Play line"), L"Up");
_hkeys[idAndType(AUDIO_PLAY_LINE_ALT, AUDIO_HOTKEY)] = hdata(_("Play line alt"), L"R");
_hkeys[idAndType(AUDIO_STOP, AUDIO_HOTKEY)] = hdata(_("Stop"), L"H");
_hkeys[idAndType(AUDIO_GOTO, AUDIO_HOTKEY)] = hdata(_("Go to selection"), L"B");
_hkeys[idAndType(AUDIO_SCROLL_RIGHT, AUDIO_HOTKEY)] = hdata(_("Scroll left"), L"A");
_hkeys[idAndType(AUDIO_SCROLL_LEFT, AUDIO_HOTKEY)] = hdata(_("Scroll right"), L"F");
_hkeys[idAndType(AUDIO_PLAY_BEFORE_MARK, AUDIO_HOTKEY)] = hdata(_("Play before the marker"), L"Num 0");
_hkeys[idAndType(AUDIO_PLAY_AFTER_MARK, AUDIO_HOTKEY)] = hdata(_("Play after the marker"), L"Num .");
_hkeys[idAndType(AUDIO_PLAY_500MS_FIRST, AUDIO_HOTKEY)] = hdata(_("Play first 500ms"), L"E");
_hkeys[idAndType(AUDIO_PLAY_500MS_LAST, AUDIO_HOTKEY)] = hdata(_("Play last 500ms"), L"D");
_hkeys[idAndType(AUDIO_PLAY_500MS_BEFORE, AUDIO_HOTKEY)] = hdata(_("Play 500ms before"), L"Q");
_hkeys[idAndType(AUDIO_PLAY_500MS_AFTER, AUDIO_HOTKEY)] = hdata(_("Play 500ms after"), L"W");
_hkeys[idAndType(AUDIO_PLAY_TO_END, AUDIO_HOTKEY)] = hdata(_("Play to the end"), L"T");
_hkeys[idAndType(AUDIO_LEAD_IN, AUDIO_HOTKEY)] = hdata(_("Add lead-in"), L"C");
_hkeys[idAndType(AUDIO_LEAD_OUT, AUDIO_HOTKEY)] = hdata(_("Add lead-out"), L"V");
```

## Appendix C: source census, per-surface options and entry points

Each file lists its configuration/color symbols (including array references), user-control inheritance and menu/control/tooltip source declarations. Multi-line declarations continue through the first semicolon. Click the source line to inspect conditions and handlers. This is a source census, not a runtime trace.

### AudioBox.cpp

Configuration/color symbols: `AUDIO_AUTO_COMMIT`, `AUDIO_AUTO_SCROLL`, `AUDIO_BOX_HEIGHT`, `AUDIO_DONT_PLAY_WHEN_LINE_CHANGES`, `AUDIO_HORIZONTAL_ZOOM`, `AUDIO_KARAOKE`, `AUDIO_KARAOKE_SPLIT_MODE`, `AUDIO_LINK`, `AUDIO_MARK_PLAY_TIME`, `AUDIO_NEXT_LINE_ON_COMMIT`, `AUDIO_SPECTRUM_NON_LINEAR_ON`, `AUDIO_SPECTRUM_ON`, `AUDIO_VERTICAL_ZOOM`, `AUDIO_VOLUME`, `WINDOW_BACKGROUND`.

- [L93](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L93) `audioScroll->SetToolTip(_("Search bar"));`
- [L107](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L107) `HorizontalZoom->SetToolTip(_("Horizontal stretching"));`
- [L113](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L113) `VerticalZoom->SetToolTip(_("Vertical stretching"));`
- [L115](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L115) `VolumeBar->SetToolTip(_("Volume"));`
- [L122](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L122) `VerticalLink = new ToggleButton(this, Audio_Vertical_Link, emptyString, emptyString, wxDefaultPosition, wxSize(40, -1));`
- [L124](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L124) `VerticalLink->SetToolTip(_("Link the volume and stretch sliders"));`
- [L149](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L149) `temp = new MappedButton(this, AUDIO_PREVIOUS, emptyString, wxBITMAP_PNG(L"button_prev"), wxDefaultPosition, wxDefaultSize, AUDIO_HOTKEY, MAKE_SQUARE_BUTTON);`
- [L151](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L151) `temp->SetToolTip(_("Play the previous line"));`
- [L153](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L153) `temp = new MappedButton(this, AUDIO_NEXT, emptyString, wxBITMAP_PNG(L"button_next"), wxDefaultPosition, wxDefaultSize, AUDIO_HOTKEY, MAKE_SQUARE_BUTTON);`
- [L155](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L155) `temp->SetToolTip(_("Play the next line"));`
- [L157](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L157) `temp = new MappedButton(this, AUDIO_PLAY, emptyString, wxBITMAP_PNG(L"BUTTON_PLAY_LINE"), wxDefaultPosition, wxDefaultSize, AUDIO_HOTKEY, MAKE_SQUARE_BUTTON);`
- [L159](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L159) `temp->SetToolTip(_("Play the current syllable / line"));`
- [L161](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L161) `temp = new MappedButton(this, AUDIO_PLAY_LINE, emptyString, wxBITMAP_PNG(L"button_playsel"), wxDefaultPosition, wxDefaultSize, AUDIO_HOTKEY, MAKE_SQUARE_BUTTON);`
- [L163](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L163) `temp->SetToolTip(_("Play the current line"));`
- [L165](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L165) `temp = new MappedButton(this, AUDIO_STOP, _("Stop playback"), wxBITMAP_PNG(L"button_stop"), wxDefaultPosition, wxDefaultSize, AUDIO_HOTKEY, MAKE_SQUARE_BUTTON);`
- [L168](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L168) `temp = new MappedButton(this, AUDIO_PLAY_BEFORE_MARK, _("Play before the tag"), wxBITMAP_PNG(L"button_playbefore"), wxDefaultPosition, wxDefaultSize, AUDIO_HOTKEY, MAKE_SQUARE_BUTTON);`
- [L170](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L170) `temp = new MappedButton(this, AUDIO_PLAY_AFTER_MARK, _("Play after the tag"), wxBITMAP_PNG(L"button_playafter"), wxDefaultPosition, wxDefaultSize, AUDIO_HOTKEY, MAKE_SQUARE_BUTTON);`
- [L173](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L173) `temp = new MappedButton(this, AUDIO_PLAY_500MS_BEFORE, _("Play 500ms before the start time"), wxBITMAP_PNG(L"button_playfivehbefore"), wxDefaultPosition, wxDefaultSize, AUDIO_HOTKEY, MAKE_SQUARE_BUTTON);`
- [L175](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L175) `temp = new MappedButton(this, AUDIO_PLAY_500MS_FIRST, _("Play 500 ms after the start time"), wxBITMAP_PNG(L"button_playfirstfiveh"), wxDefaultPosition, wxDefaultSize, AUDIO_HOTKEY, MAKE_SQUARE_BUTTON);`
- [L177](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L177) `temp = new MappedButton(this, AUDIO_PLAY_500MS_LAST, _("Play 500ms before the end time"), wxBITMAP_PNG(L"button_playlastfiveh"), wxDefaultPosition, wxDefaultSize, AUDIO_HOTKEY, MAKE_SQUARE_BUTTON);`
- [L179](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L179) `temp = new MappedButton(this, AUDIO_PLAY_500MS_AFTER, _("Play 500ms after the end time"), wxBITMAP_PNG(L"button_playfivehafter"), wxDefaultPosition, wxDefaultSize, AUDIO_HOTKEY, MAKE_SQUARE_BUTTON);`
- [L181](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L181) `temp = new MappedButton(this, AUDIO_PLAY_TO_END, _("Play to the end"), wxBITMAP_PNG(L"button_playtoend"), wxDefaultPosition, wxDefaultSize, AUDIO_HOTKEY, MAKE_SQUARE_BUTTON);`
- [L184](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L184) `temp = new MappedButton(this, AUDIO_LEAD_IN, _("Add lead-in to the active line"), wxBITMAP_PNG(L"button_leadin"), wxDefaultPosition, wxDefaultSize, AUDIO_HOTKEY, MAKE_SQUARE_BUTTON);`
- [L186](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L186) `temp = new MappedButton(this, AUDIO_LEAD_OUT, _("Add lead-out to the active line"), wxBITMAP_PNG(L"button_leadout"), wxDefaultPosition, wxDefaultSize, AUDIO_HOTKEY, MAKE_SQUARE_BUTTON);`
- [L189](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L189) `temp = new MappedButton(this, AUDIO_COMMIT, emptyString, wxBITMAP_PNG(L"button_audio_commit"), wxDefaultPosition, wxDefaultSize, AUDIO_HOTKEY, MAKE_SQUARE_BUTTON);`
- [L191](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L191) `temp->SetToolTip(_("Apply changes"));`
- [L193](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L193) `temp = new MappedButton(this, AUDIO_GOTO, _("Go to selection"), wxBITMAP_PNG(L"button_audio_goto"), wxDefaultPosition, wxDefaultSize, AUDIO_HOTKEY, MAKE_SQUARE_BUTTON);`
- [L196](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L196) `KaraSwitch = new ToggleButton(this, Audio_Button_Karaoke, emptyString, _("Enable / disable karaoke creation"), wxDefaultPosition, wxDefaultSize, MAKE_SQUARE_BUTTON);`
- [L200](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L200) `KaraMode = new ToggleButton(this, Audio_Button_Split, emptyString, _("Enable / Disable automatic splitting of syllables"), wxDefaultPosition, wxDefaultSize, MAKE_SQUARE_BUTTON);`
- [L205](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L205) `AutoCommit = new ToggleButton(this, Audio_Check_AutoCommit, emptyString, _("Automatically apply changes"), wxDefaultPosition, wxDefaultSize, MAKE_SQUARE_BUTTON);`
- [L209](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L209) `NextCommit = new ToggleButton(this, Audio_Check_NextCommit, emptyString, _("Go to the next line after applying changes"), wxDefaultPosition, wxDefaultSize, MAKE_SQUARE_BUTTON);`
- [L213](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L213) `AutoScroll = new ToggleButton(this, Audio_Check_AutoGoto, emptyString, _("Auto-scroll to the active line"), wxDefaultPosition, wxDefaultSize, MAKE_SQUARE_BUTTON);`
- [L217](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L217) `SpectrumMode = new ToggleButton(this, Audio_Check_Spectrum, emptyString, _("Spectrum mode"), wxDefaultPosition, wxDefaultSize, MAKE_SQUARE_BUTTON);`
- [L221](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L221) `SpectrumNonLinear = new ToggleButton(this, Audio_Check_Spectrum_Non_Linear, emptyString, _("Enhance speech frequencies in the spectrum"), wxDefaultPosition, wxDefaultSize, MAKE_SQUARE_BUTTON);`

### AudioBox.h

- [L65](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.h#L65) `class AudioBox : public HikariPanel {`

### AudioDisplay.cpp

Configuration/color symbols: `AUDIO_AUTO_COMMIT`, `AUDIO_AUTO_FOCUS`, `AUDIO_AUTO_SCROLL`, `AUDIO_BACKGROUND`, `AUDIO_DRAW_KEYFRAMES`, `AUDIO_DRAW_SECONDARY_LINES`, `AUDIO_DRAW_SELECTION_BACKGROUND`, `AUDIO_DRAW_VIDEO_POSITION`, `AUDIO_GRAB_TIMES_ON_SELECT`, `AUDIO_HORIZONTAL_ZOOM`, `AUDIO_INACTIVE_LINES_BACKGROUND`, `AUDIO_INACTIVE_LINES_DISPLAY_MODE`, `AUDIO_KARAOKE`, `AUDIO_KARAOKE_MOVE_ON_CLICK`, `AUDIO_KARAOKE_SPLIT_MODE`, `AUDIO_KEYFRAMES`, `AUDIO_LEAD_IN_VALUE`, `AUDIO_LEAD_OUT_VALUE`, `AUDIO_LINE_BOUNDARIES_THICKNESS`, `AUDIO_LINE_BOUNDARY_END`, `AUDIO_LINE_BOUNDARY_INACTIVE_LINE`, `AUDIO_LINE_BOUNDARY_MARK`, `AUDIO_LINE_BOUNDARY_START`, `AUDIO_PLAY_CURSOR`, `AUDIO_SECONDS_BOUNDARIES`, `AUDIO_SELECTION_BACKGROUND`, `AUDIO_SELECTION_BACKGROUND_MODIFIED`, `AUDIO_SNAP_TO_KEYFRAMES`, `AUDIO_SNAP_TO_OTHER_LINES`, `AUDIO_SPECTRUM_BACKGROUND`, `AUDIO_SPECTRUM_NON_LINEAR_ON`, `AUDIO_SPECTRUM_ON`, `AUDIO_START_DRAG_SENSITIVITY`, `AUDIO_SYLLABLE_BOUNDARIES`, `AUDIO_SYLLABLE_TEXT`, `AUDIO_VERTICAL_ZOOM`, `AUDIO_VOLUME`, `AUDIO_WAVEFORM`, `AUDIO_WAVEFORM_INACTIVE`, `AUDIO_WAVEFORM_MODIFIED`, `AUDIO_WAVEFORM_SELECTED`, `AUDIO_WHEEL_DEFAULT_TO_ZOOM`, `DISABLE_LIVE_VIDEO_EDITING`, `PROGRAM_FONT_SIZE`, `WINDOW_BACKGROUND`, `WINDOW_TEXT`.


### AudioDisplay.h

- [L53](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioDisplay.h#L53) `class AudioDisplay : public wxWindow {`

### AudioPlayerDSound.h

- [L127](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioPlayerDSound.h#L127) `class DirectSoundPlayer2 : public wxEvtHandler {`

### AudioSpectrum.cpp

Configuration/color symbols: `AUDIO_SPECTRUM_BACKGROUND`, `AUDIO_SPECTRUM_ECHO`, `AUDIO_SPECTRUM_INNER`, `AUDIO_SPECTRUM_NON_LINEAR_ON`.


### Automation.cpp

Configuration/color symbols: `AUTOMATION_LOADING_METHOD`, `AUTOMATION_SCRIPT_EDITOR`.

- [L1402](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Automation.cpp#L1402) `submenu->Append(start, strippedbug, _("Error"));`
- [L1409](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Automation.cpp#L1409) `submenu->Append(start, _("Edit"), _("Edit"));`
- [L1414](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Automation.cpp#L1414) `submenu->Append(start, _("Refresh"), _("Refresh"));`
- [L1419](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Automation.cpp#L1419) `(*bar)->Append(-1, script->GetName(), submenu, script->GetDescription());`
- [L1449](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Automation.cpp#L1449) `submenu->Append(start, strippedbug, _("Error"));`
- [L1456](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Automation.cpp#L1456) `submenu->Append(start, _("Edit"), _("Edit"));`
- [L1461](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Automation.cpp#L1461) `submenu->Append(start, _("Refresh"), _("Refresh"));`
- [L1466](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Automation.cpp#L1466) `(*bar)->Append(-1, script->GetName(), submenu, script->GetDescription());`

### Automation.h

- [L228](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Automation.h#L228) `class LuaThreadedCall : public wxThread {`

### AutomationDialog.cpp

Configuration/color symbols: `WINDOW_BACKGROUND`, `WINDOW_TEXT`.

- [L190](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutomationDialog.cpp#L190) `cw->SetToolTip(wxString(hint));`
- [L222](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutomationDialog.cpp#L222) `cw->SetToolTip(wxString(hint));`
- [L241](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutomationDialog.cpp#L241) `cw->SetToolTip(wxString(hint));`
- [L273](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutomationDialog.cpp#L273) `cw->SetToolTip(wxString(hint));`
- [L311](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutomationDialog.cpp#L311) `scd->SetToolTip(wxString(hint));`
- [L343](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutomationDialog.cpp#L343) `cw = new HikariChoice(parent, -1, wxString(value), wxDefaultPosition, wxDefaultSize, items, wxCB_READONLY);`
- [L344](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutomationDialog.cpp#L344) `cw->SetToolTip(wxString(hint));`
- [L372](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutomationDialog.cpp#L372) `cw = new HikariCheckBox(parent, -1, wxString(label));`
- [L373](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutomationDialog.cpp#L373) `cw->SetToolTip(wxString(hint));`
- [L460](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutomationDialog.cpp#L460) `window = new HikariDialog(parent, -1, name);`
- [L487](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutomationDialog.cpp#L487) `auto button = new MappedButton(window, id, buttons[i].second);`

### AutomationHotkeysDialog.cpp

Configuration/color symbols: `WINDOW_TEXT`, `WINDOW_TEXT_INACTIVE`, `WINDOW_WARNING_ELEMENTS`.

- [L44](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutomationHotkeysDialog.cpp#L44) `theList->SetToolTip(accelerator);`
- [L143](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutomationHotkeysDialog.cpp#L143) `MappedButton *OK = new MappedButton(this, ID_HOTKEYS_OK, L"OK");`
- [L145](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutomationHotkeysDialog.cpp#L145) `MappedButton *setHotkey = new MappedButton(this, ID_HOTKEYS_MAP, _("Map hotkey"));`
- [L147](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutomationHotkeysDialog.cpp#L147) `MappedButton *deleteHotkey = new MappedButton(this, ID_HOTKEYS_DELETE, _("Delete hotkey"));`
- [L149](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutomationHotkeysDialog.cpp#L149) `MappedButton *cancel = new MappedButton(this, wxID_CANCEL, _("Cancel"));`

### AutomationHotkeysDialog.h

- [L22](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutomationHotkeysDialog.h#L22) `class AutomationHotkeysDialog : public HikariDialog`

### AutomationProgress.cpp

Configuration/color symbols: `AUTOMATION_TRACE_LEVEL`, `WINDOW_BACKGROUND`, `WINDOW_TEXT`.

- [L115](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutomationProgress.cpp#L115) `lpd = new LuaProgressDialog(_parent, L);`
- [L296](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutomationProgress.cpp#L296) `cancel_button = new MappedButton(this, 6666, _("Cancel"));`

### AutomationProgress.h

- [L45](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutomationProgress.h#L45) `class LuaProgressDialog :public wxDialog`
- [L80](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutomationProgress.h#L80) `class LuaProgressSink : public wxEvtHandler{`

### AutomationScriptReader.cpp

Configuration/color symbols: `AUTOMATION_OLD_SCRIPTS_COMPATIBILITY`.


### AutoSaveOpen.cpp

- [L33](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutoSaveOpen.cpp#L33) `filterList = new MappedButton(this, ID_AUTO_SAVE_FILTER, _("Filter list"));`
- [L34](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutoSaveOpen.cpp#L34) `seekAllWords = new HikariCheckBox(this, -1, _("All words"));`
- [L58](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutoSaveOpen.cpp#L58) `open = new MappedButton(this, ID_AUTO_SAVE_OK, _("Open"), -1, wxDefaultPosition, wxSize(100, -1));`
- [L59](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutoSaveOpen.cpp#L59) `MappedButton* cancel = new MappedButton(this, wxID_CANCEL, _("Cancel"));`

### AutoSaveOpen.h

- [L56](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutoSaveOpen.h#L56) `class AutoSaveOpen : public HikariDialog`

### AutoSavesRemoving.cpp

- [L42](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutoSavesRemoving.cpp#L42) `day = new HikariChoice(this, ID_DATE_DAY_LIST, wxDefaultPosition, wxDefaultSize, days);`
- [L47](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutoSavesRemoving.cpp#L47) `month = new HikariChoice(this, ID_DATE_MONTH_LIST, wxDefaultPosition, wxDefaultSize, 12, months);`
- [L53](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutoSavesRemoving.cpp#L53) `year = new HikariChoice(this, -1, wxDefaultPosition, wxDefaultSize, years);`
- [L90](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutoSavesRemoving.cpp#L90) `new MappedButton(this, ID_REMOVE_ALL, _("Remove all files"));`
- [L92](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutoSavesRemoving.cpp#L92) `new MappedButton(this, ID_REMOVE_ALL_BY_DATE, _("Remove all older files"));`
- [L103](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutoSavesRemoving.cpp#L103) `new MappedButton(this, ID_REMOVE_SELECTED_AUTO_SAVES, _("Remove selected autosave files"));`
- [L105](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutoSavesRemoving.cpp#L105) `new MappedButton(this, ID_REMOVE_ALL_AUTO_SAVES, _("Remove all auto save files"));`
- [L107](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutoSavesRemoving.cpp#L107) `new MappedButton(this, ID_REMOVE_AUTO_SAVES_BY_DATE, _("Remove older auto save files"));`
- [L116](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutoSavesRemoving.cpp#L116) `new MappedButton(this, ID_REMOVE_SELECTED_INDICES, _("Remove selected index files"));`
- [L118](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutoSavesRemoving.cpp#L118) `new MappedButton(this, ID_REMOVE_ALL_INDICES, _("Remove all index files"));`
- [L120](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutoSavesRemoving.cpp#L120) `new MappedButton(this, ID_REMOVE_INDICES_BY_DATE, _("Remove older index files"));`
- [L129](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutoSavesRemoving.cpp#L129) `new MappedButton(this, ID_REMOVE_SELECTED_AUDIO_CACHES, _("Remove selected audio cache files"));`
- [L131](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutoSavesRemoving.cpp#L131) `new MappedButton(this, ID_REMOVE_ALL_AUDIO_CACHES, _("Remove all audio cache files"));`
- [L133](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutoSavesRemoving.cpp#L133) `new MappedButton(this, ID_REMOVE_AUDIO_CACHE_BY_DATE, _("Remove older audio cache files"));`
- [L182](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutoSavesRemoving.cpp#L182) `wxFileDialog* FileDialog = new wxFileDialog(this, _("Choose file to remove"), path, emptyString, description, wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_MULTIPLE);`

### AutoSavesRemoving.h

- [L22](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutoSavesRemoving.h#L22) `class AutoSavesRemoving : public HikariDialog`

### BitmapButton.h

- [L22](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/BitmapButton.h#L22) `class BitmapButton : public wxStaticBitmap`

### ColorPicker.cpp

Configuration/color symbols: `COLORPICKER_RECENT_COLORS`, `COLORPICKER_SWITCH_CLICKS`, `STATICLIST_BORDER`, `STYLE_PREVIEW_COLOR1`, `STYLE_PREVIEW_COLOR2`, `WINDOW_BACKGROUND`, `WINDOW_TEXT`.

- [L548](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ColorPicker.cpp#L548) `colorType = new HikariChoice(this, 9766, wxDefaultPosition, wxSize(-1, -1), 4, types);`
- [L563](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ColorPicker.cpp#L563) `HikariCheckBox *SwitchClicks = new HikariCheckBox(this, 9456, _("Swap shortcuts between the color picker\nand the color selection window"));`
- [L565](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ColorPicker.cpp#L565) `SwitchClicks->SetToolTip(_("Checking this option opens the color picker on left-click,\nand the color selection window on right-click."));`
- [L644](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ColorPicker.cpp#L644) `button_sizer->Add(new MappedButton(this, wxID_OK, L"OK"), 1, wxALL, 4);`
- [L645](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ColorPicker.cpp#L645) `button_sizer->Add(new MappedButton(this, wxID_CANCEL, _("Cancel")), 1, wxALL, 4);`
- [L1380](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ColorPicker.cpp#L1380) `colorType = new HikariChoice(this, 9764, wxDefaultPosition, wxDefaultSize, 4, types);`
- [L1407](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ColorPicker.cpp#L1407) `moveWindowToMousePosition = new HikariCheckBox(this, -1, _("Move the window\nto the color selection location"));`
- [L1410](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ColorPicker.cpp#L1410) `MappedButton *OK = new MappedButton(this, wxID_OK, L"OK");`
- [L1411](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ColorPicker.cpp#L1411) `MappedButton *cancel = new MappedButton(this, wxID_CANCEL, _("Cancel"));`
- [L1517](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ColorPicker.cpp#L1517) `scpd = new SimpleColorPickerDialog(parent, actualColor, colorType);`

### ColorPicker.h

- [L44](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ColorPicker.h#L44) `class ColorPickerSpectrum : public wxWindow {`
- [L70](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ColorPicker.h#L70) `class ColorPickerRecent : public wxWindow {`
- [L103](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ColorPicker.h#L103) `class ColorPickerScreenDropper : public wxWindow {`
- [L137](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ColorPicker.h#L137) `class  ColorEvent : public wxEvent`
- [L166](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ColorPicker.h#L166) `class DialogColorPicker : public HikariDialog {`
- [L247](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ColorPicker.h#L247) `class ButtonColorPicker : public MappedButton`
- [L276](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ColorPicker.h#L276) `class SimpleColorPickerDialog : public HikariDialog`

### config.cpp

Configuration/color symbols: `AUDIO_AUTO_COMMIT`, `AUDIO_AUTO_FOCUS`, `AUDIO_AUTO_SCROLL`, `AUDIO_BACKGROUND`, `AUDIO_BOX_HEIGHT`, `AUDIO_CACHE_FILES_LIMIT`, `AUDIO_DELAY`, `AUDIO_DRAW_KEYFRAMES`, `AUDIO_DRAW_SECONDARY_LINES`, `AUDIO_DRAW_SELECTION_BACKGROUND`, `AUDIO_DRAW_TIME_CURSOR`, `AUDIO_DRAW_VIDEO_POSITION`, `AUDIO_GRAB_TIMES_ON_SELECT`, `AUDIO_HORIZONTAL_ZOOM`, `AUDIO_INACTIVE_LINES_BACKGROUND`, `AUDIO_INACTIVE_LINES_DISPLAY_MODE`, `AUDIO_KARAOKE`, `AUDIO_KARAOKE_SPLIT_MODE`, `AUDIO_KEYFRAMES`, `AUDIO_LEAD_IN_VALUE`, `AUDIO_LEAD_OUT_VALUE`, `AUDIO_LINE_BOUNDARIES_THICKNESS`, `AUDIO_LINE_BOUNDARY_END`, `AUDIO_LINE_BOUNDARY_INACTIVE_LINE`, `AUDIO_LINE_BOUNDARY_MARK`, `AUDIO_LINE_BOUNDARY_START`, `AUDIO_LINK`, `AUDIO_LOCK_SCROLL_ON_CURSOR`, `AUDIO_MARK_PLAY_TIME`, `AUDIO_NEXT_LINE_ON_COMMIT`, `AUDIO_PLAY_CURSOR`, `AUDIO_RAM_CACHE`, `AUDIO_SECONDS_BOUNDARIES`, `AUDIO_SELECTION_BACKGROUND`, `AUDIO_SELECTION_BACKGROUND_MODIFIED`, `AUDIO_SNAP_TO_KEYFRAMES`, `AUDIO_SNAP_TO_OTHER_LINES`, `AUDIO_SPECTRUM_BACKGROUND`, `AUDIO_SPECTRUM_ECHO`, `AUDIO_SPECTRUM_INNER`, `AUDIO_SPECTRUM_ON`, `AUDIO_START_DRAG_SENSITIVITY`, `AUDIO_SYLLABLE_BOUNDARIES`, `AUDIO_SYLLABLE_TEXT`, `AUDIO_VERTICAL_ZOOM`, `AUDIO_VOLUME`, `AUDIO_WAVEFORM`, `AUDIO_WAVEFORM_INACTIVE`, `AUDIO_WAVEFORM_MODIFIED`, `AUDIO_WAVEFORM_SELECTED`, `AUDIO_WHEEL_DEFAULT_TO_ZOOM`, `AUTOMATION_TRACE_LEVEL`, `AUTOSAVE_MAX_FILES`, `BUTTON_BACKGROUND`, `BUTTON_BACKGROUND_HOVER`, `BUTTON_BACKGROUND_ON_FOCUS`, `BUTTON_BACKGROUND_PUSHED`, `BUTTON_BORDER`, `BUTTON_BORDER_HOVER`, `BUTTON_BORDER_INACTIVE`, `BUTTON_BORDER_ON_FOCUS`, `BUTTON_BORDER_PUSHED`, `CONVERT_FPS`, `CONVERT_NEW_END_TIMES`, `CONVERT_RESOLUTION_HEIGHT`, `CONVERT_RESOLUTION_WIDTH`, `CONVERT_SHOW_SETTINGS`, `CONVERT_STYLE`, `CONVERT_STYLE_CATALOG`, `CONVERT_TIME_PER_CHARACTER`, `DICTIONARY_LANGUAGE`, `EDITOR_BACKGROUND`, `EDITOR_BORDER`, `EDITOR_BORDER_ON_FOCUS`, `EDITOR_BRACES_BACKGROUND`, `EDITOR_CURLY_BRACES`, `EDITOR_ON`, `EDITOR_PHRASE_SEARCH`, `EDITOR_SELECTION`, `EDITOR_SELECTION_NO_FOCUS`, `EDITOR_SPELLCHECKER`, `EDITOR_SPLIT_LINES_AND_DRAWINGS`, `EDITOR_TAG_NAMES`, `EDITOR_TAG_OPERATORS`, `EDITOR_TAG_VALUES`, `EDITOR_TEMPLATE_CODE_MARKS`, `EDITOR_TEMPLATE_FUNCTIONS`, `EDITOR_TEMPLATE_KEYWORDS`, `EDITOR_TEMPLATE_STRINGS`, `EDITOR_TEMPLATE_VARIABLES`, `EDITOR_TEXT`, `FFMS2_VIDEO_SEEKING`, `FIND_RESULT_FILENAME_BACKGROUND`, `FIND_RESULT_FILENAME_FOREGROUND`, `FIND_RESULT_FOUND_PHRASE_BACKGROUND`, `FIND_RESULT_FOUND_PHRASE_FOREGROUND`, `GRID_ACTIVE_LINE`, `GRID_BACKGROUND`, `GRID_CHANGE_ACTIVE_ON_SELECTION`, `GRID_COLLISIONS`, `GRID_COMMENT`, `GRID_COMPARISON_BACKGROUND_MATCH`, `GRID_COMPARISON_BACKGROUND_NOT_MATCH`, `GRID_COMPARISON_COMMENT_BACKGROUND_MATCH`, `GRID_COMPARISON_COMMENT_BACKGROUND_NOT_MATCH`, `GRID_COMPARISON_OUTLINE`, `GRID_DIALOGUE`, `GRID_FONT`, `GRID_FONT_SIZE`, `GRID_HEADER`, `GRID_HEADER_TEXT`, `GRID_INSERT_END_OFFSET`, `GRID_INSERT_START_OFFSET`, `GRID_LABEL_DOUBTFUL`, `GRID_LABEL_MODIFIED`, `GRID_LABEL_NORMAL`, `GRID_LABEL_SAVED`, `GRID_LINE_VISIBLE_ON_VIDEO`, `GRID_LINES`, `GRID_SAVE_AFTER_CHARACTER_COUNT`, `GRID_SELECTION`, `GRID_SPELLCHECKER`, `GRID_TAGS_SWAP_CHARACTER`, `GRID_TEXT`, `MENU_BACKGROUND_SELECTION`, `MENU_BORDER_SELECTION`, `MENUBAR_BACKGROUND`, `MENUBAR_BACKGROUND_HOVER`, `MENUBAR_BACKGROUND_SELECTION`, `MENUBAR_BACKGROUND1`, `MENUBAR_BACKGROUND2`, `MENUBAR_BORDER_SELECTION`, `PROGRAM_FONT`, `PROGRAM_FONT_SIZE`, `PROGRAM_THEME`, `SCROLLBAR_BACKGROUND`, `SCROLLBAR_THUMB`, `SCROLLBAR_THUMB_HOVER`, `SCROLLBAR_THUMB_PUSHED`, `SHIFT_TIMES_BY_TIME`, `SHIFT_TIMES_MOVE_FORWARD`, `SHIFT_TIMES_ON`, `SHIFT_TIMES_STYLES`, `SHIFT_TIMES_TIME`, `SHIFT_TIMES_WHICH_LINES`, `SHIFT_TIMES_WHICH_TIMES`, `SLIDER_BACKGROUND`, `SLIDER_BACKGROUND_HOVER`, `SLIDER_BACKGROUND_PUSHED`, `SLIDER_BORDER`, `SLIDER_BORDER_HOVER`, `SLIDER_BORDER_PUSHED`, `SLIDER_PATH_BACKGROUND`, `SLIDER_PATH_BORDER`, `SPELLCHECKER_ON`, `STATICBOX_BORDER`, `STATICLIST_BACKGROUND`, `STATICLIST_BACKGROUND_HEADLINE`, `STATICLIST_BORDER`, `STATICLIST_SELECTION`, `STATICLIST_TEXT_HEADLINE`, `STATUSBAR_BORDER`, `STYLE_EDIT_FILTER_TEXT`, `STYLE_PREVIEW_COLOR1`, `STYLE_PREVIEW_COLOR2`, `STYLE_PREVIEW_TEXT`, `TABS_BACKGROUND_ACTIVE`, `TABS_BACKGROUND_INACTIVE`, `TABS_BACKGROUND_INACTIVE_HOVER`, `TABS_BACKGROUND_SECOND_WINDOW`, `TABS_BORDER_ACTIVE`, `TABS_BORDER_INACTIVE`, `TABS_CLOSE_HOVER`, `TABS_TEXT_ACTIVE`, `TABS_TEXT_INACTIVE`, `TABSBAR_ARROW`, `TABSBAR_ARROW_BACKGROUND`, `TABSBAR_ARROW_BACKGROUND_HOVER`, `TABSBAR_BACKGROUND1`, `TABSBAR_BACKGROUND2`, `TEXT_FIELD_BACKGROUND`, `TEXT_FIELD_BORDER`, `TEXT_FIELD_BORDER_ON_FOCUS`, `TEXT_FIELD_SELECTION`, `TEXT_FIELD_SELECTION_NO_FOCUS`, `TOGGLE_BUTTON_BACKGROUND_TOGGLED`, `TOGGLE_BUTTON_BORDER_TOGGLED`, `UPDATER_CHECK_FOR_STABLE`, `VIDEO_GPU_CONVERSION`, `VIDEO_INDEX`, `VIDEO_PLAY_AFTER_SELECTION`, `VIDEO_PROGRESS_BAR`, `VIDEO_WINDOW_SIZE`, `WINDOW_BACKGROUND`, `WINDOW_BACKGROUND_INACTIVE`, `WINDOW_BORDER`, `WINDOW_BORDER_BACKGROUND`, `WINDOW_BORDER_BACKGROUND_INACTIVE`, `WINDOW_BORDER_INACTIVE`, `WINDOW_HEADER_TEXT`, `WINDOW_HEADER_TEXT_INACTIVE`, `WINDOW_HOVER_CLOSE_BUTTON`, `WINDOW_HOVER_HEADER_ELEMENT`, `WINDOW_PUSHED_CLOSE_BUTTON`, `WINDOW_PUSHED_HEADER_ELEMENT`, `WINDOW_RESIZER_DOTS`, `WINDOW_SIZE`, `WINDOW_TEXT`, `WINDOW_TEXT_INACTIVE`, `WINDOW_WARNING_ELEMENTS`.


### config.h

Configuration/color symbols: `ACCEPTED_AUDIO_STREAM`, `ASS_PROPERTIES_ASK_FOR_CHANGE`, `ASS_PROPERTIES_EDITING`, `ASS_PROPERTIES_EDITING_ON`, `ASS_PROPERTIES_SCRIPT`, `ASS_PROPERTIES_SCRIPT_ON`, `ASS_PROPERTIES_TIMING`, `ASS_PROPERTIES_TIMING_ON`, `ASS_PROPERTIES_TITLE`, `ASS_PROPERTIES_TITLE_ON`, `ASS_PROPERTIES_TRANSLATION`, `ASS_PROPERTIES_TRANSLATION_ON`, `ASS_PROPERTIES_UPDATE`, `ASS_PROPERTIES_UPDATE_ON`, `AUDIO_AUTO_COMMIT`, `AUDIO_AUTO_FOCUS`, `AUDIO_AUTO_SCROLL`, `AUDIO_BACKGROUND`, `AUDIO_BOX_HEIGHT`, `AUDIO_CACHE_FILES_LIMIT`, `AUDIO_DELAY`, `AUDIO_DONT_PLAY_WHEN_LINE_CHANGES`, `AUDIO_DRAW_KEYFRAMES`, `AUDIO_DRAW_SECONDARY_LINES`, `AUDIO_DRAW_SELECTION_BACKGROUND`, `AUDIO_DRAW_TIME_CURSOR`, `AUDIO_DRAW_VIDEO_POSITION`, `AUDIO_GRAB_TIMES_ON_SELECT`, `AUDIO_HORIZONTAL_ZOOM`, `AUDIO_INACTIVE_LINES_BACKGROUND`, `AUDIO_INACTIVE_LINES_DISPLAY_MODE`, `AUDIO_KARAOKE`, `AUDIO_KARAOKE_MOVE_ON_CLICK`, `AUDIO_KARAOKE_SPLIT_MODE`, `AUDIO_KEYFRAMES`, `AUDIO_LEAD_IN_VALUE`, `AUDIO_LEAD_OUT_VALUE`, `AUDIO_LINE_BOUNDARIES_THICKNESS`, `AUDIO_LINE_BOUNDARY_END`, `AUDIO_LINE_BOUNDARY_INACTIVE_LINE`, `AUDIO_LINE_BOUNDARY_MARK`, `AUDIO_LINE_BOUNDARY_START`, `AUDIO_LINK`, `AUDIO_LOCK_SCROLL_ON_CURSOR`, `AUDIO_MARK_PLAY_TIME`, `AUDIO_MERGE_EVERY_N_WITH_SYLLABLE`, `AUDIO_NEXT_LINE_ON_COMMIT`, `AUDIO_PLAY_CURSOR`, `AUDIO_RAM_CACHE`, `AUDIO_RECENT_FILES`, `AUDIO_SECONDS_BOUNDARIES`, `AUDIO_SELECTION_BACKGROUND`, `AUDIO_SELECTION_BACKGROUND_MODIFIED`, `AUDIO_SNAP_TO_KEYFRAMES`, `AUDIO_SNAP_TO_OTHER_LINES`, `AUDIO_SPECTRUM_BACKGROUND`, `AUDIO_SPECTRUM_ECHO`, `AUDIO_SPECTRUM_INNER`, `AUDIO_SPECTRUM_NON_LINEAR_ON`, `AUDIO_SPECTRUM_ON`, `AUDIO_START_DRAG_SENSITIVITY`, `AUDIO_SYLLABLE_BOUNDARIES`, `AUDIO_SYLLABLE_TEXT`, `AUDIO_VERTICAL_ZOOM`, `AUDIO_VOLUME`, `AUDIO_WAVEFORM`, `AUDIO_WAVEFORM_INACTIVE`, `AUDIO_WAVEFORM_MODIFIED`, `AUDIO_WAVEFORM_SELECTED`, `AUDIO_WHEEL_DEFAULT_TO_ZOOM`, `AUTO_MOVE_TAGS_FROM_ORIGINAL`, `AUTO_SELECT_LINES_FROM_LAST_TAB`, `AUTOMATION_LOADING_METHOD`, `AUTOMATION_OLD_SCRIPTS_COMPATIBILITY`, `AUTOMATION_RECENT_FILES`, `AUTOMATION_SCRIPT_EDITOR`, `AUTOMATION_TRACE_LEVEL`, `AUTOSAVE_MAX_FILES`, `BUTTON_BACKGROUND`, `BUTTON_BACKGROUND_HOVER`, `BUTTON_BACKGROUND_ON_FOCUS`, `BUTTON_BACKGROUND_PUSHED`, `BUTTON_BORDER`, `BUTTON_BORDER_HOVER`, `BUTTON_BORDER_INACTIVE`, `BUTTON_BORDER_ON_FOCUS`, `BUTTON_BORDER_PUSHED`, `CALC_SPACES_AND_PUNCTATION_FOR_CPS`, `CALC_SPACES_AND_PUNCTATION_FOR_WRAPS`, `COLORPICKER_RECENT_COLORS`, `COLORPICKER_SWITCH_CLICKS`, `CONVERT_ASS_TAGS_TO_INSERT_IN_LINE`, `CONVERT_FPS`, `CONVERT_FPS_FROM_VIDEO`, `CONVERT_NEW_END_TIMES`, `CONVERT_RESOLUTION_HEIGHT`, `CONVERT_RESOLUTION_WIDTH`, `CONVERT_SHOW_SETTINGS`, `CONVERT_STYLE`, `CONVERT_STYLE_CATALOG`, `CONVERT_TIME_PER_CHARACTER`, `COPY_COLLUMS_SELECTIONS`, `DICTIONARY_LANGUAGE`, `DISABLE_LIVE_VIDEO_EDITING`, `DONT_ASK_FOR_BAD_RESOLUTION`, `DONT_SHOW_CRASH_INFO`, `EDITBOX_DONT_GO_TO_NEXT_LINE_ON_TIMES_EDIT`, `EDITBOX_SUGGESTIONS_ON_DOUBLE_CLICK`, `EDITBOX_TAG_BUTTON_VALUE1`, `EDITBOX_TAG_BUTTON_VALUE10`, `EDITBOX_TAG_BUTTON_VALUE11`, `EDITBOX_TAG_BUTTON_VALUE12`, `EDITBOX_TAG_BUTTON_VALUE13`, `EDITBOX_TAG_BUTTON_VALUE14`, `EDITBOX_TAG_BUTTON_VALUE15`, `EDITBOX_TAG_BUTTON_VALUE16`, `EDITBOX_TAG_BUTTON_VALUE17`, `EDITBOX_TAG_BUTTON_VALUE18`, `EDITBOX_TAG_BUTTON_VALUE19`, `EDITBOX_TAG_BUTTON_VALUE2`, `EDITBOX_TAG_BUTTON_VALUE20`, `EDITBOX_TAG_BUTTON_VALUE3`, `EDITBOX_TAG_BUTTON_VALUE4`, `EDITBOX_TAG_BUTTON_VALUE5`, `EDITBOX_TAG_BUTTON_VALUE6`, `EDITBOX_TAG_BUTTON_VALUE7`, `EDITBOX_TAG_BUTTON_VALUE8`, `EDITBOX_TAG_BUTTON_VALUE9`, `EDITBOX_TAG_BUTTONS`, `EDITBOX_TIMES_TO_FRAMES_SWITCH`, `EDITOR_BACKGROUND`, `EDITOR_BORDER`, `EDITOR_BORDER_ON_FOCUS`, `EDITOR_BRACES_BACKGROUND`, `EDITOR_CURLY_BRACES`, `EDITOR_ON`, `EDITOR_PHRASE_SEARCH`, `EDITOR_SELECTION`, `EDITOR_SELECTION_NO_FOCUS`, `EDITOR_SPELLCHECKER`, `EDITOR_SPLIT_LINES_AND_DRAWINGS`, `EDITOR_TAG_NAMES`, `EDITOR_TAG_OPERATORS`, `EDITOR_TAG_VALUES`, `EDITOR_TEMPLATE_CODE_MARKS`, `EDITOR_TEMPLATE_FUNCTIONS`, `EDITOR_TEMPLATE_KEYWORDS`, `EDITOR_TEMPLATE_STRINGS`, `EDITOR_TEMPLATE_VARIABLES`, `EDITOR_TEXT`, `EXTERNAL_FONTS_DIRECTORY`, `FFMS2_VIDEO_SEEKING`, `FIND_IN_SUBS_FILTERS_RECENT`, `FIND_IN_SUBS_PATHS_RECENT`, `FIND_RECENT_FINDS`, `FIND_REPLACE_OPTIONS`, `FIND_REPLACE_STYLES`, `FIND_RESULT_FILENAME_BACKGROUND`, `FIND_RESULT_FILENAME_FOREGROUND`, `FIND_RESULT_FOUND_PHRASE_BACKGROUND`, `FIND_RESULT_FOUND_PHRASE_FOREGROUND`, `FONT_COLLECTOR_ACTION`, `FONT_COLLECTOR_DIRECTORY`, `FONT_COLLECTOR_FROM_MKV`, `FONT_COLLECTOR_USE_SUBS_DIRECTORY`, `GRID_ACTIVE_LINE`, `GRID_ADD_TO_FILTER`, `GRID_BACKGROUND`, `GRID_CHANGE_ACTIVE_ON_SELECTION`, `GRID_COLLISIONS`, `GRID_COMMENT`, `GRID_COMPARISON_BACKGROUND_MATCH`, `GRID_COMPARISON_BACKGROUND_NOT_MATCH`, `GRID_COMPARISON_COMMENT_BACKGROUND_MATCH`, `GRID_COMPARISON_COMMENT_BACKGROUND_NOT_MATCH`, `GRID_COMPARISON_OUTLINE`, `GRID_DIALOGUE`, `GRID_DONT_CENTER_ACTIVE_LINE`, `GRID_DUPLICATION_DONT_CHANGE_SELECTION`, `GRID_FILTER_AFTER_LOAD`, `GRID_FILTER_BY`, `GRID_FILTER_INVERTED`, `GRID_FILTER_STYLES`, `GRID_FONT`, `GRID_FONT_SIZE`, `GRID_HEADER`, `GRID_HEADER_TEXT`, `GRID_HIDE_COLUMNS`, `GRID_HIDE_TAGS`, `GRID_IGNORE_FILTERING`, `GRID_INSERT_END_OFFSET`, `GRID_INSERT_START_OFFSET`, `GRID_LABEL_DOUBTFUL`, `GRID_LABEL_MODIFIED`, `GRID_LABEL_NORMAL`, `GRID_LABEL_SAVED`, `GRID_LINE_VISIBLE_ON_VIDEO`, `GRID_LINES`, `GRID_LOAD_SORTED_SUBS`, `GRID_SAVE_AFTER_CHARACTER_COUNT`, `GRID_SELECTION`, `GRID_SET_VISIBLE_LINE_AFTER_FULL_SCREEN`, `GRID_SPELLCHECKER`, `GRID_TAGS_SWAP_CHARACTER`, `GRID_TEXT`, `KEYFRAMES_RECENT`, `LAST_SESSION_CONFIG`, `LINK_RESOLUTIONS`, `MENU_BACKGROUND_SELECTION`, `MENU_BORDER_SELECTION`, `MENUBAR_BACKGROUND`, `MENUBAR_BACKGROUND_HOVER`, `MENUBAR_BACKGROUND_SELECTION`, `MENUBAR_BACKGROUND1`, `MENUBAR_BACKGROUND2`, `MENUBAR_BORDER_SELECTION`, `MONITOR_POSITION`, `MONITOR_SIZE`, `MOVE_VIDEO_TO_ACTIVE_LINE`, `OPEN_SUBS_IN_NEW_TAB`, `OPEN_VIDEO_AT_ACTIVE_LINE`, `PASTE_COLUMNS_SELECTION`, `POSTPROCESSOR_KEYFRAME_AFTER_END`, `POSTPROCESSOR_KEYFRAME_AFTER_START`, `POSTPROCESSOR_KEYFRAME_BEFORE_END`, `POSTPROCESSOR_KEYFRAME_BEFORE_START`, `POSTPROCESSOR_LEAD_IN`, `POSTPROCESSOR_LEAD_OUT`, `POSTPROCESSOR_ON`, `POSTPROCESSOR_THRESHOLD_END`, `POSTPROCESSOR_THRESHOLD_START`, `PROGRAM_FONT`, `PROGRAM_FONT_SIZE`, `PROGRAM_LANGUAGE`, `PROGRAM_THEME`, `REPLACE_RECENT_REPLACEMENTS`, `SCROLLBAR_BACKGROUND`, `SCROLLBAR_THUMB`, `SCROLLBAR_THUMB_HOVER`, `SCROLLBAR_THUMB_PUSHED`, `SELECT_LINES_OPTIONS`, `SELECT_LINES_RECENT_SELECTIONS`, `SHIFT_TIMES_BY_TIME`, `SHIFT_TIMES_CHANGE_VALUES_WITH_TAB`, `SHIFT_TIMES_CORRECT_END_TIMES`, `SHIFT_TIMES_DISPLAY_FRAMES`, `SHIFT_TIMES_MOVE_FORWARD`, `SHIFT_TIMES_ON`, `SHIFT_TIMES_OPTIONS`, `SHIFT_TIMES_PROFILES`, `SHIFT_TIMES_STYLES`, `SHIFT_TIMES_TIME`, `SHIFT_TIMES_WHICH_LINES`, `SHIFT_TIMES_WHICH_TIMES`, `SLIDER_BACKGROUND`, `SLIDER_BACKGROUND_HOVER`, `SLIDER_BACKGROUND_PUSHED`, `SLIDER_BORDER`, `SLIDER_BORDER_HOVER`, `SLIDER_BORDER_PUSHED`, `SLIDER_PATH_BACKGROUND`, `SLIDER_PATH_BORDER`, `SPELLCHECKER_ON`, `STATICBOX_BORDER`, `STATICLIST_BACKGROUND`, `STATICLIST_BACKGROUND_HEADLINE`, `STATICLIST_BORDER`, `STATICLIST_SELECTION`, `STATICLIST_TEXT_HEADLINE`, `STATUSBAR_BORDER`, `STYLE_EDIT_FILTER_TEXT`, `STYLE_EDIT_FILTER_TEXT_ON`, `STYLE_MANAGER_DETACH_EDIT_WINDOW`, `STYLE_MANAGER_POSITION`, `STYLE_PREVIEW_COLOR1`, `STYLE_PREVIEW_COLOR2`, `STYLE_PREVIEW_TEXT`, `SUBS_AUTONAMING`, `SUBS_COMPARISON_STYLES`, `SUBS_COMPARISON_TYPE`, `SUBS_RECENT_FILES`, `TAB_TEXT_MAX_CHARS`, `TABS_BACKGROUND_ACTIVE`, `TABS_BACKGROUND_INACTIVE`, `TABS_BACKGROUND_INACTIVE_HOVER`, `TABS_BACKGROUND_SECOND_WINDOW`, `TABS_BORDER_ACTIVE`, `TABS_BORDER_INACTIVE`, `TABS_CLOSE_HOVER`, `TABS_TEXT_ACTIVE`, `TABS_TEXT_INACTIVE`, `TABSBAR_ARROW`, `TABSBAR_ARROW_BACKGROUND`, `TABSBAR_ARROW_BACKGROUND_HOVER`, `TABSBAR_BACKGROUND1`, `TABSBAR_BACKGROUND2`, `TEXT_EDITOR_CHANGE_QUOTES`, `TEXT_EDITOR_FONT_SIZE`, `TEXT_EDITOR_HIDE_STATUS_BAR`, `TEXT_EDITOR_TAG_LIST_OPTIONS`, `TEXT_FIELD_ALLOW_NUMPAD_HOTKEYS`, `TEXT_FIELD_BACKGROUND`, `TEXT_FIELD_BORDER`, `TEXT_FIELD_BORDER_ON_FOCUS`, `TEXT_FIELD_SELECTION`, `TEXT_FIELD_SELECTION_NO_FOCUS`, `TL_MODE_HIDE_ORIGINAL_ON_VIDEO`, `TL_MODE_SHOW_ORIGINAL`, `TOGGLE_BUTTON_BACKGROUND_TOGGLED`, `TOGGLE_BUTTON_BORDER_TOGGLED`, `TOOLBAR_ALIGNMENT`, `TOOLBAR_IDS`, `UPDATER_AUTO_CHECK`, `UPDATER_CHECK_FOR_STABLE`, `UPDATER_NEXT_CHECK`, `VIDEO_FULL_SCREEN_ON_START`, `VIDEO_GPU_CONVERSION`, `VIDEO_INDEX`, `VIDEO_PAUSE_ON_CLICK`, `VIDEO_PLAY_AFTER_SELECTION`, `VIDEO_PROGRESS_BAR`, `VIDEO_RECENT_FILES`, `VIDEO_VISUAL_WARNINGS_OFF`, `VIDEO_VOLUME`, `VIDEO_WINDOW_SIZE`, `VIDEO_ZOOM_PERCENT`, `VSFILTER_INSTANCE`, `WINDOW_BACKGROUND`, `WINDOW_BACKGROUND_INACTIVE`, `WINDOW_BORDER`, `WINDOW_BORDER_BACKGROUND`, `WINDOW_BORDER_BACKGROUND_INACTIVE`, `WINDOW_BORDER_INACTIVE`, `WINDOW_HEADER_TEXT`, `WINDOW_HEADER_TEXT_INACTIVE`, `WINDOW_HOVER_CLOSE_BUTTON`, `WINDOW_HOVER_HEADER_ELEMENT`, `WINDOW_MAXIMIZED`, `WINDOW_POSITION`, `WINDOW_PUSHED_CLOSE_BUTTON`, `WINDOW_PUSHED_HEADER_ELEMENT`, `WINDOW_RESIZER_DOTS`, `WINDOW_SIZE`, `WINDOW_TEXT`, `WINDOW_TEXT_INACTIVE`, `WINDOW_WARNING_ELEMENTS`.


### ConfigConverter.cpp

Configuration/color symbols: `ACCEPTED_AUDIO_STREAM`, `ASS_PROPERTIES_ASK_FOR_CHANGE`, `ASS_PROPERTIES_EDITING`, `ASS_PROPERTIES_EDITING_ON`, `ASS_PROPERTIES_SCRIPT`, `ASS_PROPERTIES_SCRIPT_ON`, `ASS_PROPERTIES_TIMING`, `ASS_PROPERTIES_TIMING_ON`, `ASS_PROPERTIES_TITLE`, `ASS_PROPERTIES_TITLE_ON`, `ASS_PROPERTIES_TRANSLATION`, `ASS_PROPERTIES_TRANSLATION_ON`, `ASS_PROPERTIES_UPDATE`, `ASS_PROPERTIES_UPDATE_ON`, `AUDIO_AUTO_COMMIT`, `AUDIO_AUTO_FOCUS`, `AUDIO_AUTO_SCROLL`, `AUDIO_BACKGROUND`, `AUDIO_BOX_HEIGHT`, `AUDIO_DELAY`, `AUDIO_DRAW_KEYFRAMES`, `AUDIO_DRAW_SECONDARY_LINES`, `AUDIO_DRAW_SELECTION_BACKGROUND`, `AUDIO_DRAW_TIME_CURSOR`, `AUDIO_DRAW_VIDEO_POSITION`, `AUDIO_GRAB_TIMES_ON_SELECT`, `AUDIO_HORIZONTAL_ZOOM`, `AUDIO_INACTIVE_LINES_BACKGROUND`, `AUDIO_INACTIVE_LINES_DISPLAY_MODE`, `AUDIO_KARAOKE`, `AUDIO_KARAOKE_MOVE_ON_CLICK`, `AUDIO_KARAOKE_SPLIT_MODE`, `AUDIO_KEYFRAMES`, `AUDIO_LEAD_IN_VALUE`, `AUDIO_LEAD_OUT_VALUE`, `AUDIO_LINE_BOUNDARIES_THICKNESS`, `AUDIO_LINE_BOUNDARY_END`, `AUDIO_LINE_BOUNDARY_INACTIVE_LINE`, `AUDIO_LINE_BOUNDARY_MARK`, `AUDIO_LINE_BOUNDARY_START`, `AUDIO_LINK`, `AUDIO_LOCK_SCROLL_ON_CURSOR`, `AUDIO_MARK_PLAY_TIME`, `AUDIO_MERGE_EVERY_N_WITH_SYLLABLE`, `AUDIO_NEXT_LINE_ON_COMMIT`, `AUDIO_PLAY_CURSOR`, `AUDIO_RAM_CACHE`, `AUDIO_RECENT_FILES`, `AUDIO_SECONDS_BOUNDARIES`, `AUDIO_SELECTION_BACKGROUND`, `AUDIO_SELECTION_BACKGROUND_MODIFIED`, `AUDIO_SNAP_TO_KEYFRAMES`, `AUDIO_SNAP_TO_OTHER_LINES`, `AUDIO_SPECTRUM_BACKGROUND`, `AUDIO_SPECTRUM_ECHO`, `AUDIO_SPECTRUM_INNER`, `AUDIO_SPECTRUM_NON_LINEAR_ON`, `AUDIO_SPECTRUM_ON`, `AUDIO_START_DRAG_SENSITIVITY`, `AUDIO_SYLLABLE_BOUNDARIES`, `AUDIO_SYLLABLE_TEXT`, `AUDIO_VERTICAL_ZOOM`, `AUDIO_VOLUME`, `AUDIO_WAVEFORM`, `AUDIO_WAVEFORM_INACTIVE`, `AUDIO_WAVEFORM_MODIFIED`, `AUDIO_WAVEFORM_SELECTED`, `AUDIO_WHEEL_DEFAULT_TO_ZOOM`, `AUTO_MOVE_TAGS_FROM_ORIGINAL`, `AUTO_SELECT_LINES_FROM_LAST_TAB`, `AUTOMATION_LOADING_METHOD`, `AUTOMATION_OLD_SCRIPTS_COMPATIBILITY`, `AUTOMATION_RECENT_FILES`, `AUTOMATION_SCRIPT_EDITOR`, `AUTOMATION_TRACE_LEVEL`, `AUTOSAVE_MAX_FILES`, `BUTTON_BACKGROUND`, `BUTTON_BACKGROUND_HOVER`, `BUTTON_BACKGROUND_ON_FOCUS`, `BUTTON_BACKGROUND_PUSHED`, `BUTTON_BORDER`, `BUTTON_BORDER_HOVER`, `BUTTON_BORDER_INACTIVE`, `BUTTON_BORDER_ON_FOCUS`, `BUTTON_BORDER_PUSHED`, `COLORPICKER_RECENT_COLORS`, `CONVERT_ASS_TAGS_TO_INSERT_IN_LINE`, `CONVERT_FPS`, `CONVERT_FPS_FROM_VIDEO`, `CONVERT_NEW_END_TIMES`, `CONVERT_RESOLUTION_HEIGHT`, `CONVERT_RESOLUTION_WIDTH`, `CONVERT_SHOW_SETTINGS`, `CONVERT_STYLE`, `CONVERT_STYLE_CATALOG`, `CONVERT_TIME_PER_CHARACTER`, `COPY_COLLUMS_SELECTIONS`, `DICTIONARY_LANGUAGE`, `DISABLE_LIVE_VIDEO_EDITING`, `DONT_ASK_FOR_BAD_RESOLUTION`, `EDITBOX_DONT_GO_TO_NEXT_LINE_ON_TIMES_EDIT`, `EDITBOX_SUGGESTIONS_ON_DOUBLE_CLICK`, `EDITBOX_TAG_BUTTON_VALUE1`, `EDITBOX_TAG_BUTTON_VALUE10`, `EDITBOX_TAG_BUTTON_VALUE11`, `EDITBOX_TAG_BUTTON_VALUE12`, `EDITBOX_TAG_BUTTON_VALUE13`, `EDITBOX_TAG_BUTTON_VALUE14`, `EDITBOX_TAG_BUTTON_VALUE15`, `EDITBOX_TAG_BUTTON_VALUE16`, `EDITBOX_TAG_BUTTON_VALUE17`, `EDITBOX_TAG_BUTTON_VALUE18`, `EDITBOX_TAG_BUTTON_VALUE19`, `EDITBOX_TAG_BUTTON_VALUE2`, `EDITBOX_TAG_BUTTON_VALUE20`, `EDITBOX_TAG_BUTTON_VALUE3`, `EDITBOX_TAG_BUTTON_VALUE4`, `EDITBOX_TAG_BUTTON_VALUE5`, `EDITBOX_TAG_BUTTON_VALUE6`, `EDITBOX_TAG_BUTTON_VALUE7`, `EDITBOX_TAG_BUTTON_VALUE8`, `EDITBOX_TAG_BUTTON_VALUE9`, `EDITBOX_TAG_BUTTONS`, `EDITOR_BACKGROUND`, `EDITOR_BORDER`, `EDITOR_BORDER_ON_FOCUS`, `EDITOR_BRACES_BACKGROUND`, `EDITOR_CURLY_BRACES`, `EDITOR_ON`, `EDITOR_PHRASE_SEARCH`, `EDITOR_SELECTION`, `EDITOR_SELECTION_NO_FOCUS`, `EDITOR_SPELLCHECKER`, `EDITOR_TAG_NAMES`, `EDITOR_TAG_OPERATORS`, `EDITOR_TAG_VALUES`, `EDITOR_TEMPLATE_CODE_MARKS`, `EDITOR_TEMPLATE_FUNCTIONS`, `EDITOR_TEMPLATE_KEYWORDS`, `EDITOR_TEMPLATE_STRINGS`, `EDITOR_TEMPLATE_VARIABLES`, `EDITOR_TEXT`, `FFMS2_VIDEO_SEEKING`, `FIND_IN_SUBS_FILTERS_RECENT`, `FIND_IN_SUBS_PATHS_RECENT`, `FIND_RECENT_FINDS`, `FIND_REPLACE_OPTIONS`, `FONT_COLLECTOR_ACTION`, `FONT_COLLECTOR_DIRECTORY`, `FONT_COLLECTOR_FROM_MKV`, `FONT_COLLECTOR_USE_SUBS_DIRECTORY`, `GRID_ACTIVE_LINE`, `GRID_ADD_TO_FILTER`, `GRID_BACKGROUND`, `GRID_CHANGE_ACTIVE_ON_SELECTION`, `GRID_COLLISIONS`, `GRID_COMMENT`, `GRID_COMPARISON_BACKGROUND_MATCH`, `GRID_COMPARISON_BACKGROUND_NOT_MATCH`, `GRID_COMPARISON_COMMENT_BACKGROUND_MATCH`, `GRID_COMPARISON_COMMENT_BACKGROUND_NOT_MATCH`, `GRID_COMPARISON_OUTLINE`, `GRID_DIALOGUE`, `GRID_FILTER_AFTER_LOAD`, `GRID_FILTER_BY`, `GRID_FILTER_INVERTED`, `GRID_FILTER_STYLES`, `GRID_FONT`, `GRID_FONT_SIZE`, `GRID_HEADER`, `GRID_HEADER_TEXT`, `GRID_HIDE_COLUMNS`, `GRID_HIDE_TAGS`, `GRID_IGNORE_FILTERING`, `GRID_INSERT_END_OFFSET`, `GRID_INSERT_START_OFFSET`, `GRID_LABEL_DOUBTFUL`, `GRID_LABEL_MODIFIED`, `GRID_LABEL_NORMAL`, `GRID_LABEL_SAVED`, `GRID_LINE_VISIBLE_ON_VIDEO`, `GRID_LINES`, `GRID_LOAD_SORTED_SUBS`, `GRID_SAVE_AFTER_CHARACTER_COUNT`, `GRID_SELECTION`, `GRID_SET_VISIBLE_LINE_AFTER_FULL_SCREEN`, `GRID_SPELLCHECKER`, `GRID_TAGS_SWAP_CHARACTER`, `GRID_TEXT`, `KEYFRAMES_RECENT`, `MENU_BACKGROUND_SELECTION`, `MENU_BORDER_SELECTION`, `MENUBAR_BACKGROUND`, `MENUBAR_BACKGROUND_HOVER`, `MENUBAR_BACKGROUND_SELECTION`, `MENUBAR_BACKGROUND1`, `MENUBAR_BACKGROUND2`, `MENUBAR_BORDER_SELECTION`, `MOVE_VIDEO_TO_ACTIVE_LINE`, `OPEN_SUBS_IN_NEW_TAB`, `OPEN_VIDEO_AT_ACTIVE_LINE`, `PASTE_COLUMNS_SELECTION`, `POSTPROCESSOR_KEYFRAME_AFTER_END`, `POSTPROCESSOR_KEYFRAME_AFTER_START`, `POSTPROCESSOR_KEYFRAME_BEFORE_END`, `POSTPROCESSOR_KEYFRAME_BEFORE_START`, `POSTPROCESSOR_LEAD_IN`, `POSTPROCESSOR_LEAD_OUT`, `POSTPROCESSOR_ON`, `POSTPROCESSOR_THRESHOLD_END`, `POSTPROCESSOR_THRESHOLD_START`, `PROGRAM_LANGUAGE`, `PROGRAM_THEME`, `REPLACE_RECENT_REPLACEMENTS`, `SCROLLBAR_BACKGROUND`, `SCROLLBAR_THUMB`, `SCROLLBAR_THUMB_HOVER`, `SCROLLBAR_THUMB_PUSHED`, `SELECT_LINES_OPTIONS`, `SELECT_LINES_RECENT_SELECTIONS`, `SHIFT_TIMES_BY_TIME`, `SHIFT_TIMES_CHANGE_VALUES_WITH_TAB`, `SHIFT_TIMES_CORRECT_END_TIMES`, `SHIFT_TIMES_DISPLAY_FRAMES`, `SHIFT_TIMES_MOVE_FORWARD`, `SHIFT_TIMES_ON`, `SHIFT_TIMES_OPTIONS`, `SHIFT_TIMES_PROFILES`, `SHIFT_TIMES_STYLES`, `SHIFT_TIMES_TIME`, `SHIFT_TIMES_WHICH_LINES`, `SHIFT_TIMES_WHICH_TIMES`, `SLIDER_BACKGROUND`, `SLIDER_BACKGROUND_HOVER`, `SLIDER_BACKGROUND_PUSHED`, `SLIDER_BORDER`, `SLIDER_BORDER_HOVER`, `SLIDER_BORDER_PUSHED`, `SLIDER_PATH_BACKGROUND`, `SLIDER_PATH_BORDER`, `SPELLCHECKER_ON`, `STATICBOX_BORDER`, `STATICLIST_BACKGROUND`, `STATICLIST_BACKGROUND_HEADLINE`, `STATICLIST_BORDER`, `STATICLIST_SELECTION`, `STATICLIST_TEXT_HEADLINE`, `STATUSBAR_BORDER`, `STYLE_EDIT_FILTER_TEXT`, `STYLE_EDIT_FILTER_TEXT_ON`, `STYLE_MANAGER_DETACH_EDIT_WINDOW`, `STYLE_MANAGER_POSITION`, `STYLE_PREVIEW_COLOR1`, `STYLE_PREVIEW_COLOR2`, `STYLE_PREVIEW_TEXT`, `SUBS_AUTONAMING`, `SUBS_COMPARISON_STYLES`, `SUBS_COMPARISON_TYPE`, `SUBS_RECENT_FILES`, `TABS_BACKGROUND_ACTIVE`, `TABS_BACKGROUND_INACTIVE`, `TABS_BACKGROUND_INACTIVE_HOVER`, `TABS_BACKGROUND_SECOND_WINDOW`, `TABS_BORDER_ACTIVE`, `TABS_BORDER_INACTIVE`, `TABS_CLOSE_HOVER`, `TABS_TEXT_ACTIVE`, `TABS_TEXT_INACTIVE`, `TABSBAR_ARROW`, `TABSBAR_ARROW_BACKGROUND`, `TABSBAR_ARROW_BACKGROUND_HOVER`, `TABSBAR_BACKGROUND1`, `TABSBAR_BACKGROUND2`, `TEXT_FIELD_ALLOW_NUMPAD_HOTKEYS`, `TEXT_FIELD_BACKGROUND`, `TEXT_FIELD_BORDER`, `TEXT_FIELD_BORDER_ON_FOCUS`, `TEXT_FIELD_SELECTION`, `TEXT_FIELD_SELECTION_NO_FOCUS`, `TL_MODE_SHOW_ORIGINAL`, `TOGGLE_BUTTON_BACKGROUND_TOGGLED`, `TOGGLE_BUTTON_BORDER_TOGGLED`, `TOOLBAR_ALIGNMENT`, `TOOLBAR_IDS`, `UPDATER_CHECK_FOR_STABLE`, `VIDEO_FULL_SCREEN_ON_START`, `VIDEO_INDEX`, `VIDEO_PAUSE_ON_CLICK`, `VIDEO_PLAY_AFTER_SELECTION`, `VIDEO_PROGRESS_BAR`, `VIDEO_RECENT_FILES`, `VIDEO_VISUAL_WARNINGS_OFF`, `VIDEO_VOLUME`, `VIDEO_WINDOW_SIZE`, `WINDOW_BACKGROUND`, `WINDOW_BACKGROUND_INACTIVE`, `WINDOW_BORDER`, `WINDOW_BORDER_BACKGROUND`, `WINDOW_BORDER_BACKGROUND_INACTIVE`, `WINDOW_BORDER_INACTIVE`, `WINDOW_HEADER_TEXT`, `WINDOW_HEADER_TEXT_INACTIVE`, `WINDOW_HOVER_CLOSE_BUTTON`, `WINDOW_HOVER_HEADER_ELEMENT`, `WINDOW_MAXIMIZED`, `WINDOW_POSITION`, `WINDOW_PUSHED_CLOSE_BUTTON`, `WINDOW_PUSHED_HEADER_ELEMENT`, `WINDOW_SIZE`, `WINDOW_TEXT`, `WINDOW_TEXT_INACTIVE`, `WINDOW_WARNING_ELEMENTS`.


### DialogueTextEditor.cpp

Configuration/color symbols: `DICTIONARY_LANGUAGE`, `EDITBOX_SUGGESTIONS_ON_DOUBLE_CLICK`, `EDITOR_BACKGROUND`, `EDITOR_BORDER`, `EDITOR_BORDER_ON_FOCUS`, `EDITOR_BRACES_BACKGROUND`, `EDITOR_CURLY_BRACES`, `EDITOR_PHRASE_SEARCH`, `EDITOR_SELECTION`, `EDITOR_SELECTION_NO_FOCUS`, `EDITOR_SPELLCHECKER`, `EDITOR_SPLIT_LINES_AND_DRAWINGS`, `EDITOR_TAG_NAMES`, `EDITOR_TAG_OPERATORS`, `EDITOR_TAG_VALUES`, `EDITOR_TEMPLATE_CODE_MARKS`, `EDITOR_TEMPLATE_FUNCTIONS`, `EDITOR_TEMPLATE_KEYWORDS`, `EDITOR_TEMPLATE_STRINGS`, `EDITOR_TEMPLATE_VARIABLES`, `EDITOR_TEXT`, `SPELLCHECKER_ON`, `TEXT_EDITOR_CHANGE_QUOTES`, `TEXT_EDITOR_FONT_SIZE`, `TEXT_EDITOR_HIDE_STATUS_BAR`, `TEXT_FIELD_ALLOW_NUMPAD_HOTKEYS`.

- [L2300](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/DialogueTextEditor.cpp#L2300) `menut.Append(i + 30200, suggs[i]);`
- [L2307](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/DialogueTextEditor.cpp#L2307) `menut.Append(TEXTM_COPY, _("&Copy"))->Enable(Selend.x != Cursor.x);`
- [L2308](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/DialogueTextEditor.cpp#L2308) `menut.Append(TEXTM_CUT, _("Cu&t"))->Enable(Selend.x != Cursor.x);`
- [L2309](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/DialogueTextEditor.cpp#L2309) `menut.Append(TEXTM_PASTE, _("&Paste"));`
- [L2312](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/DialogueTextEditor.cpp#L2312) `menut.Append(TEXTM_SEEKWORDL, _("Search word translation on ling.pl"))->Enable(Selend.x != Cursor.x);`
- [L2313](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/DialogueTextEditor.cpp#L2313) `menut.Append(TEXTM_SEEKWORDB, _("Search word translation on pl.ba.bla"))->Enable(Selend.x != Cursor.x);`
- [L2314](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/DialogueTextEditor.cpp#L2314) `menut.Append(TEXTM_SEEKWORDG, _("Search for the selected phrase on Google"))->Enable(Selend.x != Cursor.x);`
- [L2315](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/DialogueTextEditor.cpp#L2315) `menut.Append(TEXTM_SEEKWORDS, _("Search for synonyms on synonimy.net"))->Enable(Selend.x != Cursor.x);`
- [L2325](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/DialogueTextEditor.cpp#L2325) `menut.Append(MENU_SPELLCHECKER_ON, _("Spellchecker"), emptyString, true, nullptr, nullptr, ITEM_CHECK_AND_HIDE)->Check(Options.GetBool(SPELLCHECKER_ON));`
- [L2328](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/DialogueTextEditor.cpp#L2328) `languageMenu->Append(MENU_SPELLCHECKER_ON + k + 1, dics[k], emptyString, true, nullptr, nullptr, (language == dics[k])? ITEM_RADIO : ITEM_NORMAL);`
- [L2331](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/DialogueTextEditor.cpp#L2331) `menut.Append(MENU_SPELLCHECKER_ON - 1, _("Installed languages"), languageMenu);`
- [L2335](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/DialogueTextEditor.cpp#L2335) `menut.Append(TEXTM_ADD, wxString::Format(_("&Add word \"%s\" to dictionary"), err));`
- [L2338](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/DialogueTextEditor.cpp#L2338) `menut.Append(TEXTM_DEL, _("&Delete"))->Enable(Selend.x != Cursor.x);`
- [L2339](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/DialogueTextEditor.cpp#L2339) `menut.Append(MENU_SHOW_STATUS_BAR, _("Show status bar"), nullptr, emptyString, ITEM_CHECK)->Check(!Options.GetBool(TEXT_EDITOR_HIDE_STATUS_BAR));`
- [L2341](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/DialogueTextEditor.cpp#L2341) `menut.Append(MENU_CHANGE_QUOTES, _("Automatically change quotes"), nullptr, emptyString, ITEM_CHECK)->Check(Options.GetBool(TEXT_EDITOR_CHANGE_QUOTES));`

### DialogueTextEditor.h

- [L35](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/DialogueTextEditor.h#L35) `class TextEditor : public wxWindow`

### DropFiles.h

- [L25](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/DropFiles.h#L25) `class DragnDrop : public wxFileDropTarget`

### DummyVideo.cpp

- [L39](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/DummyVideo.cpp#L39) `videoResolution = new HikariChoice(this, ID_VIDEO_RESOLUTION, wxDefaultPosition, wxDefaultSize, resolutions);`
- [L56](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/DummyVideo.cpp#L56) `pattern = new HikariCheckBox(this, -1, _("Checkerboard pattern"));`
- [L86](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/DummyVideo.cpp#L86) `frameRate = new HikariChoice(this, -1, L"23.976", wxDefaultPosition, wxDefaultSize, FPSes, 0, valid);`
- [L101](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/DummyVideo.cpp#L101) `MappedButton* OK = new MappedButton(this, wxID_OK, L"OK", -1, wxDefaultPosition, wxSize(100, -1));`
- [L102](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/DummyVideo.cpp#L102) `MappedButton* cancel = new MappedButton(this, wxID_CANCEL, _("Cancel"), -1, wxDefaultPosition, wxSize(100, -1));`

### DummyVideo.h

- [L26](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/DummyVideo.h#L26) `class DummyVideo : public HikariDialog`

### EditBox.cpp

Configuration/color symbols: `AUDIO_BOX_HEIGHT`, `AUTO_MOVE_TAGS_FROM_ORIGINAL`, `COLORPICKER_SWITCH_CLICKS`, `DISABLE_LIVE_VIDEO_EDITING`, `EDITBOX_DONT_GO_TO_NEXT_LINE_ON_TIMES_EDIT`, `EDITBOX_TAG_BUTTON_VALUE1`, `EDITBOX_TAG_BUTTONS`, `EDITBOX_TIMES_TO_FRAMES_SWITCH`, `GRID_SAVE_AFTER_CHARACTER_COUNT`, `WINDOW_BACKGROUND`, `WINDOW_TEXT`, `WINDOW_TEXT_INACTIVE`, `WINDOW_WARNING_ELEMENTS`.

- [L83](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L83) `type = new HikariChoice(this, -1, wxDefaultPosition, wxDefaultSize, 3, types);`
- [L94](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L94) `siz1->Add(new MappedButton(this, wxID_OK, _("Save tag")), 0, wxEXPAND | wxALL, 4);`
- [L95](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L95) `siz1->Add(new MappedButton(this, wxID_CANCEL, _("Cancel")), 0, wxEXPAND | wxALL, 4);`
- [L166](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L166) `Bfont = new MappedButton(this, EDITBOX_CHANGE_FONT, emptyString, _("Font selection"), wxDefaultPosition, wxDefaultSize, EDITBOX_HOTKEY, MAKE_SQUARE_BUTTON);`
- [L169](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L169) `Bbold = new MappedButton(this, EDITBOX_INSERT_BOLD, emptyString, _("Bold"), wxDefaultPosition, wxDefaultSize, EDITBOX_HOTKEY, MAKE_SQUARE_BUTTON);`
- [L172](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L172) `Bital = new MappedButton(this, EDITBOX_INSERT_ITALIC, emptyString, _("Italic"), wxDefaultPosition, wxDefaultSize, EDITBOX_HOTKEY, MAKE_SQUARE_BUTTON);`
- [L175](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L175) `Bund = new MappedButton(this, EDITBOX_CHANGE_UNDERLINE, emptyString, _("Underline"), wxDefaultPosition, wxDefaultSize, EDITBOX_HOTKEY, MAKE_SQUARE_BUTTON);`
- [L178](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L178) `Bstrike = new MappedButton(this, EDITBOX_CHANGE_STRIKEOUT, emptyString, _("Strikethrough"), wxDefaultPosition, wxDefaultSize, EDITBOX_HOTKEY, MAKE_SQUARE_BUTTON);`
- [L181](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L181) `Bcol1 = new MappedButton(this, EDITBOX_CHANGE_COLOR_PRIMARY, emptyString, _("Primary color"), wxDefaultPosition, wxDefaultSize, EDITBOX_HOTKEY, MAKE_SQUARE_BUTTON);`
- [L185](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L185) `Bcol2 = new MappedButton(this, EDITBOX_CHANGE_COLOR_SECONDARY, emptyString, _("Secondary color for karaoke"), wxDefaultPosition, wxDefaultSize, EDITBOX_HOTKEY, MAKE_SQUARE_BUTTON);`
- [L189](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L189) `Bcol3 = new MappedButton(this, EDITBOX_CHANGE_COLOR_OUTLINE, emptyString, _("Border color"), wxDefaultPosition, wxDefaultSize, EDITBOX_HOTKEY, MAKE_SQUARE_BUTTON);`
- [L193](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L193) `Bcol4 = new MappedButton(this, EDITBOX_CHANGE_COLOR_SHADOW, emptyString, _("Shadow color"), wxDefaultPosition, wxDefaultSize, EDITBOX_HOTKEY, MAKE_SQUARE_BUTTON);`
- [L198](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L198) `Ban = new HikariChoice(this, ID_AN, wxDefaultPosition, wxDefaultSize, 9, alignments);`
- [L215](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L215) `TlMode = new HikariCheckBox(this, ID_TLMODE, _("Translator mode"));`
- [L242](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L242) `Bcpall = new MappedButton(this, EDITBOX_PASTE_ALL_TO_TRANSLATION, _("Paste all"), EDITBOX_HOTKEY);`
- [L244](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L244) `Bcpsel = new MappedButton(this, EDITBOX_PASTE_SELECTION_TO_TRANSLATION, _("Paste the selected"), EDITBOX_HOTKEY);`
- [L246](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L246) `Bhide = new MappedButton(this, EDITBOX_HIDE_ORIGINAL, _("Hide original"), EDITBOX_HOTKEY);`
- [L248](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L248) `DoubtfulTL = new ToggleButton(this, ID_DOUBTFULTL, _("Not confirmed"));`
- [L250](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L250) `AutoMoveTags = new ToggleButton(this, ID_AUTOMOVETAGS, _("Moving tags"));`
- [L268](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L268) `Comment = new HikariCheckBox(this, ID_COMMENT, _("Comment")/*, wxDefaultPosition, wxSize(82, -1)*/);`
- [L279](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L279) `StyleChoice = new HikariChoice(this, ID_STYLE, wxDefaultPosition, wxSize(100, -1), styles);//wxSize(145,-1)`
- [L280](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L280) `StyleEdit = new MappedButton(this, ID_EDIT_STYLE, _("Edit"), EDITBOX_HOTKEY/*, wxDefaultPosition, wxSize(45, -1)*/);`
- [L1182](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1182) `StyleChoice->Append(grid->file->GetStyle(i)->Name);`
- [L1199](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1199) `Ban->SetToolTip(_("Text position"));`
- [L1200](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1200) `TlMode->SetToolTip(_("Translator mode displays and saves both foreign text and translation text"));`
- [L1201](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1201) `Bcpall->SetToolTip(_("Copies all foreign-language text to the translation field"));`
- [L1202](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1202) `Bcpsel->SetToolTip(_("Copies the selected foreign-language text to the translation field"));`
- [L1203](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1203) `Comment->SetToolTip(_("Sets the line as a comment. Comments are not shown"));`
- [L1204](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1204) `LayerEdit->SetToolTip(_("Line layer. Higher layers are on top"));`
- [L1205](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1205) `StartEdit->SetToolTip(_("Line start time"));`
- [L1206](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1206) `EndEdit->SetToolTip(_("Line end time"));`
- [L1207](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1207) `DurEdit->SetToolTip(_("Line duration"));`
- [L1208](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1208) `StyleChoice->SetToolTip(_("Line style"));`
- [L1209](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1209) `StyleEdit->SetToolTip(_("Allows quick editing of the current line's style"));`
- [L1210](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1210) `ActorEdit->SetToolTip(_("Line actor label. Does not affect the appearance of the subtitles"));`
- [L1211](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1211) `MarginLEdit->SetToolTip(_("Line left margin"));`
- [L1212](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1212) `MarginREdit->SetToolTip(_("Line right margin"));`
- [L1213](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1213) `MarginVEdit->SetToolTip(_("Line top and bottom margins"));`
- [L1214](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1214) `EffectEdit->SetToolTip(_("Line effect. Used to mark lines to which karaoke or VSFilter effects should be applied"));`
- [L1215](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1215) `Chars->SetToolTip(_("Number of characters in each line.\nNo more than 43 characters per line (maximum 2 lines)."));`
- [L1216](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1216) `Chtime->SetToolTip(_("Characters per second.\nShould not exceed 15 characters per second"));`
- [L1741](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1741) `class NumTagButtons : public HikariDialog`
- [L1750](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1750) `MappedButton *ok = new MappedButton(this, wxID_OK, L"OK");`
- [L1751](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1751) `MappedButton *cancel = new MappedButton(this, wxID_CANCEL, _("Cancel"));`
- [L1797](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1797) `if (tb->tag != emptyString){ tb->SetToolTip(tb->tag); }`
- [L2042](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L2042) `menu->Append(EDITBOX_TAG_BUTTON1 + i, name);`
- [L2044](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L2044) `menu->Append(ID_NUM_TAG_BUTTONS, _("Change number of buttons"));`
- [L2046](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L2046) `TagButtonManager = new MenuButton(this, -1, _("Manage tag buttons"), wxDefaultPosition, wxDefaultSize);`
- [L2098](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L2098) `ActorEdit->Append(dial->Actor);`
- [L2101](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L2101) `EffectEdit->Append(dial->Effect);`

### EditBox.h

- [L42](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.h#L42) `class ComboBoxCtrl : public HikariChoice`
- [L55](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.h#L55) `class TagButtonDialog :public HikariDialog`
- [L65](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.h#L65) `class TagButton :public MappedButton`
- [L80](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.h#L80) `class EditBox : public HikariPanel, TagFindReplace`

### findreplace.cpp

Configuration/color symbols: `FIND_IN_SUBS_FILTERS_RECENT`, `FIND_IN_SUBS_PATHS_RECENT`, `FIND_RECENT_FINDS`, `REPLACE_RECENT_REPLACEMENTS`.

- [L481](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/findreplace.cpp#L481) `FRRD = new FindReplaceResultsDialog(Hikari, this);`
- [L531](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/findreplace.cpp#L531) `FRRD = new FindReplaceResultsDialog(Hikari, this);`
- [L785](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/findreplace.cpp#L785) `FRRD = new FindReplaceResultsDialog(Hikari, this, true);`
- [L1318](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/findreplace.cpp#L1318) `KMD = new HikariMessageDialog(FRD, _("None of the selected styles exist in the subtitles being searched,\nso nothing will be found.\nWhat would you like to do?"), _("Confirmation"), wxYES | wxCANCEL);`
- [L1322](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/findreplace.cpp#L1322) `KMD = new HikariMessageDialog(FRD, wxString::Format(_("Styles named \"%s\" do not exist in the subtitles being searched,\nwhich may significantly reduce the number of search results.\nWhat would you like to do?"), notFoundStyles), _("Confirmation"), wxOK | wxYES_NO | wxCANCEL);`

### FindReplaceDialog.cpp

Configuration/color symbols: `FIND_REPLACE_OPTIONS`, `FIND_REPLACE_STYLES`, `WINDOW_BACKGROUND`, `WINDOW_TEXT`.

- [L44](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FindReplaceDialog.cpp#L44) `FindText = new HikariChoice(this, ID_FIND_TEXT, emptyString, wxDefaultPosition, wxSize(276, -1), FR->findRecent);`
- [L45](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FindReplaceDialog.cpp#L45) `FindText->SetToolTip(_("Search text:"));`
- [L56](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FindReplaceDialog.cpp#L56) `ReplaceText = new HikariChoice(this, ID_REPLACE_TEXT, emptyString, wxDefaultPosition, wxSize(276, -1), FR->replaceRecent);`
- [L57](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FindReplaceDialog.cpp#L57) `ReplaceText->SetToolTip(_("Replace with:"));`
- [L68](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FindReplaceDialog.cpp#L68) `FindInSubsPattern = new HikariChoice(this, ID_REPLACE_TEXT, emptyString, wxDefaultPosition, wxSize(276, -1), FR->subsFindingFilters);`
- [L69](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FindReplaceDialog.cpp#L69) `FindInSubsPattern->SetToolTip(_("Windows search filters separated by semicolons, e.g. \"*.ass; *.srt\".\nBecause of the large video file,\nthe search for everything \"*. *\" Is changed to *.ass"));`
- [L79](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FindReplaceDialog.cpp#L79) `FindInSubsPath = new HikariChoice(this, ID_REPLACE_TEXT, emptyString, wxDefaultPosition, wxSize(236, -1), FR->subsFindingPaths);`
- [L80](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FindReplaceDialog.cpp#L80) `FindInSubsPath->SetToolTip(_("Subtitle search folder:"));`
- [L83](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FindReplaceDialog.cpp#L83) `MappedButton *selectFolder = new MappedButton(this, 21345, L" ... ");`
- [L99](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FindReplaceDialog.cpp#L99) `MatchCase = new HikariCheckBox(this, -1, _("Match case"));`
- [L101](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FindReplaceDialog.cpp#L101) `RegEx = new HikariCheckBox(this, -1, _("Regular expressions"));`
- [L103](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FindReplaceDialog.cpp#L103) `StartLine = new HikariCheckBox(this, ID_START_OF_LINE, _("Beginning of text"));`
- [L105](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FindReplaceDialog.cpp#L105) `EndLine = new HikariCheckBox(this, ID_END_OF_LINE, _("End of text"));`
- [L111](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FindReplaceDialog.cpp#L111) `UseComments = new HikariCheckBox(this, -1, _("Include comments"));`
- [L113](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FindReplaceDialog.cpp#L113) `OnlyText = new HikariCheckBox(this, ID_ONLY_TEXT, _("Skip tags"));`
- [L115](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FindReplaceDialog.cpp#L115) `OnlyTags = new HikariCheckBox(this, ID_ONLY_TAGS, _("Skip text"));`
- [L161](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FindReplaceDialog.cpp#L161) `MappedButton *ButtonFind = new MappedButton(this, ID_BUTTON_FIND, _("Find"), -1, wxDefaultPosition, wxSize(150, -1));`
- [L168](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FindReplaceDialog.cpp#L168) `MappedButton *ButtonFindInAllOpenedSubs = new MappedButton(this, ID_BUTTON_FIND_IN_ALL_OPENED_SUBS, _("Find in all open\nsubtitles"), -1);`
- [L170](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FindReplaceDialog.cpp#L170) `MappedButton *ButtonFindAllInCurrentSubs = new MappedButton(this, ID_BUTTON_FIND_ALL_IN_CURRENT_SUBS, _("Find all\nin current subtitles"), -1);`
- [L185](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FindReplaceDialog.cpp#L185) `MappedButton *ButtonReplaceNext = new MappedButton(this, ID_BUTTON_REPLACE, _("Replace next"), -1);`
- [L186](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FindReplaceDialog.cpp#L186) `MappedButton *ButtonReplaceAll = new MappedButton(this, ID_BUTTON_REPLACE_ALL, _("Replace all"), -1);`
- [L187](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FindReplaceDialog.cpp#L187) `MappedButton *ButtonReplaceOnAllTabs = new MappedButton(this, ID_BUTTON_REPLACE_IN_ALL_OPENED_SUBS, _("Replace in all open\nsubtitles"));`
- [L205](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FindReplaceDialog.cpp#L205) `MappedButton *ButtonFindInSubs = new MappedButton(this, ID_BUTTON_FIND_IN_SUBS, _("Find in subtitles"), -1, wxDefaultPosition, wxSize(150, -1));`
- [L207](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FindReplaceDialog.cpp#L207) `MappedButton *ButtonReplaceInSubs = new MappedButton(this, ID_BUTTON_REPLACE_IN_SUBS, _("Replace in subtitles"), -1);`
- [L221](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FindReplaceDialog.cpp#L221) `MappedButton *ButtonClose = new MappedButton(this, ID_BUTTON_CLOSE, _("Close"));`
- [L226](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FindReplaceDialog.cpp#L226) `SeekInSubFolders = new HikariCheckBox(this, -1, _("Search in subfolders"));`
- [L228](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FindReplaceDialog.cpp#L228) `SeekInHiddenFolders = new HikariCheckBox(this, -1, _("Search in hidden\nfolders"));`
- [L261](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FindReplaceDialog.cpp#L261) `MappedButton *ButtonChooseStyle = new MappedButton(this, ID_BUTTON_CHOOSE_STYLE, L"+", -1, wxDefaultPosition, wxDefaultSize, (long)MAKE_SQUARE_BUTTON);`

### FindReplaceDialog.h

- [L29](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FindReplaceDialog.h#L29) `class TabWindow : public wxWindow`
- [L66](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FindReplaceDialog.h#L66) `class FindReplaceDialog : public HikariDialog`

### FindReplaceResultsDialog.cpp

Configuration/color symbols: `FIND_RESULT_FILENAME_BACKGROUND`, `FIND_RESULT_FILENAME_FOREGROUND`, `FIND_RESULT_FOUND_PHRASE_BACKGROUND`, `FIND_RESULT_FOUND_PHRASE_FOREGROUND`, `WINDOW_TEXT`, `WINDOW_TEXT_INACTIVE`.

- [L47](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FindReplaceResultsDialog.cpp#L47) `MappedButton *checkAll = new MappedButton(this, ID_CHECK_ALL, _("Check all"), -1);`
- [L48](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FindReplaceResultsDialog.cpp#L48) `MappedButton *unCheckAll = new MappedButton(this, ID_UNCHECK_ALL, _("Uncheck all"));`
- [L49](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FindReplaceResultsDialog.cpp#L49) `replaceChecked = new MappedButton(this, ID_REPLACE_CHECKED, _("Replace"), -1);`
- [L50](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FindReplaceResultsDialog.cpp#L50) `ReplaceText = new HikariChoice(this, -1, FR->actualReplace, wxDefaultPosition, wxDefaultSize, FR->replaceRecent);`

### FindReplaceResultsDialog.h

- [L86](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FindReplaceResultsDialog.h#L86) `class FindReplaceResultsDialog : public HikariDialog`

### FontCatalogList.cpp

Configuration/color symbols: `BUTTON_BACKGROUND`, `BUTTON_BACKGROUND_HOVER`, `BUTTON_BACKGROUND_PUSHED`, `BUTTON_BORDER`, `BUTTON_BORDER_HOVER`, `BUTTON_BORDER_PUSHED`, `STYLE_EDIT_FILTER_TEXT`, `STYLE_PREVIEW_TEXT`, `WINDOW_TEXT`, `WINDOW_TEXT_INACTIVE`.

- [L38](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCatalogList.cpp#L38) `class CatalogEdition : public HikariDialog`
- [L63](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCatalogList.cpp#L63) `currentCatalog = new HikariChoice(this, -1, wxDefaultPosition, wxDefaultSize, *FCManagement.GetCatalogNames());`
- [L70](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCatalogList.cpp#L70) `MappedButton* OK = new MappedButton(this, wxID_OK, L"OK");`
- [L71](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCatalogList.cpp#L71) `MappedButton* cancel = new MappedButton(this, wxID_CANCEL, _("Cancel"));`
- [L108](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCatalogList.cpp#L108) `MappedButton* addCatalog = new MappedButton(this, ID_ADD_CATALOG, _("Add"));`
- [L109](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCatalogList.cpp#L109) `MappedButton* editCatalog = new MappedButton(this, ID_EDIT_CATALOG, _("Edit"));`
- [L110](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCatalogList.cpp#L110) `MappedButton* removeCatalog = new MappedButton(this, ID_REMOVE_CATALOG, _("Delete"));`
- [L111](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCatalogList.cpp#L111) `MappedButton* loadCatalogs = new MappedButton(this, ID_LOAD_CATALOGS, _("Load"));`
- [L114](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCatalogList.cpp#L114) `catalog = new HikariChoice(this, -1, emptyString, wxDefaultPosition, wxDefaultSize, *catalogList);`
- [L118](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCatalogList.cpp#L118) `MappedButton* saveFilter = new MappedButton(this, ID_SAVE_FILTER, _("Save filter"));`
- [L123](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCatalogList.cpp#L123) `catalog->Append(ctlg);`
- [L315](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCatalogList.cpp#L315) `wxFileDialog* FileDialog = new wxFileDialog(this, _("Choose video file"), emptyString, emptyString, _("Text files (*.txt)|*.txt"), wxFD_OPEN | wxFD_FILE_MUST_EXIST);`
- [L393](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCatalogList.cpp#L393) `menuList.Append(3000 + i, catalog, nullptr, emptyString, ITEM_CHECK_AND_HIDE)->Check(checked);`
- [L621](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCatalogList.cpp#L621) `fontCatalogsText.Append(it->first + L"={\r\n");`
- [L623](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCatalogList.cpp#L623) `fontCatalogsText.Append(L"\t" + font + L"\r\n");`
- [L625](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCatalogList.cpp#L625) `fontCatalogsText.Append(L"}\r\n");`
- [L682](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCatalogList.cpp#L682) `menuList.Append(2999, _("Add fonts from subtitles"), nullptr, emptyString, ITEM_NORMAL);`
- [L687](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCatalogList.cpp#L687) `menuList.Append(3000 + i, catalog, nullptr, emptyString, ITEM_CHECK_AND_HIDE)->Check(checked);`
- [L840](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCatalogList.cpp#L840) `class GetFontsFromASSDialog : public HikariDialog`
- [L849](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCatalogList.cpp#L849) `catalog = new HikariChoice(this, -1, emptyString, wxDefaultPosition, wxDefaultSize, *catalogs);`
- [L851](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCatalogList.cpp#L851) `MappedButton* add = new MappedButton(this, 2998, _("Add"));`
- [L856](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCatalogList.cpp#L856) `catalog->Append(ctlg);`
- [L864](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCatalogList.cpp#L864) `emptyCatalog = new HikariCheckBox(this, -1, _("Remove all contents of catalog"));`
- [L865](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCatalogList.cpp#L865) `allSubs = new HikariCheckBox(this, -1, _("Add fonts from all open subtitles"));`
- [L868](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCatalogList.cpp#L868) `MappedButton* Buttonok = new MappedButton(this, wxID_OK, L"OK");`
- [L869](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCatalogList.cpp#L869) `MappedButton* Buttoncancel = new MappedButton(this, 8999, _("Cancel"));`

### FontCatalogList.h

- [L108](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCatalogList.h#L108) `class FontCatalogList : public HikariDialog`

### FontCollector.cpp

Configuration/color symbols: `EXTERNAL_FONTS_DIRECTORY`, `FONT_COLLECTOR_ACTION`, `FONT_COLLECTOR_DIRECTORY`, `FONT_COLLECTOR_FROM_MKV`, `FONT_COLLECTOR_USE_SUBS_DIRECTORY`, `WINDOW_BACKGROUND`, `WINDOW_TEXT`, `WINDOW_WARNING_ELEMENTS`.

- [L159](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCollector.cpp#L159) `choosepath = new MappedButton(this, 8799, _("Select a folder"));`
- [L171](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCollector.cpp#L171) `opts = new HikariRadioBox(this, 9987, _("Options"), wxDefaultPosition, wxDefaultSize, choices, 0, wxRA_SPECIFY_ROWS);`
- [L175](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCollector.cpp#L175) `subsdir = new HikariCheckBox(this, 7998, _("Save to video / subtitles folder."));`
- [L176](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCollector.cpp#L176) `subsdir->SetToolTip(_("Saves to the video folder\nwhen demuxing fonts from an MKV file."));`
- [L181](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCollector.cpp#L181) `fromMKV = new HikariCheckBox(this, 7991, _("Demux fonts from loaded MKV file"));`
- [L189](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCollector.cpp#L189) `bok = new MappedButton(this, 9879, _("Start"));`
- [L191](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCollector.cpp#L191) `bStartOnAllTabs = new MappedButton(this, 9880, _("Start on tabs"));`
- [L192](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCollector.cpp#L192) `bOpenFontFolder = new MappedButton(this, 9877, _("Save folder"));`
- [L194](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCollector.cpp#L194) `bClose = new MappedButton(this, 9881, _("Close"));`
- [L529](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCollector.cpp#L529) `wxFileDialog *fd = new wxFileDialog(this, _("Wybierz plik mkvmerge.exe"), L"C:\\Program Files", L"mkvmerge.exe", _("Programy (.exe)|*.exe"), wxFD_OPEN | wxFD_FILE_MUST_EXIST);`
- [L1290](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCollector.cpp#L1290) `fcd = new FontCollectorDialog(parent, this);`

### FontCollector.h

- [L111](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCollector.h#L111) `class FontCollectorDialog : public HikariDialog`
- [L206](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCollector.h#L206) `class FontCollectorThread : public wxThread`

### FontDialog.cpp

Configuration/color symbols: `STATICLIST_BACKGROUND`, `STATICLIST_BORDER`, `STATICLIST_SELECTION`, `STYLE_EDIT_FILTER_TEXT`, `STYLE_EDIT_FILTER_TEXT_ON`, `TEXT_FIELD_BORDER_ON_FOCUS`, `WINDOW_BACKGROUND`, `WINDOW_TEXT`.

- [L400](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontDialog.cpp#L400) `Bold = new HikariCheckBox(this, ID_FONTATTR, _("Bold"));`
- [L402](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontDialog.cpp#L402) `Italic = new HikariCheckBox(this, ID_FONTATTR, _("Italic"));`
- [L404](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontDialog.cpp#L404) `Underl = new HikariCheckBox(this, ID_FONTATTR, _("Underline"));`
- [L406](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontDialog.cpp#L406) `Strike = new HikariCheckBox(this, ID_FONTATTR, _("Strikethrough"));`
- [L410](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontDialog.cpp#L410) `Buttok = new MappedButton(this, wxID_OK, L"OK");`
- [L411](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontDialog.cpp#L411) `Buttcancel = new MappedButton(this, 8999, _("Cancel"));`
- [L423](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontDialog.cpp#L423) `fontCatalog = new HikariChoice(this, ID_FONT_CATALOG_LIST1, wxDefaultPosition, wxDefaultSize, *FCManagement.GetCatalogNames());`
- [L427](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontDialog.cpp#L427) `MappedButton* CatalogAdd = new MappedButton(this, ID_CATALOG_ADD1, _("Add"));`
- [L428](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontDialog.cpp#L428) `CatalogAdd->SetToolTip(_("Adds fonts to a previously created catalog"));`
- [L429](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontDialog.cpp#L429) `MappedButton* CatalogManage = new MappedButton(this, ID_CATALOG_MANAGE1, _("Manage"));`
- [L430](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontDialog.cpp#L430) `CatalogManage->SetToolTip(_("Manages font catalogs"));`
- [L432](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontDialog.cpp#L432) `Filter = new ToggleButton(this, ID_FILTER1, _("Filter"));`
- [L433](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontDialog.cpp#L433) `Filter->SetToolTip(_("Filters fonts to those containing the entered characters"));`
- [L602](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontDialog.cpp#L602) `FDialog = new FontDialog(parent, actualStyle, changePointToPixel);`
- [L687](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontDialog.cpp#L687) `Fonts->Append(font);`

### FontDialog.h

- [L30](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontDialog.h#L30) `class FontList : public wxWindow`
- [L71](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontDialog.h#L71) `class FontDialog : public HikariDialog`
- [L115](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontDialog.h#L115) `class FontPickerButton : public MappedButton{`

### FontEnumerator.cpp

Configuration/color symbols: `EXTERNAL_FONTS_DIRECTORY`.


### GraphicsD2D.cpp

- [L548](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/GraphicsD2D.cpp#L548) `class wxManagedResourceHolder : public wxResourceHolder, public wxD2DManagedObject`
- [L563](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/GraphicsD2D.cpp#L563) `class wxD2DResourceManager: public wxD2DContextSupplier`
- [L608](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/GraphicsD2D.cpp#L608) `class wxD2DResourceHolder: public wxManagedResourceHolder`
- [L700](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/GraphicsD2D.cpp#L700) `class wxD2DManagedGraphicsData : public wxD2DManagedObject`
- [L2230](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/GraphicsD2D.cpp#L2230) `class wxD2DBitmapResourceHolder : public wxD2DResourceHolder<ID2D1Bitmap>`
- [L2391](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/GraphicsD2D.cpp#L2391) `class wxD2DBrushResourceHolder : public wxD2DResourceHolder<B>`
- [L2400](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/GraphicsD2D.cpp#L2400) `class wxD2DSolidBrushResourceHolder : public wxD2DBrushResourceHolder<ID2D1SolidColorBrush>`
- [L2414](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/GraphicsD2D.cpp#L2414) `class wxD2DBitmapBrushResourceHolder : public wxD2DBrushResourceHolder<ID2D1BitmapBrush>`
- [L2438](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/GraphicsD2D.cpp#L2438) `class wxD2DHatchBrushResourceHolder : public wxD2DBrushResourceHolder<ID2D1BitmapBrush>`
- [L2464](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/GraphicsD2D.cpp#L2464) `class wxD2DLinearGradientBrushResourceHolder : public wxD2DResourceHolder<ID2D1LinearGradientBrush>`
- [L2513](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/GraphicsD2D.cpp#L2513) `class wxD2DRadialGradientBrushResourceHolder : public wxD2DResourceHolder<ID2D1RadialGradientBrush>`
- [L3024](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/GraphicsD2D.cpp#L3024) `class wxD2DRenderTargetResourceHolder : public wxD2DResourceHolder<ID2D1RenderTarget>`
- [L3091](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/GraphicsD2D.cpp#L3091) `class wxD2DImageRenderTargetResourceHolder : public wxD2DRenderTargetResourceHolder`
- [L3263](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/GraphicsD2D.cpp#L3263) `class wxD2DHwndRenderTargetResourceHolder : public wxD2DRenderTargetResourceHolder`
- [L3333](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/GraphicsD2D.cpp#L3333) `class wxD2DDeviceContextResourceHolder : public wxD2DRenderTargetResourceHolder`
- [L3529](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/GraphicsD2D.cpp#L3529) `class wxD2DDCRenderTargetResourceHolder : public wxD2DRenderTargetResourceHolder`
- [L3653](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/GraphicsD2D.cpp#L3653) `class wxD2DMeasuringContext : public wxNullContext`
- [L5170](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/GraphicsD2D.cpp#L5170) `class wxD2DLentRenderTargetResourceHolder : public wxD2DRenderTargetResourceHolder`
- [L5506](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/GraphicsD2D.cpp#L5506) `class wxDirect2DModule : public wxModule`

### HikariCheckBox.cpp

Configuration/color symbols: `WINDOW_BACKGROUND`, `WINDOW_TEXT`, `WINDOW_TEXT_INACTIVE`.


### HikariCheckBox.h

- [L24](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariCheckBox.h#L24) `class HikariCheckBox : public wxWindow`

### HikariDialog.cpp

Configuration/color symbols: `WINDOW_BACKGROUND`, `WINDOW_BORDER`, `WINDOW_BORDER_BACKGROUND`, `WINDOW_BORDER_BACKGROUND_INACTIVE`, `WINDOW_BORDER_INACTIVE`, `WINDOW_HEADER_TEXT`, `WINDOW_HEADER_TEXT_INACTIVE`, `WINDOW_HOVER_CLOSE_BUTTON`, `WINDOW_PUSHED_CLOSE_BUTTON`, `WINDOW_TEXT`.


### HikariDialog.h

- [L27](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariDialog.h#L27) `class DialogSizer : public wxBoxSizer`
- [L40](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariDialog.h#L40) `class HikariDialog : public wxTopLevelWindow`

### HikariFrame.cpp

Configuration/color symbols: `AUDIO_BOX_HEIGHT`, `MONITOR_POSITION`, `MONITOR_SIZE`, `VIDEO_WINDOW_SIZE`, `WINDOW_BACKGROUND`, `WINDOW_BORDER`, `WINDOW_BORDER_BACKGROUND`, `WINDOW_BORDER_BACKGROUND_INACTIVE`, `WINDOW_BORDER_INACTIVE`, `WINDOW_HEADER_TEXT`, `WINDOW_HEADER_TEXT_INACTIVE`, `WINDOW_HOVER_CLOSE_BUTTON`, `WINDOW_HOVER_HEADER_ELEMENT`, `WINDOW_POSITION`, `WINDOW_PUSHED_CLOSE_BUTTON`, `WINDOW_PUSHED_HEADER_ELEMENT`, `WINDOW_SIZE`, `WINDOW_TEXT`.

- [L61](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariFrame.cpp#L61) `wxTopLevelWindows.Append(this);`

### HikariFrame.h

- [L30](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariFrame.h#L30) `class HikariFrame : public wxTopLevelWindow`

### HikariGauge.cpp

Configuration/color symbols: `WINDOW_BACKGROUND`, `WINDOW_BORDER`, `WINDOW_TEXT`.


### HikariGauge.h

- [L22](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariGauge.h#L22) `class HikariGauge : public wxWindow`

### HikariListCtrl.cpp

Configuration/color symbols: `STATICLIST_BACKGROUND`, `STATICLIST_BACKGROUND_HEADLINE`, `STATICLIST_BORDER`, `STATICLIST_SELECTION`, `STATICLIST_TEXT_HEADLINE`, `STYLE_PREVIEW_COLOR1`, `STYLE_PREVIEW_COLOR2`, `WINDOW_BACKGROUND_INACTIVE`, `WINDOW_TEXT`, `WINDOW_TEXT_INACTIVE`, `WINDOW_WARNING_ELEMENTS`.

- [L49](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariListCtrl.cpp#L49) `theList->SetToolTip(name.Mid(0, 1000));`
- [L51](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariListCtrl.cpp#L51) `theList->SetToolTip(name);`
- [L118](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariListCtrl.cpp#L118) `menut.Append(7786, _("&Copy"));`
- [L119](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariListCtrl.cpp#L119) `menut.Append(7787, _("&Paste"));`

### HikariListCtrl.h

- [L155](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariListCtrl.h#L155) `class HikariListCtrl : public HikariScrolledWindow`

### HikariMessageBox.cpp

- [L39](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariMessageBox.cpp#L39) `kcb = new HikariCheckBox(this, -1, _("Apply to All"));`
- [L43](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariMessageBox.cpp#L43) `btn = new MappedButton(this, 9009, L"OK", -1, wxDefaultPosition, wxSize(60, -1));`
- [L54](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariMessageBox.cpp#L54) `btn = new MappedButton(this, wxYES_TO_ALL, _("Yes to all"));`
- [L63](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariMessageBox.cpp#L63) `btn = new MappedButton(this, wxID_YES, _("Yes"), -1, wxDefaultPosition, wxSize(60, -1));`
- [L72](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariMessageBox.cpp#L72) `btn = new MappedButton(this, wxID_NO, _("No"), -1, wxDefaultPosition, wxSize(60, -1));`
- [L80](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariMessageBox.cpp#L80) `btn = new MappedButton(this, 9010, _("Cancel"), -1);`
- [L88](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariMessageBox.cpp#L88) `btn = new MappedButton(this, 9011, _("Help"), -1);`

### HikariMessageBox.h

- [L25](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariMessageBox.h#L25) `class HikariMessageDialog : public HikariDialog`

### HikariPanel.h

- [L66](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariPanel.h#L66) `class HikariPanel : public HikariNavigation<wxWindow>`

### HikariRadioButton.h

- [L22](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariRadioButton.h#L22) `class HikariRadioButton : public HikariCheckBox`
- [L41](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariRadioButton.h#L41) `class HikariRadioBox : public wxWindow`

### HikariScrollbar.cpp

Configuration/color symbols: `SCROLLBAR_BACKGROUND`, `SCROLLBAR_THUMB`, `SCROLLBAR_THUMB_HOVER`, `SCROLLBAR_THUMB_PUSHED`.


### HikariScrollbar.h

- [L24](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariScrollbar.h#L24) `class HikariScrollbar : public wxWindow`
- [L73](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariScrollbar.h#L73) `class HikariScrolledWindow : public wxWindow`

### HikariSlider.cpp

Configuration/color symbols: `BUTTON_BACKGROUND_ON_FOCUS`, `BUTTON_BORDER_INACTIVE`, `BUTTON_BORDER_ON_FOCUS`, `SLIDER_BACKGROUND`, `SLIDER_BACKGROUND_HOVER`, `SLIDER_BACKGROUND_PUSHED`, `SLIDER_BORDER`, `SLIDER_BORDER_HOVER`, `SLIDER_BORDER_PUSHED`, `SLIDER_PATH_BACKGROUND`, `SLIDER_PATH_BORDER`, `WINDOW_BACKGROUND`, `WINDOW_BACKGROUND_INACTIVE`.


### HikariSlider.h

- [L22](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSlider.h#L22) `class HikariSlider :public wxWindow`

### HikariStaticBoxSizer.cpp

Configuration/color symbols: `STATICBOX_BORDER`.


### HikariStaticBoxSizer.h

- [L53](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariStaticBoxSizer.h#L53) `class HikariStaticBoxSizer : public wxBoxSizer`

### HikariStaticText.cpp

Configuration/color symbols: `WINDOW_BACKGROUND`, `WINDOW_TEXT`.


### HikariStaticText.h

- [L23](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariStaticText.h#L23) `class HikariStaticText : public wxWindow`

### HikariStatusBar.cpp

Configuration/color symbols: `STATUSBAR_BORDER`, `WINDOW_BACKGROUND`, `WINDOW_TEXT`.


### HikariStatusBar.h

- [L24](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariStatusBar.h#L24) `class HikariStatusBar : public wxWindow{`

### hikarisubApp.cpp

Configuration/color symbols: `AUDIO_BOX_HEIGHT`, `DICTIONARY_LANGUAGE`, `EXTERNAL_FONTS_DIRECTORY`, `LAST_SESSION_CONFIG`, `MONITOR_POSITION`, `MONITOR_SIZE`, `PROGRAM_LANGUAGE`, `STYLE_MANAGER_POSITION`, `VIDEO_FULL_SCREEN_ON_START`, `VIDEO_WINDOW_SIZE`, `WINDOW_POSITION`, `WINDOW_SIZE`.

- [L120](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/hikarisubApp.cpp#L120) `class HikariSubIpcConnection : public wxConnection`
- [L140](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/hikarisubApp.cpp#L140) `class HikariSubIpcServer : public wxServer`
- [L580](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/hikarisubApp.cpp#L580) `subs.Append(argv[i]);`

### hikarisubApp.h

- [L45](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/hikarisubApp.h#L45) `class hikarisubApp : public wxApp`

### HikariSubFrame.cpp

Configuration/color symbols: `AUDIO_INACTIVE_LINES_DISPLAY_MODE`, `AUDIO_RECENT_FILES`, `AUTO_SELECT_LINES_FROM_LAST_TAB`, `AUTOMATION_RECENT_FILES`, `DONT_ASK_FOR_BAD_RESOLUTION`, `EDITOR_ON`, `GRID_INSERT_END_OFFSET`, `GRID_INSERT_START_OFFSET`, `KEYFRAMES_RECENT`, `LAST_SESSION_CONFIG`, `LINK_RESOLUTIONS`, `MONITOR_POSITION`, `MONITOR_SIZE`, `OPEN_SUBS_IN_NEW_TAB`, `SHIFT_TIMES_CHANGE_VALUES_WITH_TAB`, `SHIFT_TIMES_ON`, `SUBS_AUTONAMING`, `SUBS_RECENT_FILES`, `TOOLBAR_ALIGNMENT`, `VIDEO_FULL_SCREEN_ON_START`, `VIDEO_INDEX`, `VIDEO_RECENT_FILES`, `VIDEO_WINDOW_SIZE`, `WINDOW_MAXIMIZED`, `WINDOW_POSITION`, `WINDOW_SIZE`, `WINDOW_TEXT`, `WINDOW_WARNING_ELEMENTS`.

- [L161](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L161) `lastSession->AppendTool(Toolbar, GLOBAL_LOAD_LAST_SESSION, _("Load last session"), _("Loads previously loaded files"), PTR_BITMAP_PNG(L"OPEN_LAST_SESSION"));`
- [L163](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L163) `lastSession->Append(GLOBAL_LOAD_EXTERNAL_SESSION, _("Load session from file"), _("Loads session from saved session file"));`
- [L165](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L165) `lastSession->Append(GLOBAL_SAVE_EXTERNAL_SESSION, _("Save session to file"), _("Saves session to file"));`
- [L168](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L168) `lastSession->Append(GLOBAL_ASK_FOR_LOAD_LAST_SESSION, _("Ask whether to load the last session at program startup"), nullptr, _("Asks whether to load previously loaded files at program startup"), ITEM_CHECK_AND_HIDE)->Check(lastSessionConfig == 1);`
- [L172](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L172) `lastSession->Append(GLOBAL_LOAD_LAST_SESSION_ON_START, _("Load last session after program start"), nullptr, _("Loads previously loaded files at program startup"), ITEM_CHECK_AND_HIDE)->Check(lastSessionConfig == 2);`
- [L178](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L178) `FileMenu->AppendTool(Toolbar, GLOBAL_OPEN_SUBS, _("&Open subtitles"), _("Open subtitle file"), PTR_BITMAP_PNG(L"opensubs"));`
- [L180](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L180) `FileMenu->AppendTool(Toolbar, GLOBAL_SAVE_SUBS, _("&Save"), _("Save current file"), PTR_BITMAP_PNG(L"save"), false);`
- [L182](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L182) `FileMenu->AppendTool(Toolbar, GLOBAL_SAVE_ALL_SUBS, _("Save &all"), _("Save all subtitles"), PTR_BITMAP_PNG(L"saveall"));`
- [L184](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L184) `FileMenu->AppendTool(Toolbar, GLOBAL_SAVE_SUBS_AS, _("Save &as..."), _("Save as"), PTR_BITMAP_PNG(L"saveas"));`
- [L186](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L186) `FileMenu->AppendTool(Toolbar, GLOBAL_SAVE_TRANSLATION, _("Save &translation"), _("Save translation"), PTR_BITMAP_PNG(L"savetl"), false);`
- [L188](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L188) `FileMenu->AppendTool(Toolbar, GLOBAL_RECENT_SUBS, _("Recently opened &subtitles"), _("Recently opened subtitles"), PTR_BITMAP_PNG(L"recentsubs"), true, SubsRecMenu);`
- [L190](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L190) `FileMenu->AppendTool(Toolbar, GLOBAL_REMOVE_SUBS, _("Remove subtitles from the &editor"), _("Remove subtitles from the editor"), PTR_BITMAP_PNG(L"close"));`
- [L192](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L192) `FileMenu->Append(GLOBAL_SAVE_WITH_VIDEO_NAME, _("Save subtitles using the video name"), _("Save subtitles using the video name"), true, PTR_BITMAP_PNG(L"SAVEWITHVIDEONAME"), nullptr, ITEM_CHECK)->Check(Options.GetBool(SUBS_AUTONAMING));`
- [L196](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L196) `FileMenu->Append(GLOBAL_OPEN_AUTO_SAVE, _("Open auto save"), _("Opens the selected autosave from the list"));`
- [L197](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L197) `FileMenu->Append(GLOBAL_DELETE_TEMPORARY_FILES, _("Remove temporary files"), _("Opens the temporary file removal window"));`
- [L199](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L199) `FileMenu->Append(9989, _("Show / Hide log window"))->DisableMapping();`
- [L200](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L200) `FileMenu->Append(9990, _("Last session"), _("Last session options"), true, PTR_BITMAP_PNG(L"OPEN_LAST_SESSION"), lastSession);`
- [L202](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L202) `FileMenu->AppendTool(Toolbar, GLOBAL_SETTINGS, _("&Settings"), _("Program settings"), PTR_BITMAP_PNG(L"SETTINGS"));`
- [L204](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L204) `FileMenu->AppendTool(Toolbar, GLOBAL_QUIT, _("&Exit\tAlt-F4"), _("Exit the program"), PTR_BITMAP_PNG(L"exit"))->DisableMapping();`
- [L206](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L206) `Menubar->Append(FileMenu, _("&File"));`
- [L209](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L209) `EditMenu->AppendTool(Toolbar, GLOBAL_UNDO, _("&Undo"), _("Undo"), PTR_BITMAP_PNG(L"undo"), false);`
- [L211](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L211) `EditMenu->AppendTool(Toolbar, GLOBAL_UNDO_TO_LAST_SAVE, _("Undo to last save"), _("Undo to last save"), PTR_BITMAP_PNG(L"UNDOTOLASTSAVE"), false);`
- [L214](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L214) `EditMenu->AppendTool(Toolbar, GLOBAL_REDO, _("&Redo"), _("Redo"), PTR_BITMAP_PNG(L"redo"), false);`
- [L216](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L216) `EditMenu->AppendTool(Toolbar, GLOBAL_HISTORY, _("&History"), _("History"), PTR_BITMAP_PNG(L"history"), true);`
- [L218](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L218) `EditMenu->AppendTool(Toolbar, GLOBAL_FIND_REPLACE, _("Find and re&place"), _("Searches for the specified text phrases and replaces them"), PTR_BITMAP_PNG(L"findreplace"));`
- [L220](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L220) `EditMenu->AppendTool(Toolbar, GLOBAL_SEARCH, _("&Find"), _("Searches for the specified text phrase"), PTR_BITMAP_PNG(L"search"));`
- [L222](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L222) `EditMenu->AppendTool(Toolbar, GLOBAL_FIND_NEXT, _("Find next"), _("Finds the next occurrence of the phrase in the text"), PTR_BITMAP_PNG(L"search"));`
- [L227](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L227) `SortMenu[i]->Append(GLOBAL_SORT_ALL_BY_START_TIMES + (6 * i), _("The starting time"), _("Sort by start time"));`
- [L229](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L229) `SortMenu[i]->Append(GLOBAL_SORT_ALL_BY_END_TIMES + (6 * i), _("End time"), _("Sort by end time"));`
- [L231](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L231) `SortMenu[i]->Append(GLOBAL_SORT_ALL_BY_STYLE + (6 * i), _("Styles"), _("Sort by styles"));`
- [L232](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L232) `SortMenu[i]->Append(GLOBAL_SORT_ALL_BY_ACTOR + (6 * i), _("Actor"), _("Sort by actor"));`
- [L233](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L233) `SortMenu[i]->Append(GLOBAL_SORT_ALL_BY_EFFECT + (6 * i), _("Effect"), _("Sort by effect"));`
- [L234](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L234) `SortMenu[i]->Append(GLOBAL_SORT_ALL_BY_LAYER + (6 * i), _("Layer"), _("Sort by layer"));`
- [L237](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L237) `EditMenu->AppendTool(Toolbar, GLOBAL_SORT_LINES, _("So&rt all lines"), _("Sorts all lines in ASS file"), PTR_BITMAP_PNG(L"sort"), true, SortMenu[0]);`
- [L239](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L239) `EditMenu->AppendTool(Toolbar, GLOBAL_SORT_SELECTED_LINES, _("So&rt selected lines"), _("Sorts selected lines in ASS file"), PTR_BITMAP_PNG(L"sortsel"), true, SortMenu[1]);`
- [L241](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L241) `EditMenu->AppendTool(Toolbar, GLOBAL_MISSPELLS_REPLACER, _("Fix minor errors (experimental)"), _("Turns on multireplacer"), PTR_BITMAP_PNG(L"sellines"));`
- [L243](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L243) `EditMenu->AppendTool(Toolbar, GLOBAL_OPEN_SELECT_LINES, _("Select &lines"), _("Selects lines by expressions"), PTR_BITMAP_PNG(L"sellines"));`
- [L244](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L244) `Menubar->Append(EditMenu, _("&Edit"));`
- [L247](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L247) `VidMenu->AppendTool(Toolbar, GLOBAL_OPEN_VIDEO, _("Open video"), _("Opens video file"), PTR_BITMAP_PNG(L"openvideo"));`
- [L250](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L250) `VidMenu->AppendTool(Toolbar, GLOBAL_RECENT_VIDEO, _("Recently opened videos"), _("Recently opened videos"), PTR_BITMAP_PNG(L"recentvideo"), true, VidsRecMenu);`
- [L252](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L252) `VidMenu->AppendTool(Toolbar, GLOBAL_OPEN_KEYFRAMES, _("Open keyframes"), _("Open keyframes"), PTR_BITMAP_PNG(L"OPEN_KEYFRAMES"));`
- [L255](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L255) `VidMenu->AppendTool(Toolbar, GLOBAL_RECENT_KEYFRAMES, _("Recently opened keyframes"), _("Recently opened keyframes"), PTR_BITMAP_PNG(L"RECENT_KEYFRAMES"), true, KeyframesRecentMenu);`
- [L258](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L258) `VidMenu->Append(GLOBAL_OPEN_DUMMY_VIDEO, _("Open dummy video"), _("Open dummy video"));`
- [L259](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L259) `VidMenu->AppendTool(Toolbar, GLOBAL_SET_START_TIME, _("Insert start time from video"), _("Inserts the start time from video"), PTR_BITMAP_PNG(L"setstarttime"), false);`
- [L261](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L261) `VidMenu->AppendTool(Toolbar, GLOBAL_SET_END_TIME, _("Insert end time from video"), _("Inserts the end time from video"), PTR_BITMAP_PNG(L"setendtime"), false);`
- [L263](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L263) `VidMenu->AppendTool(Toolbar, GLOBAL_PREVIOUS_FRAME, _("Previous frame"), _("Go to previous frame"), PTR_BITMAP_PNG(L"prevframe"), false);`
- [L265](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L265) `VidMenu->AppendTool(Toolbar, GLOBAL_NEXT_FRAME, _("Next frame"), _("Go to next frame"), PTR_BITMAP_PNG(L"nextframe"), false);`
- [L267](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L267) `VidMenu->AppendTool(Toolbar, GLOBAL_SET_VIDEO_AT_START_TIME, _("Go to start time"), _("Moves video to start time"), PTR_BITMAP_PNG(L"videoonstime"));`
- [L270](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L270) `VidMenu->AppendTool(Toolbar, GLOBAL_SET_VIDEO_AT_END_TIME, _("Go to end time of line"), _("Moves video to end time"), PTR_BITMAP_PNG(L"videoonetime"));`
- [L273](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L273) `VidMenu->AppendTool(Toolbar, GLOBAL_PLAY_PAUSE, _("Play / Pause"), _("Plays / Pauses video"), PTR_BITMAP_PNG(L"pausemenu"), false);`
- [L275](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L275) `VidMenu->AppendTool(Toolbar, GLOBAL_GO_TO_PREVIOUS_KEYFRAME, _("Go to previous keyframe"), emptyString, PTR_BITMAP_PNG(L"prevkeyframe"));`
- [L277](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L277) `VidMenu->AppendTool(Toolbar, GLOBAL_GO_TO_NEXT_KEYFRAME, _("Go to next keyframe"), emptyString, PTR_BITMAP_PNG(L"nextkeyframe"));`
- [L279](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L279) `VidMenu->AppendTool(Toolbar, GLOBAL_SET_AUDIO_FROM_VIDEO, _("Set audio position to video time"), emptyString, PTR_BITMAP_PNG(L"SETVIDEOTIMEONAUDIO"));`
- [L281](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L281) `VidMenu->AppendTool(Toolbar, GLOBAL_SET_AUDIO_MARK_FROM_VIDEO, _("Set audio marker to video time"), emptyString, PTR_BITMAP_PNG(L"SETVIDEOTIMEONAUDIOMARK"));`
- [L283](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L283) `VidMenu->AppendTool(Toolbar, GLOBAL_VIDEO_ZOOM, _("Zoom video"), "", PTR_BITMAP_PNG(L"zoom"));`
- [L284](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L284) `VidMenu->Append(GLOBAL_RESET_VIDEO_ZOOM, _("Turn off video zoom"));`
- [L286](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L286) `VidMenu->Append(GLOBAL_VIDEO_INDEXING, _("Open video with FFMS2"), _("Opens video with FFMS2 for frame precision"), true, PTR_BITMAP_PNG(L"FFMS2INDEXING"), 0, ITEM_CHECK)->Check(videoIndex);`
- [L290](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L290) `Menubar->Append(VidMenu, _("&Video"));`
- [L293](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L293) `AudMenu->AppendTool(Toolbar, GLOBAL_OPEN_AUDIO, _("Open audio"), _("Opens audio file"), PTR_BITMAP_PNG(L"openaudio"));`
- [L297](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L297) `AudMenu->AppendTool(Toolbar, GLOBAL_RECENT_AUDIO, _("Recently opened audio"), _("Recently opened audio"), PTR_BITMAP_PNG(L"recentaudio"), true, AudsRecMenu);`
- [L299](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L299) `AudMenu->AppendTool(Toolbar, GLOBAL_AUDIO_FROM_VIDEO, _("Open audio from video"), _("Opens audio from video"), PTR_BITMAP_PNG(L"audiofromvideo"));`
- [L301](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L301) `AudMenu->Append(GLOBAL_OPEN_DUMMY_AUDIO, _("Open blank 2h30m audio"), _("Opens blank audio 2 hour and 30 minutes long"));`
- [L303](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L303) `AudMenu->AppendTool(Toolbar, GLOBAL_CLOSE_AUDIO, _("Close audio"), _("Closes audio"), PTR_BITMAP_PNG(L"closeaudio"));`
- [L305](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L305) `Menubar->Append(AudMenu, _("A&udio"));`
- [L308](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L308) `ViewMenu->Append(GLOBAL_VIEW_ALL, _("All"), _("Shows all windows"));`
- [L309](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L309) `ViewMenu->Append(GLOBAL_VIEW_VIDEO, _("Video and subs"), _("Shows only video window and subs window"));`
- [L310](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L310) `ViewMenu->Append(GLOBAL_VIEW_AUDIO, _("Audio and subs"), _("Shows only audio window and subs window"));`
- [L311](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L311) `ViewMenu->Append(GLOBAL_VIEW_ONLY_VIDEO, _("Only video"), _("Shows only video window"));`
- [L312](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L312) `ViewMenu->Append(GLOBAL_VIEW_SUBS, _("Only subtitles"), _("Shows only the subtitle window"));`
- [L313](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L313) `Menubar->Append(ViewMenu, _("View"));`
- [L316](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L316) `SubsMenu->AppendTool(Toolbar, GLOBAL_EDITOR, _("Enable / Disable editor"), _("Enables / disables editor"), PTR_BITMAP_PNG(L"editor"))->Enable(!videoIndex);`
- [L318](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L318) `SubsMenu->AppendTool(Toolbar, GLOBAL_OPEN_ASS_PROPERTIES, _("ASS file properties"), _("ASS subtitle properties"), PTR_BITMAP_PNG(L"ASSPROPS"));`
- [L320](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L320) `SubsMenu->AppendTool(Toolbar, GLOBAL_OPEN_STYLE_MANAGER, _("Style &manager"), _("Is used to manage ASS styles"), PTR_BITMAP_PNG(L"styles"));`
- [L323](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L323) `ConvMenu->AppendTool(Toolbar, GLOBAL_CONVERT_TO_ASS, _("Convert to ASS"), _("Converts to ASS format"), PTR_BITMAP_PNG(L"convass"), false);`
- [L325](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L325) `ConvMenu->AppendTool(Toolbar, GLOBAL_CONVERT_TO_SRT, _("Convert to SRT"), _("Converts to SRT format"), PTR_BITMAP_PNG(L"convsrt"));`
- [L327](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L327) `ConvMenu->AppendTool(Toolbar, GLOBAL_CONVERT_TO_MDVD, _("Convert to MDVD"), _("Converts to microDVD format"), PTR_BITMAP_PNG(L"convmdvd"));`
- [L329](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L329) `ConvMenu->AppendTool(Toolbar, GLOBAL_CONVERT_TO_MPL2, _("Convert to MPL2"), _("Converts to MPL2 format"), PTR_BITMAP_PNG(L"convmpl2"));`
- [L331](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L331) `ConvMenu->AppendTool(Toolbar, GLOBAL_CONVERT_TO_TMP, _("Convert to TMP"), _("Converts to TMPlayer format (not recommended)"), PTR_BITMAP_PNG(L"convtmp"));`
- [L334](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L334) `SubsMenu->Append(ID_CONVERSION, _("Conversion"), _("Converts from one format to another"), true, PTR_BITMAP_PNG(L"convert"), ConvMenu);`
- [L336](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L336) `SubsMenu->AppendTool(Toolbar, GLOBAL_SHOW_SHIFT_TIMES, _("Shift &times...\tCtrl-I"), _("Shifting subtitle times"), PTR_BITMAP_PNG(L"times"));`
- [L338](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L338) `SubsMenu->AppendTool(Toolbar, GLOBAL_OPEN_FONT_COLLECTOR, _("Font collector"), _("Font collector"), PTR_BITMAP_PNG(L"fontcollector"));`
- [L340](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L340) `SubsMenu->AppendTool(Toolbar, GLOBAL_OPEN_SUBS_RESAMPLE, _("Resample subtitles"), _("Resample subtitles"), PTR_BITMAP_PNG(L"subsresample"));`
- [L342](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L342) `SubsMenu->AppendTool(Toolbar, GLOBAL_OPEN_SPELLCHECKER, _("Check spelling"), _("Check spelling"), PTR_BITMAP_PNG(L"spellchecker"));`
- [L344](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L344) `SubsMenu->AppendTool(Toolbar, GLOBAL_HIDE_TAGS, _("Hide tags"), _("Hides tags in ASS and MDVD"), PTR_BITMAP_PNG(L"hidetags"));`
- [L346](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L346) `Menubar->Append(SubsMenu, _("Su&btitles"));`
- [L349](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L349) `m_AutoMenu->AppendTool(Toolbar, GLOBAL_AUTOMATION_LOAD_SCRIPT, _("Load script"), _("Load script"), PTR_BITMAP_PNG(L"automation"));`
- [L351](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L351) `m_AutoMenu->Append(GLOBAL_AUTOMATION_RELOAD_AUTOLOAD, _("Refresh autoload scripts"), _("Refresh autoload scripts"), true, PTR_BITMAP_PNG(L"automation"));`
- [L353](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L353) `m_AutoMenu->Append(GLOBAL_AUTOMATION_LOAD_LAST_SCRIPT, _("Run the last loaded script"), _("Run the last loaded script"));`
- [L355](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L355) `m_AutoMenu->Append(GLOBAL_AUTOMATION_OPEN_HOTKEYS_WINDOW, _("Open shortcut mapping window"), _("Open shortcut mapping window"));`
- [L357](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L357) `Menubar->Append(m_AutoMenu, _("Au&tomation"));`
- [L360](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L360) `HelpMenu->AppendTool(Toolbar, GLOBAL_HELP, _("HikariSub &website"), _("Opens the HikariSub website in the default browser"), PTR_BITMAP_PNG(L"help"));`
- [L362](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L362) `HelpMenu->AppendTool(Toolbar, GLOBAL_ANSI, _("&Report an issue"), _("Opens the HikariSub issue tracker"), PTR_BITMAP_PNG(L"ansi"));`
- [L364](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L364) `HelpMenu->AppendTool(Toolbar, GLOBAL_CHECK_FOR_UPDATES, _("Check for &updates"), _("Checks whether a newer version is available"), PTR_BITMAP_PNG(L"about"));`
- [L366](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L366) `HelpMenu->AppendTool(Toolbar, GLOBAL_ABOUT, _("&About"), _("Shows program info"), PTR_BITMAP_PNG(L"about"));`
- [L368](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L368) `HelpMenu->AppendTool(Toolbar, GLOBAL_HELPERS, _("&Credits"), _("Shows credits"), PTR_BITMAP_PNG(L"helpers"));`
- [L371](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L371) `Menubar->Append(HelpMenu, _("&Help"));`
- [L740](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L740) `if (!FR){ FR = new FindReplaceDialog(this, (id == GLOBAL_FIND_REPLACE) ? WINDOW_REPLACE : WINDOW_FIND); }`
- [L833](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L833) `new SpellCheckerDialog(this);`
- [L901](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L901) `wxFileDialog *FileDialog1 = new wxFileDialog(this, _("Choose script"), Options.GetString(AUTOMATION_RECENT_FILES), emptyString, _("Script files (*.lua),(*.moon)|*.lua;*.moon;"), wxFD_OPEN);`
- [L1010](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L1010) `wxFileDialog *FileDialog1 = new wxFileDialog(this, _("Choose subtitle file"), (tab->VideoPath != emptyString) ? HikariPathDir(tab->VideoPath) : (subsrec.size() > 0) ? HikariPathDir(subsrec[0]) : emptyString, emptyString, _("Subtitle files (*.ass),(*.ssa),(*.srt),(*.sub),(*.txt)|*.ass;*.ssa;*.srt;*.sub;*.txt|Videos with embedded subtitles (*.mkv),(*.ogm)|*.mkv;*.ogm"),`
- [L1040](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L1040) `wxFileDialog* FileDialog2 = new wxFileDialog(this, _("Choose video file"), (tab->SubsPath != emptyString) ? HikariPathDir(tab->SubsPath) : (videorec.size() > 0) ? HikariPathDir(videorec[0]) : emptyString, emptyString, _("Video files (*.avi),(*.mkv),(*.mp4),(*.ogm),(*.wmv),(*.asf),(*.rmvb),(*.rm),(*.3gp),(*.mpg),(*.mpeg),(*.avs)|*.avi;*.mkv;*.mp4;*.ogm;*.wmv;*.asf;*.rmvb;*.rm;*.mpg;*.mpeg;*.3gp;*.avs|All files (*.*)|*.*"),`
- [L1056](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L1056) `wxFileDialog* FileDialog2 = new wxFileDialog(this, _("Choose video file"), tab->VideoPath != emptyString ? HikariPathDir(tab->VideoPath) : (keyframesRecent.size() > 0) ? HikariPathDir(keyframesRecent[0]) : emptyString, emptyString, _("Keyframes file (*.txt),(*.pass),(*.stats),(*.log)|*.txt;*.pass;*.stats;*.log|All files (*.*)|*.*"),`
- [L1365](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L1365) `Tabs->AddPage(true);`
- [L1576](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L1576) `wmenu->Append(MI);`
- [L1583](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L1583) `wmenu->Append(MI);`
- [L1819](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L1819) `Tabs->AddPage(refresh);`
- [L2176](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L2176) `wxFileDialog *FileDialog1 = new wxFileDialog(this, _("Choose audio file"), (tab->VideoPath != emptyString) ? HikariPathDir(tab->VideoPath) : (videorec.size() > 0) ? HikariPathDir(videorec[0]) : emptyString, emptyString, _("Audio and video files") + L" (*.wav),(*.w64),(*.flac),(*.ac3),(*.aac),(*.ogg),(*.mp3),(*.mp4),(*.m4a),(*.mkv),(*.avi)|*.wav;*.w64;*.flac;*.ac3;*.aac;*.ogg;*.mp3;*.mp4;*.m4a;*.mkv;*.avi|" +`

### HikariSubFrame.h

- [L48](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.h#L48) `class HikariSubFrame : public HikariFrame`

### HikariTabBar.cpp

Configuration/color symbols: `TABS_BACKGROUND_ACTIVE`, `TABS_BACKGROUND_INACTIVE`, `TABS_BACKGROUND_INACTIVE_HOVER`, `TABS_BORDER_ACTIVE`, `TABS_BORDER_INACTIVE`, `TABS_TEXT_ACTIVE`, `TABS_TEXT_INACTIVE`, `WINDOW_BACKGROUND`, `WINDOW_TEXT`.


### HikariTabBar.h

- [L37](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariTabBar.h#L37) `class HikariTabBar : public wxWindow`

### HikariTextCtrl.cpp

Configuration/color symbols: `BUTTON_BORDER_INACTIVE`, `TEXT_FIELD_ALLOW_NUMPAD_HOTKEYS`, `TEXT_FIELD_BORDER`, `TEXT_FIELD_BORDER_ON_FOCUS`, `TEXT_FIELD_SELECTION`, `TEXT_FIELD_SELECTION_NO_FOCUS`, `WINDOW_BACKGROUND`, `WINDOW_BACKGROUND_INACTIVE`, `WINDOW_TEXT`, `WINDOW_TEXT_INACTIVE`.

- [L1475](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariTextCtrl.cpp#L1475) `menut.Append(TEXT_COPY, _("&Copy"))->Enable(Selend.x != Cursor.x);`
- [L1476](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariTextCtrl.cpp#L1476) `menut.Append(TEXT_CUT, _("Cu&t"))->Enable(Selend.x != Cursor.x && !(style & wxTE_READONLY));`
- [L1477](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariTextCtrl.cpp#L1477) `menut.Append(TEXT_PASTE, _("&Paste"))->Enable(!(style & wxTE_READONLY));`
- [L1480](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariTextCtrl.cpp#L1480) `/*menut.Append(TEXT_SEEKWORDL,_("Szukaj tłumaczenia słowa na ling.pl"))->Enable(Selend.x!=Cursor.x);`
- [L1481](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariTextCtrl.cpp#L1481) `menut.Append(TEXT_SEEKWORDB,_("Szukaj tłumaczenia słowa na pl.ba.bla"))->Enable(Selend.x!=Cursor.x);*/`
- [L1482](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariTextCtrl.cpp#L1482) `menut.Append(TEXT_SEEKWORDG, _("Search for the selected phrase on Google"))->Enable(Selend.x != Cursor.x);`
- [L1484](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariTextCtrl.cpp#L1484) `menut.Append(TEXT_DEL, _("&Delete"))->Enable(Selend.x != Cursor.x && !(style & wxTE_READONLY));`

### HikariTextCtrl.h

- [L43](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariTextCtrl.h#L43) `class HikariTextCtrl : public HikariScrolledWindow`

### HikariTextValidator.h

- [L23](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariTextValidator.h#L23) `class HikariTextValidator: public wxValidator`

### HikariTreeBook.cpp

Configuration/color symbols: `STATICLIST_BACKGROUND`, `STATICLIST_BORDER`, `STATICLIST_SELECTION`, `WINDOW_BACKGROUND`, `WINDOW_TEXT`.


### HikariTreebook.h

- [L39](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariTreebook.h#L39) `class HikariTreebook :public wxWindow`

### HikariWindowResizer.cpp

Configuration/color symbols: `WINDOW_BACKGROUND`, `WINDOW_RESIZER_DOTS`, `WINDOW_TEXT`.

- [L95](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariWindowResizer.cpp#L95) `splitLine = new wxDialog(this, -1, emptyString, wxPoint(px, py), wxSize(GetSize().GetWidth(), 2), wxSTAY_ON_TOP | wxBORDER_NONE);`

### HikariWindowResizer.h

- [L24](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariWindowResizer.h#L24) `class HikariWindowResizer : public wxWindow`

### Hotkeys.cpp

Configuration/color symbols: `WINDOW_BACKGROUND`, `WINDOW_TEXT`.

- [L427](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Hotkeys.cpp#L427) `HkeysDialog *hkd = new HkeysDialog(parent, (name.empty()) ? GetName(id) : name, hotkeyWindow, showWindowSelection);`
- [L604](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Hotkeys.cpp#L604) `global = new HikariChoice(this, -1, wxDefaultPosition, wxDefaultSize, elems, windows, wxWANTS_CHARS);`

### Hotkeys.h

- [L329](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Hotkeys.h#L329) `class HkeysDialog : public HikariDialog`

### KaraokeSplitting.cpp

Configuration/color symbols: `AUDIO_MERGE_EVERY_N_WITH_SYLLABLE`.


### ListControls.cpp

Configuration/color symbols: `BUTTON_BACKGROUND`, `BUTTON_BACKGROUND_HOVER`, `BUTTON_BACKGROUND_ON_FOCUS`, `BUTTON_BACKGROUND_PUSHED`, `BUTTON_BORDER`, `BUTTON_BORDER_HOVER`, `BUTTON_BORDER_INACTIVE`, `BUTTON_BORDER_ON_FOCUS`, `BUTTON_BORDER_PUSHED`, `MENU_BACKGROUND_SELECTION`, `MENU_BORDER_SELECTION`, `MENUBAR_BACKGROUND`, `STYLE_PREVIEW_TEXT`, `TEXT_FIELD_BACKGROUND`, `WINDOW_BACKGROUND_INACTIVE`, `WINDOW_BORDER`, `WINDOW_TEXT`, `WINDOW_TEXT_INACTIVE`.

- [L235](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ListControls.cpp#L235) `if (choiceText){ choiceText->SetToolTip(tt); }`

### ListControls.h

- [L30](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ListControls.h#L30) `class PopupList : public wxPopupWindow{`
- [L77](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ListControls.h#L77) `class HikariChoice :public wxWindow`

### LogHandler.cpp

- [L25](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/LogHandler.cpp#L25) `class LogWindow : public HikariDialog`
- [L47](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/LogHandler.cpp#L47) `//MappedButton *collapse = new MappedButton(this, 12456, _("Pokaż resztę logów"));`
- [L50](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/LogHandler.cpp#L50) `MappedButton *OK = new MappedButton(this, 12457, _("Close"));`

### MappedButton.cpp

Configuration/color symbols: `BUTTON_BACKGROUND`, `BUTTON_BACKGROUND_HOVER`, `BUTTON_BACKGROUND_ON_FOCUS`, `BUTTON_BACKGROUND_PUSHED`, `BUTTON_BORDER`, `BUTTON_BORDER_HOVER`, `BUTTON_BORDER_INACTIVE`, `BUTTON_BORDER_ON_FOCUS`, `BUTTON_BORDER_PUSHED`, `TOGGLE_BUTTON_BACKGROUND_TOGGLED`, `TOGGLE_BUTTON_BORDER_TOGGLED`, `WINDOW_BACKGROUND_INACTIVE`, `WINDOW_TEXT`, `WINDOW_TEXT_INACTIVE`.


### MappedButton.h

- [L25](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/MappedButton.h#L25) `class MappedButton :public wxWindow`
- [L75](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/MappedButton.h#L75) `class ToggleButton :public wxWindow`

### Menu.cpp

Configuration/color symbols: `MENU_BACKGROUND_SELECTION`, `MENU_BORDER_SELECTION`, `MENUBAR_BACKGROUND`, `MENUBAR_BACKGROUND_HOVER`, `MENUBAR_BACKGROUND_SELECTION`, `MENUBAR_BACKGROUND1`, `MENUBAR_BACKGROUND2`, `MENUBAR_BORDER_SELECTION`, `WINDOW_BORDER`, `WINDOW_TEXT`, `WINDOW_TEXT_INACTIVE`.

- [L131](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Menu.cpp#L131) `dialog = new MenuDialog(this, parent, npos, size, false);`
- [L149](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Menu.cpp#L149) `dialog = new MenuDialog(this, parent, npos, size, showIcons);`

### Menu.h

- [L35](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Menu.h#L35) `class  MenuEvent : public wxEvent`
- [L122](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Menu.h#L122) `class MenuDialog : public wxPopupWindow{`
- [L247](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Menu.h#L247) `class MenuBar : public wxWindow, Mnemonics`

### MenuButton.h

- [L21](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/MenuButton.h#L21) `class MenuButton : public MappedButton`

### MispellReplacerDialog.cpp

Configuration/color symbols: `FIND_RESULT_FILENAME_BACKGROUND`, `FIND_RESULT_FILENAME_FOREGROUND`, `FIND_RESULT_FOUND_PHRASE_BACKGROUND`, `FIND_RESULT_FOUND_PHRASE_FOREGROUND`, `WINDOW_TEXT`, `WINDOW_TEXT_INACTIVE`.

- [L213](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/MispellReplacerDialog.cpp#L213) `MappedButton *checkAll = new MappedButton(this, ID_CHECK_ALL, _("Check all"));`
- [L214](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/MispellReplacerDialog.cpp#L214) `MappedButton *unCheckAll = new MappedButton(this, ID_UNCHECK_ALL, _("Uncheck all"));`
- [L215](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/MispellReplacerDialog.cpp#L215) `replaceChecked = new MappedButton(this, ID_REPLACE_CHECKED, _("Replace"));`

### MispellReplacerDialog.h

- [L89](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/MispellReplacerDialog.h#L89) `class FindResultDialog : public HikariDialog`

### MisspellReplacer.cpp

- [L36](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/MisspellReplacer.cpp#L36) `//PutWordBoundary = new HikariCheckBox(this, ID_PUT_WORD_BOUNDARY, _("Wstawiaj automatycznie granice\npoczątku słowa \\m i końca słowa \\M"));`
- [L37](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/MisspellReplacer.cpp#L37) `//ShowBuiltInRules = new HikariCheckBox(this, ID_SHOW_BUILT_IN_RULES, _("Pokaż wbudowane zasady"));`
- [L50](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/MisspellReplacer.cpp#L50) `MatchCase = new HikariCheckBox(this, ID_MATCH_CASE, _("Match case"));`
- [L51](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/MisspellReplacer.cpp#L51) `ReplaceAsLower = new HikariCheckBox(this, ID_REPLACE_LOWER, _("Change to lower case"));`
- [L52](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/MisspellReplacer.cpp#L52) `ReplaceAsUpper = new HikariCheckBox(this, ID_REPLACE_UPPER, _("Change to upper case"));`
- [L53](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/MisspellReplacer.cpp#L53) `ReplaceWithUnchangedCase = new HikariCheckBox(this, ID_REPLACE_UPPER, _("Dont change case"));`
- [L54](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/MisspellReplacer.cpp#L54) `ReplaceOnlyTags = new HikariCheckBox(this, ID_REPLACE_ONLY_TAGS, _("Replace only in tags"));`
- [L55](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/MisspellReplacer.cpp#L55) `ReplaceOnlyText = new HikariCheckBox(this, ID_REPLACE_ONLY_TEXT, _("Replace only in text"));`
- [L105](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/MisspellReplacer.cpp#L105) `WhichLines = new HikariChoice(this, ID_WHICH_LINES_LIST, wxDefaultPosition, wxDefaultSize, 4, choices);`
- [L108](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/MisspellReplacer.cpp#L108) `MappedButton *ChooseStylesButton = new MappedButton(this, ID_STYLES_CHOOSE, L"+");`
- [L118](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/MisspellReplacer.cpp#L118) `MappedButton *AddRuleToList = new MappedButton(this, ID_ADD_RULE, _("Add rule"));`
- [L119](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/MisspellReplacer.cpp#L119) `MappedButton *EditRuleFromList = new MappedButton(this, ID_EDIT_RULE, _("Edit rule"));`
- [L120](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/MisspellReplacer.cpp#L120) `MappedButton *RemoveRuleFromList = new MappedButton(this, ID_REMOVE_RULE, _("Delete rule"));`
- [L121](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/MisspellReplacer.cpp#L121) `MappedButton *FindRule = new MappedButton(this, ID_FIND_RULE, _("Find error"));`
- [L123](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/MisspellReplacer.cpp#L123) `MappedButton *FindRulesOnTab = new MappedButton(this, ID_FIND_ALL_RULES, _("Find errors\nin current tab"));`
- [L124](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/MisspellReplacer.cpp#L124) `MappedButton *FindRulesOnAllTabs = new MappedButton(this, ID_FIND_ALL_RULES_ON_ALL_TABS, _("Find errors\nin all tabs"));`
- [L125](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/MisspellReplacer.cpp#L125) `MappedButton *ReplaceRule = new MappedButton(this, ID_REPLACE_RULE, _("Replace error"));`
- [L127](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/MisspellReplacer.cpp#L127) `MappedButton *ReplaceRules = new MappedButton(this, ID_REPLACE_ALL_RULES, _("Replace all errors\nin current tab"));`
- [L128](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/MisspellReplacer.cpp#L128) `MappedButton *ReplaceRulesOnAllTabs = new MappedButton(this, ID_REPLACE_ALL_RULES_ON_ALL_TABS, _("Replace all errors\nin all tabs"));`
- [L447](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/MisspellReplacer.cpp#L447) `resultDialog = new FindResultDialog(GetParent(), this);`
- [L461](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/MisspellReplacer.cpp#L461) `resultDialog = new FindResultDialog(GetParent(), this);`

### MisspellReplacer.h

- [L57](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/MisspellReplacer.h#L57) `class MisspellReplacer : public HikariDialog`

### NewCatalog.cpp

- [L41](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/NewCatalog.cpp#L41) `Button1 = new MappedButton(this, wxID_OK, _("Create"));`
- [L42](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/NewCatalog.cpp#L42) `Button2 = new MappedButton(this, wxID_CANCEL, _("Cancel"));`

### NewCatalog.h

- [L22](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/NewCatalog.h#L22) `class NewCatalog: public HikariDialog`

### Notebook.cpp

Configuration/color symbols: `BUTTON_BACKGROUND_PUSHED`, `EDITOR_ON`, `SUBS_COMPARISON_STYLES`, `SUBS_COMPARISON_TYPE`, `TAB_TEXT_MAX_CHARS`, `TABS_BACKGROUND_ACTIVE`, `TABS_BACKGROUND_INACTIVE`, `TABS_BACKGROUND_INACTIVE_HOVER`, `TABS_BACKGROUND_SECOND_WINDOW`, `TABS_BORDER_ACTIVE`, `TABS_BORDER_INACTIVE`, `TABS_CLOSE_HOVER`, `TABS_TEXT_ACTIVE`, `TABS_TEXT_INACTIVE`, `TABSBAR_ARROW`, `TABSBAR_ARROW_BACKGROUND`, `TABSBAR_ARROW_BACKGROUND_HOVER`, `TABSBAR_BACKGROUND1`, `TABSBAR_BACKGROUND2`, `WINDOW_BACKGROUND`.

- [L411](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Notebook.cpp#L411) `sline = new wxDialog(this, -1, emptyString, wxPoint(px, py), wxSize(3, h - 27), wxSTAY_ON_TOP | wxBORDER_NONE);`
- [L888](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Notebook.cpp#L888) `tabsMenu.Append(MENU_CHOOSE + g, Page(g)->SubsName, emptyString, true, 0, 0, (g == iter) ? ITEM_RADIO : ITEM_NORMAL);`
- [L891](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Notebook.cpp#L891) `tabsMenu.Append(MENU_SAVE + i, _("Save"), _("Save"))->Enable(i >= 0 && Pages[i]->grid->file->IsModified());`
- [L892](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Notebook.cpp#L892) `tabsMenu.Append(MENU_SAVE - 1, _("Save all"), _("Save all"));`
- [L893](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Notebook.cpp#L893) `tabsMenu.Append(MENU_CHOOSE - 1, _("Close all tabs"), _("Close all tabs"));`
- [L899](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Notebook.cpp#L899) `tabsMenu.Append(MENU_OPEN_SUBS_FOLDER, _("Open the folder containing the subtitles"), _("Open the folder containing the subtitles"));`
- [L902](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Notebook.cpp#L902) `tabsMenu.Append(MENU_OPEN_VIDEO_FOLDER, _("Open video containing folder"), _("Open video containing folder"));`
- [L905](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Notebook.cpp#L905) `tabsMenu.Append(MENU_OPEN_AUDIO_FOLDER, _("Open audio containing folder"), _("Open audio containing folder"));`
- [L908](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Notebook.cpp#L908) `tabsMenu.Append(MENU_OPEN_KEYFRAMES_FOLDER, _("Open keyframes containing folder"), _("Open keyframes containing folder"));`
- [L913](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Notebook.cpp#L913) `tabsMenu.Append((MENU_CHOOSE - 2) - i, txt);`
- [L923](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Notebook.cpp#L923) `MenuItem * styleItem = styleComparisonMenu->Append(4448, availableStyles[i], emptyString, true, nullptr, nullptr, ITEM_CHECK);`
- [L932](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Notebook.cpp#L932) `comparisonMenu->Append(MENU_COMPARE + 1, _("Compare by times"), nullptr, emptyString, ITEM_CHECK, canCompare)->Check(compareBy & COMPARE_BY_TIMES);`
- [L933](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Notebook.cpp#L933) `comparisonMenu->Append(MENU_COMPARE + 2, _("Compare by visible lines"), nullptr, emptyString, ITEM_CHECK, canCompare)->Check((compareBy & COMPARE_BY_VISIBLE)>0);`
- [L934](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Notebook.cpp#L934) `comparisonMenu->Append(MENU_COMPARE + 3, _("Compare by selections"), nullptr, emptyString, ITEM_CHECK, canCompare && Pages[iter]->grid->file->SelectionsSize() > 0 && Pages[i]->grid->file->SelectionsSize() > 0)->Check((compareBy & COMPARE_BY_SELECTIONS) > 0);`
- [L935](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Notebook.cpp#L935) `comparisonMenu->Append(MENU_COMPARE + 4, _("Compare by styles"), nullptr, emptyString, ITEM_CHECK, canCompare)->Check((compareBy & COMPARE_BY_STYLES) > 0);`
- [L936](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Notebook.cpp#L936) `comparisonMenu->Append(MENU_COMPARE + 5, _("Compare by selected styles"), styleComparisonMenu, emptyString, ITEM_CHECK, canCompare)->Check(SubsGrid::compareStyles.size() > 0);`
- [L937](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Notebook.cpp#L937) `comparisonMenu->Append(MENU_COMPARE, _("Compare"))->Enable(canCompare);`
- [L938](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Notebook.cpp#L938) `comparisonMenu->Append(MENU_COMPARE - 1, _("Turn off comparison"))->Enable(SubsGrid::hasCompare);`
- [L939](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Notebook.cpp#L939) `tabsMenu.Append(MENU_COMPARE + 6, _("Subtitle comparison"), comparisonMenu, _("Subtitle comparison"))->Enable(canCompare || SubsGrid::hasCompare);`
- [L1432](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Notebook.cpp#L1432) `sthis->AddPage(false);`
- [L1483](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Notebook.cpp#L1483) `sthis->AddPage(true);`

### Notebook.h

- [L30](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Notebook.h#L30) `class Notebook : public wxWindow`

### NumCtrl.cpp

- [L190](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/NumCtrl.cpp#L190) `if (val.EndsWith(L'.')){ val.Append(L"0"); }`

### NumCtrl.h

- [L24](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/NumCtrl.h#L24) `class NumCtrl : public HikariTextCtrl`

### OptionsDialog.cpp

Configuration/color symbols: `ACCEPTED_AUDIO_STREAM`, `AUDIO_AUTO_FOCUS`, `AUDIO_CACHE_FILES_LIMIT`, `AUDIO_DELAY`, `AUDIO_DONT_PLAY_WHEN_LINE_CHANGES`, `AUDIO_DRAW_KEYFRAMES`, `AUDIO_DRAW_SECONDARY_LINES`, `AUDIO_DRAW_SELECTION_BACKGROUND`, `AUDIO_DRAW_TIME_CURSOR`, `AUDIO_DRAW_VIDEO_POSITION`, `AUDIO_INACTIVE_LINES_DISPLAY_MODE`, `AUDIO_KARAOKE_MOVE_ON_CLICK`, `AUDIO_LEAD_IN_VALUE`, `AUDIO_LEAD_OUT_VALUE`, `AUDIO_LINE_BOUNDARIES_THICKNESS`, `AUDIO_LOCK_SCROLL_ON_CURSOR`, `AUDIO_MARK_PLAY_TIME`, `AUDIO_MERGE_EVERY_N_WITH_SYLLABLE`, `AUDIO_RAM_CACHE`, `AUDIO_SNAP_TO_KEYFRAMES`, `AUDIO_SNAP_TO_OTHER_LINES`, `AUTO_SELECT_LINES_FROM_LAST_TAB`, `AUTOMATION_LOADING_METHOD`, `AUTOMATION_OLD_SCRIPTS_COMPATIBILITY`, `AUTOMATION_TRACE_LEVEL`, `AUTOSAVE_MAX_FILES`, `CALC_SPACES_AND_PUNCTATION_FOR_CPS`, `CALC_SPACES_AND_PUNCTATION_FOR_WRAPS`, `CONVERT_ASS_TAGS_TO_INSERT_IN_LINE`, `CONVERT_FPS`, `CONVERT_FPS_FROM_VIDEO`, `CONVERT_NEW_END_TIMES`, `CONVERT_RESOLUTION_HEIGHT`, `CONVERT_RESOLUTION_WIDTH`, `CONVERT_SHOW_SETTINGS`, `CONVERT_STYLE`, `CONVERT_STYLE_CATALOG`, `CONVERT_TIME_PER_CHARACTER`, `DICTIONARY_LANGUAGE`, `DISABLE_LIVE_VIDEO_EDITING`, `DONT_ASK_FOR_BAD_RESOLUTION`, `EDITBOX_DONT_GO_TO_NEXT_LINE_ON_TIMES_EDIT`, `EDITBOX_SUGGESTIONS_ON_DOUBLE_CLICK`, `EXTERNAL_FONTS_DIRECTORY`, `FFMS2_VIDEO_SEEKING`, `GRID_CHANGE_ACTIVE_ON_SELECTION`, `GRID_DONT_CENTER_ACTIVE_LINE`, `GRID_DUPLICATION_DONT_CHANGE_SELECTION`, `GRID_FONT`, `GRID_FONT_SIZE`, `GRID_INSERT_END_OFFSET`, `GRID_INSERT_START_OFFSET`, `GRID_LOAD_SORTED_SUBS`, `GRID_SAVE_AFTER_CHARACTER_COUNT`, `GRID_SET_VISIBLE_LINE_AFTER_FULL_SCREEN`, `GRID_TAGS_SWAP_CHARACTER`, `OPEN_SUBS_IN_NEW_TAB`, `OPEN_VIDEO_AT_ACTIVE_LINE`, `PROGRAM_FONT`, `PROGRAM_FONT_SIZE`, `PROGRAM_LANGUAGE`, `PROGRAM_THEME`, `SHIFT_TIMES_CHANGE_VALUES_WITH_TAB`, `SPELLCHECKER_ON`, `TAB_TEXT_MAX_CHARS`, `TEXT_FIELD_ALLOW_NUMPAD_HOTKEYS`, `TL_MODE_HIDE_ORIGINAL_ON_VIDEO`, `TL_MODE_SHOW_ORIGINAL`, `VIDEO_FULL_SCREEN_ON_START`, `VIDEO_GPU_CONVERSION`, `VIDEO_PAUSE_ON_CLICK`, `VIDEO_VISUAL_WARNINGS_OFF`, `VIDEO_ZOOM_PERCENT`, `VSFILTER_INSTANCE`, `WINDOW_BACKGROUND`, `WINDOW_TEXT`, `WINDOW_TEXT_INACTIVE`, `WINDOW_WARNING_ELEMENTS`.

- [L67](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L67) `theList->SetToolTip(accel);`
- [L335](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L335) `HikariChoice* programLanguage = new HikariChoice(GLOBAL_EDITOR, ID_PROGRAM_LANGUAGE, wxDefaultPosition, wxDefaultSize, langs);`
- [L349](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L349) `HikariChoice* dic = new HikariChoice(GLOBAL_EDITOR, ID_DICTIONARY_LANGUAGE, wxDefaultPosition, wxDefaultSize, dictionaries);`
- [L358](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L358) `HikariCheckBox* opt = new HikariCheckBox(GLOBAL_EDITOR, -1, labels[i]);`
- [L370](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L370) `gridSaveAfter->SetToolTip(_("0 turns off saving while editing"));`
- [L372](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L372) `autoSaveMax->SetToolTip(_("Number of autosaves can be set from 2 to 1000000"));`
- [L377](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L377) `maxTabChars->SetToolTip(_("Number of tab name characters. Range from 20 to 150"));`
- [L428](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L428) `HikariCheckBox* allCharWraps = new HikariCheckBox(EditorAdvanced, -1, _("Calculate spaces and punctation characters for wraps"));`
- [L431](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L431) `HikariCheckBox* allCharCPS = new HikariCheckBox(EditorAdvanced, -1, _("Calculate spaces and punctation characters for CPS"));`
- [L438](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L438) `HikariChoice* cmb = new HikariChoice(EditorAdvanced, ID_HIKARI_CHOICE, wxDefaultPosition, wxSize(200, -1), 4, methods, wxTE_PROCESS_ENTER);`
- [L446](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L446) `MappedButton* choosePath = new MappedButton(EditorAdvanced, ID_EXTERNAL_FONTS_CHOOSE_FOLDER, _("Choose"));`
- [L497](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L497) `HikariChoice* cmb = new HikariChoice(ConvOpt, (i == 0) ? ID_CONVERSION_STYLE_CATALOG : ID_CONVERSION_STYLE, wxDefaultPosition, wxSize(200, -1), (i == 0) ? Options.dirs : styles, wxTE_PROCESS_ENTER);`
- [L530](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L530) `HikariChoice* cmb = new HikariChoice(ConvOpt, -1, convFPS, wxDefaultPosition, wxSize(200, -1), FPSes, wxTE_PROCESS_ENTER);`
- [L542](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L542) `HikariCheckBox* opt = new HikariCheckBox(ConvOpt, -1, (i == 0) ? _("FPS from video") : (i == 1) ? _("New end times") : _("Show window before conversion"));`
- [L588](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L588) `HikariCheckBox *opt = new HikariCheckBox(video, -1, voptspl[i]);`
- [L603](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L603) `HikariChoice *sopts = new HikariChoice(video, ID_HIKARI_CHOICE, wxDefaultPosition, wxSize(200, -1), 4, seekingOpts, wxTE_PROCESS_ENTER);`
- [L619](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L619) `HikariChoice *vsfiltersList = new HikariChoice(video, ID_VSFILTER_PROVIDER, wxDefaultPosition, wxSize(200, -1), vsfilters, wxTE_PROCESS_ENTER);`
- [L647](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L647) `HikariChoice *filterList = new HikariChoice(Hotkeyss, 14568, wxDefaultPosition, wxDefaultSize, 7, filteringModes);`
- [L649](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L649) `filterList->SetToolTip(_("Filtering mode:"));`
- [L673](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L673) `MappedButton *setHotkey = new MappedButton(Hotkeyss, 23232, _("Map hotkey"));`
- [L675](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L675) `MappedButton *resetHotkey = new MappedButton(Hotkeyss, 23231, _("Restore default hotkey"));`
- [L677](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L677) `MappedButton *deleteHotkey = new MappedButton(Hotkeyss, 23230, _("Delete hotkey"));`
- [L710](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L710) `HikariCheckBox *opt = new HikariCheckBox(AudioMain, -1, names[i]);`
- [L729](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L729) `audioCacheFilesLimit->SetToolTip(_("Range from 0 to 10000, where 0 turns off\nremoving audio cache files."));`
- [L731](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L731) `HikariChoice *displayNonActiveLines = new HikariChoice(AudioSecond, ID_HIKARI_CHOICE, wxDefaultPosition, wxSize(300, -1), 3, inact);`
- [L854](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L854) `HikariChoice *themeList = new HikariChoice(Themes, 14567, wxDefaultPosition, wxDefaultSize, choices);`
- [L856](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L856) `themeList->SetToolTip(_("Theme name:"));`
- [L858](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L858) `newTheme->SetToolTip(_("Name of the copied theme.\nDefault themes, DarkSentro and LightSentro,\ncannot be edited and must be copied."));`
- [L859](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L859) `MappedButton *copyTheme = new MappedButton(Themes, 14566, _("Copy"));`
- [L909](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L909) `int size = themeList->Append(themeName);`
- [L987](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L987) `MappedButton *btn = new MappedButton(Assocs, 17777 + i, buttonTexts[i]);`
- [L998](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L998) `OptionsTree->AddPage(GLOBAL_EDITOR, _("Editor"));`
- [L1000](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L1000) `OptionsTree->AddSubPage(ConvOpt, _("Conversion"));`
- [L1001](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L1001) `OptionsTree->AddSubPage(EditorAdvanced, _("Advanced"));`
- [L1002](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L1002) `OptionsTree->AddPage(video, _("Video"));`
- [L1003](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L1003) `OptionsTree->AddPage(AudioMain, _("Audio"));`
- [L1004](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L1004) `OptionsTree->AddSubPage(AudioSecond, _("Advanced"));`
- [L1005](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L1005) `OptionsTree->AddPage(Themes, _("Themes"));`
- [L1006](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L1006) `OptionsTree->AddPage(Hotkeyss, _("Hotkeys"));`
- [L1008](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L1008) `OptionsTree->AddPage(Assocs, _("Associations"));`
- [L1010](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L1010) `OptionsTree->AddPage(SubsProps, _("Subtitle properties"));`
- [L1016](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L1016) `okok = new MappedButton(this, wxID_OK, L"OK");`
- [L1017](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L1017) `MappedButton *oknow = new MappedButton(this, ID_BCOMMIT, _("Apply"));`
- [L1018](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L1018) `MappedButton *cancel = new MappedButton(this, wxID_CANCEL, _("Cancel"));`
- [L1019](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L1019) `MappedButton *resetDefaults = new MappedButton(this, ID_RESET_DEFAULTS, _("Set default"));`
- [L1282](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L1282) `Stylelist->Append(Options.GetStyle(i)->Name);`

### OptionsDialog.h

- [L54](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.h#L54) `class OptionsDialog : public HikariDialog`

### OptionsPanels.cpp

Configuration/color symbols: `ASS_PROPERTIES_ASK_FOR_CHANGE`, `ASS_PROPERTIES_EDITING`, `ASS_PROPERTIES_EDITING_ON`, `ASS_PROPERTIES_SCRIPT`, `ASS_PROPERTIES_SCRIPT_ON`, `ASS_PROPERTIES_TIMING`, `ASS_PROPERTIES_TIMING_ON`, `ASS_PROPERTIES_TITLE`, `ASS_PROPERTIES_TITLE_ON`, `ASS_PROPERTIES_TRANSLATION`, `ASS_PROPERTIES_TRANSLATION_ON`, `ASS_PROPERTIES_UPDATE`, `ASS_PROPERTIES_UPDATE_ON`.

- [L40](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsPanels.cpp#L40) `HikariCheckBox *fieldOn = new HikariCheckBox(this, -1, emptyString, wxDefaultPosition, wxSize(18, -1));`
- [L55](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsPanels.cpp#L55) `HikariCheckBox *option = new HikariCheckBox(this, -1, _("Always ask before changing subtitle information"), wxDefaultPosition, wxSize(18, -1));`

### OptionsPanels.h

- [L22](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsPanels.h#L22) `class SubtitlesProperties : public wxWindow{`

### platform.h

- [L1521](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/platform.h#L1521) `argument.append(slashCount / 2, L'\\');`
- [L1530](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/platform.h#L1530) `argument.append(slashCount, L'\\');`

### ProgressDialog.cpp

Configuration/color symbols: `WINDOW_BACKGROUND`, `WINDOW_TEXT`.

- [L44](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ProgressDialog.cpp#L44) `cancel = new MappedButton(this, 23333, _("Cancel"));`
- [L159](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ProgressDialog.cpp#L159) `dlg = new ProgresDialog(parent, title, pos, size, style | wxBORDER_NONE);`

### ProgressDialog.h

- [L29](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ProgressDialog.h#L29) `class ProgresDialog : public wxDialog`

### Provider.cpp

- [L111](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Provider.cpp#L111) `m_peaks.Append(samples.data(), count);`

### ProviderFFMS2.cpp

Configuration/color symbols: `ACCEPTED_AUDIO_STREAM`, `AUDIO_CACHE_FILES_LIMIT`, `AUDIO_DELAY`, `AUDIO_RAM_CACHE`, `FFMS2_VIDEO_SEEKING`, `VIDEO_GPU_CONVERSION`.


### RendererVideo.cpp

Configuration/color symbols: `VIDEO_PROGRESS_BAR`, `VIDEO_ZOOM_PERCENT`.


### ScriptInfo.cpp

Configuration/color symbols: `LINK_RESOLUTIONS`, `WINDOW_BACKGROUND`, `WINDOW_TEXT`.

- [L80](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ScriptInfo.cpp#L80) `resolutionFromVideo = new MappedButton(this, 25456, _("From video"), -1, wxDefaultPosition, wxSize(70, -1));`
- [L82](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ScriptInfo.cpp#L82) `layoutFromVideo = new MappedButton(this, 25457, _("From video"), -1, wxDefaultPosition, wxSize(70, -1));`
- [L84](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ScriptInfo.cpp#L84) `linkResolutions = new ToggleButton(this, 25458, L"", L"", wxDefaultPosition, wxSize(26 * 1.5, 50));`
- [L118](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ScriptInfo.cpp#L118) `matrix = new HikariChoice(this, -1, wxDefaultPosition, wxSize(160, -1));`
- [L119](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ScriptInfo.cpp#L119) `matrix->SetSelection(matrix->Append(_("None")));`
- [L120](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ScriptInfo.cpp#L120) `matrix->Append("TV.601");`
- [L121](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ScriptInfo.cpp#L121) `matrix->Append("PC.601");`
- [L122](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ScriptInfo.cpp#L122) `matrix->Append("TV.709");`
- [L123](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ScriptInfo.cpp#L123) `matrix->Append("PC.709");`
- [L124](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ScriptInfo.cpp#L124) `matrix->Append("TV.FCC");`
- [L125](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ScriptInfo.cpp#L125) `matrix->Append("PC.FCC");`
- [L126](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ScriptInfo.cpp#L126) `matrix->Append("TV.240M");`
- [L127](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ScriptInfo.cpp#L127) `matrix->Append("PC.240M");`
- [L138](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ScriptInfo.cpp#L138) `wrapstyle = new HikariChoice(this, -1/*, wxDefaultPosition, wxSize(160,-1)*/);`
- [L139](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ScriptInfo.cpp#L139) `wrapstyle->SetSelection(wrapstyle->Append(_("0: Auto, top line wider")));`
- [L140](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ScriptInfo.cpp#L140) `wrapstyle->Append(_("1: End-of-line wrapping, only \\N breaks"));`
- [L141](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ScriptInfo.cpp#L141) `wrapstyle->Append(_("2: No wrapping, both \\n and \\N break"));`
- [L142](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ScriptInfo.cpp#L142) `wrapstyle->Append(_("3: Auto, bottom line wider"));`
- [L143](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ScriptInfo.cpp#L143) `collision = new HikariChoice(this, -1);`
- [L144](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ScriptInfo.cpp#L144) `collision->SetSelection(collision->Append(_("Normal")));`
- [L145](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ScriptInfo.cpp#L145) `collision->Append(_("Reversed"));`
- [L152](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ScriptInfo.cpp#L152) `scaleBorderAndShadow = new HikariCheckBox(this, -1, _("Scale border and shadow"));`
- [L160](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ScriptInfo.cpp#L160) `save = new MappedButton(this, wxID_OK, _("Save"));`
- [L161](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ScriptInfo.cpp#L161) `cancel = new MappedButton(this, wxID_CANCEL, _("Cancel"));`
- [L216](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ScriptInfo.cpp#L216) `height->SetToolTip(_("Video height"));`
- [L217](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ScriptInfo.cpp#L217) `width->SetToolTip(_("Video width"));`
- [L218](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ScriptInfo.cpp#L218) `wrapstyle->SetToolTip(_("How to wrap lines"));`
- [L219](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ScriptInfo.cpp#L219) `collision->SetToolTip(_("Colliding lines"));`
- [L220](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ScriptInfo.cpp#L220) `scaleBorderAndShadow->SetToolTip(_("Scale border and shadow"));`

### ScriptInfo.h

- [L26](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ScriptInfo.h#L26) `class ScriptInfo : public HikariDialog`

### SelectLines.cpp

Configuration/color symbols: `SELECT_LINES_OPTIONS`, `SELECT_LINES_RECENT_SELECTIONS`.

- [L54](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SelectLines.cpp#L54) `FindText = new HikariChoice(this, -1, emptyString, wxDefaultPosition, wxSize(-1, -1), selsRecent);`
- [L55](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SelectLines.cpp#L55) `FindText->SetToolTip(_("Search text:"));`
- [L57](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SelectLines.cpp#L57) `ChooseStyles = new MappedButton(this, ID_CHOOSE_STYLES, L"+", -1/*, wxDefaultPosition, wxSize(-1, -1)*/);`
- [L61](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SelectLines.cpp#L61) `MatchCase = new HikariCheckBox(this, -1, _("Match case"));`
- [L63](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SelectLines.cpp#L63) `RegEx = new HikariCheckBox(this, -1, _("Regular expressions"));`
- [L104](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SelectLines.cpp#L104) `Dialogues = new HikariCheckBox(this, -1, _("Dialogue"));`
- [L106](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SelectLines.cpp#L106) `Comments = new HikariCheckBox(this, -1, _("Comments"));`
- [L117](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SelectLines.cpp#L117) `Selections = new HikariRadioBox(this, -1, _("Selection"), wxDefaultPosition, wxDefaultSize, sels, 2);`
- [L130](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SelectLines.cpp#L130) `Actions = new HikariRadioBox(this, -1, _("Action"), wxDefaultPosition, wxDefaultSize, action, 2);`
- [L140](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SelectLines.cpp#L140) `Select = new MappedButton(this, ID_SELECTIONS, _("Select"));`
- [L141](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SelectLines.cpp#L141) `MappedButton *SelectOnAllTabs = new MappedButton(this, ID_SELECT_ON_ALL_TABS, _("Select in all tabs"));`
- [L142](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SelectLines.cpp#L142) `Close = new MappedButton(this, wxID_CANCEL, _("Close"));`

### SelectLines.h

- [L28](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SelectLines.h#L28) `class SelectLines: public HikariDialog`

### ShiftTimes.cpp

Configuration/color symbols: `POSTPROCESSOR_KEYFRAME_AFTER_END`, `POSTPROCESSOR_KEYFRAME_AFTER_START`, `POSTPROCESSOR_KEYFRAME_BEFORE_END`, `POSTPROCESSOR_KEYFRAME_BEFORE_START`, `POSTPROCESSOR_LEAD_IN`, `POSTPROCESSOR_LEAD_OUT`, `POSTPROCESSOR_ON`, `POSTPROCESSOR_THRESHOLD_END`, `POSTPROCESSOR_THRESHOLD_START`, `SHIFT_TIMES_CORRECT_END_TIMES`, `SHIFT_TIMES_DISPLAY_FRAMES`, `SHIFT_TIMES_OPTIONS`, `SHIFT_TIMES_PROFILES`, `SHIFT_TIMES_STYLES`, `SHIFT_TIMES_TIME`, `SHIFT_TIMES_WHICH_LINES`, `SHIFT_TIMES_WHICH_TIMES`, `WINDOW_BACKGROUND`, `WINDOW_TEXT`, `WINDOW_WARNING_ELEMENTS`.

- [L32](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L32) `class ProfileEdition : public HikariDialog`
- [L64](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L64) `profilesList = new HikariChoice(this, -1, emptyString, wxDefaultPosition, wxDefaultSize, profiles, 0, valid);`
- [L67](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L67) `MappedButton *OK = new MappedButton(this, wxID_OK, L"OK");`
- [L68](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L68) `MappedButton *cancel = new MappedButton(this, wxID_CANCEL, _("Cancel"));`
- [L259](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L259) `coll = new MappedButton(panel, 22999, (normal) ? _("Post processor") : _("Shift times"));`
- [L274](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L274) `WhichLines = new HikariChoice(panel, 22888, wxDefaultPosition, wxDefaultSize, choices, HIKARI_SCROLL_ON_FOCUS);`
- [L277](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L277) `AddStyles = new MappedButton(panel, ID_BSTYLE, L"+", emptyString, wxDefaultPosition, wxDefaultSize, -1, MAKE_SQUARE_BUTTON);`
- [L288](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L288) `NewProfile = new MappedButton(panel, 31229, L"+", _("Adding and editing profiles"), wxDefaultPosition, wxDefaultSize, -1, MAKE_SQUARE_BUTTON);`
- [L291](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L291) `RemoveProfile = new MappedButton(panel, 31230, L"-", _("Removing profiles"), wxDefaultPosition, wxDefaultSize, -1, MAKE_SQUARE_BUTTON);`
- [L297](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L297) `ProfilesList = new HikariChoice(panel, 31231, wxDefaultPosition, wxDefaultSize, profileList);`
- [L302](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L302) `ProfilesList = new HikariChoice(panel, 31231, wxDefaultPosition, wxSize(100, NewProfile->GetMinSize().GetHeight()), profileList);`
- [L312](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L312) `MoveTime = new MappedButton(panel, GLOBAL_SHIFT_TIMES, _("Shift"), _("Shift subtitle times"), wxDefaultPosition, wxSize(60, -1), GLOBAL_HOTKEY);`
- [L316](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L316) `DisplayFrames = new HikariCheckBox(panel, 31221, _("Frames"));`
- [L317](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L317) `MoveTagTimes = new HikariCheckBox(panel, 22889, _("Tag times"));`
- [L340](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L340) `MoveToVideoTime = new HikariCheckBox(panel, ID_VIDEO, _("Move the marker\nto video time"));`
- [L344](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L344) `MoveToAudioTime = new HikariCheckBox(panel, ID_AUDIO, _("Move the marker\nto audio time"));`
- [L361](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L361) `WhichTimes = new HikariChoice(panel, 22888, wxDefaultPosition, wxDefaultSize, choices, HIKARI_SCROLL_ON_FOCUS);`
- [L371](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L371) `EndTimeCorrection = new HikariChoice(panel, 22888, wxDefaultPosition, wxSize(130, -1), choices, HIKARI_SCROLL_ON_FOCUS);`
- [L392](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L392) `MoveTime = new MappedButton(panel, GLOBAL_SHIFT_TIMES, _("Run post processor"), _("Run post processor"), wxDefaultPosition, wxDefaultSize, GLOBAL_HOTKEY);`
- [L396](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L396) `LeadIn = new HikariCheckBox(panel, -1, _("Lead-in"), wxDefaultPosition, wxSize(-1, -1));`
- [L399](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L399) `LeadOut = new HikariCheckBox(panel, -1, _("Lead-out"), wxDefaultPosition, wxSize(-1, -1));`
- [L412](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L412) `Continous = new HikariCheckBox(panel, -1, _("Enable"));`
- [L431](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L431) `SnapKF = new HikariCheckBox(panel, -1, _("Enable"));`
- [L551](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L551) `WhichLines->SetToolTip(_("The choice of lines to move"));`
- [L552](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L552) `AddStyles->SetToolTip(_("Select a style from the list"));`
- [L553](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L553) `Stylestext->SetToolTip(_("Move according to the following styles (separated by a coma)"));`
- [L555](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L555) `TimeText->SetToolTip(_("Time shifts"));`
- [L556](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L556) `MoveToVideoTime->SetToolTip(_(L"Move the selected line \nto the time video ± time shift"));`
- [L557](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L557) `MoveToAudioTime->SetToolTip(_(L"Move the selected lines to\nmark time audio ± time shift"));`
- [L558](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L558) `StartVAtime->SetToolTip(_("Move the start time to the time video / audio"));`
- [L559](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L559) `EndVAtime->SetToolTip(_("Moves the end time to the video/audio time"));`
- [L560](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L560) `Forward->SetToolTip(_("Delays subtitles"));`
- [L561](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L561) `Backward->SetToolTip(_("Speeds up subtitles"));`
- [L562](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L562) `DisplayFrames->SetToolTip(_("Shift times by time / frames"));`
- [L563](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L563) `MoveTagTimes->SetToolTip(_("Shifts \\move, \\t, and \\fad tag times\nso their positions on video do not change\n(slows down time shifting)"));`
- [L564](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L564) `WhichTimes->SetToolTip(_("The choice of times to move"));`
- [L565](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L565) `EndTimeCorrection->SetToolTip(_("Correction end times, when they are improper or overlap each other"));`
- [L568](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L568) `LeadIn->SetToolTip(_("Inserts introduction to the start time (good when using fad)"));`
- [L569](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L569) `LITime->SetToolTip(_("Introduction time in milliseconds"));`
- [L570](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L570) `LeadOut->SetToolTip(_("Inserts introduction to the end time (good when using fad)"));`
- [L571](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L571) `LOTime->SetToolTip(_("End time in milliseconds"));`
- [L572](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L572) `ThresStart->SetToolTip(_("Start time extension threshold"));`
- [L573](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L573) `ThresEnd->SetToolTip(_("End time extension threshold"));`
- [L574](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L574) `BeforeStart->SetToolTip(_("The maximum shift to a keyframe \nbefore the start time in milliseconds"));`
- [L575](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L575) `AfterStart->SetToolTip(_("The maximum shift to a keyframe \nafter the start time in milliseconds"));`
- [L576](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L576) `BeforeEnd->SetToolTip(_("The maximum shift to a keyframe \nbefore the end time in milliseconds"));`
- [L577](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L577) `AfterEnd->SetToolTip(_("The maximum shift to a keyframe \nafter the end time in milliseconds"));`

### ShiftTimes.h

- [L34](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.h#L34) `class ShiftTimes: public HikariPanel`

### SpellChecker.cpp

Configuration/color symbols: `CALC_SPACES_AND_PUNCTATION_FOR_CPS`, `CALC_SPACES_AND_PUNCTATION_FOR_WRAPS`, `DICTIONARY_LANGUAGE`, `SPELLCHECKER_ON`.


### SpellCheckerDialog.cpp

- [L44](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SpellCheckerDialog.cpp#L44) `ignoreComments = new HikariCheckBox(this, -1, _("Ignore comments"));`
- [L45](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SpellCheckerDialog.cpp#L45) `ignoreUpper = new HikariCheckBox(this, -1, _("Ignore words written entirely\nin uppercase"));`
- [L56](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SpellCheckerDialog.cpp#L56) `replace = new MappedButton(this, ID_REPLACE, _("Replace"));`
- [L57](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SpellCheckerDialog.cpp#L57) `replaceAll = new MappedButton(this, ID_REPLACE_ALL, _("Replace all"));`
- [L58](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SpellCheckerDialog.cpp#L58) `ignore = new MappedButton(this, ID_IGNORE, _("Ignore"));`
- [L59](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SpellCheckerDialog.cpp#L59) `ignoreAll = new MappedButton(this, ID_IGNORE_ALL, _("Ignore All"));`
- [L60](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SpellCheckerDialog.cpp#L60) `addWord = new MappedButton(this, ID_ADD_WORD, _("Add to dictionary"));`
- [L61](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SpellCheckerDialog.cpp#L61) `removeWord = new MappedButton(this, ID_REMOVE_WORD, _("Remove from dictionary"));`
- [L62](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SpellCheckerDialog.cpp#L62) `removeWord->SetToolTip(_("Remove from dictionary words added by user."));`
- [L64](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SpellCheckerDialog.cpp#L64) `close = new MappedButton(this, ID_CLOSE_DIALOG, _("Close"));`

### SpellCheckerDialog.h

- [L28](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SpellCheckerDialog.h#L28) `class SpellCheckerDialog : public HikariDialog`

### StyleChange.cpp

Configuration/color symbols: `COLORPICKER_SWITCH_CLICKS`, `STYLE_EDIT_FILTER_TEXT`, `STYLE_EDIT_FILTER_TEXT_ON`, `WINDOW_BACKGROUND`, `WINDOW_TEXT`.

- [L46](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L46) `SCD = new HikariDialog(parent->GetParent(), -1, _("Style editing"), pos, wxDefaultSize, wxRESIZE_BORDER);`
- [L76](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L76) `styleFont = new HikariChoice(this, ID_FONTNAME, emptyString, wxDefaultPosition, wxDefaultSize, wxArrayString(), HIKARI_FONT_LIST);`
- [L79](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L79) `fontCatalog = new HikariChoice(this, ID_FONT_CATALOG_LIST, wxDefaultPosition, wxDefaultSize, *FCManagement.GetCatalogNames());`
- [L83](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L83) `CatalogAdd = new MappedButton(this, ID_CATALOG_ADD, _("Add"));`
- [L84](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L84) `CatalogAdd->SetToolTip(_("Adds fonts to a previously created catalog"));`
- [L85](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L85) `CatalogManage = new MappedButton(this, ID_CATALOG_MANAGE, _("Manage"));`
- [L86](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L86) `CatalogManage->SetToolTip(_("Allows managing font catalogs"));`
- [L87](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L87) `Filter = new ToggleButton(this, ID_FILTER, _("Filter"));`
- [L88](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L88) `Filter->SetToolTip(_("Filters fonts to those containing the entered characters"));`
- [L120](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L120) `textBold = new HikariCheckBox(this, ID_CBOLD, _("Bold"));`
- [L121](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L121) `textItalic = new HikariCheckBox(this, ID_CBOLD, _("Italic"));`
- [L122](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L122) `textUnderline = new HikariCheckBox(this, ID_CBOLD, _("Underline"));`
- [L123](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L123) `textStrikeout = new HikariCheckBox(this, ID_CBOLD, _("Strikethrough"));`
- [L145](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L145) `color1 = new MappedButton(this, ID_BCOLOR1, _("First"));`
- [L147](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L147) `color2 = new MappedButton(this, ID_BCOLOR2, _("Second"));`
- [L149](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L149) `color3 = new MappedButton(this, ID_BCOLOR3, _("Border"));`
- [L151](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L151) `color4 = new MappedButton(this, ID_BCOLOR4, _("Shadow"));`
- [L191](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L191) `borderStyle = new HikariCheckBox(this, ID_CBOLD, _("Opaque box"));`
- [L261](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L261) `textEncoding = new HikariChoice(this, ID_CENCODING, wxDefaultPosition, wxDefaultSize, encs);`
- [L269](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L269) `btnOk = new MappedButton(this, ID_BOK, L"Ok");`
- [L270](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L270) `btnCommit = new MappedButton(this, ID_B_COMMIT, _("Apply"));`
- [L271](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L271) `btnCancel = new MappedButton(this, ID_BCANCEL, _("Cancel"));`
- [L653](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L653) `styleFont->Append(font);`
- [L692](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L692) `styleName->SetToolTip(_("Style name"));`
- [L693](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L693) `styleFont->SetToolTip(_("Font"));`
- [L694](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L694) `fontSize->SetToolTip(_("Font size"));`
- [L695](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L695) `textBold->SetToolTip(_("Bold"));`
- [L696](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L696) `textItalic->SetToolTip(_("Italic"));`
- [L697](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L697) `textUnderline->SetToolTip(_("Underline"));`
- [L698](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L698) `textStrikeout->SetToolTip(_("Strikethrough"));`
- [L699](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L699) `color1->SetToolTip(_("Primary color"));`
- [L700](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L700) `color2->SetToolTip(_("Secondary color for karaoke"));`
- [L701](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L701) `color3->SetToolTip(_("Border color"));`
- [L702](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L702) `color4->SetToolTip(_("Shadow color"));`
- [L703](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L703) `alpha1->SetToolTip(_("First color transparency, 0 - none, 255 - transparent"));`
- [L704](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L704) `alpha2->SetToolTip(_("Second color transparency, 0 - none, 255 - transparent"));`
- [L705](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L705) `alpha3->SetToolTip(_("Border transparency, 0 - none, 255 - transparent"));`
- [L706](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L706) `alpha4->SetToolTip(_("Shadow transparency, 0 - none, 255 - transparent"));`
- [L707](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L707) `outline->SetToolTip(_("Border size in pixels"));`
- [L708](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L708) `shadow->SetToolTip(_("Shadow size in pixels"));`
- [L709](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L709) `scaleX->SetToolTip(_("Scale X in %"));`
- [L710](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L710) `scaleY->SetToolTip(_("Scale Y in %"));`
- [L711](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L711) `angle->SetToolTip(_("Rotation in degrees"));`
- [L712](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L712) `spacing->SetToolTip(_("Spacing in pixels (negative values permitted)"));`
- [L713](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L713) `borderStyle->SetToolTip(_("Rectangular border"));`
- [L714](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L714) `rightMargin->SetToolTip(_("Right margin"));`
- [L715](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L715) `leftMargin->SetToolTip(_("Left margin"));`
- [L716](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L716) `verticalMargin->SetToolTip(_("Vertical margin"));`
- [L717](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L717) `alignment1->SetToolTip(_("Bottom left"));`
- [L718](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L718) `alignment2->SetToolTip(_("Bottom center"));`
- [L719](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L719) `alignment3->SetToolTip(_("Bottom right"));`
- [L720](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L720) `alignment4->SetToolTip(_("Center left"));`
- [L721](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L721) `alignment5->SetToolTip(_("Center"));`
- [L722](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L722) `alignment6->SetToolTip(_("Center right"));`
- [L723](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L723) `alignment7->SetToolTip(_("Top left"));`
- [L724](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L724) `alignment8->SetToolTip(_("Top center"));`
- [L725](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L725) `alignment9->SetToolTip(_("Top right"));`
- [L726](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L726) `textEncoding->SetToolTip(_("Text encoding"));`

### StyleChange.h

- [L31](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.h#L31) `class StyleChange : public wxWindow`

### StyleList.cpp

Configuration/color symbols: `STATICLIST_BACKGROUND`, `STATICLIST_BORDER`, `STATICLIST_SELECTION`, `TEXT_FIELD_BORDER_ON_FOCUS`, `WINDOW_TEXT`, `WINDOW_WARNING_ELEMENTS`.


### StyleList.h

- [L26](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleList.h#L26) `class StyleList : public HikariScrolledWindow`

### Stylelistbox.cpp

- [L33](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Stylelistbox.cpp#L33) `OK = new MappedButton(this, wxID_OK, L"OK");`
- [L34](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Stylelistbox.cpp#L34) `Cancel = new MappedButton(this, wxID_CANCEL, _("Cancel"));`
- [L57](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Stylelistbox.cpp#L57) `OK = new MappedButton(this, wxID_OK, L"OK");`
- [L58](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Stylelistbox.cpp#L58) `Cancel = new MappedButton(this, wxID_CANCEL, _("Cancel"));`
- [L111](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Stylelistbox.cpp#L111) `OK = new MappedButton(this, wxID_OK, L"Ok");`
- [L112](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Stylelistbox.cpp#L112) `Cancel = new MappedButton(this, wxID_CANCEL, _("Cancel"));`
- [L146](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Stylelistbox.cpp#L146) `MappedButton *OK = new MappedButton(this, 8888, L"OK");`
- [L147](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Stylelistbox.cpp#L147) `MappedButton *Cancel = new MappedButton(this, wxID_CANCEL, _("Cancel"));`

### Stylelistbox.h

- [L23](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Stylelistbox.h#L23) `class Stylelistbox: public HikariDialog`
- [L40](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Stylelistbox.h#L40) `class CustomCheckListBox : public HikariDialog`
- [L58](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Stylelistbox.h#L58) `class HikariListBox : public HikariDialog`

### StylePreview.cpp

Configuration/color symbols: `STYLE_PREVIEW_COLOR1`, `STYLE_PREVIEW_COLOR2`, `STYLE_PREVIEW_TEXT`.


### StylePreview.h

- [L26](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StylePreview.h#L26) `class StylePreview : public wxWindow`

### stylestore.cpp

Configuration/color symbols: `STYLE_EDIT_FILTER_TEXT`, `STYLE_EDIT_FILTER_TEXT_ON`, `STYLE_MANAGER_DETACH_EDIT_WINDOW`, `STYLE_MANAGER_POSITION`, `WINDOW_BACKGROUND`, `WINDOW_TEXT`.

- [L69](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L69) `catalogList = new HikariChoice(this, ID_CATALOG, wxDefaultPosition, wxDefaultSize, Options.dirs);`
- [L72](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L72) `newCatalog = new MappedButton(this, ID_NEWCAT, _("New"));`
- [L73](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L73) `MappedButton *deleteCatalog = new MappedButton(this, ID_DELCAT, _("Delete"));`
- [L74](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L74) `deleteCatalog->SetToolTip(_("Delete selected style catalog"));`
- [L87](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L87) `storeNew = new MappedButton(this, ID_STORENEW, _("New"));`
- [L88](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L88) `storeCopy = new MappedButton(this, ID_STORECOPY, _("Copy"));`
- [L89](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L89) `storeEdit = new MappedButton(this, ID_STOREEDIT, _("Edit"));`
- [L90](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L90) `storeLoad = new MappedButton(this, ID_STORELOAD, _("Load"));`
- [L91](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L91) `storeDelete = new MappedButton(this, ID_STOREDEL, _("Delete"));`
- [L92](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L92) `storeSort = new MappedButton(this, ID_STORESORT, _("Sort"));`
- [L107](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L107) `MappedButton *storeMoveToStart = new MappedButton(this, ID_STORE_MOVE_TO_START, _("Move selected styles to beginning"), arrowUpDouble, wxDefaultPosition, wxSize(fw, fh), -1);`
- [L108](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L108) `MappedButton *storeMoveUp = new MappedButton(this, ID_STORE_MOVE_UP, _("Move selected styles up"), arrowUp, wxDefaultPosition, wxSize(fw, fh), -1);`
- [L109](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L109) `MappedButton *storeMoveDown = new MappedButton(this, ID_STORE_MOVE_DOWN, _("Move selected styles down"), arrowDown, wxDefaultPosition, wxSize(fw, fh), -1);`
- [L110](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L110) `MappedButton *storeMoveToEnd = new MappedButton(this, ID_STORE_MOVE_TO_END, _("Move selected styles to end"), arrowDownDouble, wxDefaultPosition, wxSize(fw, fh), -1);`
- [L125](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L125) `addToStore = new MappedButton(this, ID_ADDTOSTORE, _("Add to storage"), arrowUp, wxDefaultPosition, wxDefaultSize, -1, 0, _("Add to storage"));`
- [L126](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L126) `addToAss = new MappedButton(this, ID_ADDTOASS, _("Add to ASS"), arrowDown, wxDefaultPosition, wxDefaultSize, -1, 0, _("Add to ASS"));`
- [L127](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L127) `MappedButton* addToAllAss = new MappedButton(this, ID_ADD_TO_ALL_ASS, _("Add to all open ASS files"), arrowDownDouble, wxDefaultPosition, wxDefaultSize, -1, 0, _("Add to all open ASS files"));`
- [L140](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L140) `assNew = new MappedButton(this, ID_ASSNEW, _("New"));`
- [L141](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L141) `assCopy = new MappedButton(this, ID_ASSCOPY, _("Copy"));`
- [L142](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L142) `assEdit = new MappedButton(this, ID_ASSEDIT, _("Edit"));`
- [L143](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L143) `assLoad = new MappedButton(this, ID_ASSLOAD, _("Load"));`
- [L144](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L144) `assDelete = new MappedButton(this, ID_ASSDEL, _("Delete"));`
- [L145](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L145) `assSort = new MappedButton(this, ID_ASSSORT, _("Sort"));`
- [L146](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L146) `SClean = new MappedButton(this, ID_ASSCLEAN, _("Clear"));`
- [L156](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L156) `MappedButton *ASSMoveToStart = new MappedButton(this, ID_ASS_MOVE_TO_START, _("Move selected styles to beginning"), arrowUpDouble, wxDefaultPosition, wxSize(fw, fh), -1);`
- [L157](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L157) `MappedButton *ASSMoveUp = new MappedButton(this, ID_ASS_MOVE_UP, _("Move selected styles up"), arrowUp, wxDefaultPosition, wxSize(fw, fh), -1);`
- [L158](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L158) `MappedButton *ASSMoveDown = new MappedButton(this, ID_ASS_MOVE_DOWN, _("Move selected styles down"), arrowDown, wxDefaultPosition, wxSize(fw, fh), -1);`
- [L159](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L159) `MappedButton *ASSMoveToEnd = new MappedButton(this, ID_ASS_MOVE_TO_END, _("Move selected styles to end"), arrowDownDouble, wxDefaultPosition, wxSize(fw, fh), -1);`
- [L172](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L172) `close = new MappedButton(this, ID_CLOSE_STYLE_MANAGER, _("Close"));`
- [L173](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L173) `detachEnable = new ToggleButton(this, ID_DETACH, _("Detach editing window"));`
- [L552](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L552) `catalogList->SetSelection(catalogList->Append(catalogName));`
- [L612](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L612) `wxFileDialog *openFileDialog = new wxFileDialog(this, _("Choose ASS file"), HikariPathDir(Notebook::GetTab()->SubsPath), L"*.ass", _("ASS subtitle files(*.ass)|*.ass"), wxFD_OPEN | wxFD_FILE_MUST_EXIST);`
- [L781](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L781) `catalogList->SetToolTip(_("Style catalog"));`
- [L782](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L782) `newCatalog->SetToolTip(_("New style catalog"));`
- [L783](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L783) `storeNew->SetToolTip(_("Create new style in storage"));`
- [L784](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L784) `storeEdit->SetToolTip(_("Edit selected storage style"));`
- [L785](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L785) `storeCopy->SetToolTip(_("Copy the style in storage"));`
- [L786](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L786) `storeLoad->SetToolTip(_("Load style from external ASS file to storage"));`
- [L787](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L787) `storeDelete->SetToolTip(_("Delete style from storage"));`
- [L788](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L788) `storeSort->SetToolTip(_("Sort styles in storage"));`
- [L789](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L789) `assNew->SetToolTip(_("Create new ASS style"));`
- [L790](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L790) `assEdit->SetToolTip(_("Edit selected ASS style"));`
- [L791](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L791) `assCopy->SetToolTip(_("Copy ASS style"));`
- [L792](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L792) `assLoad->SetToolTip(_("Load style from external ASS file"));`
- [L793](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L793) `assDelete->SetToolTip(_("Delete style from ASS"));`
- [L794](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L794) `assSort->SetToolTip(_("Sort styles in ASS"));`
- [L795](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L795) `addToStore->SetToolTip(_("Copy style from ASS to storage"));`
- [L796](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L796) `addToAss->SetToolTip(_("Copy style from storage to ASS"));`
- [L797](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L797) `SClean->SetToolTip(_("Delete unused ASS styles"));`

### stylestore.h

- [L28](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.h#L28) `class StyleStore : public HikariDialog`

### SubsDialogue.cpp

Configuration/color symbols: `CONVERT_STYLE`.


### SubsFile.cpp

- [L37](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsFile.cpp#L37) `MappedButton *Set = new MappedButton(this, ID_SET_HISTORY, _("Set"));`
- [L41](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsFile.cpp#L41) `MappedButton *Ok = new MappedButton(this, ID_SET_HISTORY_AND_CLOSE, L"OK");`
- [L46](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsFile.cpp#L46) `MappedButton *Cancel = new MappedButton(this, wxID_CANCEL, _("Cancel"));`

### SubsFile.h

- [L251](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsFile.h#L251) `class HistoryDialog : public HikariDialog`

### SubsGrid.cpp

Configuration/color symbols: `ASS_PROPERTIES_ASK_FOR_CHANGE`, `ASS_PROPERTIES_EDITING`, `ASS_PROPERTIES_EDITING_ON`, `ASS_PROPERTIES_SCRIPT`, `ASS_PROPERTIES_SCRIPT_ON`, `ASS_PROPERTIES_TIMING`, `ASS_PROPERTIES_TIMING_ON`, `ASS_PROPERTIES_TITLE`, `ASS_PROPERTIES_TITLE_ON`, `ASS_PROPERTIES_TRANSLATION`, `ASS_PROPERTIES_TRANSLATION_ON`, `ASS_PROPERTIES_UPDATE`, `ASS_PROPERTIES_UPDATE_ON`, `COPY_COLLUMS_SELECTIONS`, `DONT_ASK_FOR_BAD_RESOLUTION`, `GRID_ADD_TO_FILTER`, `GRID_DUPLICATION_DONT_CHANGE_SELECTION`, `GRID_FILTER_AFTER_LOAD`, `GRID_FILTER_BY`, `GRID_FILTER_INVERTED`, `GRID_FILTER_STYLES`, `GRID_HIDE_COLUMNS`, `GRID_HIDE_TAGS`, `GRID_IGNORE_FILTERING`, `PASTE_COLUMNS_SELECTION`, `SUBS_COMPARISON_TYPE`.

- [L219](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L219) `MenuItem * styleItem = stylesMenu->Append(ID_FILTERING_STYLES, (*styles)[i]->Name, emptyString, true, nullptr, nullptr, ITEM_CHECK);`
- [L249](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L249) `menu->Append(4442, _("Insert lines"), insertMenu);`
- [L264](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L264) `menu->Append(4443, _("Split lines"), splitMenu);`
- [L271](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L271) `menu->Append(4444, _("Hide columns"), hidemenu);`
- [L273](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L273) `menu->Append(4445, _("Filtering"), filterMenu);`
- [L315](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L315) `menu->Append(6789, _("Add lines"))->Enable(sels > 0);`
- [L316](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L316) `menu->Append(6790, _("Copy tree"));`
- [L317](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L317) `menu->Append(6791, _("Change description"));`
- [L318](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L318) `menu->Append(6793, _("Select tree lines"));`
- [L319](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L319) `menu->Append(6792, _("Delete"));`
- [L939](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L939) `static TLDialog *tld = new TLDialog(this, this);`
- [L947](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L947) `wxFileDialog *FileDialog1 = new wxFileDialog(this, _("Choose subtitle file"), HikariPathDir(tab->SubsPath), emptyString, _("Subtitle files (*.ass),(*.srt),(*.sub),(*.txt)|*.ass;*.srt;*.sub;*.txt"),`

### SubsGrid.h

- [L83](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.h#L83) `class SubsGrid : public HikariScrolledWindow`

### SubsGridBase.cpp

Configuration/color symbols: `AUTOSAVE_MAX_FILES`, `CONVERT_ASS_TAGS_TO_INSERT_IN_LINE`, `CONVERT_FPS`, `CONVERT_FPS_FROM_VIDEO`, `CONVERT_NEW_END_TIMES`, `CONVERT_RESOLUTION_HEIGHT`, `CONVERT_RESOLUTION_WIDTH`, `CONVERT_SHOW_SETTINGS`, `CONVERT_TIME_PER_CHARACTER`, `GRID_DONT_CENTER_ACTIVE_LINE`, `GRID_FILTER_AFTER_LOAD`, `GRID_FILTER_BY`, `GRID_LOAD_SORTED_SUBS`, `GRID_SAVE_AFTER_CHARACTER_COUNT`, `LINK_RESOLUTIONS`, `POSTPROCESSOR_KEYFRAME_AFTER_END`, `POSTPROCESSOR_KEYFRAME_AFTER_START`, `POSTPROCESSOR_KEYFRAME_BEFORE_END`, `POSTPROCESSOR_KEYFRAME_BEFORE_START`, `POSTPROCESSOR_LEAD_IN`, `POSTPROCESSOR_LEAD_OUT`, `POSTPROCESSOR_ON`, `POSTPROCESSOR_THRESHOLD_END`, `POSTPROCESSOR_THRESHOLD_START`, `SHIFT_TIMES_CORRECT_END_TIMES`, `SHIFT_TIMES_DISPLAY_FRAMES`, `SHIFT_TIMES_OPTIONS`, `SHIFT_TIMES_STYLES`, `SHIFT_TIMES_TIME`, `SHIFT_TIMES_WHICH_LINES`, `SHIFT_TIMES_WHICH_TIMES`, `SUBS_COMPARISON_TYPE`, `TL_MODE_HIDE_ORIGINAL_ON_VIDEO`, `TL_MODE_SHOW_ORIGINAL`.


### SubsGridDialogs.cpp

Configuration/color symbols: `ASS_PROPERTIES_ASK_FOR_CHANGE`, `ASS_PROPERTIES_EDITING_ON`, `ASS_PROPERTIES_SCRIPT_ON`, `ASS_PROPERTIES_TIMING_ON`, `ASS_PROPERTIES_TITLE_ON`, `ASS_PROPERTIES_TRANSLATION_ON`, `ASS_PROPERTIES_UPDATE_ON`.

- [L45](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridDialogs.cpp#L45) `oldfps = new HikariChoice(this, -1, emptyString, wxDefaultPosition, wxDefaultSize, fpsy, 0, valid);`
- [L47](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridDialogs.cpp#L47) `newfps = new HikariChoice(this, -1, emptyString, wxDefaultPosition, wxSize(80, -1), fpsy, 0, valid);`
- [L53](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridDialogs.cpp#L53) `MappedButton *ok = new MappedButton(this, 15555, _("Change FPS"));`
- [L55](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridDialogs.cpp#L55) `MappedButton *cancel = new MappedButton(this, wxID_CANCEL, _("Cancel"));`
- [L84](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridDialogs.cpp#L84) `MappedButton *ok = new MappedButton(this, 15555, currentName.empty() ? _("Set tree name") : _("Change tree name"));`
- [L86](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridDialogs.cpp#L86) `MappedButton *cancel = new MappedButton(this, wxID_CANCEL, _("Cancel"));`
- [L117](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridDialogs.cpp#L117) `fields[i] = new HikariCheckBox(this, -1, fieldNames[i]);`
- [L122](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridDialogs.cpp#L122) `MappedButton* Ok = new MappedButton(this, wxID_OK, L"OK");`
- [L123](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridDialogs.cpp#L123) `MappedButton* Cancel = new MappedButton(this, wxID_CANCEL, _("Cancel"));`
- [L124](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridDialogs.cpp#L124) `MappedButton* TurnOf = new MappedButton(this, 19921, _("Disable confirmation"));`

### SubsGridDialogs.h

- [L23](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridDialogs.h#L23) `class FPSDialog : public HikariDialog`
- [L34](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridDialogs.h#L34) `class TreeDialog : public HikariDialog`
- [L46](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridDialogs.h#L46) `class SwapPropertiesDialog :public HikariDialog`

### SubsGridFiltering.cpp

Configuration/color symbols: `GRID_ADD_TO_FILTER`, `GRID_FILTER_BY`, `GRID_FILTER_INVERTED`, `GRID_FILTER_STYLES`.


### SubsGridPreview.cpp

Configuration/color symbols: `GRID_ACTIVE_LINE`, `GRID_BACKGROUND`, `GRID_CHANGE_ACTIVE_ON_SELECTION`, `GRID_COLLISIONS`, `GRID_COMMENT`, `GRID_COMPARISON_BACKGROUND_MATCH`, `GRID_COMPARISON_BACKGROUND_NOT_MATCH`, `GRID_COMPARISON_COMMENT_BACKGROUND_MATCH`, `GRID_COMPARISON_COMMENT_BACKGROUND_NOT_MATCH`, `GRID_COMPARISON_OUTLINE`, `GRID_DIALOGUE`, `GRID_HEADER`, `GRID_HEADER_TEXT`, `GRID_LABEL_DOUBTFUL`, `GRID_LABEL_MODIFIED`, `GRID_LABEL_NORMAL`, `GRID_LABEL_SAVED`, `GRID_LINE_VISIBLE_ON_VIDEO`, `GRID_LINES`, `GRID_SELECTION`, `GRID_SPELLCHECKER`, `GRID_TAGS_SWAP_CHARACTER`, `GRID_TEXT`, `SPELLCHECKER_ON`, `WINDOW_BORDER`, `WINDOW_BORDER_BACKGROUND`, `WINDOW_BORDER_BACKGROUND_INACTIVE`, `WINDOW_BORDER_INACTIVE`, `WINDOW_HEADER_TEXT`, `WINDOW_HOVER_CLOSE_BUTTON`, `WINDOW_PUSHED_CLOSE_BUTTON`.

- [L947](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridPreview.cpp#L947) `MenuItem * Item = menu->Append(4880 + i, name, emptyString, true, nullptr, nullptr, (lastData == previewData[i]) ? ITEM_RADIO : ITEM_NORMAL);`

### SubsGridPreview.h

- [L43](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridPreview.h#L43) `class SubsGridPreview : public wxWindow`

### SubsGridWindow.cpp

Configuration/color symbols: `EDITOR_BORDER_ON_FOCUS`, `GRID_ACTIVE_LINE`, `GRID_BACKGROUND`, `GRID_CHANGE_ACTIVE_ON_SELECTION`, `GRID_COLLISIONS`, `GRID_COMMENT`, `GRID_COMPARISON_BACKGROUND_MATCH`, `GRID_COMPARISON_BACKGROUND_NOT_MATCH`, `GRID_COMPARISON_COMMENT_BACKGROUND_MATCH`, `GRID_COMPARISON_COMMENT_BACKGROUND_NOT_MATCH`, `GRID_COMPARISON_OUTLINE`, `GRID_DIALOGUE`, `GRID_FONT`, `GRID_FONT_SIZE`, `GRID_HEADER`, `GRID_HEADER_TEXT`, `GRID_HIDE_TAGS`, `GRID_LABEL_DOUBTFUL`, `GRID_LABEL_MODIFIED`, `GRID_LABEL_NORMAL`, `GRID_LABEL_SAVED`, `GRID_LINE_VISIBLE_ON_VIDEO`, `GRID_LINES`, `GRID_SELECTION`, `GRID_SPELLCHECKER`, `GRID_TAGS_SWAP_CHARACTER`, `GRID_TEXT`, `SPELLCHECKER_ON`.


### SubsResampleDialog.cpp

Configuration/color symbols: `DONT_ASK_FOR_BAD_RESOLUTION`.

- [L37](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsResampleDialog.cpp#L37) `MappedButton *fromSubs = new MappedButton(this, 26547, _("From subtitles"));`
- [L42](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsResampleDialog.cpp#L42) `subsMatrix = new HikariChoice(this, -1 , wxDefaultPosition, wxSize(160,-1), 8, matrices);`
- [L55](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsResampleDialog.cpp#L55) `MappedButton *fromVideo = new MappedButton(this, 26548, _("Get from video"));`
- [L59](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsResampleDialog.cpp#L59) `destinedMatrix = new HikariChoice(this, -1 , wxDefaultPosition, wxSize(160,-1), 8, matrices);`
- [L73](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsResampleDialog.cpp#L73) `resamplingOptions = new HikariRadioBox(this, -1, _("Resample options"), wxDefaultPosition, wxDefaultSize, options);`
- [L133](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsResampleDialog.cpp#L133) `MappedButton *OK = new MappedButton(this, 6548, L"OK");`
- [L158](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsResampleDialog.cpp#L158) `MappedButton *Cancel = new MappedButton(this, wxID_CANCEL, _("Cancel"));`
- [L183](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsResampleDialog.cpp#L183) `resamplingOptions = new HikariRadioBox(this, -1, _("Resample options"), wxDefaultPosition, wxSize(160, -1), options);`
- [L185](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsResampleDialog.cpp#L185) `MappedButton *OK = new MappedButton(this, 26548, _("Change"));`
- [L190](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsResampleDialog.cpp#L190) `MappedButton *Cancel = new MappedButton(this, wxID_CANCEL, _("Do not change"));`
- [L191](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsResampleDialog.cpp#L191) `MappedButton *TurnOff = new MappedButton(this, 26549, _("Disable warning"));`

### SubsResampleDialog.h

- [L25](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsResampleDialog.h#L25) `class SubsResampleDialog : public HikariDialog`
- [L41](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsResampleDialog.h#L41) `class SubsMismatchResolutionDialog : public HikariDialog`

### SubsTime.cpp

Configuration/color symbols: `CONVERT_FPS`.


### SubtitlesProviderManager.cpp

Configuration/color symbols: `VSFILTER_INSTANCE`.


### SubtitlesVSFilter.cpp

Configuration/color symbols: `VSFILTER_INSTANCE`.


### TabPanel.cpp

Configuration/color symbols: `EDITBOX_TAG_BUTTONS`, `SHIFT_TIMES_ON`, `VIDEO_WINDOW_SIZE`, `WINDOW_BACKGROUND`.


### TabPanel.h

- [L32](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/TabPanel.h#L32) `class TabPanel : public HikariPanel`

### TagFindReplace.cpp

- [L427](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/TagFindReplace.cpp#L427) `text->Append(L"{" + replaceTxt + L"}");`

### TextEditorTagList.cpp

Configuration/color symbols: `MENU_BACKGROUND_SELECTION`, `MENU_BORDER_SELECTION`, `MENUBAR_BACKGROUND`, `TEXT_EDITOR_TAG_LIST_OPTIONS`, `WINDOW_BORDER`, `WINDOW_TEXT`.

- [L133](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/TextEditorTagList.cpp#L133) `listMenu.Append(ID_SHOW_DESCRIPTION, _("Show description"), nullptr, emptyString, ITEM_CHECK_AND_HIDE)->Check((options & SHOW_DESCRIPTION) != 0);`
- [L134](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/TextEditorTagList.cpp#L134) `listMenu.Append(ID_SHOW_ALL_TAGS, _("Show all tags"), nullptr, emptyString, ITEM_CHECK_AND_HIDE)->Check((options & TYPE_TAG_USED_IN_VISUAL) != 0);`
- [L135](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/TextEditorTagList.cpp#L135) `listMenu.Append(ID_SHOW_VSFILTER_MOD_TAGS, _("Show VSFiltermod tags"), nullptr, emptyString, ITEM_CHECK_AND_HIDE)->Check((options & TYPE_TAG_VSFILTER_MOD) != 0);`

### TextEditorTagList.h

- [L77](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/TextEditorTagList.h#L77) `class PopupWindow : public wxPopupWindow{`

### TimeCtrl.cpp

Configuration/color symbols: `TEXT_FIELD_ALLOW_NUMPAD_HOTKEYS`, `WINDOW_WARNING_ELEMENTS`.


### TimeCtrl.h

- [L25](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/TimeCtrl.h#L25) `class TimeCtrl : public HikariTextCtrl`

### TLDialog.cpp

- [L31](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/TLDialog.cpp#L31) `Up = new MappedButton(this, 29995, _("Delete line"));`
- [L32](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/TLDialog.cpp#L32) `Up->SetToolTip(_("Deletes the selected line.\nMoves the translation one line up."));`
- [L33](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/TLDialog.cpp#L33) `Down = new MappedButton(this, 29997, _("Add line"));`
- [L34](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/TLDialog.cpp#L34) `Down->SetToolTip(_("Adds a blank line before the selected line.\nMoves the translation one line down."));`
- [L35](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/TLDialog.cpp#L35) `UpJoin = new MappedButton(this, 29998, _("Join lines"));`
- [L36](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/TLDialog.cpp#L36) `UpJoin->SetToolTip(_("Joins the selected line with the next line.\nMoves the translation one line up."));`
- [L37](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/TLDialog.cpp#L37) `DownJoin = new MappedButton(this, 29996, _("Join lines"));`
- [L38](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/TLDialog.cpp#L38) `DownJoin->SetToolTip(_("Joins the selected line with the next line.\nMoves the original one line up."));`
- [L39](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/TLDialog.cpp#L39) `DownDel = new MappedButton(this, 29994, _("Delete line"));`
- [L40](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/TLDialog.cpp#L40) `DownDel->SetToolTip(_("Deletes the selected line.\nMoves the original one line up."));`
- [L41](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/TLDialog.cpp#L41) `UpExt = new MappedButton(this, 29993, _("Add line"));`
- [L42](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/TLDialog.cpp#L42) `UpExt->SetToolTip(_("Adds a blank line before the selected line.\nMoves the original one line down.\nThe added line must be timed."));`

### TLDialog.h

- [L23](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/TLDialog.h#L23) `class TLDialog  : public HikariDialog`

### Toolbar.cpp

Configuration/color symbols: `BUTTON_BACKGROUND_HOVER`, `BUTTON_BACKGROUND_PUSHED`, `BUTTON_BORDER_HOVER`, `BUTTON_BORDER_PUSHED`, `MENU_BACKGROUND_SELECTION`, `MENU_BORDER_SELECTION`, `MENUBAR_BACKGROUND`, `TOOLBAR_ALIGNMENT`, `TOOLBAR_IDS`, `WINDOW_BACKGROUND`, `WINDOW_BORDER`, `WINDOW_TEXT`.

- [L228](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Toolbar.cpp#L228) `shmenu.Append(itm->GetId(),itm->GetLabel(),itm->GetHelp())->Enable(itm->IsEnabled());`
- [L479](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Toolbar.cpp#L479) `alignments = new HikariChoice(this, 32213, wxPoint(4, 4), wxSize(size.x - 8, fh), 4, ans);`
- [L483](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Toolbar.cpp#L483) `alignments->SetToolTip(_("Toolbar alignment"));`
- [L631](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Toolbar.cpp#L631) `desc.Append(L")");`

### Toolbar.h

- [L63](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Toolbar.h#L63) `class HikariToolbar :public wxWindow`
- [L107](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Toolbar.h#L107) `class ToolbarMenu :public wxDialog`

### UpdateChecker.cpp

Configuration/color symbols: `UPDATER_AUTO_CHECK`, `UPDATER_CHECK_FOR_STABLE`, `UPDATER_NEXT_CHECK`.

- [L84](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/UpdateChecker.cpp#L84) `class UpdateAvailableDialog : public HikariDialog`
- [L109](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/UpdateChecker.cpp#L109) `autoCheck = new HikariCheckBox(this, -1, _("Check for updates automatically"));`
- [L113](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/UpdateChecker.cpp#L113) `stableOnly = new HikariCheckBox(this, -1, _("Stable versions only"));`
- [L120](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/UpdateChecker.cpp#L120) `MappedButton *open = new MappedButton(this, 20001, _("Open the download page"));`
- [L121](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/UpdateChecker.cpp#L121) `MappedButton *later = new MappedButton(this, 20002, _("Remind me in a week"));`
- [L122](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/UpdateChecker.cpp#L122) `MappedButton *close = new MappedButton(this, wxID_CLOSE, _("Close"));`

### VideoBox.cpp

Configuration/color symbols: `ACCEPTED_AUDIO_STREAM`, `DONT_ASK_FOR_BAD_RESOLUTION`, `EDITBOX_TIMES_TO_FRAMES_SWITCH`, `EDITOR_ON`, `GRID_SET_VISIBLE_LINE_AFTER_FULL_SCREEN`, `OPEN_VIDEO_AT_ACTIVE_LINE`, `VIDEO_PAUSE_ON_CLICK`, `VIDEO_PROGRESS_BAR`, `VIDEO_VOLUME`, `VIDEO_WINDOW_SIZE`, `WINDOW_BACKGROUND`, `WINDOW_TEXT`, `WINDOW_WARNING_ELEMENTS`.

- [L95](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoBox.cpp#L95) `class AspectRatioDialog : public HikariDialog`
- [L933](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoBox.cpp#L933) `menu->Append(VIDEO_PLAY_PAUSE, txt)->Enable(GetState() != None);`
- [L947](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoBox.cpp#L947) `menu->Append(MENU_MONITORS + i, txt2)->Enable(GetState() != None);`
- [L957](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoBox.cpp#L957) `menu1->Append(30000 + i, HikariPathName(Hikari->subsrec[i]));`
- [L961](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoBox.cpp#L961) `menu2->Append(30020 + i, HikariPathName(Hikari->videorec[i]));`
- [L965](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoBox.cpp#L965) `menu->Append(ID_MRECSUBS, _("Recently opened subtitles"), menu1);`
- [L966](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoBox.cpp#L966) `menu->Append(ID_MRECVIDEO, _("Recently opened videos"), menu2);`
- [L989](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoBox.cpp#L989) `menu->Append(23456, _("Filters"), menu3, _("Shows used filters"));`
- [L1006](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoBox.cpp#L1006) `menu->Append(MENU_STREAMS + i, name, emptyString, true, 0, 0, (enable == L"1") ? ITEM_RADIO : ITEM_NORMAL);//->Check(enable=="1");`
- [L1014](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoBox.cpp#L1014) `menu->Append(MENU_CHAPTERS + j, renderer->m_Chapters[j].name + L"\t[" + timee.raw() + L"]", emptyString, true, 0, 0, (ntime > renderer->GetCurrentPosition()) ? ITEM_RADIO : ITEM_NORMAL);`
- [L1087](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoBox.cpp#L1087) `wxFileDialog* FileDialog2 = new wxFileDialog(m_IsFullscreen ? m_FullScreenWindow : (wxWindow *)Hikari, _("Choose video file"), (tab->SubsPath != emptyString) ? HikariPathDir(tab->SubsPath) : (Hikari->videorec.size() > 0) ? HikariPathDir(Hikari->videorec[Hikari->videorec.size() - 1]) : emptyString, emptyString, _("Video files(*.avi),(*.mkv),(*.mp4),(*.ogm),(*.wmv),(*.asf),(*.rmvb),(*.rm),(*.3gp),(*.avs)|*.avi;*.mkv;*.mp4;*.ogm;*.wmv;*.asf;*.rmvb;*.rm;*.3gp;*.avs|All files (*.*)|*.*"),`
- [L1103](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoBox.cpp#L1103) `wxFileDialog* FileDialog = new wxFileDialog(m_IsFullscreen ? m_FullScreenWindow : (wxWindow *)Hikari, _("Choose subtitle file"), (tab->VideoPath != emptyString) ? HikariPathDir(tab->VideoPath) : (Hikari->subsrec.size() > 0) ? HikariPathDir(Hikari->subsrec[Hikari->subsrec.size() - 1]) : emptyString, emptyString, _("Subtitle files (*.ass),(*.sub),(*.txt)|*.ass;*.sub;*.txt"),`

### VideoBox.h

- [L44](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoBox.h#L44) `class VideoBox : public wxWindow`

### VideoFullscreen.cpp

Configuration/color symbols: `VIDEO_VOLUME`, `WINDOW_BACKGROUND`, `WINDOW_TEXT`.

- [L68](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoFullscreen.cpp#L68) `showToolbar = new HikariCheckBox(panel, 7777, _("Show toolbar"), wxPoint(180, toolBarHeight - 4), wxSize(-1, -1));`

### VideoFullscreen.h

- [L29](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoFullscreen.h#L29) `class Fullscreen : public wxFrame`

### VideoSlider.h

- [L23](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoSlider.h#L23) `class VideoSlider : public wxWindow`
- [L61](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoSlider.h#L61) `class VolSlider : public wxWindow`

### VideoToolbar.cpp

Configuration/color symbols: `BUTTON_BACKGROUND_HOVER`, `BUTTON_BACKGROUND_PUSHED`, `BUTTON_BORDER_HOVER`, `BUTTON_BORDER_PUSHED`, `MOVE_VIDEO_TO_ACTIVE_LINE`, `VIDEO_PLAY_AFTER_SELECTION`.

- [L135](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoToolbar.cpp#L135) `videoSeekAfter = new HikariChoice(this, ID_SEEK_AFTER, wxPoint(2, 1), wxDefaultSize, 6, movopts);`
- [L137](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoToolbar.cpp#L137) `videoSeekAfter->SetToolTip(_("Move video to selected line on:"));`
- [L139](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoToolbar.cpp#L139) `videoPlayAfter = new HikariChoice(this, ID_PLAY_AFTER, wxPoint(seekMinSize.GetWidth() + 2, 1), wxDefaultSize, 4, playopts);`
- [L141](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoToolbar.cpp#L141) `videoPlayAfter->SetToolTip(_("On moving to another line play:"));`
- [L371](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoToolbar.cpp#L371) `vt->SetToolTip(vt->icons[elem + startIconNumber]->help);`
- [L447](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoToolbar.cpp#L447) `vt->SetToolTip(tooltext);`
- [L511](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoToolbar.cpp#L511) `shapeList = new HikariChoice(vt, ID_SHAPE_LIST, wxDefaultPosition, wxDefaultSize, list);`
- [L513](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoToolbar.cpp#L513) `shapeList->SetToolTip(_("List of ASS drawings with edit option.\nAfter choose drawing from list just set cursor i n place\nof start of drawing and click left mouse button and drag."));`
- [L604](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoToolbar.cpp#L604) `vt->SetToolTip(vt->icons[elem + startIconNumber]->help);`
- [L745](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoToolbar.cpp#L745) `tagList = new HikariChoice(vtoolbar, ID_TAG_LIST, wxDefaultPosition, wxDefaultSize, list);`
- [L746](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoToolbar.cpp#L746) `tagList->SetToolTip(_("List of tags that can edit visual tool"));`
- [L751](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoToolbar.cpp#L751) `options = new HikariChoice(vtoolbar, ID_OPTIONS, wxDefaultPosition, wxSize(wsize.x + 26, -1), 8, optionsList);`
- [L752](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoToolbar.cpp#L752) `options->SetToolTip(_("Tag change options:\nAdd - changes all tags by adding the slider value.\n"\ "Insert - inserts the tag at the cursor position for one line,\n"\ "or at the start for multiple lines.\n"\ "Multiply - multiplies the slider value by the selected line number;\n"\`
- [L782](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoToolbar.cpp#L782) `edition = new MappedButton(vtoolbar, ID_EDITION, _("Edit"), _("Edit listed tags and create new ones"), wxDefaultPosition, wxDefaultSize, -1);`
- [L847](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoToolbar.cpp#L847) `vt->SetToolTip(vt->icons[elem + startIconNumber]->help);`
- [L926](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoToolbar.cpp#L926) `vt->SetToolTip(vt->icons[elem + startIconNumber]->help);`
- [L1019](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoToolbar.cpp#L1019) `vt->SetToolTip(vt->icons[elem + startIconNumber]->help);`
- [L1105](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoToolbar.cpp#L1105) `alignment = new HikariChoice(vt, ID_ALIGNMENT, wxDefaultPosition, wxDefaultSize, 21, alignments);`
- [L1113](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoToolbar.cpp#L1113) `alignment->SetToolTip(_("Text placing works similar like in styles"));`
- [L1149](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoToolbar.cpp#L1149) `vt->SetToolTip(vt->icons[elem + startIconNumber]->help);`
- [L1215](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoToolbar.cpp#L1215) `vt->SetToolTip(vt->icons[elem + startIconNumber]->help);`

### VideoToolbar.h

- [L280](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoToolbar.h#L280) `class VideoToolbar: public wxWindow {`

### VisualAllTagsEdition.cpp

- [L40](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VisualAllTagsEdition.cpp#L40) `tagList = new HikariChoice(this, ID_TAG_LIST, wxDefaultPosition, wxDefaultSize, list);`
- [L44](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VisualAllTagsEdition.cpp#L44) `MappedButton* addTag = new MappedButton(this, ID_BUTTON_ADD_TAG, _("Add tag"));`
- [L45](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VisualAllTagsEdition.cpp#L45) `MappedButton* removeTag = new MappedButton(this, ID_BUTTON_REMOVE_TAG, _("Delete tag"));`
- [L77](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VisualAllTagsEdition.cpp#L77) `mode = new HikariChoice(this, -1, wxDefaultPosition, wxDefaultSize, 3, modes);`
- [L93](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VisualAllTagsEdition.cpp#L93) `numOfValues = new HikariChoice(this, ID_ADDITIONAL_VALUES_LIST, wxDefaultPosition, wxDefaultSize, 4, valuesStr);`
- [L94](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VisualAllTagsEdition.cpp#L94) `numOfValues->SetToolTip(_("Used only when tag have 2 values or more"));`
- [L96](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VisualAllTagsEdition.cpp#L96) `tagInsertMode = new HikariChoice(this, ID_INSERT_MODES_LIST, wxDefaultPosition, wxDefaultSize, 8, insertModes);`
- [L97](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VisualAllTagsEdition.cpp#L97) `tagInsertMode->SetToolTip(_("Tag change options"));`
- [L103](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VisualAllTagsEdition.cpp#L103) `values[i]->SetToolTip(wxString::Format(_("Value %i"), i + 2));`
- [L126](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VisualAllTagsEdition.cpp#L126) `MappedButton* commit = new MappedButton(this, ID_BUTTON_COMMIT, _("Apply"));`
- [L127](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VisualAllTagsEdition.cpp#L127) `MappedButton* OK = new MappedButton(this, ID_BUTTON_OK, L"OK");`
- [L128](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VisualAllTagsEdition.cpp#L128) `MappedButton* cancel = new MappedButton(this, wxID_CANCEL, _("Cancel"));`
- [L129](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VisualAllTagsEdition.cpp#L129) `MappedButton* resetDefault = new MappedButton(this, ID_BUTTON_RESET_DEFAULT, _("Restore default"));`
- [L177](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VisualAllTagsEdition.cpp#L177) `tagList->Append(currentTag.name);`

### VisualAllTagsEdition.h

- [L82](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VisualAllTagsEdition.h#L82) `class AllTagsEdition : public HikariDialog`

### VisualClips.cpp

Configuration/color symbols: `TL_MODE_HIDE_ORIGINAL_ON_VIDEO`.


### VisualDrawingShapes.cpp

- [L57](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VisualDrawingShapes.cpp#L57) `shapeList = new HikariChoice(this, ID_SHAPE_LIST, wxDefaultPosition, wxDefaultSize, list);`
- [L61](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VisualDrawingShapes.cpp#L61) `MappedButton* addShape = new MappedButton(this, ID_BUTTON_ADD_SHAPE, _("Add shape"));`
- [L62](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VisualDrawingShapes.cpp#L62) `MappedButton* removeShape = new MappedButton(this, ID_BUTTON_REMOVE_SHAPE, _("Delete shape"));`
- [L77](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VisualDrawingShapes.cpp#L77) `mode = new HikariChoice(this, -1, wxDefaultPosition, wxDefaultSize, 3, modes);`
- [L80](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VisualDrawingShapes.cpp#L80) `scalingMode = new HikariChoice(this, -1, wxDefaultPosition, wxDefaultSize, 2, scalingModes);`
- [L91](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VisualDrawingShapes.cpp#L91) `MappedButton* getShapeFromLine = new MappedButton(this, ID_BUTTON_GET_SHAPE_FROM_LINE, _("Get shape from active line"));`
- [L102](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VisualDrawingShapes.cpp#L102) `MappedButton* commit = new MappedButton(this, ID_BUTTON_COMMIT, _("Apply"));`
- [L103](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VisualDrawingShapes.cpp#L103) `MappedButton* OK = new MappedButton(this, ID_BUTTON_OK, L"OK");`
- [L104](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VisualDrawingShapes.cpp#L104) `MappedButton* cancel = new MappedButton(this, wxID_CANCEL, _("Cancel"));`
- [L105](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VisualDrawingShapes.cpp#L105) `MappedButton* resetDefault = new MappedButton(this, ID_BUTTON_RESET_DEFAULT, _("Restore default"));`
- [L154](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VisualDrawingShapes.cpp#L154) `shapeList->Append(currentShape.name);`
- [L215](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VisualDrawingShapes.cpp#L215) `shapeList->Append(currentShape.name);`

### VisualDrawingShapes.h

- [L66](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VisualDrawingShapes.h#L66) `class ShapesEdition : public HikariDialog`

### VisualMoveAll.cpp

Configuration/color symbols: `TL_MODE_HIDE_ORIGINAL_ON_VIDEO`.


### VisualPosition.cpp

Configuration/color symbols: `TL_MODE_HIDE_ORIGINAL_ON_VIDEO`.


### Visuals.cpp

Configuration/color symbols: `TL_MODE_HIDE_ORIGINAL_ON_VIDEO`, `VIDEO_VISUAL_WARNINGS_OFF`.


### ZipEntryUtf8.h

- [L26](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ZipEntryUtf8.h#L26) `class Utf8ZipEntry : public wxZipEntry`
