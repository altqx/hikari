// O1: the settings registry against legacy config.h/config.cpp at 20d647c4:
// every CONFIG option with its default (LoadDefaultConfig and
// LoadDefaultAudioConfig), the AudioConfig.txt partition, typed round trips,
// legacy "Set default", the interim INI keys and the Options dialog's number
// ranges (OptionsDialog NumCtrl).

#include "hikari/application/settings.h"

#include <gtest/gtest.h>

#include <map>
#include <set>

using namespace hikari::application;

namespace {

// config.h CONFIG, in order (configSize - 1 options; 0 is reserved).
const char *const kLegacyOrder[] = {
    "AUDIO_AUTO_COMMIT", "AUDIO_AUTO_FOCUS", "AUDIO_AUTO_SCROLL", "AUDIO_CACHE_FILES_LIMIT", "AUDIO_DELAY",
    "AUDIO_DONT_PLAY_WHEN_LINE_CHANGES", "AUDIO_DRAW_KEYFRAMES", "AUDIO_DRAW_SECONDARY_LINES",
    "AUDIO_DRAW_SELECTION_BACKGROUND", "AUDIO_DRAW_TIME_CURSOR", "AUDIO_DRAW_VIDEO_POSITION",
    "AUDIO_GRAB_TIMES_ON_SELECT", "AUDIO_HORIZONTAL_ZOOM", "AUDIO_INACTIVE_LINES_DISPLAY_MODE", "AUDIO_KARAOKE",
    "AUDIO_KARAOKE_MOVE_ON_CLICK", "AUDIO_KARAOKE_SPLIT_MODE", "AUDIO_LEAD_IN_VALUE", "AUDIO_LEAD_OUT_VALUE",
    "AUDIO_LINE_BOUNDARIES_THICKNESS", "AUDIO_LINK", "AUDIO_LOCK_SCROLL_ON_CURSOR", "AUDIO_MARK_PLAY_TIME",
    "AUDIO_MERGE_EVERY_N_WITH_SYLLABLE", "AUDIO_NEXT_LINE_ON_COMMIT", "AUDIO_RAM_CACHE", "AUDIO_SNAP_TO_KEYFRAMES",
    "AUDIO_SNAP_TO_OTHER_LINES", "AUDIO_SPECTRUM_ON", "AUDIO_SPECTRUM_NON_LINEAR_ON", "AUDIO_START_DRAG_SENSITIVITY",
    "AUDIO_VERTICAL_ZOOM", "AUDIO_VOLUME", "AUDIO_WHEEL_DEFAULT_TO_ZOOM",
};

// LoadDefaultConfig and LoadDefaultAudioConfig: every explicit assignment.
const std::map<std::string, SettingValue> kExplicitDefaults{
    {"AUDIO_BOX_HEIGHT", std::int64_t{170}},
    {"SHIFT_TIMES_TIME", std::int64_t{2000}},
    {"SHIFT_TIMES_WHICH_LINES", std::int64_t{0}},
    {"CONVERT_RESOLUTION_WIDTH", std::string("1280")},
    {"CONVERT_RESOLUTION_HEIGHT", std::string("720")},
    {"CONVERT_FPS", std::string("23.976")},
    {"CONVERT_STYLE", std::string("Default")},
    {"CONVERT_STYLE_CATALOG", std::string("Default")},
    {"DICTIONARY_LANGUAGE", std::string("en_US")},
    {"SPELLCHECKER_ON", true},
    {"STYLE_EDIT_FILTER_TEXT", std::string(reinterpret_cast<const char *>(u8"ĄĆĘŁŃÓŚŹŻąćęłńóśźż"))},
    {"FFMS2_VIDEO_SEEKING", std::int64_t{2}},
    {"SHIFT_TIMES_BY_TIME", false},
    {"GRID_FONT", std::string("Tahoma")},
    {"GRID_FONT_SIZE", std::int64_t{10}},
    {"PROGRAM_FONT", std::string("Tahoma")},
    {"PROGRAM_FONT_SIZE", std::int64_t{10}},
    {"GRID_SAVE_AFTER_CHARACTER_COUNT", std::int64_t{1}},
    {"GRID_TAGS_SWAP_CHARACTER", std::string(reinterpret_cast<const char *>(u8"☀"))},
    {"SHIFT_TIMES_MOVE_FORWARD", true},
    {"CONVERT_NEW_END_TIMES", false},
    {"GRID_INSERT_START_OFFSET", std::int64_t{0}},
    {"GRID_INSERT_END_OFFSET", std::int64_t{0}},
    {"VIDEO_PLAY_AFTER_SELECTION", std::int64_t{0}},
    {"STYLE_PREVIEW_TEXT", std::string(reinterpret_cast<const char *>(u8"Podgląd"))},
    {"PROGRAM_THEME", std::string("DarkSentro")},
    {"EDITOR_ON", true},
    {"CONVERT_SHOW_SETTINGS", false},
    {"SHIFT_TIMES_ON", true},
    {"SHIFT_TIMES_WHICH_TIMES", std::int64_t{0}},
    {"SHIFT_TIMES_STYLES", std::string()},
    {"CONVERT_TIME_PER_CHARACTER", std::int64_t{110}},
    {"VIDEO_INDEX", true},
    {"VIDEO_GPU_CONVERSION", true},
    {"VIDEO_PROGRESS_BAR", true},
    {"VIDEO_WINDOW_SIZE", std::string("500,350")},
    {"WINDOW_SIZE", std::string("1000,700")},
    {"AUTOMATION_TRACE_LEVEL", std::int64_t{3}},
    {"AUTOSAVE_MAX_FILES", std::int64_t{3}},
    {"GRID_CHANGE_ACTIVE_ON_SELECTION", true},
    {"UPDATER_CHECK_FOR_STABLE", true},
    {"AUDIO_AUTO_COMMIT", true},
    {"AUDIO_AUTO_FOCUS", true},
    {"AUDIO_AUTO_SCROLL", true},
    {"AUDIO_CACHE_FILES_LIMIT", std::int64_t{10}},
    {"AUDIO_DELAY", std::int64_t{0}},
    {"AUDIO_DRAW_TIME_CURSOR", true},
    {"AUDIO_DRAW_KEYFRAMES", true},
    {"AUDIO_DRAW_SECONDARY_LINES", true},
    {"AUDIO_DRAW_SELECTION_BACKGROUND", true},
    {"AUDIO_DRAW_VIDEO_POSITION", true},
    {"AUDIO_GRAB_TIMES_ON_SELECT", true},
    {"AUDIO_HORIZONTAL_ZOOM", std::int64_t{50}},
    {"AUDIO_INACTIVE_LINES_DISPLAY_MODE", std::int64_t{1}},
    {"AUDIO_KARAOKE", false},
    {"AUDIO_KARAOKE_SPLIT_MODE", true},
    {"AUDIO_LEAD_IN_VALUE", std::int64_t{200}},
    {"AUDIO_LEAD_OUT_VALUE", std::int64_t{300}},
    {"AUDIO_LINE_BOUNDARIES_THICKNESS", std::int64_t{2}},
    {"AUDIO_LINK", false},
    {"AUDIO_LOCK_SCROLL_ON_CURSOR", false},
    {"AUDIO_MARK_PLAY_TIME", std::int64_t{1000}},
    {"AUDIO_NEXT_LINE_ON_COMMIT", true},
    {"AUDIO_RAM_CACHE", false},
    {"AUDIO_SNAP_TO_KEYFRAMES", false},
    {"AUDIO_SNAP_TO_OTHER_LINES", false},
    {"AUDIO_SPECTRUM_ON", false},
    {"AUDIO_START_DRAG_SENSITIVITY", std::int64_t{6}},
    {"AUDIO_VERTICAL_ZOOM", std::int64_t{50}},
    {"AUDIO_VOLUME", std::int64_t{50}},
    {"AUDIO_WHEEL_DEFAULT_TO_ZOOM", false},
};

SettingValue emptyOf(SettingType type)
{
    switch (type) {
    case SettingType::Bool:
        return false;
    case SettingType::Int:
        return std::int64_t{0};
    case SettingType::String:
        return std::string();
    case SettingType::StringList:
        return std::vector<std::string>();
    }
    return false;
}

// A value other than the default, for each type.
SettingValue sampleFor(const SettingDefinition &setting)
{
    switch (setting.type) {
    case SettingType::Bool:
        return !std::get<bool>(setting.defaultValue);
    case SettingType::Int:
        return std::get<std::int64_t>(setting.defaultValue) + 7;
    case SettingType::String:
        return std::get<std::string>(setting.defaultValue) + reinterpret_cast<const char *>(u8"ż;x");
    case SettingType::StringList:
        return std::vector<std::string>{"a, b", "", "c"};
    }
    return false;
}

} // namespace

