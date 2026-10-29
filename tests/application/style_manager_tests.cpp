#include "hikari/application/style_manager.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/ass_save.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>

using namespace hikari;
using namespace hikari::application;

namespace {

core::Document load(std::string_view text)
{
    std::vector<std::byte> b(text.size());
    std::memcpy(b.data(), text.data(), text.size());
    return core::loadAss(b).document;
}

std::string saved(const core::Document &d)
{
    const auto b = core::encodeAss(d);
    return std::string(reinterpret_cast<const char *>(b.data()), b.size());
}

core::StyleValues named(std::u8string name, std::u8string font = u8"Arial")
{
    auto s = defaultStyle(std::move(name));
    s.fontname = std::move(font);
    return s;
}

std::vector<std::u8string> names(const StyleList &l)
{
    std::vector<std::u8string> out;
    for (const auto &s : l)
        out.push_back(s.name);
    return out;
}

constexpr std::string_view kScript =
    "[Script Info]\nTLMode Style: TLmode\n\n[V4+ Styles]\n"
    "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
    "Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, "
    "MarginR, MarginV, Encoding\n"
    "Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1\n"
    "Style: Sign,Verdana,30,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,-1,0,0,0,100,100,0,0,1,2,2,8,10,10,10,1\n"
    "Style: Unused,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1\n"
    "Style: TLmode,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,8,10,10,10,1\n\n"
    "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,a\n"
    "Dialogue: 0,0:00:01.00,0:00:02.00,Sign,,0,0,0,,b\n";

}

TEST(StyleLists, CompareAndCopyChanges)
{
    auto a = named(u8"A"), b = named(u8"B", u8"Times");
    b.bold = true;
    b.marginLeft = u8"40";
    EXPECT_EQ(compareStyles(a, b), style_field::FontName | style_field::Bold | style_field::MarginLeft);
    const auto c = copyStyleChanges(a, b, style_field::Bold);
    EXPECT_TRUE(c.bold);
    EXPECT_EQ(c.fontname, u8"Arial");
    EXPECT_EQ(c.name, u8"A");
}

TEST(StyleLists, NewNamesTransferMoveAndSort)
{
    StyleList list{named(u8"New Style"), named(u8"New Style1"), named(u8"B")};
    EXPECT_EQ(newStyleName(list), u8"New Style2");
    EXPECT_EQ(newStyleName({}), u8"New Style");
    StyleList into{named(u8"B"), named(u8"C")};
    std::vector<std::u8string> asked;
    auto rows = transferStyles(into, {named(u8"B", u8"Times"), named(u8"A"), named(u8"C", u8"Times")},
                               [&](const std::u8string &n) {
                                   asked.push_back(n);
                                   return asked.size() == 1 ? Replace::Yes : Replace::No;
                               });
    EXPECT_EQ(asked, (std::vector<std::u8string>{u8"B", u8"C"}));
    EXPECT_EQ(rows, (std::vector<std::size_t>{0, 2}));
    EXPECT_EQ(into[0].fontname, u8"Times");
    EXPECT_EQ(into[1].fontname, u8"Arial");
    StyleList stop{named(u8"B")}, load{named(u8"B")};
    transferStyles(stop, {named(u8"B"), named(u8"D")}, [](auto &) { return Replace::Cancel; });
    transferStyles(load, {named(u8"B"), named(u8"D")}, [](auto &) { return Replace::Cancel; }, false);
    EXPECT_EQ(names(stop), (std::vector<std::u8string>{u8"B"}));
    EXPECT_EQ(names(load), (std::vector<std::u8string>{u8"B", u8"D"}));
    StyleList m{named(u8"a"), named(u8"b"), named(u8"c"), named(u8"d")};
    EXPECT_EQ(moveStyles(m, {1, 3}, StyleMove::ToStart), (std::vector<std::size_t>{0, 1}));
    EXPECT_EQ(names(m), (std::vector<std::u8string>{u8"b", u8"d", u8"a", u8"c"}));
    EXPECT_EQ(moveStyles(m, {0, 1}, StyleMove::Up), (std::vector<std::size_t>{0, 1}));
    EXPECT_EQ(moveStyles(m, {0, 2}, StyleMove::Down), (std::vector<std::size_t>{1, 3}));
    EXPECT_EQ(names(m), (std::vector<std::u8string>{u8"d", u8"b", u8"c", u8"a"}));
    sortStyles(m);
    EXPECT_EQ(names(m), (std::vector<std::u8string>{u8"a", u8"b", u8"c", u8"d"}));
    StyleList e{named(u8"a"), named(u8"b"), named(u8"c"), named(u8"d"), named(u8"e")};
    EXPECT_EQ(moveStyles(e, {0, 2, 3}, StyleMove::Up), (std::vector<std::size_t>{0, 1, 2}));
    EXPECT_EQ(names(e), (std::vector<std::u8string>{u8"a", u8"c", u8"d", u8"b", u8"e"}));
    EXPECT_EQ(moveStyles(e, {1, 4}, StyleMove::ToEnd), (std::vector<std::size_t>{3, 4}));
    EXPECT_EQ(names(e), (std::vector<std::u8string>{u8"a", u8"d", u8"b", u8"c", u8"e"}));
}

