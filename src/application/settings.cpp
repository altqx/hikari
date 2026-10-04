#include "hikari/application/settings.h"

#include <algorithm>
#include <cctype>
#include <charconv>

namespace hikari::application {

namespace {

using enum SettingType;
using enum SettingScope;
using enum SettingDisposition;

std::string utf8(std::u8string_view s)
{
    return {reinterpret_cast<const char *>(s.data()), s.size()};
}

// Generated from docs/qt/proposals/settings-migration-map/options.csv (ids
// chosen here; types, scopes, dispositions and defaults from the worksheet,
// which agrees with legacy config.cpp at 20d647c4).
const std::vector<SettingDefinition> &definitions()
{
    static const std::vector<SettingDefinition> table{
        {"audio.autoCommit", "AUDIO_AUTO_COMMIT", Bool, Profile, Mapped, true, true},
        {"audio.autoFocus", "AUDIO_AUTO_FOCUS", Bool, Profile, Mapped, true, true},
        {"audio.autoScroll", "AUDIO_AUTO_SCROLL", Bool, Profile, Mapped, true, true},
        {"audio.cacheFilesLimit", "AUDIO_CACHE_FILES_LIMIT", Int, Profile, Mapped, true, std::int64_t{10}},
        {"audio.delay", "AUDIO_DELAY", Int, Profile, Mapped, true, std::int64_t{0}},
        {"audio.dontPlayWhenLineChanges", "AUDIO_DONT_PLAY_WHEN_LINE_CHANGES", Bool, Profile, Mapped, true, false},
        {"audio.drawKeyframes", "AUDIO_DRAW_KEYFRAMES", Bool, Profile, Mapped, true, true},
        {"audio.drawSecondaryLines", "AUDIO_DRAW_SECONDARY_LINES", Bool, Profile, Mapped, true, true},
        {"audio.drawSelectionBackground", "AUDIO_DRAW_SELECTION_BACKGROUND", Bool, Profile, Mapped, true, true},
        {"audio.drawTimeCursor", "AUDIO_DRAW_TIME_CURSOR", Bool, Profile, Unresolved, true, true},
        {"audio.drawVideoPosition", "AUDIO_DRAW_VIDEO_POSITION", Bool, Profile, Mapped, true, true},
        {"audio.grabTimesOnSelect", "AUDIO_GRAB_TIMES_ON_SELECT", Bool, Profile, Mapped, true, true},
        {"audio.horizontalZoom", "AUDIO_HORIZONTAL_ZOOM", Int, Profile, Mapped, true, std::int64_t{50}},
        {"audio.inactiveLinesDisplayMode", "AUDIO_INACTIVE_LINES_DISPLAY_MODE", Int, Profile, Mapped, true, std::int64_t{1}},
        {"audio.karaoke", "AUDIO_KARAOKE", Bool, Profile, Mapped, true, false},
        {"audio.karaokeMoveOnClick", "AUDIO_KARAOKE_MOVE_ON_CLICK", Bool, Profile, Mapped, true, false},
        {"audio.karaokeSplitMode", "AUDIO_KARAOKE_SPLIT_MODE", Bool, Profile, Mapped, true, true},
        {"audio.leadInValue", "AUDIO_LEAD_IN_VALUE", Int, Profile, Mapped, true, std::int64_t{200}},
        {"audio.leadOutValue", "AUDIO_LEAD_OUT_VALUE", Int, Profile, Mapped, true, std::int64_t{300}},
        {"audio.lineBoundariesThickness", "AUDIO_LINE_BOUNDARIES_THICKNESS", Int, Profile, Mapped, true, std::int64_t{2}},
        {"audio.link", "AUDIO_LINK", Bool, Profile, Mapped, true, false},
        {"audio.lockScrollOnCursor", "AUDIO_LOCK_SCROLL_ON_CURSOR", Bool, Profile, Unresolved, true, false},
        {"audio.markPlayTime", "AUDIO_MARK_PLAY_TIME", Int, Profile, Mapped, true, std::int64_t{1000}},
        {"audio.mergeEveryNWithSyllable", "AUDIO_MERGE_EVERY_N_WITH_SYLLABLE", Bool, Profile, Mapped, true, false},
        {"audio.nextLineOnCommit", "AUDIO_NEXT_LINE_ON_COMMIT", Bool, Profile, Mapped, true, true},
        {"audio.ramCache", "AUDIO_RAM_CACHE", Bool, Profile, Mapped, true, false},
        {"audio.snapToKeyframes", "AUDIO_SNAP_TO_KEYFRAMES", Bool, Profile, Mapped, true, false},
        {"audio.snapToOtherLines", "AUDIO_SNAP_TO_OTHER_LINES", Bool, Profile, Mapped, true, false},
        {"audio.spectrumOn", "AUDIO_SPECTRUM_ON", Bool, Profile, Mapped, true, false},
        {"audio.spectrumNonLinearOn", "AUDIO_SPECTRUM_NON_LINEAR_ON", Bool, Profile, Mapped, true, false},
        {"audio.startDragSensitivity", "AUDIO_START_DRAG_SENSITIVITY", Int, Profile, Mapped, true, std::int64_t{6}},
        {"audio.verticalZoom", "AUDIO_VERTICAL_ZOOM", Int, Profile, Mapped, true, std::int64_t{50}},
        {"audio.volume", "AUDIO_VOLUME", Int, Profile, Mapped, true, std::int64_t{50}},
        {"audio.wheelDefaultToZoom", "AUDIO_WHEEL_DEFAULT_TO_ZOOM", Bool, Profile, Mapped, true, false},
        {"video.acceptedAudioStream", "ACCEPTED_AUDIO_STREAM", String, Profile, Mapped, false, std::string()},
        {"workspace.audioBoxHeight", "AUDIO_BOX_HEIGHT", Int, Workspace, Mapped, false, std::int64_t{170}},
        {"scriptProperties.title", "ASS_PROPERTIES_TITLE", String, Profile, Mapped, false, std::string()},
        {"scriptProperties.script", "ASS_PROPERTIES_SCRIPT", String, Profile, Mapped, false, std::string()},
        {"scriptProperties.translation", "ASS_PROPERTIES_TRANSLATION", String, Profile, Mapped, false, std::string()},
        {"scriptProperties.editing", "ASS_PROPERTIES_EDITING", String, Profile, Mapped, false, std::string()},
        {"scriptProperties.timing", "ASS_PROPERTIES_TIMING", String, Profile, Mapped, false, std::string()},
        {"scriptProperties.update", "ASS_PROPERTIES_UPDATE", String, Profile, Mapped, false, std::string()},
        {"scriptProperties.titleOn", "ASS_PROPERTIES_TITLE_ON", Bool, Profile, Mapped, false, false},
        {"scriptProperties.scriptOn", "ASS_PROPERTIES_SCRIPT_ON", Bool, Profile, Mapped, false, false},
        {"scriptProperties.translationOn", "ASS_PROPERTIES_TRANSLATION_ON", Bool, Profile, Mapped, false, false},
        {"scriptProperties.editingOn", "ASS_PROPERTIES_EDITING_ON", Bool, Profile, Mapped, false, false},
        {"scriptProperties.timingOn", "ASS_PROPERTIES_TIMING_ON", Bool, Profile, Mapped, false, false},
        {"scriptProperties.updateOn", "ASS_PROPERTIES_UPDATE_ON", Bool, Profile, Mapped, false, false},
        {"scriptProperties.askForChange", "ASS_PROPERTIES_ASK_FOR_CHANGE", Bool, Profile, Mapped, false, false},
        {"recent.audio", "AUDIO_RECENT_FILES", StringList, Profile, Mapped, false, std::vector<std::string>()},
        {"automation.loadingMethod", "AUTOMATION_LOADING_METHOD", Int, Profile, Mapped, false, std::int64_t{0}},
        {"automation.oldScriptsCompatibility", "AUTOMATION_OLD_SCRIPTS_COMPATIBILITY", Bool, Profile, Mapped, false, false},
        {"automation.lastScript", "AUTOMATION_RECENT_FILES", String, Profile, Mapped, false, std::string()},
        {"automation.scriptEditor", "AUTOMATION_SCRIPT_EDITOR", String, Profile, Mapped, false, std::string()},
        {"automation.traceLevel", "AUTOMATION_TRACE_LEVEL", Int, Profile, Mapped, false, std::int64_t{3}},
        {"translation.autoMoveTagsFromOriginal", "AUTO_MOVE_TAGS_FROM_ORIGINAL", Bool, Profile, Mapped, false, false},
        {"autosave.maxFiles", "AUTOSAVE_MAX_FILES", Int, Profile, Mapped, false, std::int64_t{3}},
        {"grid.autoSelectLinesFromLastTab", "AUTO_SELECT_LINES_FROM_LAST_TAB", Bool, Profile, Mapped, false, false},
        {"grid.calcSpacesAndPunctuationForWraps", "CALC_SPACES_AND_PUNCTATION_FOR_WRAPS", Bool, Profile, Mapped, false, false},
        {"grid.calcSpacesAndPunctuationForCps", "CALC_SPACES_AND_PUNCTATION_FOR_CPS", Bool, Profile, Mapped, false, false},
        {"colourPicker.recentColours", "COLORPICKER_RECENT_COLORS", String, Profile, Unresolved, false, std::string()},
        {"colourPicker.switchClicks", "COLORPICKER_SWITCH_CLICKS", Bool, Profile, Mapped, false, false},
        {"convert.assTagsToInsertInLine", "CONVERT_ASS_TAGS_TO_INSERT_IN_LINE", String, Profile, Mapped, false, std::string()},
        {"convert.fps", "CONVERT_FPS", String, Profile, Mapped, false, std::string("23.976")},
        {"convert.fpsFromVideo", "CONVERT_FPS_FROM_VIDEO", Bool, Profile, Mapped, false, false},
        {"convert.newEndTimes", "CONVERT_NEW_END_TIMES", Bool, Profile, Mapped, false, false},
        {"convert.resolutionWidth", "CONVERT_RESOLUTION_WIDTH", String, Profile, Mapped, false, std::string("1280")},
        {"convert.resolutionHeight", "CONVERT_RESOLUTION_HEIGHT", String, Profile, Mapped, false, std::string("720")},
        {"convert.showSettings", "CONVERT_SHOW_SETTINGS", Bool, Profile, Mapped, false, false},
        {"convert.style", "CONVERT_STYLE", String, Profile, Mapped, false, std::string("Default")},
        {"convert.styleCatalog", "CONVERT_STYLE_CATALOG", String, Profile, Mapped, false, std::string("Default")},
        {"convert.timePerCharacter", "CONVERT_TIME_PER_CHARACTER", Int, Profile, Mapped, false, std::int64_t{110}},
        {"grid.copyColumns", "COPY_COLLUMS_SELECTIONS", Int, Profile, Mapped, false, std::int64_t{0}},
        {"editor.dictionaryLanguage", "DICTIONARY_LANGUAGE", String, Profile, Mapped, false, std::string("en_US")},
        {"video.disableLiveEditing", "DISABLE_LIVE_VIDEO_EDITING", Bool, Profile, Mapped, false, false},
        {"video.dontAskForBadResolution", "DONT_ASK_FOR_BAD_RESOLUTION", Bool, Profile, Mapped, false, false},
        {"editor.suggestionsOnDoubleClick", "EDITBOX_SUGGESTIONS_ON_DOUBLE_CLICK", Bool, Profile, Mapped, false, false},
        {"editor.timesToFramesSwitch", "EDITBOX_TIMES_TO_FRAMES_SWITCH", Bool, Profile, Mapped, false, false},
        {"workspace.editorOn", "EDITOR_ON", Bool, Workspace, Mapped, false, true},
        {"findInSubs.recentFilters", "FIND_IN_SUBS_FILTERS_RECENT", StringList, Profile, Mapped, false, std::vector<std::string>()},
        {"findInSubs.recentPaths", "FIND_IN_SUBS_PATHS_RECENT", StringList, Profile, Mapped, false, std::vector<std::string>()},
        {"find.styles", "FIND_REPLACE_STYLES", String, Profile, Mapped, false, std::string()},
        {"find.recentFinds", "FIND_RECENT_FINDS", StringList, Profile, Mapped, false, std::vector<std::string>()},
        {"find.options", "FIND_REPLACE_OPTIONS", Int, Profile, Mapped, false, std::int64_t{0}},
        {"fontCollector.action", "FONT_COLLECTOR_ACTION", Int, Profile, Mapped, false, std::int64_t{0}},
        {"fontCollector.directory", "FONT_COLLECTOR_DIRECTORY", String, Profile, Mapped, false, std::string()},
        {"fontCollector.fromMkv", "FONT_COLLECTOR_FROM_MKV", Bool, Profile, Mapped, false, false},
        {"fontCollector.useSubsDirectory", "FONT_COLLECTOR_USE_SUBS_DIRECTORY", Bool, Profile, Mapped, false, false},
        {"fonts.externalDirectory", "EXTERNAL_FONTS_DIRECTORY", String, Profile, Mapped, false, std::string()},
        {"video.ffms2Seeking", "FFMS2_VIDEO_SEEKING", Int, Profile, Unresolved, false, std::int64_t{2}},
        {"grid.changeActiveOnSelection", "GRID_CHANGE_ACTIVE_ON_SELECTION", Bool, Profile, Mapped, false, true},
        {"grid.font", "GRID_FONT", String, Profile, Mapped, false, std::string("Tahoma")},
        {"grid.fontSize", "GRID_FONT_SIZE", Int, Profile, Mapped, false, std::int64_t{10}},
        {"grid.addToFilter", "GRID_ADD_TO_FILTER", Bool, Profile, Mapped, false, false},
        {"grid.filterAfterLoad", "GRID_FILTER_AFTER_LOAD", Bool, Profile, Mapped, false, false},
        {"grid.filterBy", "GRID_FILTER_BY", Int, Profile, Mapped, false, std::int64_t{0}},
        {"grid.filterInverted", "GRID_FILTER_INVERTED", Bool, Profile, Mapped, false, false},
        {"grid.filterStyles", "GRID_FILTER_STYLES", StringList, Profile, Mapped, false, std::vector<std::string>()},
        {"grid.hideColumns", "GRID_HIDE_COLUMNS", Int, Profile, Mapped, false, std::int64_t{0}},
        {"grid.hideTags", "GRID_HIDE_TAGS", Bool, Profile, Mapped, false, false},
        {"grid.ignoreFiltering", "GRID_IGNORE_FILTERING", Bool, Profile, Mapped, false, false},
        {"grid.loadSortedSubs", "GRID_LOAD_SORTED_SUBS", Bool, Profile, Mapped, false, false},
        {"editor.saveAfterCharacterCount", "GRID_SAVE_AFTER_CHARACTER_COUNT", Int, Profile, Mapped, false, std::int64_t{1}},
        {"grid.tagsSwapCharacter", "GRID_TAGS_SWAP_CHARACTER", String, Profile, Mapped, false, utf8(u8"☀")},
        {"grid.insertEndOffset", "GRID_INSERT_END_OFFSET", Int, Profile, Mapped, false, std::int64_t{0}},
        {"grid.insertStartOffset", "GRID_INSERT_START_OFFSET", Int, Profile, Mapped, false, std::int64_t{0}},
        {"grid.duplicationDontChangeSelection", "GRID_DUPLICATION_DONT_CHANGE_SELECTION", Bool, Profile, Mapped, false, false},
        {"grid.dontCenterActiveLine", "GRID_DONT_CENTER_ACTIVE_LINE", Bool, Profile, Mapped, false, false},
        {"recent.keyframes", "KEYFRAMES_RECENT", StringList, Profile, Mapped, false, std::vector<std::string>()},
        {"session.restore", "LAST_SESSION_CONFIG", Int, Profile, Mapped, false, std::int64_t{0}},
        {"shiftTimes.byTime", "SHIFT_TIMES_BY_TIME", Bool, Profile, Unresolved, false, false},
        {"shiftTimes.changeValuesWithTab", "SHIFT_TIMES_CHANGE_VALUES_WITH_TAB", Bool, Profile, Mapped, false, false},
        {"shiftTimes.correctEndTimes", "SHIFT_TIMES_CORRECT_END_TIMES", Int, Profile, Mapped, false, std::int64_t{0}},
        {"shiftTimes.moveForward", "SHIFT_TIMES_MOVE_FORWARD", Bool, Profile, Unresolved, false, true},
        {"shiftTimes.displayFrames", "SHIFT_TIMES_DISPLAY_FRAMES", Int, Profile, Mapped, false, std::int64_t{0}},
        {"workspace.shiftTimesOn", "SHIFT_TIMES_ON", Bool, Profile, Mapped, false, true},
        {"shiftTimes.options", "SHIFT_TIMES_OPTIONS", Int, Profile, Mapped, false, std::int64_t{0}},
        {"shiftTimes.whichLines", "SHIFT_TIMES_WHICH_LINES", Int, Profile, Mapped, false, std::int64_t{0}},
        {"shiftTimes.whichTimes", "SHIFT_TIMES_WHICH_TIMES", Int, Profile, Mapped, false, std::int64_t{0}},
        {"shiftTimes.styles", "SHIFT_TIMES_STYLES", String, Profile, Mapped, false, std::string()},
        {"shiftTimes.time", "SHIFT_TIMES_TIME", Int, Profile, Mapped, false, std::int64_t{2000}},
        {"video.moveToActiveLine", "MOVE_VIDEO_TO_ACTIVE_LINE", Int, Profile, Mapped, false, std::int64_t{0}},
        {"editor.dontGoToNextLineOnTimesEdit", "EDITBOX_DONT_GO_TO_NEXT_LINE_ON_TIMES_EDIT", Bool, Profile, Mapped, false, false},
        {"subtitles.openInNewTab", "OPEN_SUBS_IN_NEW_TAB", Bool, Profile, Mapped, false, false},
        {"video.openAtActiveLine", "OPEN_VIDEO_AT_ACTIVE_LINE", Bool, Profile, Mapped, false, false},
        {"grid.pasteColumns", "PASTE_COLUMNS_SELECTION", Int, Profile, Mapped, false, std::int64_t{0}},
        {"video.playAfterSelection", "VIDEO_PLAY_AFTER_SELECTION", Int, Profile, Mapped, false, std::int64_t{0}},
        {"postprocessor.on", "POSTPROCESSOR_ON", Int, Profile, Mapped, false, std::int64_t{0}},
        {"postprocessor.keyframeBeforeStart", "POSTPROCESSOR_KEYFRAME_BEFORE_START", Int, Profile, Mapped, false, std::int64_t{0}},
        {"postprocessor.keyframeAfterStart", "POSTPROCESSOR_KEYFRAME_AFTER_START", Int, Profile, Mapped, false, std::int64_t{0}},
        {"postprocessor.keyframeBeforeEnd", "POSTPROCESSOR_KEYFRAME_BEFORE_END", Int, Profile, Mapped, false, std::int64_t{0}},
        {"postprocessor.keyframeAfterEnd", "POSTPROCESSOR_KEYFRAME_AFTER_END", Int, Profile, Mapped, false, std::int64_t{0}},
        {"postprocessor.leadIn", "POSTPROCESSOR_LEAD_IN", Int, Profile, Mapped, false, std::int64_t{0}},
        {"postprocessor.leadOut", "POSTPROCESSOR_LEAD_OUT", Int, Profile, Mapped, false, std::int64_t{0}},
        {"postprocessor.thresholdStart", "POSTPROCESSOR_THRESHOLD_START", Int, Profile, Mapped, false, std::int64_t{0}},
        {"postprocessor.thresholdEnd", "POSTPROCESSOR_THRESHOLD_END", Int, Profile, Mapped, false, std::int64_t{0}},
        {"styles.previewText", "STYLE_PREVIEW_TEXT", String, Profile, Mapped, false, utf8(u8"Podgląd")},
        {"program.font", "PROGRAM_FONT", String, Profile, Mapped, false, std::string("Tahoma")},
        {"program.fontSize", "PROGRAM_FONT_SIZE", Int, Profile, Mapped, false, std::int64_t{10}},
        {"program.language", "PROGRAM_LANGUAGE", String, Profile, Mapped, false, std::string()},
        {"program.theme", "PROGRAM_THEME", String, Profile, Excluded, false, std::string("DarkSentro")},
        {"find.recentReplacements", "REPLACE_RECENT_REPLACEMENTS", StringList, Profile, Mapped, false, std::vector<std::string>()},
        {"selectLines.recentSelections", "SELECT_LINES_RECENT_SELECTIONS", StringList, Profile, Mapped, false, std::vector<std::string>()},
        {"selectLines.options", "SELECT_LINES_OPTIONS", Int, Profile, Mapped, false, std::int64_t{0}},
        {"grid.setVisibleLineAfterFullScreen", "GRID_SET_VISIBLE_LINE_AFTER_FULL_SCREEN", Bool, Profile, Mapped, false, false},
        {"shiftTimes.profiles", "SHIFT_TIMES_PROFILES", StringList, Profile, Unresolved, false, std::vector<std::string>()},
        {"editor.spellchecker", "SPELLCHECKER_ON", Bool, Profile, Mapped, false, true},
        {"styles.editFilterText", "STYLE_EDIT_FILTER_TEXT", String, Profile, Mapped, false, utf8(u8"ĄĆĘŁŃÓŚŹŻąćęłńóśźż")},
        {"styles.editFilterTextOn", "STYLE_EDIT_FILTER_TEXT_ON", Bool, Profile, Mapped, false, false},
        {"workspace.styleManagerPosition", "STYLE_MANAGER_POSITION", String, Workspace, Mapped, false, std::string()},
        {"workspace.styleManagerDetachEditWindow", "STYLE_MANAGER_DETACH_EDIT_WINDOW", Bool, Workspace, Mapped, false, false},
        {"subtitles.saveWithVideoName", "SUBS_AUTONAMING", Bool, Profile, Mapped, false, false},
        {"comparison.type", "SUBS_COMPARISON_TYPE", Int, Profile, Mapped, false, std::int64_t{0}},
        {"comparison.styles", "SUBS_COMPARISON_STYLES", StringList, Profile, Mapped, false, std::vector<std::string>()},
        {"recent.subtitles", "SUBS_RECENT_FILES", StringList, Profile, Mapped, false, std::vector<std::string>()},
        {"program.tabTextMaxChars", "TAB_TEXT_MAX_CHARS", Int, Workspace, Mapped, false, std::int64_t{0}},
        {"textEditor.fontSize", "TEXT_EDITOR_FONT_SIZE", Int, Profile, Mapped, false, std::int64_t{0}},
        {"textEditor.hideStatusBar", "TEXT_EDITOR_HIDE_STATUS_BAR", Bool, Profile, Mapped, false, false},
        {"textEditor.changeQuotes", "TEXT_EDITOR_CHANGE_QUOTES", Bool, Profile, Mapped, false, false},
        {"textEditor.tagListOptions", "TEXT_EDITOR_TAG_LIST_OPTIONS", Int, Profile, Mapped, false, std::int64_t{0}},
        {"editor.allowNumpadHotkeys", "TEXT_FIELD_ALLOW_NUMPAD_HOTKEYS", Bool, Profile, Mapped, false, false},
        {"translation.showOriginal", "TL_MODE_SHOW_ORIGINAL", Bool, Profile, Mapped, false, false},
        {"translation.hideOriginalOnVideo", "TL_MODE_HIDE_ORIGINAL_ON_VIDEO", Bool, Profile, Mapped, false, false},
        {"toolbar.actions", "TOOLBAR_IDS", StringList, Workspace, Mapped, false, std::vector<std::string>()},
        {"toolbar.alignment", "TOOLBAR_ALIGNMENT", Int, Workspace, Mapped, false, std::int64_t{0}},
        {"updater.autoCheck", "UPDATER_AUTO_CHECK", Bool, Profile, Mapped, false, false},
        {"updater.checkForStable", "UPDATER_CHECK_FOR_STABLE", Bool, Profile, Mapped, false, true},
        {"updater.nextCheck", "UPDATER_NEXT_CHECK", Int, Profile, Mapped, false, std::int64_t{0}},
        {"video.fullScreenOnStart", "VIDEO_FULL_SCREEN_ON_START", Bool, Profile, Mapped, false, false},
        {"video.gpuConversion", "VIDEO_GPU_CONVERSION", Bool, Profile, Unresolved, false, true},
        {"video.index", "VIDEO_INDEX", Bool, Profile, Mapped, false, true},
        {"video.pauseOnClick", "VIDEO_PAUSE_ON_CLICK", Bool, Profile, Mapped, false, false},
        {"video.progressBar", "VIDEO_PROGRESS_BAR", Bool, Profile, Mapped, false, true},
        {"recent.video", "VIDEO_RECENT_FILES", StringList, Profile, Mapped, false, std::vector<std::string>()},
        {"video.volume", "VIDEO_VOLUME", Int, Profile, Mapped, false, std::int64_t{0}},
        {"workspace.videoWindowSize", "VIDEO_WINDOW_SIZE", String, Workspace, Mapped, false, std::string("500,350")},
        {"video.visualWarningsOff", "VIDEO_VISUAL_WARNINGS_OFF", Bool, Profile, Mapped, false, false},
        {"video.zoomPercent", "VIDEO_ZOOM_PERCENT", Int, Profile, Mapped, false, std::int64_t{0}},
        {"video.subtitleProvider", "VSFILTER_INSTANCE", String, Profile, Unresolved, false, std::string()},
        {"workspace.windowMaximized", "WINDOW_MAXIMIZED", Bool, Workspace, Mapped, false, false},
        {"workspace.windowPosition", "WINDOW_POSITION", String, Workspace, Mapped, false, std::string()},
        {"workspace.windowSize", "WINDOW_SIZE", String, Workspace, Mapped, false, std::string("1000,700")},
        {"workspace.monitorPosition", "MONITOR_POSITION", String, Workspace, Mapped, false, std::string()},
        {"workspace.monitorSize", "MONITOR_SIZE", String, Workspace, Mapped, false, std::string()},
        {"program.dontShowCrashInfo", "DONT_SHOW_CRASH_INFO", String, Profile, Unresolved, false, std::string()},
        {"scriptProperties.linkResolutions", "LINK_RESOLUTIONS", Bool, Profile, Mapped, false, false},
        {"editor.tagButtons", "EDITBOX_TAG_BUTTONS", Int, Collection, Mapped, false, std::int64_t{0}},
        {"editor.tagButton1", "EDITBOX_TAG_BUTTON_VALUE1", String, Collection, Mapped, false, std::string()},
        {"editor.tagButton2", "EDITBOX_TAG_BUTTON_VALUE2", String, Collection, Mapped, false, std::string()},
        {"editor.tagButton3", "EDITBOX_TAG_BUTTON_VALUE3", String, Collection, Mapped, false, std::string()},
        {"editor.tagButton4", "EDITBOX_TAG_BUTTON_VALUE4", String, Collection, Mapped, false, std::string()},
        {"editor.tagButton5", "EDITBOX_TAG_BUTTON_VALUE5", String, Collection, Mapped, false, std::string()},
        {"editor.tagButton6", "EDITBOX_TAG_BUTTON_VALUE6", String, Collection, Mapped, false, std::string()},
        {"editor.tagButton7", "EDITBOX_TAG_BUTTON_VALUE7", String, Collection, Mapped, false, std::string()},
        {"editor.tagButton8", "EDITBOX_TAG_BUTTON_VALUE8", String, Collection, Mapped, false, std::string()},
        {"editor.tagButton9", "EDITBOX_TAG_BUTTON_VALUE9", String, Collection, Mapped, false, std::string()},
        {"editor.tagButton10", "EDITBOX_TAG_BUTTON_VALUE10", String, Collection, Mapped, false, std::string()},
        {"editor.tagButton11", "EDITBOX_TAG_BUTTON_VALUE11", String, Collection, Mapped, false, std::string()},
        {"editor.tagButton12", "EDITBOX_TAG_BUTTON_VALUE12", String, Collection, Mapped, false, std::string()},
        {"editor.tagButton13", "EDITBOX_TAG_BUTTON_VALUE13", String, Collection, Mapped, false, std::string()},
        {"editor.tagButton14", "EDITBOX_TAG_BUTTON_VALUE14", String, Collection, Mapped, false, std::string()},
        {"editor.tagButton15", "EDITBOX_TAG_BUTTON_VALUE15", String, Collection, Mapped, false, std::string()},
        {"editor.tagButton16", "EDITBOX_TAG_BUTTON_VALUE16", String, Collection, Mapped, false, std::string()},
        {"editor.tagButton17", "EDITBOX_TAG_BUTTON_VALUE17", String, Collection, Mapped, false, std::string()},
        {"editor.tagButton18", "EDITBOX_TAG_BUTTON_VALUE18", String, Collection, Mapped, false, std::string()},
        {"editor.tagButton19", "EDITBOX_TAG_BUTTON_VALUE19", String, Collection, Mapped, false, std::string()},
        {"editor.tagButton20", "EDITBOX_TAG_BUTTON_VALUE20", String, Collection, Mapped, false, std::string()},
        // The rewrite's own: automation hotkeys (S2), one "name\tkeys\tmacro\tsha256" row each.
        {kAutomationHotkeysSetting, "", StringList, Profile, Mapped, false, std::vector<std::string>()},
    };
    return table;
}

std::optional<std::int64_t> parseInteger(std::string_view text)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
        text.remove_suffix(1);
    if (!text.empty() && text.front() == '+')
        text.remove_prefix(1);
    std::int64_t out = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), out);
    if (error != std::errc() || end != text.data() + text.size())
        return std::nullopt;
    return out;
}

} // namespace

