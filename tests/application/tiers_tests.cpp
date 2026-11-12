#include "hikari/application/tiers.h"

#include <gtest/gtest.h>

using namespace hikari::application;

TEST(Tiers, HideByNameListsTheProjectFirstAndMutesMine)
{
    const auto rows = hideByName({u8"B", u8"A"}, {u8"A", u8"C"});
    EXPECT_TRUE(rows == (std::vector<TieredName>{{u8"B", Tier::Project, false},
                                                 {u8"A", Tier::Project, false},
                                                 {u8"A", Tier::Mine, true},
                                                 {u8"C", Tier::Mine, false}}));
    EXPECT_EQ(resolveByName({u8"A"}, {u8"A", u8"C"}, u8"A"), Tier::Project);
    EXPECT_EQ(resolveByName({u8"A"}, {u8"A", u8"C"}, u8"C"), Tier::Mine);
    EXPECT_FALSE(resolveByName({u8"A"}, {u8"C"}, u8"D"));
}

TEST(Tiers, LandingNameFollowsTheClashChoice)
{
    const std::vector<std::u8string> taken{u8"A", u8"A2", u8"A2 (2)"};
    EXPECT_EQ(landingName(taken, u8"B", {}), u8"B");
    EXPECT_FALSE(landingName(taken, u8"A", {ClashAction::Skip, {}}));
    EXPECT_EQ(landingName(taken, u8"A", {ClashAction::Replace, {}}), u8"A");
    EXPECT_EQ(landingName(taken, u8"A", {ClashAction::Rename, u8"New"}), u8"New");
    EXPECT_EQ(landingName(taken, u8"A", {ClashAction::Rename, u8"A2"}), u8"A2 (3)");
    EXPECT_EQ(landingName(taken, u8"A", {ClashAction::Rename, {}}), u8"A (2)");
}

TEST(Tiers, UserOnlyStoresHaveNoProjectPath)
{
    const std::filesystem::path root = "/show";
    for (const char *id : {"hotkeys", "aiProviders", "aiThreads", "recentFiles", "windowState", "hunspellDictionaries"}) {
        const auto *store = tierStore(id);
        ASSERT_NE(store, nullptr) << id;
        EXPECT_FALSE(store->rule) << id;
        EXPECT_FALSE(projectPathOf(id, root)) << id;
    }
    EXPECT_EQ(tierStore("styleCatalogs")->rule, TierRule::HiddenByName);
    EXPECT_EQ(projectPathOf("styleCatalogs", root), root / ".hikari" / "catalogs");
    EXPECT_FALSE(projectPathOf("styleCatalogs", {}));
}