TEST(StyleLists, MovingAllOfALargeListIsLinear)
{
    StyleList big(200000);
    std::vector<std::size_t> all(big.size());
    for (std::size_t i = 0; i < big.size(); ++i) {
        big[i].name = u8"s" + std::u8string(reinterpret_cast<const char8_t *>(std::to_string(i).c_str()));
        all[i] = i;
    }
    const auto start = std::chrono::steady_clock::now();
    for (const auto move : {StyleMove::Up, StyleMove::Down, StyleMove::ToStart, StyleMove::ToEnd})
        EXPECT_EQ(moveStyles(big, all, move), all);
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(2));
    EXPECT_EQ(big.front().name, u8"s0");
    EXPECT_EQ(big.back().name, u8"s199999");
}

TEST(StyleLists, TransferWithClashChoices)
{
    StyleList into{named(u8"A"), named(u8"B"), named(u8"C"), named(u8"B (2)")};
    ClashChoices choices{{{u8"A", 0}, {ClashAction::Replace, {}}},
                         {{u8"B", 0}, {ClashAction::Rename, u8"B (2)"}},
                         {{u8"C", 0}, {ClashAction::Skip, {}}}};
    const auto rows = transferStyles(
        into, {named(u8"A", u8"Times"), named(u8"B", u8"Times"), named(u8"C", u8"Times"), named(u8"D"),
               named(u8"E", u8"Times")},
        choices);
    EXPECT_EQ(names(into), (std::vector<std::u8string>{u8"A", u8"B", u8"C", u8"B (2)", u8"B (2) (2)", u8"D", u8"E"}));
    EXPECT_EQ(rows, (std::vector<std::size_t>{0, 4, 5, 6}));
    EXPECT_EQ(into[0].fontname, u8"Times");
    EXPECT_EQ(into[2].fontname, u8"Arial");
    EXPECT_EQ(into[4].fontname, u8"Times");
    StyleList kept{named(u8"A")};
    EXPECT_TRUE(transferStyles(kept, {named(u8"A", u8"Times")}, ClashChoices{}).empty());
    EXPECT_EQ(kept[0].fontname, u8"Arial");
    EXPECT_EQ(freeStyleName(StyleList{named(u8"X"), named(u8"X (2)")}, u8"X"), u8"X (3)");
    StyleList apart{named(u8"X")};
    const auto apartRows = transferStyles(apart, {named(u8"X", u8"One"), named(u8"X", u8"Two"), named(u8"X", u8"Three")},
                                          ClashChoices{{{u8"X", 0}, {ClashAction::Rename, u8"Y"}},
                                                       {{u8"X", 1}, {ClashAction::Skip, {}}},
                                                       {{u8"X", 2}, {ClashAction::Replace, {}}}});
    EXPECT_EQ(names(apart), (std::vector<std::u8string>{u8"X", u8"Y"}));
    EXPECT_EQ(apart[0].fontname, u8"Three");
    EXPECT_EQ(apart[1].fontname, u8"One");
    EXPECT_EQ(apartRows, (std::vector<std::size_t>{1, 0}));
    StyleList onto{named(u8"A")};
    transferStyles(onto, {named(u8"A", u8"Times"), named(u8"B")},
                   ClashChoices{{{u8"A", 0}, {ClashAction::Rename, u8"B"}}});
    EXPECT_EQ(names(onto), (std::vector<std::u8string>{u8"A", u8"B (2)", u8"B"}));
    const ClashChoices pair{{{u8"A", 0}, {ClashAction::Replace, {}}}, {{u8"A", 1}, {ClashAction::Rename, u8"A2"}}};
    StyleList without{named(u8"Z")};
    transferStyles(without, {named(u8"A", u8"One"), named(u8"A", u8"Two")}, pair);
    EXPECT_EQ(names(without), (std::vector<std::u8string>{u8"Z", u8"A", u8"A2"}));
    EXPECT_EQ(without[1].fontname, u8"One");
    EXPECT_EQ(without[2].fontname, u8"Two");
}

