# HikariSub settings files: key reference (CF1 #488)

Every file starts with `version = 1` (`"version": 1` in state.json) and holds only values that differ from the default. Edit them while HikariSub is closed.

## settings.toml

A setting `a.b.c` is the key `c` in table `[a.b]`.

| Table | Key | Type | Default | Legacy Config.txt name |
|---|---|---|---|---|
| `[appearance]` | `darkAccent` | string | `"green"` |  |
| `[appearance]` | `followSystem` | boolean | `true` |  |
| `[appearance]` | `lightAccent` | string | `"green"` |  |
| `[appearance]` | `theme` | string | `"dark"` |  |
| `[appearance]` | `titleBar` | string | `"auto"` |  |
| `[appearance.highContrastBlack]` | `accent` | string | `"#FFFF00"` |  |
| `[appearance.highContrastBlack]` | `border` | string | `"#FFFFFF"` |  |
| `[appearance.highContrastBlack]` | `text` | string | `"#FFFFFF"` |  |
| `[appearance.highContrastWhite]` | `accent` | string | `"#0037B3"` |  |
| `[appearance.highContrastWhite]` | `border` | string | `"#000000"` |  |
| `[appearance.highContrastWhite]` | `text` | string | `"#000000"` |  |
| `[audio]` | `autoCommit` | boolean | `true` | `AUDIO_AUTO_COMMIT` |
| `[audio]` | `autoFocus` | boolean | `true` | `AUDIO_AUTO_FOCUS` |
| `[audio]` | `autoScroll` | boolean | `true` | `AUDIO_AUTO_SCROLL` |
| `[audio]` | `cacheFilesLimit` | integer | `10` | `AUDIO_CACHE_FILES_LIMIT` |
| `[audio]` | `delay` | integer | `0` | `AUDIO_DELAY` |
| `[audio]` | `dontPlayWhenLineChanges` | boolean | `false` | `AUDIO_DONT_PLAY_WHEN_LINE_CHANGES` |
| `[audio]` | `drawKeyframes` | boolean | `true` | `AUDIO_DRAW_KEYFRAMES` |
| `[audio]` | `drawSecondaryLines` | boolean | `true` | `AUDIO_DRAW_SECONDARY_LINES` |
| `[audio]` | `drawSelectionBackground` | boolean | `true` | `AUDIO_DRAW_SELECTION_BACKGROUND` |
| `[audio]` | `drawTimeCursor` | boolean | `true` | `AUDIO_DRAW_TIME_CURSOR` |
| `[audio]` | `drawVideoPosition` | boolean | `true` | `AUDIO_DRAW_VIDEO_POSITION` |
| `[audio]` | `grabTimesOnSelect` | boolean | `true` | `AUDIO_GRAB_TIMES_ON_SELECT` |
| `[audio]` | `horizontalZoom` | integer | `50` | `AUDIO_HORIZONTAL_ZOOM` |
| `[audio]` | `inactiveLinesDisplayMode` | integer | `1` | `AUDIO_INACTIVE_LINES_DISPLAY_MODE` |
| `[audio]` | `kanjiTimerFurigana` | boolean | `false` |  |
| `[audio]` | `kanjiTimerMulti` | boolean | `false` |  |
| `[audio]` | `kanjiTimerSuggest` | boolean | `true` |  |
| `[audio]` | `karaoke` | boolean | `false` | `AUDIO_KARAOKE` |
| `[audio]` | `karaokeDoubleConsonantApart` | boolean | `false` |  |
| `[audio]` | `karaokeKfThreshold` | integer | `100` |  |
| `[audio]` | `karaokeMoveOnClick` | boolean | `false` | `AUDIO_KARAOKE_MOVE_ON_CLICK` |
| `[audio]` | `karaokeSplitMode` | boolean | `true` | `AUDIO_KARAOKE_SPLIT_MODE` |
| `[audio]` | `leadInValue` | integer | `200` | `AUDIO_LEAD_IN_VALUE` |
| `[audio]` | `leadOutValue` | integer | `300` | `AUDIO_LEAD_OUT_VALUE` |
| `[audio]` | `lineBoundariesThickness` | integer | `2` | `AUDIO_LINE_BOUNDARIES_THICKNESS` |
| `[audio]` | `link` | boolean | `false` | `AUDIO_LINK` |
| `[audio]` | `lockScrollOnCursor` | boolean | `false` | `AUDIO_LOCK_SCROLL_ON_CURSOR` |
| `[audio]` | `markPlayTime` | integer | `1000` | `AUDIO_MARK_PLAY_TIME` |
| `[audio]` | `mergeEveryNWithSyllable` | boolean | `false` | `AUDIO_MERGE_EVERY_N_WITH_SYLLABLE` |
| `[audio]` | `nextLineOnCommit` | boolean | `true` | `AUDIO_NEXT_LINE_ON_COMMIT` |
| `[audio]` | `onsetSnap` | boolean | `false` |  |
| `[audio]` | `outputDevice` | string | `""` |  |
| `[audio]` | `outputDeviceName` | string | `""` |  |
| `[audio]` | `outputHostApi` | integer | `0` |  |
| `[audio]` | `panelControls` | array of strings | `["playAround"]` |  |
| `[audio]` | `playAround` | string | `"AUDIO_PLAY_500MS_BEFORE"` |  |
| `[audio]` | `ramCache` | boolean | `false` | `AUDIO_RAM_CACHE` |
| `[audio]` | `snapToKeyframes` | boolean | `false` | `AUDIO_SNAP_TO_KEYFRAMES` |
| `[audio]` | `snapToOtherLines` | boolean | `false` | `AUDIO_SNAP_TO_OTHER_LINES` |
| `[audio]` | `spectrumNonLinearOn` | boolean | `false` | `AUDIO_SPECTRUM_NON_LINEAR_ON` |
| `[audio]` | `spectrumOn` | boolean | `false` | `AUDIO_SPECTRUM_ON` |
| `[audio]` | `speedPercent` | integer | `100` |  |
| `[audio]` | `startDragSensitivity` | integer | `6` | `AUDIO_START_DRAG_SENSITIVITY` |
| `[audio]` | `syncVideo` | boolean | `true` |  |
| `[audio]` | `tapHold` | boolean | `true` |  |
| `[audio]` | `tapLatency` | integer | `0` |  |
| `[audio]` | `verticalZoom` | integer | `50` | `AUDIO_VERTICAL_ZOOM` |
| `[audio]` | `volume` | integer | `50` | `AUDIO_VOLUME` |
| `[audio]` | `wheelDefaultToZoom` | boolean | `false` | `AUDIO_WHEEL_DEFAULT_TO_ZOOM` |
| `[automation]` | `dialogPresets` | array of strings | `[]` |  |
| `[automation]` | `disabledAutoload` | array of strings | `[]` |  |
| `[automation]` | `fillLastAnswers` | boolean | `false` |  |
| `[automation]` | `loadingMethod` | integer | `0` | `AUTOMATION_LOADING_METHOD` |
| `[automation]` | `oldScriptsCompatibility` | boolean | `false` | `AUTOMATION_OLD_SCRIPTS_COMPATIBILITY` |
| `[automation]` | `runLogTimes` | boolean | `false` |  |
| `[automation]` | `scriptEditor` | string | `""` | `AUTOMATION_SCRIPT_EDITOR` |
| `[automation]` | `scriptResolutionFromVideo` | boolean | `false` |  |
| `[automation]` | `traceLevel` | integer | `3` | `AUTOMATION_TRACE_LEVEL` |
| `[autosave]` | `maxFiles` | integer | `3` | `AUTOSAVE_MAX_FILES` |
| `[colourPicker]` | `switchClicks` | boolean | `false` | `COLORPICKER_SWITCH_CLICKS` |
| `[comparison]` | `styles` | array of strings | `[]` | `SUBS_COMPARISON_STYLES` |
| `[comparison]` | `type` | integer | `0` | `SUBS_COMPARISON_TYPE` |
| `[convert]` | `assTagsToInsertInLine` | string | `""` | `CONVERT_ASS_TAGS_TO_INSERT_IN_LINE` |
| `[convert]` | `fps` | string | `"23.976"` | `CONVERT_FPS` |
| `[convert]` | `fpsFromVideo` | boolean | `false` | `CONVERT_FPS_FROM_VIDEO` |
| `[convert]` | `newEndTimes` | boolean | `false` | `CONVERT_NEW_END_TIMES` |
| `[convert]` | `resolutionHeight` | string | `"720"` | `CONVERT_RESOLUTION_HEIGHT` |
| `[convert]` | `resolutionWidth` | string | `"1280"` | `CONVERT_RESOLUTION_WIDTH` |
| `[convert]` | `showSettings` | boolean | `false` | `CONVERT_SHOW_SETTINGS` |
| `[convert]` | `style` | string | `"Default"` | `CONVERT_STYLE` |
| `[convert]` | `styleCatalog` | string | `"Default"` | `CONVERT_STYLE_CATALOG` |
| `[convert]` | `timePerCharacter` | integer | `110` | `CONVERT_TIME_PER_CHARACTER` |
| `[editor]` | `allowNumpadHotkeys` | boolean | `false` | `TEXT_FIELD_ALLOW_NUMPAD_HOTKEYS` |
| `[editor]` | `dictionaryLanguage` | string | `"en_US"` | `DICTIONARY_LANGUAGE` |
| `[editor]` | `dontGoToNextLineOnTimesEdit` | boolean | `false` | `EDITBOX_DONT_GO_TO_NEXT_LINE_ON_TIMES_EDIT` |
| `[editor]` | `hideTags` | boolean | `false` |  |
| `[editor]` | `saveAfterCharacterCount` | integer | `1` | `GRID_SAVE_AFTER_CHARACTER_COUNT` |
| `[editor]` | `spellchecker` | boolean | `true` | `SPELLCHECKER_ON` |
| `[editor]` | `suggestionsOnDoubleClick` | boolean | `false` | `EDITBOX_SUGGESTIONS_ON_DOUBLE_CLICK` |
| `[editor]` | `tagButtons` | integer | `0` | `EDITBOX_TAG_BUTTONS` |
| `[editor]` | `timesToFramesSwitch` | boolean | `false` | `EDITBOX_TIMES_TO_FRAMES_SWITCH` |
| `[find]` | `options` | integer | `0` | `FIND_REPLACE_OPTIONS` |
| `[find]` | `styles` | string | `""` | `FIND_REPLACE_STYLES` |
| `[fontCollector]` | `action` | integer | `0` | `FONT_COLLECTOR_ACTION` |
| `[fontCollector]` | `directory` | string | `""` | `FONT_COLLECTOR_DIRECTORY` |
| `[fontCollector]` | `fromMkv` | boolean | `false` | `FONT_COLLECTOR_FROM_MKV` |
| `[fontCollector]` | `useSubsDirectory` | boolean | `false` | `FONT_COLLECTOR_USE_SUBS_DIRECTORY` |
| `[fonts]` | `externalDirectory` | string | `""` | `EXTERNAL_FONTS_DIRECTORY` |
| `[grammar]` | `language` | integer | `0` |  |
| `[grammar.group]` | `capitalizationAndCase` | boolean | `true` |  |
| `[grammar.group]` | `closedCompounds` | boolean | `true` |  |
| `[grammar.group]` | `compoundsAndHyphenation` | boolean | `true` |  |
| `[grammar.group]` | `grammarAndAgreement` | boolean | `true` |  |
| `[grammar.group]` | `initialisms` | boolean | `true` |  |
| `[grammar.group]` | `miscellaneous` | boolean | `true` |  |
| `[grammar.group]` | `numbersAndUnits` | boolean | `true` |  |
| `[grammar.group]` | `phraseCorrections` | boolean | `true` |  |
| `[grammar.group]` | `pronounsAndPossession` | boolean | `true` |  |
| `[grammar.group]` | `properNouns` | boolean | `true` |  |
| `[grammar.group]` | `punctuationAndSpacing` | boolean | `true` |  |
| `[grammar.group]` | `regionalismsAndDialect` | boolean | `true` |  |
| `[grammar.group]` | `spellingAndOrthography` | boolean | `true` |  |
| `[grammar.group]` | `styleAndRedundancy` | boolean | `true` |  |
| `[grammar.group]` | `wordChoiceAndUsage` | boolean | `true` |  |
| `[grid]` | `addToFilter` | boolean | `false` | `GRID_ADD_TO_FILTER` |
| `[grid]` | `autoHideColumns` | boolean | `true` |  |
| `[grid]` | `autoSelectLinesFromLastTab` | boolean | `false` | `AUTO_SELECT_LINES_FROM_LAST_TAB` |
| `[grid]` | `calcSpacesAndPunctuationForCps` | boolean | `false` | `CALC_SPACES_AND_PUNCTATION_FOR_CPS` |
| `[grid]` | `calcSpacesAndPunctuationForWraps` | boolean | `false` | `CALC_SPACES_AND_PUNCTATION_FOR_WRAPS` |
| `[grid]` | `changeActiveOnSelection` | boolean | `true` | `GRID_CHANGE_ACTIVE_ON_SELECTION` |
| `[grid]` | `copyColumns` | integer | `0` | `COPY_COLLUMS_SELECTIONS` |
| `[grid]` | `dontCenterActiveLine` | boolean | `false` | `GRID_DONT_CENTER_ACTIVE_LINE` |
| `[grid]` | `duplicationDontChangeSelection` | boolean | `false` | `GRID_DUPLICATION_DONT_CHANGE_SELECTION` |
| `[grid]` | `filterAfterLoad` | boolean | `false` | `GRID_FILTER_AFTER_LOAD` |
| `[grid]` | `filterBy` | integer | `0` | `GRID_FILTER_BY` |
| `[grid]` | `filterInverted` | boolean | `false` | `GRID_FILTER_INVERTED` |
| `[grid]` | `filterStyles` | array of strings | `[]` | `GRID_FILTER_STYLES` |
| `[grid]` | `foldGenerated` | boolean | `false` |  |
| `[grid]` | `font` | string | `"Tahoma"` | `GRID_FONT` |
| `[grid]` | `fontSize` | integer | `10` | `GRID_FONT_SIZE` |
| `[grid]` | `hideColumns` | integer | `0` | `GRID_HIDE_COLUMNS` |
| `[grid]` | `hideTags` | boolean | `false` | `GRID_HIDE_TAGS` |
| `[grid]` | `ignoreFiltering` | boolean | `false` | `GRID_IGNORE_FILTERING` |
| `[grid]` | `insertEndOffset` | integer | `0` | `GRID_INSERT_END_OFFSET` |
| `[grid]` | `insertStartOffset` | integer | `0` | `GRID_INSERT_START_OFFSET` |
| `[grid]` | `loadSortedSubs` | boolean | `false` | `GRID_LOAD_SORTED_SUBS` |
| `[grid]` | `pasteColumns` | integer | `0` | `PASTE_COLUMNS_SELECTION` |
| `[grid]` | `setVisibleLineAfterFullScreen` | boolean | `false` | `GRID_SET_VISIBLE_LINE_AFTER_FULL_SCREEN` |
| `[grid]` | `tagsSwapCharacter` | string | `"☀"` | `GRID_TAGS_SWAP_CHARACTER` |
| `[notices]` | `hiddenQuestions` | array of strings | `[]` |  |
| `[notices]` | `toasts` | boolean | `true` |  |
| `[postprocessor]` | `keyframeAfterEnd` | integer | `0` | `POSTPROCESSOR_KEYFRAME_AFTER_END` |
| `[postprocessor]` | `keyframeAfterStart` | integer | `0` | `POSTPROCESSOR_KEYFRAME_AFTER_START` |
| `[postprocessor]` | `keyframeBeforeEnd` | integer | `0` | `POSTPROCESSOR_KEYFRAME_BEFORE_END` |
| `[postprocessor]` | `keyframeBeforeStart` | integer | `0` | `POSTPROCESSOR_KEYFRAME_BEFORE_START` |
| `[postprocessor]` | `leadIn` | integer | `0` | `POSTPROCESSOR_LEAD_IN` |
| `[postprocessor]` | `leadOut` | integer | `0` | `POSTPROCESSOR_LEAD_OUT` |
| `[postprocessor]` | `on` | integer | `0` | `POSTPROCESSOR_ON` |
| `[postprocessor]` | `thresholdEnd` | integer | `0` | `POSTPROCESSOR_THRESHOLD_END` |
| `[postprocessor]` | `thresholdStart` | integer | `0` | `POSTPROCESSOR_THRESHOLD_START` |
| `[privacy]` | `alwaysSendCrashReports` | boolean | `false` |  |
| `[privacy]` | `usageData` | boolean | `true` |  |
| `[program]` | `dontShowCrashInfo` | string | `""` | `DONT_SHOW_CRASH_INFO` |
| `[program]` | `font` | string | `"Tahoma"` | `PROGRAM_FONT` |
| `[program]` | `fontSize` | integer | `10` | `PROGRAM_FONT_SIZE` |
| `[program]` | `language` | string | `""` | `PROGRAM_LANGUAGE` |
| `[program]` | `tabTextMaxChars` | integer | `0` | `TAB_TEXT_MAX_CHARS` |
| `[scriptProperties]` | `askForChange` | boolean | `false` | `ASS_PROPERTIES_ASK_FOR_CHANGE` |
| `[scriptProperties]` | `editing` | string | `""` | `ASS_PROPERTIES_EDITING` |
| `[scriptProperties]` | `editingOn` | boolean | `false` | `ASS_PROPERTIES_EDITING_ON` |
| `[scriptProperties]` | `linkResolutions` | boolean | `false` | `LINK_RESOLUTIONS` |
| `[scriptProperties]` | `script` | string | `""` | `ASS_PROPERTIES_SCRIPT` |
| `[scriptProperties]` | `scriptOn` | boolean | `false` | `ASS_PROPERTIES_SCRIPT_ON` |
| `[scriptProperties]` | `timing` | string | `""` | `ASS_PROPERTIES_TIMING` |
| `[scriptProperties]` | `timingOn` | boolean | `false` | `ASS_PROPERTIES_TIMING_ON` |
| `[scriptProperties]` | `title` | string | `""` | `ASS_PROPERTIES_TITLE` |
| `[scriptProperties]` | `titleOn` | boolean | `false` | `ASS_PROPERTIES_TITLE_ON` |
| `[scriptProperties]` | `translation` | string | `""` | `ASS_PROPERTIES_TRANSLATION` |
| `[scriptProperties]` | `translationOn` | boolean | `false` | `ASS_PROPERTIES_TRANSLATION_ON` |
| `[scriptProperties]` | `update` | string | `""` | `ASS_PROPERTIES_UPDATE` |
| `[scriptProperties]` | `updateOn` | boolean | `false` | `ASS_PROPERTIES_UPDATE_ON` |
| `[selectLines]` | `options` | integer | `0` | `SELECT_LINES_OPTIONS` |
| `[session]` | `restore` | integer | `0` | `LAST_SESSION_CONFIG` |
| `[shiftTimes]` | `byTime` | boolean | `false` | `SHIFT_TIMES_BY_TIME` |
| `[shiftTimes]` | `changeValuesWithTab` | boolean | `false` | `SHIFT_TIMES_CHANGE_VALUES_WITH_TAB` |
| `[shiftTimes]` | `correctEndTimes` | integer | `0` | `SHIFT_TIMES_CORRECT_END_TIMES` |
| `[shiftTimes]` | `displayFrames` | integer | `0` | `SHIFT_TIMES_DISPLAY_FRAMES` |
| `[shiftTimes]` | `moveForward` | boolean | `true` | `SHIFT_TIMES_MOVE_FORWARD` |
| `[shiftTimes]` | `options` | integer | `0` | `SHIFT_TIMES_OPTIONS` |
| `[shiftTimes]` | `profiles` | array of strings | `[]` | `SHIFT_TIMES_PROFILES` |
| `[shiftTimes]` | `styles` | string | `""` | `SHIFT_TIMES_STYLES` |
| `[shiftTimes]` | `time` | integer | `2000` | `SHIFT_TIMES_TIME` |
| `[shiftTimes]` | `whichLines` | integer | `0` | `SHIFT_TIMES_WHICH_LINES` |
| `[shiftTimes]` | `whichTimes` | integer | `0` | `SHIFT_TIMES_WHICH_TIMES` |
| `[styles]` | `editFilterText` | string | `"ĄĆĘŁŃÓŚŹŻąćęłńóśźż"` | `STYLE_EDIT_FILTER_TEXT` |
| `[styles]` | `editFilterTextOn` | boolean | `false` | `STYLE_EDIT_FILTER_TEXT_ON` |
| `[styles]` | `previewText` | string | `"光 Hikari 日本"` | `STYLE_PREVIEW_TEXT` |
| `[subtitles]` | `layoutInFile` | boolean | `true` |  |
| `[subtitles]` | `openInNewTab` | boolean | `false` | `OPEN_SUBS_IN_NEW_TAB` |
| `[subtitles]` | `saveWithVideoName` | boolean | `false` | `SUBS_AUTONAMING` |
| `[textEditor]` | `changeQuotes` | boolean | `false` | `TEXT_EDITOR_CHANGE_QUOTES` |
| `[textEditor]` | `fontSize` | integer | `0` | `TEXT_EDITOR_FONT_SIZE` |
| `[textEditor]` | `hideStatusBar` | boolean | `false` | `TEXT_EDITOR_HIDE_STATUS_BAR` |
| `[textEditor]` | `tagListOptions` | integer | `0` | `TEXT_EDITOR_TAG_LIST_OPTIONS` |
| `[toolbar]` | `actions` | array of strings | `[]` | `TOOLBAR_IDS` |
| `[toolbar]` | `alignment` | integer | `0` | `TOOLBAR_ALIGNMENT` |
| `[translation]` | `autoMoveTagsFromOriginal` | boolean | `true` | `AUTO_MOVE_TAGS_FROM_ORIGINAL` |
| `[translation]` | `hideOriginalOnVideo` | boolean | `false` | `TL_MODE_HIDE_ORIGINAL_ON_VIDEO` |
| `[translation]` | `showOriginal` | boolean | `false` | `TL_MODE_SHOW_ORIGINAL` |
| `[updater]` | `autoCheck` | boolean | `false` | `UPDATER_AUTO_CHECK` |
| `[updater]` | `autoInstall` | boolean | `true` |  |
| `[updater]` | `checkForStable` | boolean | `true` | `UPDATER_CHECK_FOR_STABLE` |
| `[updater]` | `nextCheck` | integer | `0` | `UPDATER_NEXT_CHECK` |
| `[video]` | `acceptedAudioStream` | string | `""` | `ACCEPTED_AUDIO_STREAM` |
| `[video]` | `disableLiveEditing` | boolean | `false` | `DISABLE_LIVE_VIDEO_EDITING` |
| `[video]` | `dontAskForBadResolution` | boolean | `false` | `DONT_ASK_FOR_BAD_RESOLUTION` |
| `[video]` | `ffms2Seeking` | integer | `2` | `FFMS2_VIDEO_SEEKING` |
| `[video]` | `fullScreenOnStart` | boolean | `false` | `VIDEO_FULL_SCREEN_ON_START` |
| `[video]` | `fullscreenBar` | integer | `0` |  |
| `[video]` | `gpuConversion` | boolean | `true` | `VIDEO_GPU_CONVERSION` |
| `[video]` | `index` | boolean | `true` | `VIDEO_INDEX` |
| `[video]` | `moveToActiveLine` | integer | `0` | `MOVE_VIDEO_TO_ACTIVE_LINE` |
| `[video]` | `openAtActiveLine` | boolean | `false` | `OPEN_VIDEO_AT_ACTIVE_LINE` |
| `[video]` | `pauseOnClick` | boolean | `false` | `VIDEO_PAUSE_ON_CLICK` |
| `[video]` | `playAfterSelection` | integer | `0` | `VIDEO_PLAY_AFTER_SELECTION` |
| `[video]` | `playbackPlayer` | integer | `0` |  |
| `[video]` | `previewSubtitlesAt` | integer | `0` |  |
| `[video]` | `progressBar` | boolean | `true` | `VIDEO_PROGRESS_BAR` |
| `[video]` | `renderCost` | boolean | `false` |  |
| `[video]` | `subtitleProvider` | string | `""` | `VSFILTER_INSTANCE` |
| `[video]` | `visualWarningsOff` | boolean | `false` | `VIDEO_VISUAL_WARNINGS_OFF` |
| `[video]` | `volume` | integer | `0` | `VIDEO_VOLUME` |
| `[video]` | `zoomPercent` | integer | `0` | `VIDEO_ZOOM_PERCENT` |
| `[workspace]` | `shiftTimesOn` | boolean | `true` | `SHIFT_TIMES_ON` |
| `[[editor.tagButton]]` | `tag`, `type`, `name`, `icon` | array of tables (slots 1-20) | none | `EDITBOX_TAG_BUTTON_VALUE1`..`20` |

