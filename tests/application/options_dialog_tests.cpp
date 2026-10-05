// O1: the Options dialog over the settings registry, against legacy
// OptionsDialog.cpp, OptionsPanels.cpp (SubtitlesProperties), NumCtrl.cpp,
// ListControls.cpp (HikariChoice) and config.cpp at 20d647c4.

#include "hikari/application/options_dialog.h"

#include <gtest/gtest.h>

#include <algorithm>

#include <string>
#include <vector>

using namespace hikari::application;

namespace {

// The ConOpt calls of OptionsDialog's constructor, in order, with the legacy
// control: SubtitlesProperties first (it is built before the Main page).
struct LegacyBinding {
    const char *option;
    OptionsControl control;
    OptionsPage page;
};
constexpr LegacyBinding kLegacy[] = {
    // OptionsPanels.cpp:39-56: fieldValues[i] then fieldOnValues[i], then the question.
    {"ASS_PROPERTIES_TITLE", OptionsControl::Text, OptionsPage::SubtitleProperties},
    {"ASS_PROPERTIES_TITLE_ON", OptionsControl::Check, OptionsPage::SubtitleProperties},
    {"ASS_PROPERTIES_SCRIPT", OptionsControl::Text, OptionsPage::SubtitleProperties},
    {"ASS_PROPERTIES_SCRIPT_ON", OptionsControl::Check, OptionsPage::SubtitleProperties},
    {"ASS_PROPERTIES_TRANSLATION", OptionsControl::Text, OptionsPage::SubtitleProperties},
    {"ASS_PROPERTIES_TRANSLATION_ON", OptionsControl::Check, OptionsPage::SubtitleProperties},
    {"ASS_PROPERTIES_EDITING", OptionsControl::Text, OptionsPage::SubtitleProperties},
    {"ASS_PROPERTIES_EDITING_ON", OptionsControl::Check, OptionsPage::SubtitleProperties},
    {"ASS_PROPERTIES_TIMING", OptionsControl::Text, OptionsPage::SubtitleProperties},
    {"ASS_PROPERTIES_TIMING_ON", OptionsControl::Check, OptionsPage::SubtitleProperties},
    {"ASS_PROPERTIES_UPDATE", OptionsControl::Text, OptionsPage::SubtitleProperties},
    {"ASS_PROPERTIES_UPDATE_ON", OptionsControl::Check, OptionsPage::SubtitleProperties},
    {"ASS_PROPERTIES_ASK_FOR_CHANGE", OptionsControl::Check, OptionsPage::SubtitleProperties},
    // OptionsDialog.cpp:340-367: the two choices, then opts[18].
    {"PROGRAM_LANGUAGE", OptionsControl::Language, OptionsPage::Editor},
    {"DICTIONARY_LANGUAGE", OptionsControl::Dictionary, OptionsPage::Editor},
    {"GRID_LOAD_SORTED_SUBS", OptionsControl::Check, OptionsPage::Editor},
    {"SPELLCHECKER_ON", OptionsControl::Check, OptionsPage::Editor},
    {"AUTO_SELECT_LINES_FROM_LAST_TAB", OptionsControl::Check, OptionsPage::Editor},
    {"EDITBOX_SUGGESTIONS_ON_DOUBLE_CLICK", OptionsControl::Check, OptionsPage::Editor},
    {"OPEN_SUBS_IN_NEW_TAB", OptionsControl::Check, OptionsPage::Editor},
    {"EDITBOX_DONT_GO_TO_NEXT_LINE_ON_TIMES_EDIT", OptionsControl::Check, OptionsPage::Editor},
    {"DISABLE_LIVE_VIDEO_EDITING", OptionsControl::Check, OptionsPage::Editor},
    {"GRID_SET_VISIBLE_LINE_AFTER_FULL_SCREEN", OptionsControl::Check, OptionsPage::Editor},
    {"SHIFT_TIMES_CHANGE_VALUES_WITH_TAB", OptionsControl::Check, OptionsPage::Editor},
    {"GRID_CHANGE_ACTIVE_ON_SELECTION", OptionsControl::Check, OptionsPage::Editor},
    {"TL_MODE_SHOW_ORIGINAL", OptionsControl::Check, OptionsPage::Editor},
    {"TL_MODE_HIDE_ORIGINAL_ON_VIDEO", OptionsControl::Check, OptionsPage::Editor},
    {"GRID_DUPLICATION_DONT_CHANGE_SELECTION", OptionsControl::Check, OptionsPage::Editor},
    {"GRID_DONT_CENTER_ACTIVE_LINE", OptionsControl::Check, OptionsPage::Editor},
    {"TEXT_FIELD_ALLOW_NUMPAD_HOTKEYS", OptionsControl::Check, OptionsPage::Editor},
    {"VIDEO_VISUAL_WARNINGS_OFF", OptionsControl::Check, OptionsPage::Editor},
    {"DONT_ASK_FOR_BAD_RESOLUTION", OptionsControl::Check, OptionsPage::Editor},
    {"AUTOMATION_OLD_SCRIPTS_COMPATIBILITY", OptionsControl::Check, OptionsPage::Editor},
    // :385-391, 412-416, 428-434, 437-452.
    {"GRID_SAVE_AFTER_CHARACTER_COUNT", OptionsControl::Number, OptionsPage::EditorAdvanced},
    {"AUTOSAVE_MAX_FILES", OptionsControl::Number, OptionsPage::EditorAdvanced},
    {"AUTOMATION_TRACE_LEVEL", OptionsControl::Number, OptionsPage::EditorAdvanced},
    {"TAB_TEXT_MAX_CHARS", OptionsControl::Number, OptionsPage::EditorAdvanced},
    {"GRID_INSERT_START_OFFSET", OptionsControl::Number, OptionsPage::EditorAdvanced},
    {"GRID_INSERT_END_OFFSET", OptionsControl::Number, OptionsPage::EditorAdvanced},
    {"GRID_TAGS_SWAP_CHARACTER", OptionsControl::Text, OptionsPage::EditorAdvanced},
    {"GRID_FONT", OptionsControl::Font, OptionsPage::EditorAdvanced},
    {"PROGRAM_FONT", OptionsControl::Font, OptionsPage::EditorAdvanced},
    {"CALC_SPACES_AND_PUNCTATION_FOR_WRAPS", OptionsControl::Check, OptionsPage::EditorAdvanced},
    {"CALC_SPACES_AND_PUNCTATION_FOR_CPS", OptionsControl::Check, OptionsPage::EditorAdvanced},
    {"AUTOMATION_LOADING_METHOD", OptionsControl::IndexChoice, OptionsPage::EditorAdvanced},
    {"EXTERNAL_FONTS_DIRECTORY", OptionsControl::Text, OptionsPage::EditorAdvanced},
    // :515, 535, 547, 551-567.
    {"CONVERT_STYLE_CATALOG", OptionsControl::Catalog, OptionsPage::Conversion},
    {"CONVERT_STYLE", OptionsControl::Style, OptionsPage::Conversion},
    {"CONVERT_FPS", OptionsControl::ComboText, OptionsPage::Conversion},
    {"CONVERT_FPS_FROM_VIDEO", OptionsControl::Check, OptionsPage::Conversion},
    {"CONVERT_NEW_END_TIMES", OptionsControl::Check, OptionsPage::Conversion},
    {"CONVERT_SHOW_SETTINGS", OptionsControl::Check, OptionsPage::Conversion},
    {"CONVERT_TIME_PER_CHARACTER", OptionsControl::Number, OptionsPage::Conversion},
    {"CONVERT_RESOLUTION_WIDTH", OptionsControl::Number, OptionsPage::Conversion},
    {"CONVERT_RESOLUTION_HEIGHT", OptionsControl::Number, OptionsPage::Conversion},
    {"CONVERT_ASS_TAGS_TO_INSERT_IN_LINE", OptionsControl::Text, OptionsPage::Conversion},
    // :583-638, VSFILTER_INSTANCE bound last (W2).
    {"VIDEO_FULL_SCREEN_ON_START", OptionsControl::Check, OptionsPage::Video},
    {"VIDEO_PAUSE_ON_CLICK", OptionsControl::Check, OptionsPage::Video},
    {"OPEN_VIDEO_AT_ACTIVE_LINE", OptionsControl::Check, OptionsPage::Video},
    {"VIDEO_GPU_CONVERSION", OptionsControl::Check, OptionsPage::Video},
    {"ACCEPTED_AUDIO_STREAM", OptionsControl::Text, OptionsPage::Video},
    {"FFMS2_VIDEO_SEEKING", OptionsControl::IndexChoice, OptionsPage::Video},
    {"VIDEO_ZOOM_PERCENT", OptionsControl::ZoomText, OptionsPage::Video},
    {"VSFILTER_INSTANCE", OptionsControl::Renderer, OptionsPage::Video},
    // :704-715 opts[13].
    {"AUDIO_DRAW_TIME_CURSOR", OptionsControl::Check, OptionsPage::Audio},
    {"AUDIO_DRAW_SECONDARY_LINES", OptionsControl::Check, OptionsPage::Audio},
    {"AUDIO_DRAW_SELECTION_BACKGROUND", OptionsControl::Check, OptionsPage::Audio},
    {"AUDIO_DRAW_VIDEO_POSITION", OptionsControl::Check, OptionsPage::Audio},
    {"AUDIO_DRAW_KEYFRAMES", OptionsControl::Check, OptionsPage::Audio},
    {"AUDIO_LOCK_SCROLL_ON_CURSOR", OptionsControl::Check, OptionsPage::Audio},
    {"AUDIO_AUTO_FOCUS", OptionsControl::Check, OptionsPage::Audio},
    {"AUDIO_SNAP_TO_KEYFRAMES", OptionsControl::Check, OptionsPage::Audio},
    {"AUDIO_SNAP_TO_OTHER_LINES", OptionsControl::Check, OptionsPage::Audio},
    {"AUDIO_DONT_PLAY_WHEN_LINE_CHANGES", OptionsControl::Check, OptionsPage::Audio},
    {"AUDIO_MERGE_EVERY_N_WITH_SYLLABLE", OptionsControl::Check, OptionsPage::Audio},
    {"AUDIO_KARAOKE_MOVE_ON_CLICK", OptionsControl::Check, OptionsPage::Audio},
    {"AUDIO_RAM_CACHE", OptionsControl::Check, OptionsPage::Audio},
    // :733-739.
    {"AUDIO_DELAY", OptionsControl::Number, OptionsPage::AudioAdvanced},
    {"AUDIO_MARK_PLAY_TIME", OptionsControl::Number, OptionsPage::AudioAdvanced},
    {"AUDIO_LINE_BOUNDARIES_THICKNESS", OptionsControl::Number, OptionsPage::AudioAdvanced},
    {"AUDIO_INACTIVE_LINES_DISPLAY_MODE", OptionsControl::IndexChoice, OptionsPage::AudioAdvanced},
    {"AUDIO_CACHE_FILES_LIMIT", OptionsControl::Number, OptionsPage::AudioAdvanced},
    {"AUDIO_LEAD_IN_VALUE", OptionsControl::Number, OptionsPage::AudioAdvanced},
    {"AUDIO_LEAD_OUT_VALUE", OptionsControl::Number, OptionsPage::AudioAdvanced},
};

OptionsLists lists()
{
    OptionsLists l;
    l.languageTags = {"en", "pl"};
    l.languageNames = {"English", "Polski"};
    l.dictionarySymbols = {"en_GB", "en_US", "pl_PL"};
    l.dictionaryNames = {"English", "English", "Polski"};
    l.catalogs = {"Default", "Karaoke"};
    l.styles = {"Default", "Sign"};
    l.currentCatalog = "Default";
    l.findLanguage = [](std::string_view tag) -> std::string {
        if (tag.starts_with("en"))
            return "English";
        if (tag.starts_with("pl"))
            return "Polski";
        return std::string(tag);
    };
    return l;
}

std::int64_t intOf(const OptionsState &s, const char *id)
{
    return std::get<std::int64_t>(s.at(id));
}

std::string textOf(const OptionsState &s, const char *id)
{
    return std::get<std::string>(s.at(id));
}

} // namespace