TEST(SettingsRegistry, EveryLegacyOptionHasAStableId)
{
    std::set<std::string_view> ids, legacy;
    std::size_t legacyCount = 0;
    for (const auto &s : settingDefinitions()) {
        EXPECT_TRUE(ids.insert(s.id).second) << s.id;
        EXPECT_EQ(s.id.find('/'), std::string_view::npos) << s.id;
        if (!s.legacyKey.empty()) {
            ++legacyCount;
            EXPECT_TRUE(legacy.insert(s.legacyKey).second) << s.legacyKey;
            EXPECT_EQ(findLegacySetting(s.legacyKey), &s);
        }
        EXPECT_EQ(findSetting(s.id), &s);
    }
    // The 207 rows of the migration map (34 AudioConfig.txt, 173 Config.txt).
    EXPECT_EQ(legacyCount, 207u);
    EXPECT_EQ(findSetting("no.such"), nullptr);
    EXPECT_EQ(findLegacySetting(""), nullptr);
}

TEST(SettingsRegistry, AudioOptionsAreTheFirstOrdinals)
{
    // config::IsAudioOption: opt <= AUDIO_WHEEL_DEFAULT_TO_ZOOM, by ordinal, not
    // by name (AUDIO_BOX_HEIGHT and AUDIO_RECENT_FILES are Config.txt options).
    const auto all = settingDefinitions();
    ASSERT_GE(all.size(), std::size(kLegacyOrder));
    for (std::size_t i = 0; i < std::size(kLegacyOrder); ++i) {
        EXPECT_EQ(all[i].legacyKey, kLegacyOrder[i]);
        EXPECT_TRUE(all[i].legacyAudioFile) << kLegacyOrder[i];
    }
    for (std::size_t i = std::size(kLegacyOrder); i < all.size(); ++i)
        EXPECT_FALSE(all[i].legacyAudioFile) << all[i].id;
    EXPECT_FALSE(findLegacySetting("AUDIO_BOX_HEIGHT")->legacyAudioFile);
    EXPECT_FALSE(findLegacySetting("AUDIO_RECENT_FILES")->legacyAudioFile);
}