## state.json

The same nesting, as JSON objects. `workspace.dialogGeometry` is `{ "<dialog>": [x, y, width, height] }`, `workspace.pickerFolders` is `{ "<picker>": "<folder URL>" }` and `automation.dialogAnswers` is `[ { "key", "sent", "answered", "button" } ]`, newest first.

| Object | Key | Type | Default |
|---|---|---|---|
| `automation` | `dialogAnswers` | array of strings | `[]` |
| `automation` | `lastScript` | string | `""` |
| `colourPicker` | `recentColours` | string | `""` |
| `find` | `recentFinds` | array of strings | `[]` |
| `find` | `recentReplacements` | array of strings | `[]` |
| `findInSubs` | `recentFilters` | array of strings | `[]` |
| `findInSubs` | `recentPaths` | array of strings | `[]` |
| `privacy` | `usageDataId` | string | `""` |
| `recent` | `audio` | array of strings | `[]` |
| `recent` | `keyframes` | array of strings | `[]` |
| `recent` | `subtitles` | array of strings | `[]` |
| `recent` | `subtitlesOpened` | array of strings | `[]` |
| `recent` | `video` | array of strings | `[]` |
| `selectLines` | `recentSelections` | array of strings | `[]` |
| `shiftTimes` | `collapsedSections` | array of strings | `[]` |
| `styles` | `lastCatalog` | string | `""` |
| `updater` | `skippedVersion` | string | `""` |
| `workspace` | `audioBoxHeight` | integer | `170` |
| `workspace` | `dialogGeometry` | array of strings | `[]` |
| `workspace` | `gettingStarted` | string | `""` |
| `workspace` | `karaokeTurnedOn` | array of strings | `[]` |
| `workspace` | `monitorPosition` | string | `""` |
| `workspace` | `monitorSize` | string | `""` |
| `workspace` | `pickerFolders` | array of strings | `[]` |
| `workspace` | `settingsPage` | string | `"general"` |
| `workspace` | `styleManagerDetachEditWindow` | boolean | `false` |
| `workspace` | `styleManagerPosition` | string | `""` |
| `workspace` | `videoWindowSize` | string | `"500,350"` |
| `workspace` | `windowMaximized` | boolean | `false` |
| `workspace` | `windowPosition` | string | `""` |
| `workspace` | `windowSize` | string | `"1000,700"` |

## hotkeys.toml

The changes from the default shortcuts, one table per window: `[global]`, `[grid]` (Subtitles), `[editor]`, `[video]` and `[audio]`.
- `COMMAND_SYMBOL = "Ctrl+Shift+S"` sets a shortcut, in Qt's key text: `Ctrl+`, `Alt+` and `Shift+`, then the key, such as `Num+0` or `Return`.
- `COMMAND_SYMBOL = ""` takes a default shortcut away.
- A command has one shortcut per window. In an array, only the first entry is used.
- The app writes each command's English name as a comment.
- `[automation]` holds the scripts' shortcuts: `"<legacy name>" = { keys = "Ctrl+K", macro = "<macro>", sha256 = "<script hash>" }`.

## Other rules
- A file that doesn't read (bad syntax, or no `version` or a newer one) is renamed `<name>.broken`. Its settings take their defaults, and a notice names the file and line.
- `hikari.ini` from an earlier version is converted once at the first start without `settings.toml`, and is kept as `hikari.ini.old`.