TEST(OptionsDialog, BindsTheLegacyControlsInOrder)
{
    const auto all = optionsBindings();
#ifdef _WIN32
    // A4-wasapi-default: the output's host API, the rewrite's own, last.
    ASSERT_EQ(all.size(), std::size(kLegacy) + 1);
    EXPECT_EQ(all.back().setting, "audio.outputHostApi");
    EXPECT_EQ(all.back().control, OptionsControl::IndexChoice);
    EXPECT_EQ(all.back().page, OptionsPage::Audio);
    EXPECT_EQ(all.back().entries, 2);
    const auto bindings = all.first(std::size(kLegacy));
#else
    ASSERT_EQ(all.size(), std::size(kLegacy));
    EXPECT_EQ(findOptionsBinding("audio.outputHostApi"), nullptr); // Windows only
    const auto bindings = all;
#endif
    for (std::size_t i = 0; i < bindings.size(); ++i) {
        const auto *setting = findSetting(bindings[i].setting);
        ASSERT_NE(setting, nullptr) << bindings[i].setting;
        EXPECT_EQ(setting->legacyKey, kLegacy[i].option) << i;
        EXPECT_EQ(bindings[i].control, kLegacy[i].control) << kLegacy[i].option;
        EXPECT_EQ(bindings[i].page, kLegacy[i].page) << kLegacy[i].option;
    }
    // NumCtrl ranges and HikariChoice entries.
    EXPECT_EQ(findOptionsBinding("autosave.maxFiles")->min, 2);
    EXPECT_EQ(findOptionsBinding("autosave.maxFiles")->max, 1000000);
    EXPECT_EQ(findOptionsBinding("audio.delay")->min, -50000000);
    EXPECT_EQ(findOptionsBinding("program.tabTextMaxChars")->min, 20);
    EXPECT_EQ(findOptionsBinding("automation.loadingMethod")->entries, 4);
    EXPECT_EQ(findOptionsBinding("audio.inactiveLinesDisplayMode")->entries, 3);
    EXPECT_EQ(findOptionsBinding("grid.font")->sizeSetting, "grid.fontSize");
    EXPECT_EQ(findOptionsBinding("program.font")->sizeSetting, "program.fontSize");
}