TEST(SettingsRegistry, DefaultsAreLegacyDefaults)
{
    for (const auto &s : settingDefinitions()) {
        if (s.legacyKey.empty())
            continue;
        const auto it = kExplicitDefaults.find(std::string(s.legacyKey));
        // Options without an assignment start as "": false, 0, "" or no entries.
        const SettingValue expected = it != kExplicitDefaults.end() ? it->second : emptyOf(s.type);
        EXPECT_EQ(s.defaultValue, expected) << s.legacyKey;
    }
    EXPECT_EQ(findSetting("autosave.maxFiles")->legacyKey, "AUTOSAVE_MAX_FILES");
    EXPECT_EQ(findSetting("subtitles.saveWithVideoName")->legacyKey, "SUBS_AUTONAMING");
}

TEST(SettingsRegistry, ThemesAreExcludedAndUnresolvedOnesKept)
{
    const auto *theme = findLegacySetting("PROGRAM_THEME");
    ASSERT_NE(theme, nullptr);
    EXPECT_EQ(theme->disposition, SettingDisposition::Excluded);
    MemorySettingsStorage storage;
    Settings settings(storage);
    EXPECT_FALSE(settings.set(theme->id, std::string("LightSentro")));
    EXPECT_FALSE(settings.isSet(theme->id));
    // The worksheet's preserve-unresolved rows keep their values.
    for (const char *key : {"AUDIO_DRAW_TIME_CURSOR", "AUDIO_LOCK_SCROLL_ON_CURSOR", "COLORPICKER_RECENT_COLORS",
                            "FFMS2_VIDEO_SEEKING", "SHIFT_TIMES_BY_TIME", "SHIFT_TIMES_MOVE_FORWARD",
                            "SHIFT_TIMES_PROFILES", "VIDEO_GPU_CONVERSION", "VSFILTER_INSTANCE", "DONT_SHOW_CRASH_INFO"}) {
        const auto *s = findLegacySetting(key);
        ASSERT_NE(s, nullptr) << key;
        EXPECT_EQ(s->disposition, SettingDisposition::Unresolved) << key;
        EXPECT_TRUE(settings.set(s->id, sampleFor(*s))) << key;
        EXPECT_EQ(settings.value(s->id), sampleFor(*s)) << key;
    }
}

