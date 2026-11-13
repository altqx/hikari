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

namespace {

struct Entry {
    std::u16string name;
    int value = 0;
    TierMark mark;
    bool operator==(const Entry &) const = default;
};

std::u16string folded(const std::u16string &s)
{
    std::u16string out = s;
    for (auto &c : out)
        if (c >= u'A' && c <= u'Z')
            c = static_cast<char16_t>(c - u'A' + u'a');
    return out;
}

const auto nameOf = [](const Entry &e) { return e.name; };
const auto renameTo = [](Entry &e, const std::u16string &n) { e.name = n; };
const auto same = [](const std::u16string &a, const std::u16string &b) { return folded(a) == folded(b); };

std::vector<std::u16string> listed(const std::vector<Entry> &all)
{
    std::vector<std::u16string> out;
    for (const auto &e : all)
        out.push_back((e.mark.tier == Tier::Project ? u"P:" : u"M:") + e.name + (e.mark.hidden ? u"*" : u""));
    return out;
}

}

TEST(TieredLists, CombineMarksTheHiddenMineItemsIgnoringCase)
{
    const auto all = combineTiers<Entry>({{u"Logo", 1}}, {{u"logo", 2}, {u"Arrow", 3}}, nameOf, same);
    EXPECT_EQ(listed(all), (std::vector<std::u16string>{u"P:Logo", u"M:logo*", u"M:Arrow"}));
    EXPECT_EQ(itemsIn(all, Tier::Mine).size(), 2u);
    EXPECT_EQ(itemsIn(all, Tier::Project)[0].value, 1);
    EXPECT_EQ(resolveItem(all, std::u16string(u"LOGO"), nameOf, same), 0u);
    EXPECT_EQ(resolveItem(all, std::u16string(u"arrow"), nameOf, same), 2u);
    EXPECT_FALSE(resolveItem(all, std::u16string(u"none"), nameOf, same));
    EXPECT_EQ(otherTierClash(all, 0, nameOf, same), 1u);
    EXPECT_FALSE(otherTierClash(all, 2, nameOf, same));
    EXPECT_EQ(visibleItems(all).size(), 2u);
}

TEST(TieredLists, SendCopiesAndMovesAcrossTiers)
{
    auto all = combineTiers<Entry>({{u"Logo", 1}}, {{u"Arrow", 3}}, nameOf, same);
    EXPECT_EQ(sendToOtherTier(all, 1, {}, std::u16string(), false, nameOf, renameTo, same), 1u);
    EXPECT_EQ(listed(all), (std::vector<std::u16string>{u"P:Logo", u"P:Arrow", u"M:Arrow*"}));

    EXPECT_EQ(sendToOtherTier(all, 0, {}, std::u16string(), true, nameOf, renameTo, same), 2u);
    EXPECT_EQ(listed(all), (std::vector<std::u16string>{u"P:Arrow", u"M:Arrow*", u"M:Logo"}));
    EXPECT_EQ(all[2].value, 1);
}

TEST(TieredLists, SendFollowsTheClashChoice)
{
    auto all = combineTiers<Entry>({{u"Logo", 1}, {u"Logo copy", 9}}, {{u"logo", 2}}, nameOf, same);
    EXPECT_FALSE(sendToOtherTier(all, 2, {ClashAction::Skip, {}}, std::u16string(), false, nameOf, renameTo, same));
    EXPECT_EQ(sendToOtherTier(all, 2, {ClashAction::Rename, {}}, std::u16string(u"Logo copy"), false, nameOf, renameTo,
                              same),
              2u);
    EXPECT_EQ(listed(all), (std::vector<std::u16string>{u"P:Logo", u"P:Logo copy", u"P:Logo copy (2)", u"M:logo*"}));
    EXPECT_EQ(sendToOtherTier(all, 3, {ClashAction::Replace, {}}, std::u16string(), false, nameOf, renameTo, same), 0u);
    EXPECT_EQ(all[0].value, 2);
    EXPECT_EQ(all[0].name, u"logo");
}

TEST(Tiers, PresetStoresShareTheProjectsPresetsFile)
{
    const std::filesystem::path root = "/show";
    for (const char *id : {"dialogPresets", "shiftPresets", "postprocessorPresets"}) {
        ASSERT_NE(tierStore(id), nullptr) << id;
        EXPECT_EQ(tierStore(id)->rule, TierRule::HiddenByName) << id;
        EXPECT_EQ(projectPathOf(id, root), root / ".hikari" / "presets.toml") << id;
    }
}

TEST(Tiers, TheDictionaryMergesAndProjectTomlSettingsGoPerKey)
{
    const std::filesystem::path root = "/show";
    EXPECT_EQ(tierStore("dictionary")->rule, TierRule::Merged);
    EXPECT_EQ(projectPathOf("dictionary", root), root / ".hikari" / "dictionary.txt");
    for (const char *id : {"exportDefault", "translationPair"}) {
        ASSERT_NE(tierStore(id), nullptr) << id;
        EXPECT_EQ(tierStore(id)->rule, TierRule::PerKey) << id;
        EXPECT_EQ(projectPathOf(id, root), root / ".hikari" / "project.toml") << id;
    }
}