TEST(OptionsDialog, OpensWithTheControlsLegacyShows)
{
    MemorySettingsStorage storage;
    Settings settings(storage);
    const auto l = lists();
    settings.set("video.ffms2Seeking", std::int64_t{9});
    settings.set("autosave.maxFiles", std::int64_t{1});
    settings.set("automation.loadingMethod", std::int64_t{7});
    settings.set("audio.inactiveLinesDisplayMode", std::int64_t{-2});
    settings.set("program.language", std::string("ko"));
    settings.set("editor.dictionaryLanguage", std::string("en_US"));
    settings.set("convert.fps", std::string("29.97"));
    const auto open = openOptionsDialog(settings, l);
    const auto &s = open.state;
    // The seeking method is fixed and stored at once.
    EXPECT_EQ(settings.integer("video.ffms2Seeking"), 2);
    EXPECT_EQ(intOf(s, "video.ffms2Seeking"), 2);
    // NumCtrl clamps what it shows; an unset tab-name limit shows 40, an
    // unset zoom 200.
    EXPECT_EQ(intOf(s, "autosave.maxFiles"), 2);
    EXPECT_EQ(intOf(s, "program.tabTextMaxChars"), 40);
    EXPECT_EQ(textOf(s, "video.zoomPercent"), "200");
    EXPECT_EQ(intOf(s, "convert.resolutionWidth"), 1280);
    // A stored index past the list leaves the choice at -1; a negative one is kept.
    EXPECT_EQ(intOf(s, "automation.loadingMethod"), -1);
    EXPECT_EQ(intOf(s, "audio.inactiveLinesDisplayMode"), -2);
    // An unknown language shows the first; dictionaries are found by their
    // shown name, so en_US selects the first "English" (en_GB).
    EXPECT_EQ(intOf(s, "program.language"), 0);
    EXPECT_EQ(intOf(s, "editor.dictionaryLanguage"), 0);
    EXPECT_EQ(textOf(s, "convert.fps"), "29.97");
    EXPECT_EQ(textOf(s, "grid.font"), "Tahoma");
    EXPECT_EQ(intOf(s, "grid.fontSize"), 10);
    EXPECT_EQ(intOf(s, "convert.styleCatalog"), 0);
    EXPECT_EQ(intOf(s, "convert.style"), 0);
    EXPECT_FALSE(open.catalogMissing);
    EXPECT_FALSE(open.styleMissing);
    EXPECT_TRUE(std::get<bool>(s.at("grid.changeActiveOnSelection")));
    // Only bound controls are in the state.
    EXPECT_FALSE(s.contains("updater.nextCheck"));
    EXPECT_FALSE(s.contains("recent.subtitles"));
}