TEST(StyleLists, FreeNamesForAHostileListAreFound)
{
    constexpr int kCount = 3000;
    StyleList into{named(u8"X")};
    StyleList styles;
    for (int i = 2; i <= kCount; ++i) {
        const auto n = std::to_string(i);
        into.push_back(named(u8"X (" + std::u8string(n.begin(), n.end()) + u8")"));
    }
    for (int i = 0; i < kCount; ++i)
        styles.push_back(named(u8"X", u8"Times"));
    const StyleNames taken = namesOf(into);
    EXPECT_EQ(freeStyleName(taken, u8"X"), u8"X (3001)");
    ClashChoices renames;
    for (int i = 0; i < kCount; ++i)
        renames[{u8"X", i}] = {ClashAction::Rename, u8"X"};
    const auto rows = transferStyles(into, styles, renames);
    ASSERT_EQ(rows.size(), std::size_t(kCount));
    EXPECT_EQ(into.back().name, u8"X (6000)");
    EXPECT_EQ(namesOf(into).size(), into.size());
}

TEST(StyleLists, CommitRefusesTakenNamesAndAppliesMultiEdit)
{
    StyleList l{named(u8"A"), named(u8"B"), named(u8"C")};
    EXPECT_EQ(commitStyle(l, {std::nullopt, named(u8"B"), u8"B"}).error(), u8"B");
    EXPECT_EQ(commitStyle(l, {0, named(u8"B"), u8"A"}).error(), u8"B");
    EXPECT_EQ(*commitStyle(l, {0, named(u8"Z"), u8"A"}), 0u);
    EXPECT_EQ(names(l), (std::vector<std::u8string>{u8"Z", u8"B", u8"C"}));
    EXPECT_EQ(*commitStyle(l, {std::nullopt, named(u8"New"), u8"New"}), 3u);
    auto bold = named(u8"B");
    bold.bold = true;
    bold.fontname = u8"Times";
    EXPECT_EQ(*commitStyle(l, {1, bold, u8"B", {1, 2}, style_field::Bold}), 1u);
    EXPECT_TRUE(l[2].bold);
    EXPECT_EQ(l[2].fontname, u8"Arial");
    EXPECT_EQ(l[1].fontname, u8"Times");
}

TEST(StyleLists, CommitEditsTheOpenedCopyOfARepeatedName)
{
    StyleList l{named(u8"Dup", u8"First"), named(u8"Mid"), named(u8"Dup", u8"Last")};
    EXPECT_EQ(*commitStyle(l, {0, named(u8"Dup", u8"Edited"), u8"Dup"}), 0u);
    EXPECT_EQ(l[0].fontname, u8"Edited");
    EXPECT_EQ(l[2].fontname, u8"Last");
    EXPECT_EQ(*commitStyle(l, {1, named(u8"Dup", u8"Again"), u8"Dup"}), 2u);
    EXPECT_EQ(l[1].name, u8"Mid");
    EXPECT_EQ(l[2].fontname, u8"Again");
}