TEST(SettingsRegistry, EverySettingRoundTripsAndResets)
{
    MemorySettingsStorage storage;
    Settings settings(storage);
    std::vector<std::string> heard;
    settings.setObserver([&](const SettingDefinition &s) { heard.emplace_back(s.id); });
    for (const auto &s : settingDefinitions()) {
        if (s.disposition == SettingDisposition::Excluded)
            continue;
        EXPECT_FALSE(settings.isSet(s.id)) << s.id;
        EXPECT_EQ(settings.value(s.id), s.defaultValue) << s.id;
        const auto sample = sampleFor(s);
        EXPECT_TRUE(settings.set(s.id, sample)) << s.id;
        EXPECT_TRUE(settings.isSet(s.id)) << s.id;
        EXPECT_EQ(settings.value(s.id), sample) << s.id;
        ASSERT_FALSE(heard.empty());
        EXPECT_EQ(heard.back(), s.id);
    }
    // Setting the same value again is not a change.
    heard.clear();
    settings.set("grid.hideColumns", settings.value("grid.hideColumns"));
    EXPECT_TRUE(heard.empty());
    // Legacy ResetDefault: everything back to the defaults at once.
    settings.resetAll();
    for (const auto &s : settingDefinitions()) {
        EXPECT_FALSE(settings.isSet(s.id)) << s.id;
        EXPECT_EQ(settings.value(s.id), s.defaultValue) << s.id;
    }
    EXPECT_FALSE(heard.empty());
}

TEST(SettingsRegistry, ValuesAreConvertedToTheSettingsType)
{
    MemorySettingsStorage storage;
    Settings settings(storage);
    settings.set("grid.hideColumns", std::string("8193"));
    EXPECT_EQ(settings.integer("grid.hideColumns"), 8193);
    settings.set("grid.hideColumns", std::string("12abc"));
    EXPECT_EQ(settings.integer("grid.hideColumns"), 0);
    settings.set("video.index", std::string("false"));
    EXPECT_FALSE(settings.boolean("video.index"));
    settings.set("video.index", std::string("0"));
    EXPECT_FALSE(settings.boolean("video.index"));
    settings.set("video.index", std::string("yes"));
    EXPECT_TRUE(settings.boolean("video.index"));
    settings.set("convert.resolutionWidth", std::int64_t{1920});
    EXPECT_EQ(settings.text("convert.resolutionWidth"), "1920");
    settings.set("recent.subtitles", std::string("a.ass"));
    EXPECT_EQ(settings.list("recent.subtitles"), std::vector<std::string>{"a.ass"});
    EXPECT_EQ(settings.integer("convert.resolutionWidth"), 1920);
    // An unknown id reads as false and cannot be set.
    EXPECT_EQ(settings.value("no.such"), SettingValue(false));
    EXPECT_FALSE(settings.set("no.such", true));
}