TEST(OptionsDialog, MissingCatalogOrStyleFallsBackAndWarns)
{
    MemorySettingsStorage storage;
    Settings settings(storage);
    auto l = lists();
    settings.set("convert.styleCatalog", std::string("karaoke"));
    // The option's catalog (found without case) is loaded first.
    EXPECT_EQ(optionsCatalogToLoad(settings, l), std::optional<std::string>("Karaoke"));
    settings.set("convert.styleCatalog", std::string("Gone"));
    settings.set("convert.style", std::string("Gone"));
    l.currentCatalog = "Karaoke";
    EXPECT_EQ(optionsCatalogToLoad(settings, l), std::nullopt);
    const auto open = openOptionsDialog(settings, l);
    EXPECT_TRUE(open.catalogMissing);
    EXPECT_TRUE(open.styleMissing);
    EXPECT_EQ(intOf(open.state, "convert.styleCatalog"), 1); // the current catalog
    EXPECT_EQ(intOf(open.state, "convert.style"), 0);
    // OK writes what the choices now show.
    commitOptionsDialog(settings, l, open.state);
    EXPECT_EQ(settings.text("convert.styleCatalog"), "Karaoke");
    EXPECT_EQ(settings.text("convert.style"), "Default");
}

// FindString is wxArrayString::Index(text, false): CmpNoCase under the
// process locale. Without a comparator only ASCII letters fold (legacy's "C"
// locale); the application passes one that folds every letter when a
// translation language set the locale at startup.
TEST(OptionsDialog, ChoicesFindEntriesWithoutCaseAsTheLocaleFolds)
{
    MemorySettingsStorage storage;
    Settings settings(storage);
    auto l = lists();
    l.catalogs = {"Default", "\xC4\x86wiczenia"}; // "Ćwiczenia"
    settings.set("convert.styleCatalog", std::string("\xC4\x87wiczenia")); // "ćwiczenia"
    EXPECT_EQ(optionsCatalogToLoad(settings, l), std::nullopt);
    EXPECT_TRUE(openOptionsDialog(settings, l).catalogMissing);
    settings.set("convert.style", std::string("sIGN"));
    EXPECT_EQ(intOf(openOptionsDialog(settings, l).state, "convert.style"), 1);
    int calls = 0;
    l.sameIgnoringCase = [&](std::string_view a, std::string_view b) {
        ++calls;
        // Stands in for full folding: the two forms of the letter compare equal.
        const auto fold = [](std::string s) {
            if (s.starts_with("\xC4\x86"))
                s[1] = '\x87';
            return s;
        };
        return fold(std::string(a)) == fold(std::string(b));
    };
    EXPECT_EQ(optionsCatalogToLoad(settings, l), std::optional<std::string>("\xC4\x86wiczenia"));
    const auto open = openOptionsDialog(settings, l);
    EXPECT_FALSE(open.catalogMissing);
    EXPECT_EQ(intOf(open.state, "convert.styleCatalog"), 1);
    EXPECT_GT(calls, 0);
}

