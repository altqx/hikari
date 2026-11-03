#include "hikari/application/fold_groups.h"
#include "hikari/core/aegisub_folds.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/ass_save.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

using namespace hikari;
using namespace hikari::application;

namespace {

core::Document load(std::string_view text)
{
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return core::loadAss(bytes).document;
}

core::Document fixture(const std::string &name)
{
    std::ifstream in(std::string(HIKARI_EXTRADATA_FIXTURES) + "/" + name, std::ios::binary);
    return load(std::string(std::istreambuf_iterator<char>(in), {}));
}

std::string shape(const core::Document &d)
{
    auto marker = [](core::GroupMarker g) {
        return g == core::GroupMarker::Description ? "[D]"
               : g == core::GroupMarker::Opened    ? "[o]"
               : g == core::GroupMarker::Closed    ? "[c]"
                                                   : "";
    };
    std::string out;
    for (const auto *l : d.lines()) {
        out += std::string(l->text.begin(), l->text.end());
        for (const auto g : l->outerGroups)
            out += marker(g);
        out += marker(l->group);
        out += l->visibility == core::LineVisibility::Hidden ? "h|" : "|";
    }
    return out;
}

bool hasFoldEntry(const core::LineRecord &l)
{
    return std::ranges::any_of(l.extra, [](const core::Extradata &e) { return e.key == core::kFoldKey; });
}

constexpr auto kConverted = "Outer fold opens[D]|Outer fold opens[o]|Inner fold opens, collapsed[o][D]|"
                            "Inner fold opens, collapsed[o][c]h|{\\i1}Inside both{\\i0}[o][c]h|Inner fold closes[o][c]h|"
                            "Outer fold closes[o]|First adjacent fold[D]|First adjacent fold[o]|"
                            "First adjacent fold ends[o]|Second adjacent fold[D]|Second adjacent fold[o]|"
                            "Second adjacent fold ends, with a script value[o]|Shares the script value|"
                            "Names an id with no entry|";

}

TEST(FoldGroups, NestedAndAdjacentFoldsBecomeLineGroupsInOneStep)
{
    EditSession session{fixture("folds.aegisub.ass")};
    const auto before = shape(session.document());
    EXPECT_EQ(foldConversion(session.document()).converted, 4u);
    const auto steps = session.historySize();
    const auto result = convertFoldsToGroups(session);
    ASSERT_TRUE(result);
    EXPECT_EQ(result->converted, 4u);
    EXPECT_EQ(result->skipped, 0u);
    EXPECT_EQ(shape(session.document()), kConverted);
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(session.history().back().name, "Converting folds to line groups");
    EXPECT_TRUE(session.document().lines()[0]->comment);
    EXPECT_TRUE(session.lastGroupBreak().empty());
    EXPECT_EQ(foldConversion(session.document()).converted, 0u);
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(shape(session.document()), before);
    EXPECT_EQ(core::aegisubFolds(session.document()).folds.size(), 4u);
}

TEST(FoldGroups, ConvertedDelimitersLoseOnlyTheirFoldEntry)
{
    EditSession session{fixture("folds.aegisub.ass")};
    ASSERT_TRUE(convertFoldsToGroups(session));
    for (const auto *l : session.document().lines())
        EXPECT_FALSE(hasFoldEntry(*l)) << std::string(l->text.begin(), l->text.end());
    const auto bytes = core::encodeAss(session.document());
    const auto reopened = core::loadAss(bytes).document;
    EXPECT_EQ(shape(reopened), kConverted);
    const auto folds = core::aegisubFolds(reopened);
    EXPECT_TRUE(folds.folds.empty());
    EXPECT_TRUE(folds.dropped.empty());
    const auto *scriptValue = reopened.lines()[12];
    ASSERT_EQ(scriptValue->extra.size(), 1u);
    EXPECT_EQ(scriptValue->extra[0].key, u8"hikari.fixture");
}

TEST(FoldGroups, TheDescriptionIsTheOpenersTextWithoutTags)
{
    EditSession session{load("[Events]\n"
                             "Dialogue: 0,0:00:01.00,0:00:02.00,Sign,,0,0,0,,{=0}{\\an8}Station {\\i1}sign{\\i0}\n"
                             "Dialogue: 0,0:00:02.00,0:00:03.00,Default,,0,0,0,,{=1}end\n"
                             "\n[Aegisub Extradata]\n"
                             "Data: 0,_aegi_folddata,e0;0;1\n"
                             "Data: 1,_aegi_folddata,e1;0;1\n")};
    ASSERT_TRUE(convertFoldsToGroups(session));
    const auto *description = session.document().lines()[0];
    EXPECT_EQ(description->text, u8"Station sign");
    EXPECT_EQ(description->style, u8"Sign");
    EXPECT_EQ(description->group, core::GroupMarker::Description);
}

TEST(FoldGroups, FoldsThatTouchAGroupOrDoNotPairAreSkippedAndKeepTheirEntries)
{
    EditSession session{load("[Events]\n"
                             "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{=0}a\n"
                             "Comment: 0,0:00:00.00,0:00:00.00,Default,[tree_description],0,0,0,,G\n"
                             "Dialogue: 0,0:00:01.00,0:00:02.00,Default,[tree_opened],0,0,0,,{=1}b\n"
                             "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{=2}c\n"
                             "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{=3}d\n"
                             "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{=4}e\n"
                             "\n[Aegisub Extradata]\n"
                             "Data: 0,_aegi_folddata,e0;0;1\n"
                             "Data: 1,_aegi_folddata,e1;0;1\n"
                             "Data: 2,_aegi_folddata,e0;0;2\n"
                             "Data: 3,_aegi_folddata,e1;0;2\n"
                             "Data: 4,_aegi_folddata,e1;0;9\n")};
    const auto planned = foldConversion(session.document());
    EXPECT_EQ(planned.converted, 1u);
    EXPECT_EQ(planned.skipped, 2u);
    const auto result = convertFoldsToGroups(session);
    ASSERT_TRUE(result);
    EXPECT_EQ(result->skipped, 2u);
    EXPECT_EQ(shape(session.document()), "a|G[D]|b[o]|c[D]|c[o]|d[o]|e|");
    const auto lines = session.document().lines();
    EXPECT_TRUE(hasFoldEntry(*lines[0]));
    EXPECT_TRUE(hasFoldEntry(*lines[2]));
    EXPECT_FALSE(hasFoldEntry(*lines[4]));
    EXPECT_TRUE(hasFoldEntry(*lines[6]));
}

TEST(FoldGroups, NothingToConvertIsRefused)
{
    EditSession session{load("[Events]\nDialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,plain\n")};
    EXPECT_EQ(foldConversion(session.document()).converted, 0u);
    EXPECT_EQ(convertFoldsToGroups(session).error(), CommandRefusal::Invalid);
}