std::span<const SettingDefinition> settingDefinitions()
{
    return definitions();
}

const SettingDefinition *findSetting(std::string_view id)
{
    const auto &all = definitions();
    const auto it = std::ranges::find(all, id, &SettingDefinition::id);
    return it == all.end() ? nullptr : &*it;
}

const SettingDefinition *findLegacySetting(std::string_view legacyKey)
{
    if (legacyKey.empty())
        return nullptr;
    const auto &all = definitions();
    const auto it = std::ranges::find(all, legacyKey, &SettingDefinition::legacyKey);
    return it == all.end() ? nullptr : &*it;
}

std::string_view scopeName(SettingScope scope)
{
    switch (scope) {
    case Profile:
        return "profile";
    case Collection:
        return "collections";
    case Workspace:
        return "workspace";
    }
    return "profile";
}

SettingValue convertSetting(const SettingValue &value, SettingType type)
{
    return std::visit(
        [type](const auto &v) -> SettingValue {
            using T = std::decay_t<decltype(v)>;
            switch (type) {
            case Bool:
                if constexpr (std::is_same_v<T, bool>)
                    return v;
                else if constexpr (std::is_same_v<T, std::int64_t>)
                    return v != 0;
                else if constexpr (std::is_same_v<T, std::string>) {
                    // QVariant::toBool on text: anything but "", "0" and "false".
                    std::string lower = v;
                    std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
                    return !(lower.empty() || lower == "0" || lower == "false");
                } else
                    return v.size() == 1 && std::get<bool>(convertSetting(v.front(), Bool));
            case Int:
                if constexpr (std::is_same_v<T, bool>)
                    return std::int64_t(v ? 1 : 0);
                else if constexpr (std::is_same_v<T, std::int64_t>)
                    return v;
                else if constexpr (std::is_same_v<T, std::string>)
                    return parseInteger(v).value_or(0);
                else
                    return v.size() == 1 ? parseInteger(v.front()).value_or(0) : std::int64_t(0);
            case String:
                if constexpr (std::is_same_v<T, bool>)
                    return std::string(v ? "true" : "false");
                else if constexpr (std::is_same_v<T, std::int64_t>)
                    return std::to_string(v);
                else if constexpr (std::is_same_v<T, std::string>)
                    return v;
                else {
                    // QVariant::toString joins nothing: a list of one gives it, else empty.
                    return v.size() == 1 ? v.front() : std::string();
                }
            case StringList:
                if constexpr (std::is_same_v<T, std::vector<std::string>>)
                    return v;
                else if constexpr (std::is_same_v<T, std::string>)
                    return std::vector<std::string>{v};
                else if constexpr (std::is_same_v<T, bool>)
                    return std::vector<std::string>{v ? "true" : "false"};
                else
                    return std::vector<std::string>{std::to_string(v)};
            }
            return v;
        },
        value);
}