TEST(OptionsDialog, OkWritesOnlyBoundControlsThatDiffer)
{
    MemorySettingsStorage storage;
    Settings settings(storage);
    const auto l = lists();
    settings.set("automation.loadingMethod", std::int64_t{7});
    settings.set("audio.inactiveLinesDisplayMode", std::int64_t{-2});
    settings.set("editor.dictionaryLanguage", std::string("en_US"));
    settings.set("program.language", std::string("ko"));
    settings.set("updater.nextCheck", std::int64_t{100});
    auto state = openOptionsDialog(settings, l).state;
    // Something else writes while the dialog is open.
    settings.set("updater.nextCheck", std::int64_t{200});
    settings.set("recent.subtitles", std::vector<std::string>{"a.ass"});
    const auto written = commitOptionsDialog(settings, l, state);
    EXPECT_EQ(settings.integer("updater.nextCheck"), 200);
    EXPECT_EQ(settings.list("recent.subtitles"), std::vector<std::string>{"a.ass"});
    // GetSelection -1 against 7 writes -1; -2 is what the choice holds.
    EXPECT_EQ(settings.integer("automation.loadingMethod"), -1);
    EXPECT_EQ(settings.integer("audio.inactiveLinesDisplayMode"), -2);
    // The first "English" dictionary is written over en_US (FindString by name).
    EXPECT_EQ(settings.text("editor.dictionaryLanguage"), "en_GB");
    // The language falls back to English and is written.
    EXPECT_EQ(settings.text("program.language"), "en");
    // The shown fallbacks are written; untouched values are not.
    EXPECT_EQ(settings.integer("program.tabTextMaxChars"), 40);
    EXPECT_EQ(settings.integer("video.zoomPercent"), 200);
    EXPECT_FALSE(settings.isSet("autosave.maxFiles"));
    EXPECT_FALSE(settings.isSet("grid.loadSortedSubs"));
    EXPECT_FALSE(settings.isSet("grid.font"));
    const std::vector<std::string> expected{"program.language",          "editor.dictionaryLanguage",
                                            "program.tabTextMaxChars",   "automation.loadingMethod",
                                            "video.zoomPercent"};
    EXPECT_EQ(written, expected);
    // Numbers are clamped (NumCtrl::GetInt); the zoom's text is stored as typed.
    state["autosave.maxFiles"] = std::int64_t{1};
    state["video.zoomPercent"] = std::string("50");
    state["grid.fontSize"] = std::int64_t{12};
    commitOptionsDialog(settings, l, state);
    EXPECT_EQ(settings.integer("autosave.maxFiles"), 2);
    EXPECT_EQ(settings.integer("video.zoomPercent"), 50);
    EXPECT_EQ(settings.integer("grid.fontSize"), 12);
    EXPECT_FALSE(settings.isSet("grid.font"));
    // An emptied zoom is the empty option: unset.
    state["video.zoomPercent"] = std::string();
    commitOptionsDialog(settings, l, state);
    EXPECT_FALSE(settings.isSet("video.zoomPercent"));
}

