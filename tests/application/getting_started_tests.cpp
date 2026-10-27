// FR1 (#363): what Getting started's pages set, and their state while legacy
// settings are imported.

#include "hikari/application/getting_started.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

using namespace hikari::application;

namespace {

OptionsLists lists()
{
    OptionsLists l;
    l.languageTags = {"en", "pl"};
    l.languageNames = {"English", "Polski"};
    l.dictionarySymbols = {"en_GB", "en_US", "pl_PL"};
    l.dictionaryNames = {"English", "English", "Polski"};
    l.renderers = {"libass", "xy-VSFilter"};
    l.outputDevices = {{OutputDeviceChoice::Kind::SystemDefault, "", ""},
                       {OutputDeviceChoice::Kind::Device, "{0.0.0.00000000}.{C244E1FF}", "Speakers"}};
    l.findLanguage = [](std::string_view tag) -> std::string {
        if (tag.starts_with("en"))
            return "English";
        return tag.starts_with("pl") ? "Polski" : "";
    };
    return l;
}

} // namespace

TEST(GettingStarted, SetsTheCrucialSettingsInPageOrder)
{
    const auto all = gettingStartedSettings();
    EXPECT_EQ(all.front(), "program.language");
    EXPECT_EQ(all.back(), "updater.autoCheck");
    // Every one is a registry setting, and none is listed twice.
    for (const auto id : all) {
        EXPECT_NE(findSetting(id), nullptr) << id;
        EXPECT_EQ(std::ranges::count(all, id), 1) << id;
    }
}

TEST(GettingStarted, PagesShowTheLiveValuesAsTheSettingsWindowDoes)
{
    MemorySettingsStorage storage;
    Settings live(storage);
    live.set("editor.dictionaryLanguage", std::string("pl_PL"));
    live.set("appearance.theme", std::string("light"));
    const auto state = gettingStartedState(live, lists(), {});
    // The choices by index, the appearance as stored.
    EXPECT_EQ(std::get<std::int64_t>(state.at("program.language")), 0);
    EXPECT_EQ(std::get<std::int64_t>(state.at("editor.dictionaryLanguage")), 2);
    EXPECT_EQ(std::get<std::string>(state.at("appearance.theme")), "light");
    EXPECT_EQ(std::get<std::string>(state.at("appearance.titleBar")), "auto");
    EXPECT_EQ(std::get<bool>(state.at("updater.autoCheck")), false);
    EXPECT_EQ(state.size(), gettingStartedSettings().size());
}

TEST(GettingStarted, ImportedValuesFillThePagesWithoutWriting)
{
    MemorySettingsStorage storage;
    Settings live(storage);
    const std::map<std::string, SettingValue, std::less<>> imported{
        {"program.language", std::string("pl")},
        {"appearance.theme", std::string("light")},
        {"audio.outputDevice", std::string("{0.0.0.00000000}.{C244E1FF}")},
        {"audio.outputDeviceName", std::string("Speakers")},
        {"autosave.maxFiles", std::int64_t{10}},
        // Not on a page: left for the import itself.
        {"grid.font", std::string("Tahoma")},
    };
    const auto state = gettingStartedState(live, lists(), imported);
    EXPECT_EQ(std::get<std::int64_t>(state.at("program.language")), 1);
    EXPECT_EQ(std::get<std::string>(state.at("appearance.theme")), "light");
    EXPECT_EQ(std::get<std::int64_t>(state.at("audio.outputDevice")), 1);
    EXPECT_EQ(std::get<std::int64_t>(state.at("autosave.maxFiles")), 10);
    EXPECT_FALSE(state.contains("grid.font"));
    // The live settings are untouched.
    EXPECT_FALSE(live.isSet("program.language"));
    EXPECT_FALSE(live.isSet("appearance.theme"));
    EXPECT_FALSE(live.isSet("grid.font"));
}

TEST(GettingStarted, WhatTheSettingsWindowDoesNotOfferIsLeftOut)
{
    // One renderer: the Settings window offers no choice, and neither do the
    // pages (an imported name is not turned into a choice either).
    MemorySettingsStorage storage;
    Settings live(storage);
    OptionsLists one = lists();
    one.renderers = {"libass"};
    const std::map<std::string, SettingValue, std::less<>> imported{
        {"video.subtitleProvider", std::string("xy-VSFilter")}};
    const auto state = gettingStartedState(live, one, imported);
    EXPECT_FALSE(state.contains("video.subtitleProvider"));
    EXPECT_EQ(state.size(), gettingStartedSettings().size() - 1);
    EXPECT_EQ(std::get<std::int64_t>(gettingStartedState(live, lists(), imported).at("video.subtitleProvider")), 1);
}

TEST(GettingStarted, AChoiceThePagesCannotShowStaysTheImports)
{
    // The legacy dictionary is imported with its files: not installed yet,
    // the pages show no choice (-1) and the import keeps its language.
    MemorySettingsStorage storage;
    Settings live(storage);
    const std::map<std::string, SettingValue, std::less<>> imported{
        {"editor.dictionaryLanguage", std::string("de_DE")}, {"autosave.maxFiles", std::int64_t{10}}};
    const auto state = gettingStartedState(live, lists(), imported);
    EXPECT_EQ(std::get<std::int64_t>(state.at("editor.dictionaryLanguage")), -1);
    const auto pages = gettingStartedPageSettings(state);
    EXPECT_FALSE(pages.contains("editor.dictionaryLanguage"));
    EXPECT_TRUE(pages.contains("autosave.maxFiles"));
    EXPECT_TRUE(pages.contains("appearance.theme"));
    // The device's name goes with its id.
    EXPECT_TRUE(pages.contains("audio.outputDevice"));
    EXPECT_TRUE(pages.contains("audio.outputDeviceName"));
    // An installed one is a page value: the page's choice wins.
    auto chosen = state;
    chosen["editor.dictionaryLanguage"] = std::int64_t{2};
    EXPECT_TRUE(gettingStartedPageSettings(chosen).contains("editor.dictionaryLanguage"));
}