std::optional<SettingValue> MemorySettingsStorage::read(const SettingDefinition &setting) const
{
    const auto it = m_values.find(setting.id);
    if (it == m_values.end())
        return std::nullopt;
    return convertSetting(it->second, setting.type);
}

void MemorySettingsStorage::write(const SettingDefinition &setting, const SettingValue &value)
{
    m_values.insert_or_assign(std::string(setting.id), value);
}

void MemorySettingsStorage::remove(const SettingDefinition &setting)
{
    if (const auto it = m_values.find(setting.id); it != m_values.end())
        m_values.erase(it);
}

SettingValue Settings::value(std::string_view id) const
{
    const auto *setting = findSetting(id);
    if (!setting)
        return false;
    if (setting->disposition != Excluded)
        if (auto stored = m_storage.read(*setting))
            return convertSetting(*stored, setting->type);
    return setting->defaultValue;
}

bool Settings::boolean(std::string_view id) const
{
    return std::get<bool>(convertSetting(value(id), Bool));
}

std::int64_t Settings::integer(std::string_view id) const
{
    return std::get<std::int64_t>(convertSetting(value(id), Int));
}

std::string Settings::text(std::string_view id) const
{
    return std::get<std::string>(convertSetting(value(id), String));
}

std::vector<std::string> Settings::list(std::string_view id) const
{
    return std::get<std::vector<std::string>>(convertSetting(value(id), StringList));
}