TEST(OptionsDialog, ExternalFontsFolderGetsItsSeparator)
{
    MemorySettingsStorage storage;
    Settings settings(storage);
    auto l = lists();
    auto state = openOptionsDialog(settings, l).state;
    state["fonts.externalDirectory"] = std::string("C:\\fonts");
    commitOptionsDialog(settings, l, state);
    EXPECT_EQ(settings.text("fonts.externalDirectory"), "C:/fonts/"); // HikariNormalizePath off Windows
    l.pathSeparator = '\\';
    l.slashesForBackslashes = false;
    state["fonts.externalDirectory"] = std::string("C:\\other");
    commitOptionsDialog(settings, l, state);
    EXPECT_EQ(settings.text("fonts.externalDirectory"), "C:\\other\\");
    // Unchanged text is not normalized again.
    state = openOptionsDialog(settings, l).state;
    commitOptionsDialog(settings, l, state);
    EXPECT_EQ(settings.text("fonts.externalDirectory"), "C:\\other\\");
}

TEST(OptionsDialog, SetDefaultRefreshesTheControlsAsLegacyDoes)
{
    MemorySettingsStorage storage;
    Settings settings(storage);
    const auto l = lists();
    settings.set("program.language", std::string("pl"));
    settings.set("video.zoomPercent", std::int64_t{300});
    settings.set("program.tabTextMaxChars", std::int64_t{60});
    settings.set("grid.loadSortedSubs", true);
    auto state = openOptionsDialog(settings, l).state;
    EXPECT_EQ(intOf(state, "program.language"), 1);
    settings.resetAll();
    state = refreshOptionsDialogAfterReset(settings, l, state);
    // Check boxes, texts, fonts and the FPS box show the defaults.
    EXPECT_FALSE(std::get<bool>(state.at("grid.loadSortedSubs")));
    EXPECT_EQ(textOf(state, "convert.fps"), "23.976");
    EXPECT_EQ(textOf(state, "grid.font"), "Tahoma");
    // The index choices show FindString of "0"/"2"/"1": nothing.
    EXPECT_EQ(intOf(state, "automation.loadingMethod"), -1);
    EXPECT_EQ(intOf(state, "video.ffms2Seeking"), -1);
    EXPECT_EQ(intOf(state, "audio.inactiveLinesDisplayMode"), -1);
    // The string choices show the entry at wxAtoi of their text: 0.
    EXPECT_EQ(intOf(state, "program.language"), 0);
    EXPECT_EQ(intOf(state, "editor.dictionaryLanguage"), 0);
    EXPECT_EQ(intOf(state, "convert.styleCatalog"), 0);
    EXPECT_EQ(intOf(state, "convert.style"), 0);
    // NumCtrl::SetInt clamps without the constructor's 40; the zoom shows its raw (empty) text.
    EXPECT_EQ(intOf(state, "program.tabTextMaxChars"), 20);
    EXPECT_EQ(intOf(state, "autosave.maxFiles"), 3);
    EXPECT_EQ(textOf(state, "video.zoomPercent"), "");
    // OK then writes what the controls show.
    commitOptionsDialog(settings, l, state);
    EXPECT_EQ(settings.integer("automation.loadingMethod"), -1);
    EXPECT_EQ(settings.integer("video.ffms2Seeking"), -1);
    EXPECT_EQ(settings.integer("audio.inactiveLinesDisplayMode"), -1);
    EXPECT_EQ(settings.text("program.language"), "en");
    EXPECT_EQ(settings.text("editor.dictionaryLanguage"), "en_GB");
    EXPECT_EQ(settings.integer("program.tabTextMaxChars"), 20);
    EXPECT_FALSE(settings.isSet("video.zoomPercent"));
    EXPECT_FALSE(settings.isSet("autosave.maxFiles"));
    EXPECT_FALSE(settings.isSet("convert.styleCatalog")); // "Default" again
    // Reopening fixes the seeking method once more.
    EXPECT_EQ(intOf(openOptionsDialog(settings, l).state, "video.ffms2Seeking"), 2);
}

