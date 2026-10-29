#include "hikari/application/options_dialog.h"
#include "hikari/core/grammar.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <optional>

#include <string>
#include <utility>
#include <vector>

using namespace hikari::application;

namespace {
struct LegacyBinding {
    const char *option;
    OptionsControl control;
    OptionsPage page;
};
constexpr LegacyBinding kLegacy[] = {
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
    {"PROGRAM_LANGUAGE", OptionsControl::Language, OptionsPage::General},
    {"DICTIONARY_LANGUAGE", OptionsControl::Dictionary, OptionsPage::Spelling},
    {"GRID_LOAD_SORTED_SUBS", OptionsControl::Check, OptionsPage::General},
    {"SPELLCHECKER_ON", OptionsControl::Check, OptionsPage::Spelling},
    {"AUTO_SELECT_LINES_FROM_LAST_TAB", OptionsControl::Check, OptionsPage::Grid},
    {"EDITBOX_SUGGESTIONS_ON_DOUBLE_CLICK", OptionsControl::Check, OptionsPage::Spelling},
    {"OPEN_SUBS_IN_NEW_TAB", OptionsControl::Check, OptionsPage::General},
    {"EDITBOX_DONT_GO_TO_NEXT_LINE_ON_TIMES_EDIT", OptionsControl::Check, OptionsPage::LineEditor},
    {"DISABLE_LIVE_VIDEO_EDITING", OptionsControl::Check, OptionsPage::Video},
    {"GRID_SET_VISIBLE_LINE_AFTER_FULL_SCREEN", OptionsControl::Check, OptionsPage::Grid},
    {"GRID_CHANGE_ACTIVE_ON_SELECTION", OptionsControl::Check, OptionsPage::Grid},
    {"TL_MODE_SHOW_ORIGINAL", OptionsControl::Check, OptionsPage::LineEditor},
    {"TL_MODE_HIDE_ORIGINAL_ON_VIDEO", OptionsControl::Check, OptionsPage::LineEditor},
    {"GRID_DUPLICATION_DONT_CHANGE_SELECTION", OptionsControl::Check, OptionsPage::Grid},
    {"GRID_DONT_CENTER_ACTIVE_LINE", OptionsControl::Check, OptionsPage::Grid},
    {"TEXT_FIELD_ALLOW_NUMPAD_HOTKEYS", OptionsControl::Check, OptionsPage::LineEditor},
    {"VIDEO_VISUAL_WARNINGS_OFF", OptionsControl::Check, OptionsPage::Video},
    {"DONT_ASK_FOR_BAD_RESOLUTION", OptionsControl::Check, OptionsPage::Video},
    {"AUTOMATION_OLD_SCRIPTS_COMPATIBILITY", OptionsControl::Check, OptionsPage::Automation},
    {"GRID_SAVE_AFTER_CHARACTER_COUNT", OptionsControl::Number, OptionsPage::General},
    {"AUTOSAVE_MAX_FILES", OptionsControl::Number, OptionsPage::General},
    {"AUTOMATION_TRACE_LEVEL", OptionsControl::Number, OptionsPage::Automation},
    {"TAB_TEXT_MAX_CHARS", OptionsControl::Number, OptionsPage::General},
    {"GRID_INSERT_START_OFFSET", OptionsControl::Number, OptionsPage::Grid},
    {"GRID_INSERT_END_OFFSET", OptionsControl::Number, OptionsPage::Grid},
    {"GRID_TAGS_SWAP_CHARACTER", OptionsControl::Text, OptionsPage::Grid},
    {"GRID_FONT", OptionsControl::Font, OptionsPage::Grid},
    {"PROGRAM_FONT", OptionsControl::Font, OptionsPage::Appearance},
    {"CALC_SPACES_AND_PUNCTATION_FOR_WRAPS", OptionsControl::Check, OptionsPage::Grid},
    {"CALC_SPACES_AND_PUNCTATION_FOR_CPS", OptionsControl::Check, OptionsPage::Grid},
    {"AUTOMATION_LOADING_METHOD", OptionsControl::IndexChoice, OptionsPage::Automation},
    {"EXTERNAL_FONTS_DIRECTORY", OptionsControl::Text, OptionsPage::General},
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
    {"VIDEO_FULL_SCREEN_ON_START", OptionsControl::Check, OptionsPage::Video},
    {"VIDEO_PAUSE_ON_CLICK", OptionsControl::Check, OptionsPage::Video},
    {"OPEN_VIDEO_AT_ACTIVE_LINE", OptionsControl::Check, OptionsPage::Video},
    {"VIDEO_GPU_CONVERSION", OptionsControl::Check, OptionsPage::Video},
    {"ACCEPTED_AUDIO_STREAM", OptionsControl::Text, OptionsPage::Video},
    {"FFMS2_VIDEO_SEEKING", OptionsControl::IndexChoice, OptionsPage::Video},
    {"VIDEO_ZOOM_PERCENT", OptionsControl::ZoomText, OptionsPage::Video},
    {"VSFILTER_INSTANCE", OptionsControl::Renderer, OptionsPage::Video},
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
    {"AUDIO_DELAY", OptionsControl::Number, OptionsPage::Audio},
    {"AUDIO_MARK_PLAY_TIME", OptionsControl::Number, OptionsPage::Audio},
    {"AUDIO_LINE_BOUNDARIES_THICKNESS", OptionsControl::Number, OptionsPage::Audio},
    {"AUDIO_INACTIVE_LINES_DISPLAY_MODE", OptionsControl::IndexChoice, OptionsPage::Audio},
    {"AUDIO_CACHE_FILES_LIMIT", OptionsControl::Number, OptionsPage::Audio},
    {"AUDIO_LEAD_IN_VALUE", OptionsControl::Number, OptionsPage::Audio},
    {"AUDIO_LEAD_OUT_VALUE", OptionsControl::Number, OptionsPage::Audio},
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

}

TEST(OptionsDialog, BindsTheLegacyControlsInOrder)
{
    const auto all = optionsBindings();
    ASSERT_GT(all.size(), std::size(kLegacy) + 33);
    const auto &fromVideo = all[std::size(kLegacy)];
    EXPECT_EQ(fromVideo.setting, "automation.scriptResolutionFromVideo");
    EXPECT_EQ(fromVideo.control, OptionsControl::Check);
    EXPECT_EQ(fromVideo.page, OptionsPage::Video);
    const auto &autoHide = all[std::size(kLegacy) + 1];
    EXPECT_EQ(autoHide.setting, "grid.autoHideColumns");
    EXPECT_EQ(autoHide.control, OptionsControl::Check);
    EXPECT_EQ(autoHide.page, OptionsPage::Grid);
    const auto &device = all[std::size(kLegacy) + 2];
    EXPECT_EQ(device.setting, "audio.outputDevice");
    EXPECT_EQ(device.control, OptionsControl::OutputDevice);
    EXPECT_EQ(device.page, OptionsPage::Audio);
    const auto &frames = all[std::size(kLegacy) + 3];
    EXPECT_EQ(frames.setting, "editor.timesToFramesSwitch");
    EXPECT_EQ(frames.control, OptionsControl::Check);
    EXPECT_EQ(frames.page, OptionsPage::LineEditor);
    const auto &tags = all[std::size(kLegacy) + 4];
    EXPECT_EQ(tags.setting, "editor.hideTags");
    EXPECT_EQ(tags.control, OptionsControl::Check);
    EXPECT_EQ(tags.page, OptionsPage::LineEditor);
    const auto &layout = all[std::size(kLegacy) + 5];
    EXPECT_EQ(layout.setting, "subtitles.layoutInFile");
    EXPECT_EQ(layout.control, OptionsControl::Check);
    EXPECT_EQ(layout.page, OptionsPage::SubtitleProperties);
    ASSERT_NE(findSetting("subtitles.layoutInFile"), nullptr);
    EXPECT_EQ(std::get<bool>(findSetting("subtitles.layoutInFile")->defaultValue), true);
    const auto &seek = all[std::size(kLegacy) + 6];
    EXPECT_EQ(seek.setting, "video.moveToActiveLine");
    EXPECT_EQ(seek.control, OptionsControl::IndexChoice);
    EXPECT_EQ(seek.page, OptionsPage::Video);
    EXPECT_EQ(seek.entries, 6);
    const auto &play = all[std::size(kLegacy) + 7];
    EXPECT_EQ(play.setting, "video.playAfterSelection");
    EXPECT_EQ(play.control, OptionsControl::IndexChoice);
    EXPECT_EQ(play.page, OptionsPage::Video);
    EXPECT_EQ(play.entries, 4);
    const auto &consonants = all[std::size(kLegacy) + 8];
    EXPECT_EQ(consonants.setting, "audio.karaokeDoubleConsonantApart");
    EXPECT_EQ(consonants.control, OptionsControl::Check);
    EXPECT_EQ(consonants.page, OptionsPage::Audio);
    const auto &kf = all[std::size(kLegacy) + 9];
    EXPECT_EQ(kf.setting, "audio.karaokeKfThreshold");
    EXPECT_EQ(kf.control, OptionsControl::Number);
    EXPECT_EQ(kf.page, OptionsPage::Audio);
    EXPECT_EQ(kf.min, 10);
    EXPECT_EQ(std::get<std::int64_t>(findSetting("audio.karaokeKfThreshold")->defaultValue), 100);
    const auto &toasts = all[std::size(kLegacy) + 10];
    EXPECT_EQ(toasts.setting, "notices.toasts");
    EXPECT_EQ(toasts.control, OptionsControl::Check);
    EXPECT_EQ(toasts.page, OptionsPage::General);
    const std::pair<const char *, OptionsControl> updates[] = {{"updater.autoCheck", OptionsControl::Check},
                                                               {"updater.checkForStable", OptionsControl::Check},
                                                               {"updater.autoInstall", OptionsControl::Check},
                                                               {"updater.skippedVersion", OptionsControl::Text}};
    for (std::size_t i = 0; i < std::size(updates); ++i) {
        const auto &binding = all[std::size(kLegacy) + 11 + i];
        EXPECT_EQ(binding.setting, updates[i].first);
        EXPECT_EQ(binding.control, updates[i].second) << updates[i].first;
        EXPECT_EQ(binding.page, OptionsPage::Updates) << updates[i].first;
    }
    const auto &fill = all[std::size(kLegacy) + 15];
    EXPECT_EQ(fill.setting, "automation.fillLastAnswers");
    EXPECT_EQ(fill.control, OptionsControl::Check);
    EXPECT_EQ(fill.page, OptionsPage::Automation);
    ASSERT_NE(findSetting("automation.fillLastAnswers"), nullptr);
    EXPECT_EQ(std::get<bool>(findSetting("automation.fillLastAnswers")->defaultValue), false);
    const auto &previewAt = all[std::size(kLegacy) + 16];
    EXPECT_EQ(previewAt.setting, "video.previewSubtitlesAt");
    EXPECT_EQ(previewAt.control, OptionsControl::IndexChoice);
    EXPECT_EQ(previewAt.page, OptionsPage::Video);
    EXPECT_EQ(previewAt.entries, 2);
    ASSERT_NE(findSetting("video.previewSubtitlesAt"), nullptr);
    EXPECT_EQ(std::get<std::int64_t>(findSetting("video.previewSubtitlesAt")->defaultValue), 0);
    const auto &previewText = all[std::size(kLegacy) + 17];
    EXPECT_EQ(previewText.setting, "styles.previewText");
    EXPECT_EQ(previewText.control, OptionsControl::Text);
    EXPECT_EQ(previewText.page, OptionsPage::Appearance);
    const auto &grammar = all[std::size(kLegacy) + 18];
    EXPECT_EQ(grammar.setting, "grammar.language");
    EXPECT_EQ(grammar.control, OptionsControl::IndexChoice);
    EXPECT_EQ(grammar.page, OptionsPage::Spelling);
    EXPECT_EQ(grammar.entries, 5);
    ASSERT_NE(findSetting("grammar.language"), nullptr);
    EXPECT_EQ(std::get<std::int64_t>(findSetting("grammar.language")->defaultValue), 0);
    const auto groups = hikari::core::grammar::groupIds();
    ASSERT_EQ(groups.size(), 15u);
    for (std::size_t i = 0; i < groups.size(); ++i) {
        const auto &binding = all[std::size(kLegacy) + 19 + i];
        EXPECT_EQ(binding.setting, "grammar.group." + std::string(groups[i]));
        EXPECT_EQ(binding.control, OptionsControl::Check);
        EXPECT_EQ(binding.page, OptionsPage::Spelling);
        ASSERT_NE(findSetting(binding.setting), nullptr);
        EXPECT_EQ(std::get<bool>(findSetting(binding.setting)->defaultValue), true);
    }
    const auto &usageData = all[std::size(kLegacy) + 34];
    EXPECT_EQ(usageData.setting, "privacy.usageData");
    EXPECT_EQ(usageData.control, OptionsControl::Check);
    EXPECT_EQ(usageData.page, OptionsPage::Updates);
    ASSERT_NE(findSetting("privacy.usageData"), nullptr);
    EXPECT_EQ(std::get<bool>(findSetting("privacy.usageData")->defaultValue), true);
    const auto &alwaysSend = all[std::size(kLegacy) + 35];
    EXPECT_EQ(alwaysSend.setting, "privacy.alwaysSendCrashReports");
    EXPECT_EQ(alwaysSend.control, OptionsControl::Check);
    EXPECT_EQ(alwaysSend.page, OptionsPage::Updates);
    ASSERT_NE(findSetting("privacy.alwaysSendCrashReports"), nullptr);
    EXPECT_EQ(std::get<bool>(findSetting("privacy.alwaysSendCrashReports")->defaultValue), false);
#ifdef _WIN32
    ASSERT_EQ(all.size(), std::size(kLegacy) + 38);
    const auto &hostApi = all[std::size(kLegacy) + 36];
    EXPECT_EQ(hostApi.setting, "audio.outputHostApi");
    EXPECT_EQ(hostApi.control, OptionsControl::IndexChoice);
    EXPECT_EQ(hostApi.page, OptionsPage::Audio);
    EXPECT_EQ(hostApi.entries, 2);
    EXPECT_EQ(all.back().setting, "video.playbackPlayer");
    EXPECT_EQ(all.back().control, OptionsControl::IndexChoice);
    EXPECT_EQ(all.back().page, OptionsPage::Video);
    EXPECT_EQ(all.back().entries, 2);
    const auto bindings = all.first(std::size(kLegacy));
#else
    ASSERT_EQ(all.size(), std::size(kLegacy) + 36);
    EXPECT_EQ(findOptionsBinding("audio.outputHostApi"), nullptr);
    EXPECT_EQ(findOptionsBinding("video.playbackPlayer"), nullptr);
    const auto bindings = all.first(std::size(kLegacy));
#endif
    for (std::size_t i = 0; i < bindings.size(); ++i) {
        const auto *setting = findSetting(bindings[i].setting);
        ASSERT_NE(setting, nullptr) << bindings[i].setting;
        EXPECT_EQ(setting->legacyKey, kLegacy[i].option) << i;
        EXPECT_EQ(bindings[i].control, kLegacy[i].control) << kLegacy[i].option;
        EXPECT_EQ(bindings[i].page, kLegacy[i].page) << kLegacy[i].option;
    }
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
    EXPECT_EQ(settings.integer("video.ffms2Seeking"), 2);
    EXPECT_EQ(intOf(s, "video.ffms2Seeking"), 2);
    EXPECT_EQ(intOf(s, "autosave.maxFiles"), 2);
    EXPECT_EQ(intOf(s, "program.tabTextMaxChars"), 40);
    EXPECT_EQ(textOf(s, "video.zoomPercent"), "200");
    EXPECT_EQ(intOf(s, "convert.resolutionWidth"), 1280);
    EXPECT_EQ(intOf(s, "automation.loadingMethod"), -1);
    EXPECT_EQ(intOf(s, "audio.inactiveLinesDisplayMode"), -2);
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
    EXPECT_FALSE(s.contains("updater.nextCheck"));
    EXPECT_FALSE(s.contains("recent.subtitles"));
}

TEST(OptionsDialog, MissingCatalogOrStyleFallsBackAndWarns)
{
    MemorySettingsStorage storage;
    Settings settings(storage);
    auto l = lists();
    settings.set("convert.styleCatalog", std::string("karaoke"));
    EXPECT_EQ(optionsCatalogToLoad(settings, l), std::optional<std::string>("Karaoke"));
    settings.set("convert.styleCatalog", std::string("Gone"));
    settings.set("convert.style", std::string("Gone"));
    l.currentCatalog = "Karaoke";
    EXPECT_EQ(optionsCatalogToLoad(settings, l), std::nullopt);
    const auto open = openOptionsDialog(settings, l);
    EXPECT_TRUE(open.catalogMissing);
    EXPECT_TRUE(open.styleMissing);
    EXPECT_EQ(intOf(open.state, "convert.styleCatalog"), 1);
    EXPECT_EQ(intOf(open.state, "convert.style"), 0);
    commitOptionsDialog(settings, l, open.state);
    EXPECT_EQ(settings.text("convert.styleCatalog"), "Karaoke");
    EXPECT_EQ(settings.text("convert.style"), "Default");
}

TEST(OptionsDialog, ChoicesFindEntriesWithoutCaseAsTheLocaleFolds)
{
    MemorySettingsStorage storage;
    Settings settings(storage);
    auto l = lists();
    l.catalogs = {"Default", "\xC4\x86wiczenia"};
    settings.set("convert.styleCatalog", std::string("\xC4\x87wiczenia"));
    EXPECT_EQ(optionsCatalogToLoad(settings, l), std::nullopt);
    EXPECT_TRUE(openOptionsDialog(settings, l).catalogMissing);
    settings.set("convert.style", std::string("sIGN"));
    EXPECT_EQ(intOf(openOptionsDialog(settings, l).state, "convert.style"), 1);
    int calls = 0;
    l.sameIgnoringCase = [&](std::string_view a, std::string_view b) {
        ++calls;
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
    settings.set("updater.nextCheck", std::int64_t{200});
    settings.set("recent.subtitles", std::vector<std::string>{"a.ass"});
    const auto written = commitOptionsDialog(settings, l, state);
    EXPECT_EQ(settings.integer("updater.nextCheck"), 200);
    EXPECT_EQ(settings.list("recent.subtitles"), std::vector<std::string>{"a.ass"});
    EXPECT_EQ(settings.integer("automation.loadingMethod"), -1);
    EXPECT_EQ(settings.integer("audio.inactiveLinesDisplayMode"), -2);
    EXPECT_EQ(settings.text("editor.dictionaryLanguage"), "en_GB");
    EXPECT_EQ(settings.text("program.language"), "en");
    EXPECT_EQ(settings.integer("program.tabTextMaxChars"), 40);
    EXPECT_EQ(settings.integer("video.zoomPercent"), 200);
    EXPECT_FALSE(settings.isSet("autosave.maxFiles"));
    EXPECT_FALSE(settings.isSet("grid.loadSortedSubs"));
    EXPECT_FALSE(settings.isSet("grid.font"));
    const std::vector<std::string> expected{"program.language",          "editor.dictionaryLanguage",
                                            "program.tabTextMaxChars",   "automation.loadingMethod",
                                            "video.zoomPercent"};
    EXPECT_EQ(written, expected);
    state["autosave.maxFiles"] = std::int64_t{1};
    state["video.zoomPercent"] = std::string("50");
    state["grid.fontSize"] = std::int64_t{12};
    commitOptionsDialog(settings, l, state);
    EXPECT_EQ(settings.integer("autosave.maxFiles"), 2);
    EXPECT_EQ(settings.integer("video.zoomPercent"), 50);
    EXPECT_EQ(settings.integer("grid.fontSize"), 12);
    EXPECT_FALSE(settings.isSet("grid.font"));
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
    EXPECT_EQ(settings.text("fonts.externalDirectory"), "C:/fonts/");
    l.pathSeparator = '\\';
    l.slashesForBackslashes = false;
    state["fonts.externalDirectory"] = std::string("C:\\other");
    commitOptionsDialog(settings, l, state);
    EXPECT_EQ(settings.text("fonts.externalDirectory"), "C:\\other\\");
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
    EXPECT_FALSE(std::get<bool>(state.at("grid.loadSortedSubs")));
    EXPECT_EQ(textOf(state, "convert.fps"), "23.976");
    EXPECT_EQ(textOf(state, "grid.font"), "Tahoma");
    EXPECT_EQ(intOf(state, "automation.loadingMethod"), -1);
    EXPECT_EQ(intOf(state, "video.ffms2Seeking"), -1);
    EXPECT_EQ(intOf(state, "audio.inactiveLinesDisplayMode"), -1);
    EXPECT_EQ(intOf(state, "program.language"), 0);
    EXPECT_EQ(intOf(state, "editor.dictionaryLanguage"), 0);
    EXPECT_EQ(intOf(state, "convert.styleCatalog"), 0);
    EXPECT_EQ(intOf(state, "convert.style"), 0);
    EXPECT_EQ(intOf(state, "program.tabTextMaxChars"), 20);
    EXPECT_EQ(intOf(state, "autosave.maxFiles"), 3);
    EXPECT_EQ(textOf(state, "video.zoomPercent"), "");
    commitOptionsDialog(settings, l, state);
    EXPECT_EQ(settings.integer("automation.loadingMethod"), -1);
    EXPECT_EQ(settings.integer("video.ffms2Seeking"), -1);
    EXPECT_EQ(settings.integer("audio.inactiveLinesDisplayMode"), -1);
    EXPECT_EQ(settings.text("program.language"), "en");
    EXPECT_EQ(settings.text("editor.dictionaryLanguage"), "en_GB");
    EXPECT_EQ(settings.integer("program.tabTextMaxChars"), 20);
    EXPECT_FALSE(settings.isSet("video.zoomPercent"));
    EXPECT_FALSE(settings.isSet("autosave.maxFiles"));
    EXPECT_FALSE(settings.isSet("convert.styleCatalog"));
    EXPECT_EQ(intOf(openOptionsDialog(settings, l).state, "video.ffms2Seeking"), 2);
}

TEST(OptionsDialog, TheFollowListsShowAndKeepTheirChoices)
{
    MemorySettingsStorage storage;
    Settings settings(storage);
    const auto l = lists();
    settings.set("video.moveToActiveLine", std::int64_t{3});
    settings.set("video.playAfterSelection", std::int64_t{2});
    auto state = openOptionsDialog(settings, l).state;
    EXPECT_EQ(intOf(state, "video.moveToActiveLine"), 3);
    EXPECT_EQ(intOf(state, "video.playAfterSelection"), 2);
    commitOptionsDialog(settings, l, state);
    EXPECT_EQ(settings.integer("video.moveToActiveLine"), 3);
    EXPECT_EQ(settings.integer("video.playAfterSelection"), 2);
    state["video.playAfterSelection"] = std::int64_t{1};
    commitOptionsDialog(settings, l, state);
    EXPECT_EQ(settings.integer("video.playAfterSelection"), 1);
    settings.resetAll();
    state = refreshOptionsDialogAfterReset(settings, l, state);
    EXPECT_EQ(intOf(state, "video.moveToActiveLine"), 0);
    EXPECT_EQ(intOf(state, "video.playAfterSelection"), 0);
    commitOptionsDialog(settings, l, state);
    EXPECT_EQ(settings.integer("video.moveToActiveLine"), 0);
    EXPECT_EQ(settings.integer("video.playAfterSelection"), 0);
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
    state["program.language"] = std::int64_t{1};
    settings.resetAll();
    settings.set("program.language", std::string("12"));
    state = refreshOptionsDialogAfterReset(settings, l, state);
    EXPECT_EQ(intOf(state, "program.language"), 1);
    EXPECT_EQ(intOf(state, "editor.dictionaryLanguage"), 0);
    commitOptionsDialog(settings, l, state);
    EXPECT_EQ(settings.text("program.language"), "pl");
    EXPECT_FALSE(settings.isSet("editor.dictionaryLanguage"));
}

TEST(OptionsDialog, TheSubtitleDisplayFilterChoosesARendererByName)
{
    MemorySettingsStorage storage;
    Settings settings(storage);
    auto l = lists();
    l.renderers = {"libass"};
    auto state = openOptionsDialog(settings, l).state;
    EXPECT_FALSE(state.contains("video.subtitleProvider"));
    EXPECT_FALSE(std::ranges::contains(commitOptionsDialog(settings, l, state), std::string("video.subtitleProvider")));
    EXPECT_FALSE(settings.isSet("video.subtitleProvider"));
    settings.resetAll();
    EXPECT_FALSE(refreshOptionsDialogAfterReset(settings, l, state).contains("video.subtitleProvider"));
    settings.set("video.subtitleProvider", std::string("xy-vsfilter_textsub"));
    state = openOptionsDialog(settings, l).state;
    EXPECT_FALSE(state.contains("video.subtitleProvider"));
    commitOptionsDialog(settings, l, state);
    EXPECT_EQ(settings.text("video.subtitleProvider"), "xy-vsfilter_textsub");
    settings.resetAll();

    l.renderers = {"libass", "vsfiltermod_textsub"};
    state = openOptionsDialog(settings, l).state;
    EXPECT_EQ(intOf(state, "video.subtitleProvider"), 0);
    EXPECT_TRUE(std::ranges::contains(commitOptionsDialog(settings, l, state), std::string("video.subtitleProvider")));
    EXPECT_EQ(settings.text("video.subtitleProvider"), "libass");
    settings.set("video.subtitleProvider", std::string("vsfiltermod_textsub"));
    EXPECT_EQ(intOf(openOptionsDialog(settings, l).state, "video.subtitleProvider"), 1);
    settings.set("video.subtitleProvider", std::string("LIBASS"));
    state = openOptionsDialog(settings, l).state;
    EXPECT_EQ(intOf(state, "video.subtitleProvider"), 0);
    state["video.subtitleProvider"] = std::int64_t{1};
    commitOptionsDialog(settings, l, state);
    EXPECT_EQ(settings.text("video.subtitleProvider"), "vsfiltermod_textsub");
    settings.resetAll();
    state = refreshOptionsDialogAfterReset(settings, l, state);
    EXPECT_EQ(intOf(state, "video.subtitleProvider"), 0);
    commitOptionsDialog(settings, l, state);
    EXPECT_EQ(settings.text("video.subtitleProvider"), "libass");
    l.renderers = {"other_textsub", "libass"};
    settings.resetAll();
    state = refreshOptionsDialogAfterReset(settings, l, openOptionsDialog(settings, l).state);
    EXPECT_EQ(intOf(state, "video.subtitleProvider"), 1);
    l.renderers = {"libass", "vsfiltermod_textsub"};
    for (const char *stale : {"removed_textsub", "xy-vsfilter_textsub"}) {
        settings.set("video.subtitleProvider", std::string(stale));
        state = openOptionsDialog(settings, l).state;
        EXPECT_EQ(intOf(state, "video.subtitleProvider"), 0) << stale;
        commitOptionsDialog(settings, l, state);
        EXPECT_EQ(settings.text("video.subtitleProvider"), "libass") << stale;
    }
}

TEST(OptionsDialog, TheOutputDeviceIsChosenByIdWithItsName)
{
    using Kind = OutputDeviceChoice::Kind;
    MemorySettingsStorage storage;
    Settings settings(storage);
    auto l = lists();
    auto state = openOptionsDialog(settings, l).state;
    EXPECT_FALSE(state.contains("audio.outputDevice"));
    EXPECT_FALSE(std::ranges::contains(commitOptionsDialog(settings, l, state), std::string("audio.outputDevice")));

    l.outputDevices = {{Kind::SystemDefault, "", ""},
                       {Kind::Device, "{0.0.0.00000000}.{F2906D39}", "22E1W (NVIDIA High Definition Audio)"},
                       {Kind::Device, "{0.0.0.00000000}.{C244E1FF}", "Speakers (Razer BlackShark V2 X USB)"}};
    state = openOptionsDialog(settings, l).state;
    EXPECT_EQ(intOf(state, "audio.outputDevice"), 0);
    EXPECT_TRUE(commitOptionsDialog(settings, l, state).empty());
    EXPECT_FALSE(settings.isSet("audio.outputDevice"));
    state["audio.outputDevice"] = std::int64_t{2};
    EXPECT_EQ(commitOptionsDialog(settings, l, state),
              (std::vector<std::string>{"audio.outputDevice", "audio.outputDeviceName"}));
    EXPECT_EQ(settings.text("audio.outputDevice"), "{0.0.0.00000000}.{C244E1FF}");
    EXPECT_EQ(settings.text("audio.outputDeviceName"), "Speakers (Razer BlackShark V2 X USB)");
    EXPECT_EQ(intOf(openOptionsDialog(settings, l).state, "audio.outputDevice"), 2);
    l.outputDevices.erase(l.outputDevices.begin() + 2);
    l.outputDevices.push_back({Kind::Missing, "{0.0.0.00000000}.{C244E1FF}", "Speakers (Razer BlackShark V2 X USB)"});
    state = openOptionsDialog(settings, l).state;
    EXPECT_EQ(intOf(state, "audio.outputDevice"), 2);
    EXPECT_TRUE(commitOptionsDialog(settings, l, state).empty());
    EXPECT_EQ(settings.text("audio.outputDevice"), "{0.0.0.00000000}.{C244E1FF}");
    l.outputDevices.pop_back();
    EXPECT_EQ(intOf(openOptionsDialog(settings, l).state, "audio.outputDevice"), 0);
    state["audio.outputDevice"] = std::int64_t{0};
    commitOptionsDialog(settings, l, state);
    EXPECT_EQ(settings.text("audio.outputDevice"), "");
    EXPECT_EQ(settings.text("audio.outputDeviceName"), "");
    settings.set("audio.outputDevice", std::string("{0.0.0.00000000}.{F2906D39}"));
    state["audio.outputDevice"] = std::int64_t{1};
    settings.resetAll();
    EXPECT_EQ(intOf(refreshOptionsDialogAfterReset(settings, l, state), "audio.outputDevice"), 0);
    const auto *definition = findSetting("audio.outputDevice");
    ASSERT_NE(definition, nullptr);
    EXPECT_EQ(definition->scope, SettingScope::Profile);
    EXPECT_NE(findSetting("audio.outputDeviceName"), nullptr);
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

TEST(OptionsDialog, PagesHaveIdsAndAPageResetsAlone)
{
    for (const auto page : {OptionsPage::General, OptionsPage::Appearance, OptionsPage::LineEditor,
                            OptionsPage::Spelling, OptionsPage::Grid,
                            OptionsPage::Video, OptionsPage::Audio, OptionsPage::Conversion,
                            OptionsPage::SubtitleProperties, OptionsPage::Automation, OptionsPage::Updates})
        EXPECT_EQ(optionsPageOfId(optionsPageId(page)), page);
    EXPECT_EQ(optionsPageId(OptionsPage::LineEditor), "lineEditor");
    EXPECT_EQ(optionsPageId(OptionsPage::Updates), "updates");
    EXPECT_EQ(optionsPageId(OptionsPage::Spelling), "spelling");
    EXPECT_EQ(optionsPageOfId("hotkeys"), std::nullopt);
    EXPECT_EQ(optionsPageOfId(""), std::nullopt);

    MemorySettingsStorage storage;
    Settings settings(storage);
    const auto l = lists();
    settings.set("grid.loadSortedSubs", true);
    settings.set("grid.dontCenterActiveLine", true);
    auto state = openOptionsDialog(settings, l).state;
    MemorySettingsStorage defaultsStorage;
    const Settings defaults(defaultsStorage);
    state = refreshOptionsDialogAfterReset(defaults, l, state, OptionsPage::General);
    EXPECT_FALSE(std::get<bool>(state.at("grid.loadSortedSubs")));
    EXPECT_TRUE(std::get<bool>(state.at("grid.dontCenterActiveLine")));
    EXPECT_TRUE(settings.boolean("grid.loadSortedSubs"));
}