bool Settings::isSet(std::string_view id) const
{
    const auto *setting = findSetting(id);
    return setting && setting->disposition != Excluded && m_storage.read(*setting).has_value();
}

bool Settings::set(std::string_view id, const SettingValue &value)
{
    const auto *setting = findSetting(id);
    if (!setting || setting->disposition == Excluded)
        return false;
    const SettingValue typed = convertSetting(value, setting->type);
    const SettingValue before = this->value(id);
    m_storage.write(*setting, typed);
    if (before != typed && m_observer)
        m_observer(*setting);
    return true;
}

void Settings::reset(std::string_view id)
{
    const auto *setting = findSetting(id);
    if (!setting || setting->disposition == Excluded)
        return;
    const SettingValue before = value(id);
    m_storage.remove(*setting);
    if (before != setting->defaultValue && m_observer)
        m_observer(*setting);
}

void Settings::resetAll()
{
    for (const auto &setting : definitions())
        reset(setting.id);
}

std::span<const InterimKey> interimKeys()
{
    static constexpr InterimKey keys[] = {
        {"SelectLines/Options", "selectLines.options"},
        {"SelectLines/Recent", "selectLines.recentSelections"},
        {"Subtitles/SaveWithVideoName", "subtitles.saveWithVideoName"},
        {"Video/DontAskForBadResolution", "video.dontAskForBadResolution"},
        {"Convert/fps", "convert.fps"},
        {"Convert/fpsFromVideo", "convert.fpsFromVideo"},
        {"Convert/style", "convert.style"},
        {"Convert/styleCatalog", "convert.styleCatalog"},
        {"Convert/newEndTimes", "convert.newEndTimes"},
        {"Convert/timePerCharacter", "convert.timePerCharacter"},
        {"Convert/prefix", "convert.assTagsToInsertInLine"},
        {"Convert/resolutionWidth", "convert.resolutionWidth"},
        {"Convert/resolutionHeight", "convert.resolutionHeight"},
        {"Updates/AutoCheck", "updater.autoCheck"},
        {"Updates/StableOnly", "updater.checkForStable"},
        {"Updates/NextCheck", "updater.nextCheck"},
        {"ShiftTimes/timeMs", "shiftTimes.time"},
        {"ShiftTimes/frames", "shiftTimes.displayFrames"},
        {"ShiftTimes/whichLines", "shiftTimes.whichLines"},
        {"ShiftTimes/whichTimes", "shiftTimes.whichTimes"},
        {"ShiftTimes/correctEndTimes", "shiftTimes.correctEndTimes"},
        {"ShiftTimes/styles", "shiftTimes.styles"},
        {"ShiftTimes/postprocessor", "postprocessor.on"},
        {"ShiftTimes/leadIn", "postprocessor.leadIn"},
        {"ShiftTimes/leadOut", "postprocessor.leadOut"},
        {"ShiftTimes/thresholdStart", "postprocessor.thresholdStart"},
        {"ShiftTimes/thresholdEnd", "postprocessor.thresholdEnd"},
        {"ShiftTimes/keyframeBeforeStart", "postprocessor.keyframeBeforeStart"},
        {"ShiftTimes/keyframeAfterStart", "postprocessor.keyframeAfterStart"},
        {"ShiftTimes/keyframeBeforeEnd", "postprocessor.keyframeBeforeEnd"},
        {"ShiftTimes/keyframeAfterEnd", "postprocessor.keyframeAfterEnd"},
        {"ShiftTimes/Profiles", "shiftTimes.profiles"},
        {"ColorPicker/Recent", "colourPicker.recentColours"},
        {"ScriptInfo/LinkResolutions", "scriptProperties.linkResolutions"},
        {"Grid/FilterBy", "grid.filterBy"},
        {"Grid/FilterStyles", "grid.filterStyles"},
        {"Grid/FilterInverted", "grid.filterInverted"},
        {"Grid/AddToFilter", "grid.addToFilter"},
        {"Grid/FilterAfterLoad", "grid.filterAfterLoad"},
        {"Grid/IgnoreFiltering", "grid.ignoreFiltering"},
        {"Grid/HiddenColumns", "grid.hideColumns"},
        {"Grid/CopyColumns", "grid.copyColumns"},
        {"Grid/PasteColumns", "grid.pasteColumns"},
        {"Editor/TagButtons", "editor.tagButtons"},
        {"Editor/TagButton1", "editor.tagButton1"},
        {"Editor/TagButton2", "editor.tagButton2"},
        {"Editor/TagButton3", "editor.tagButton3"},
        {"Editor/TagButton4", "editor.tagButton4"},
        {"Editor/TagButton5", "editor.tagButton5"},
        {"Editor/TagButton6", "editor.tagButton6"},
        {"Editor/TagButton7", "editor.tagButton7"},
        {"Editor/TagButton8", "editor.tagButton8"},
        {"Editor/TagButton9", "editor.tagButton9"},
        {"Editor/TagButton10", "editor.tagButton10"},
        {"Editor/TagButton11", "editor.tagButton11"},
        {"Editor/TagButton12", "editor.tagButton12"},
        {"Editor/TagButton13", "editor.tagButton13"},
        {"Editor/TagButton14", "editor.tagButton14"},
        {"Editor/TagButton15", "editor.tagButton15"},
        {"Editor/TagButton16", "editor.tagButton16"},
        {"Editor/TagButton17", "editor.tagButton17"},
        {"Editor/TagButton18", "editor.tagButton18"},
        {"Editor/TagButton19", "editor.tagButton19"},
        {"Editor/TagButton20", "editor.tagButton20"},
        {"Recent/Subtitles", "recent.subtitles"},
        {"Recovery/Capacity", "autosave.maxFiles"},
    };
    return keys;
}

std::span<const InterimBit> interimShiftOptionBits()
{
    // Legacy ShiftTimes::SaveOptions: 1 forward, 2 start time for video/audio,
    // 4 move to video time, 8 move to audio time, 16 frames, 32 tag times.
    static constexpr InterimBit bits[] = {
        {"ShiftTimes/forward", 1},          {"ShiftTimes/fromStartTime", 2}, {"ShiftTimes/moveToVideoTime", 4},
        {"ShiftTimes/moveToAudioTime", 8}, {"ShiftTimes/byFrames", 16},    {"ShiftTimes/tagTimes", 32},
    };
    return bits;
}

} // namespace hikari::application