TEST(OptionsDialog, SetDefaultKeepsAChoiceWhoseIndexIsPastItsList)
{
    MemorySettingsStorage storage;
    Settings settings(storage);
    auto l = lists();
    l.dictionarySymbols.clear();
    l.dictionaryNames = {"Put files .dic and .aff to \"Dictionary\" folder"};
    auto state = openOptionsDialog(settings, l).state;
    EXPECT_EQ(intOf(state, "editor.dictionaryLanguage"), -1);
    // "12" names no entry: SetSelection(12) changes nothing.
    state["program.language"] = std::int64_t{1};
    settings.resetAll();
    settings.set("program.language", std::string("12"));
    state = refreshOptionsDialogAfterReset(settings, l, state);
    EXPECT_EQ(intOf(state, "program.language"), 1);
    EXPECT_EQ(intOf(state, "editor.dictionaryLanguage"), 0); // the placeholder entry
    commitOptionsDialog(settings, l, state);
    EXPECT_EQ(settings.text("program.language"), "pl");
    EXPECT_FALSE(settings.isSet("editor.dictionaryLanguage")); // no symbol to write
}

// W2: "Subtitle display filter" (OptionsDialog.cpp:616-638, 1145-1152): the
// CSRI renderers then libass; the selection's name is written.
TEST(OptionsDialog, TheSubtitleDisplayFilterChoosesARendererByName)
{
    MemorySettingsStorage storage;
    Settings settings(storage);
    auto l = lists();
    // Only libass (Linux, or Windows without a CSRI renderer): not offered.
    l.renderers = {"libass"};
    auto state = openOptionsDialog(settings, l).state;
    EXPECT_FALSE(state.contains("video.subtitleProvider"));
    EXPECT_FALSE(std::ranges::contains(commitOptionsDialog(settings, l, state), std::string("video.subtitleProvider")));
    EXPECT_FALSE(settings.isSet("video.subtitleProvider"));
    settings.resetAll();
    EXPECT_FALSE(refreshOptionsDialogAfterReset(settings, l, state).contains("video.subtitleProvider"));

    l.renderers = {"xy-vsfilter_textsub", "vsfiltermod_textsub", "libass"};
    // Unset reads as libass (W2-libass-default; legacy Windows showed the
    // first entry, its default CSRI renderer). OK writes the shown name when
    // it differs, as SetOptions does.
    state = openOptionsDialog(settings, l).state;
    EXPECT_EQ(intOf(state, "video.subtitleProvider"), 2);
    EXPECT_TRUE(std::ranges::contains(commitOptionsDialog(settings, l, state), std::string("video.subtitleProvider")));
    EXPECT_EQ(settings.text("video.subtitleProvider"), "libass");
    settings.set("video.subtitleProvider", std::string("xy-vsfilter_textsub"));
    state = openOptionsDialog(settings, l).state;
    EXPECT_EQ(intOf(state, "video.subtitleProvider"), 0);
    // Index is exact: another case is not listed.
    settings.set("video.subtitleProvider", std::string("LIBASS"));
    EXPECT_EQ(intOf(openOptionsDialog(settings, l).state, "video.subtitleProvider"), 0);
    state["video.subtitleProvider"] = std::int64_t{1};
    commitOptionsDialog(settings, l, state);
    EXPECT_EQ(settings.text("video.subtitleProvider"), "vsfiltermod_textsub");
    // Set default: the default is unset, which is libass, so the choice shows
    // libass (legacy's SetSelection(GetInt("")) showed the first entry, its
    // default CSRI renderer) and OK keeps the video on libass.
    settings.resetAll();
    state = refreshOptionsDialogAfterReset(settings, l, state);
    EXPECT_EQ(intOf(state, "video.subtitleProvider"), 2);
    commitOptionsDialog(settings, l, state);
    EXPECT_EQ(settings.text("video.subtitleProvider"), "libass");
    // libass listed first: the reset still finds it by name.
    l.renderers = {"libass", "xy-vsfilter_textsub"};
    settings.resetAll();
    state = refreshOptionsDialogAfterReset(settings, l, openOptionsDialog(settings, l).state);
    EXPECT_EQ(intOf(state, "video.subtitleProvider"), 0);
    // A name that is not listed opens on the first entry, the default CSRI
    // renderer the selection falls back to (GetVSFilter), so OK names what
    // draws.
    l.renderers = {"xy-vsfilter_textsub", "libass"};
    settings.set("video.subtitleProvider", std::string("removed_textsub"));
    EXPECT_EQ(intOf(openOptionsDialog(settings, l).state, "video.subtitleProvider"), 0);
}