TEST(SettingsRegistry, InterimKeysNameSettingsOfTheirType)
{
    std::set<std::string_view> keys, targets;
    for (const auto &k : interimKeys()) {
        EXPECT_TRUE(keys.insert(k.key).second) << k.key;
        EXPECT_TRUE(targets.insert(k.setting).second) << k.setting;
        const auto *s = findSetting(k.setting);
        ASSERT_NE(s, nullptr) << k.setting;
        EXPECT_NE(s->disposition, SettingDisposition::Excluded);
    }
    EXPECT_EQ(findSetting("selectLines.options")->legacyKey, "SELECT_LINES_OPTIONS");
    EXPECT_EQ(findSetting("grid.hideColumns")->legacyKey, "GRID_HIDE_COLUMNS");
    EXPECT_EQ(findSetting("convert.assTagsToInsertInLine")->legacyKey, "CONVERT_ASS_TAGS_TO_INSERT_IN_LINE");
    EXPECT_EQ(findSetting("updater.checkForStable")->legacyKey, "UPDATER_CHECK_FOR_STABLE");
    // ShiftTimes::SaveOptions: the six switches are bits of SHIFT_TIMES_OPTIONS.
    std::int64_t all = 0;
    for (const auto &b : interimShiftOptionBits()) {
        EXPECT_FALSE(keys.contains(b.key));
        EXPECT_EQ(all & b.bit, 0);
        all |= b.bit;
    }
    EXPECT_EQ(all, 63);
    EXPECT_EQ(findSetting("shiftTimes.options")->legacyKey, "SHIFT_TIMES_OPTIONS");
    ASSERT_NE(findSetting(kAutomationHotkeysSetting), nullptr);
    EXPECT_TRUE(findSetting(kAutomationHotkeysSetting)->legacyKey.empty());
}

TEST(SettingsDialog, NumbersShowAndCommitAsNumCtrlDoes)
{
    const auto *autosave = findSettingsNumberField("autosave.maxFiles");
    ASSERT_NE(autosave, nullptr);
    EXPECT_EQ(autosave->min, 2);
    EXPECT_EQ(autosave->max, 1000000);
    // NumCtrl clamps what it shows and what it returns.
    EXPECT_EQ(settingsDialogNumber(*autosave, std::int64_t{1}), 2);
    EXPECT_EQ(settingsDialogNumber(*autosave, std::int64_t{3}), 3);
    EXPECT_EQ(settingsDialogCommit(*autosave, 0), 2);
    EXPECT_EQ(settingsDialogCommit(*autosave, 5000000), 1000000);
    // An unset tab-name limit shows 40.
    const auto *tabs = findSettingsNumberField("program.tabTextMaxChars");
    EXPECT_EQ(settingsDialogNumber(*tabs, std::int64_t{0}), 40);
    EXPECT_EQ(settingsDialogNumber(*tabs, std::int64_t{5}), 20);
    // A zoom outside 100-1100 shows 200.
    const auto *zoom = findSettingsNumberField("video.zoomPercent");
    EXPECT_EQ(settingsDialogNumber(*zoom, std::int64_t{0}), 200);
    EXPECT_EQ(settingsDialogNumber(*zoom, std::int64_t{1200}), 200);
    EXPECT_EQ(settingsDialogNumber(*zoom, std::int64_t{150}), 150);
    // The conversion resolution is a text option read as a number.
    const auto *width = findSettingsNumberField("convert.resolutionWidth");
    EXPECT_EQ(settingsDialogNumber(*width, std::string("1280")), 1280);
    EXPECT_EQ(settingsDialogNumber(*width, std::string("wide")), 1);
    for (const auto &f : settingsNumberFields())
        EXPECT_NE(findSetting(f.setting), nullptr) << f.setting;
}