TEST(DocumentStyles, EditsKeepUntouchedStylesAndRenameLines)
{
    EditSession session{load(kScript)};
    auto styles = *documentStyles(session);
    ASSERT_EQ(styles.size(), 4u);
    styles[1].name = u8"Title";
    styles[1].fontsize = u8"36";
    std::rotate(styles.begin(), styles.begin() + 1, styles.begin() + 2);
    ASSERT_TRUE(setDocumentStyles(session, styles, {1, 0, 2, 3}, std::pair{std::u8string(u8"Sign"), std::u8string(u8"Title")}));
    EXPECT_EQ(session.history().back().name, "Style editing");
    EXPECT_EQ(session.document().lines()[1]->style, u8"Title");
    const std::string file = saved(session.document());
    EXPECT_NE(file.find("Style: Title,Verdana,36,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,-1,0,0,0,100,100,0,0,1,2,2,8,10,10,10,1\n"
                        "Style: Default,Arial,20,"),
              std::string::npos)
        << file;
    const auto steps = session.historySize();
    ASSERT_TRUE(setDocumentStyles(session, *documentStyles(session), {0, 1, 2, 3}));
    EXPECT_EQ(session.historySize(), steps);
    auto more = *documentStyles(session);
    more.push_back(named(u8"Extra"));
    ASSERT_TRUE(setDocumentStyles(session, more, {0, 1, 2, 3, std::nullopt}));
    EXPECT_NE(saved(session.document()).find("Style: TLmode,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,8,10,10,10,1\nStyle: Extra,Arial,40,"),
              std::string::npos);
}

TEST(DocumentStyles, CleanKeepsUsedAndTheTranslationStyle)
{
    EditSession session{load(kScript)};
    const auto r = cleanDocumentStyles(session);
    ASSERT_TRUE(r);
    EXPECT_EQ(r->used, (std::vector<std::u8string>{u8"Default", u8"Sign", u8"TLmode"}));
    EXPECT_EQ(r->deleted, (std::vector<std::u8string>{u8"Unused"}));
    EXPECT_EQ(documentStyles(session)->size(), 3u);
    EXPECT_EQ(session.history().back().name, "Style editing");
}

TEST(StyleCatalogFiles, LegacyFormatAndCatalogs)
{
    const auto bytes = writeCatalog({named(u8"A")});
    EXPECT_EQ(bytes.substr(0, 3), "\xEF\xBB\xBF");
    EXPECT_EQ(bytes.substr(3), "Style: A,Arial,40,&H00FFFFFF,&H00000000,&H00FF0000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,20,20,20,1\r\n");
    EXPECT_EQ(names(readCatalog(bytes + "Comment\nStyle: B,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1")),
              (std::vector<std::u8string>{u8"A", u8"B"}));
    const auto dir = std::filesystem::temp_directory_path() / "hikari-style-catalogs-test";
    std::filesystem::remove_all(dir);
    {
        StyleCatalogs catalogs(dir);
        EXPECT_EQ(catalogs.names(), (std::vector<std::u8string>{u8"Default"}));
        ASSERT_EQ(catalogs.styles().size(), 1u);
        EXPECT_EQ(catalogs.styles()[0].fontsize, u8"30");
        ASSERT_TRUE(catalogs.create(u8"Show"));
        catalogs.styles().push_back(named(u8"Title"));
        ASSERT_TRUE(catalogs.choose(u8"Default"));
        EXPECT_FALSE(catalogs.remove(u8"Default"));
    }
    StyleCatalogs again(dir);
    EXPECT_EQ(again.names(), (std::vector<std::u8string>{u8"Default", u8"Show"}));
    EXPECT_EQ(again.find(u8"Show", u8"Title")->name, u8"Title");
    EXPECT_FALSE(again.find(u8"Show", u8"Missing"));
    EXPECT_FALSE(again.find(u8"Nope", u8"Title"));
    EXPECT_EQ(again.current(), u8"Default");
    ASSERT_TRUE(again.choose(u8"Show"));
    EXPECT_EQ(names(again.styles()), (std::vector<std::u8string>{u8"Title"}));
    ASSERT_TRUE(again.remove(u8"Show"));
    EXPECT_EQ(again.current(), u8"Default");
    EXPECT_FALSE(std::filesystem::exists(dir / "Show.sty"));
    std::filesystem::remove_all(dir);
}