TEST(OptionsDialog, ChangingTheCatalogListsItsStyles)
{
    MemorySettingsStorage storage;
    Settings settings(storage);
    auto l = lists();
    auto state = openOptionsDialog(settings, l).state;
    state["convert.style"] = std::int64_t{1};
    l.styles = {"Karaoke"};
    chooseOptionsDialogCatalog(l, state, 1);
    EXPECT_EQ(intOf(state, "convert.styleCatalog"), 1);
    EXPECT_EQ(intOf(state, "convert.style"), 0);
    // A catalog without Styles keeps the old index past the empty list, so
    // OK writes an empty style.
    state["convert.style"] = std::int64_t{1};
    l.styles.clear();
    chooseOptionsDialogCatalog(l, state, 0);
    EXPECT_EQ(intOf(state, "convert.style"), 1);
    commitOptionsDialog(settings, l, state);
    EXPECT_EQ(settings.text("convert.style"), "");
    EXPECT_EQ(settings.text("convert.styleCatalog"), "Default");
}

TEST(OptionsDialog, NumbersReadAsLegacyReadsThem)
{
    EXPECT_EQ(legacyAtoi("12abc"), 12);
    EXPECT_EQ(legacyAtoi("  -7"), -7);
    EXPECT_EQ(legacyAtoi("en_US"), 0);
    EXPECT_EQ(legacyAtoi(""), 0);
    EXPECT_EQ(numCtrlValue("12.7", 0, 100), 12);
    EXPECT_EQ(numCtrlValue("12,7", 0, 100), 12);
    EXPECT_EQ(numCtrlValue("wide", 1, 3000), 1);
    EXPECT_EQ(numCtrlValue("", 2, 10), 2);
    EXPECT_EQ(numCtrlValue("5000000", 2, 1000000), 1000000);
}
