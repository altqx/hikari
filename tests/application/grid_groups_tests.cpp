#include "hikari/application/grid_commands.h"
#include "hikari/application/grid_groups.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/ass_save.h"
#include "hikari/core/line_groups.h"

#include <gtest/gtest.h>

#include <cstring>
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

constexpr std::string_view kGrouped = "[Events]\n"
                                      "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,a\n"
                                      "Comment: 0,0:00:00.00,0:00:00.00,Default,[tree_description],0,0,0,,G\n"
                                      "Dialogue: 0,0:00:03.00,0:00:04.00,Default,[tree_opened],0,0,0,,m1\n"
                                      "Dialogue: 0,0:00:05.00,0:00:06.00,Default,[tree_opened],0,0,0,,m2\n"
                                      "Dialogue: 0,0:00:00.50,0:00:07.00,Default,,0,0,0,,z\n";

std::string shape(const EditSession &s)
{
    auto marker = [](core::GroupMarker g) {
        return g == core::GroupMarker::Description ? "[D]"
               : g == core::GroupMarker::Opened    ? "[o]"
               : g == core::GroupMarker::Closed    ? "[c]"
                                                   : "";
    };
    std::string out;
    for (const auto *l : s.document().lines()) {
        out += std::string(l->text.begin(), l->text.end());
        for (const auto g : l->outerGroups)
            out += marker(g);
        out += marker(l->group);
        out += l->visibility == core::LineVisibility::Hidden ? "h " : " ";
    }
    return out;
}

constexpr std::string_view kNested = "[Events]\n"
                                     "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,a\n"
                                     "Comment: 0,0:00:00.00,0:00:00.00,Default,[tree_description],0,0,0,,S\n"
                                     "Dialogue: 0,0:00:03.00,0:00:04.00,Default,[tree_opened],0,0,0,,s1\n"
                                     "Comment: 0,0:00:00.00,0:00:00.00,Default,[tree_opened][tree_description],0,0,0,,C\n"
                                     "Dialogue: 0,0:00:05.00,0:00:06.00,Default,[tree_opened][tree_opened],0,0,0,,c1\n"
                                     "Dialogue: 0,0:00:07.00,0:00:08.00,Default,[tree_opened][tree_opened],0,0,0,,c2\n"
                                     "Dialogue: 0,0:00:09.00,0:00:10.00,Default,[tree_opened],0,0,0,,s2\n"
                                     "Dialogue: 0,0:00:11.00,0:00:12.00,Default,,0,0,0,,z\n";

struct GroupsTest : ::testing::Test {
    EditSession session{load(kGrouped)};
    core::LineId a{1}, g{2}, m1{3}, m2{4}, z{5};
    void select(std::set<core::LineId> lines, core::LineId active) { session.setSelection(Selection{active, lines, active, {}}); }
};

struct NestedTest : ::testing::Test {
    EditSession session{load(kNested)};
    core::LineId a{1}, S{2}, s1{3}, C{4}, c1{5}, c2{6}, s2{7}, z{8};
    void select(std::set<core::LineId> lines, core::LineId active) { session.setSelection(Selection{active, lines, active, {}}); }
};

constexpr auto kNestedShape = "a S[D] s1[o] C[o][D] c1[o][o] c2[o][o] s2[o] z ";

}

TEST_F(GroupsTest, OwnersFollowTheDescription)
{
    const auto owners = core::groupOwners(session.document());
    EXPECT_EQ(owners.at(m1.value), g);
    EXPECT_EQ(owners.at(m2.value), g);
    EXPECT_FALSE(owners.contains(a.value));
    EXPECT_EQ(core::groupMembers(session.document(), g), (std::vector<core::LineId>{m1, m2}));
}

TEST_F(GroupsTest, CommandsThatKeepGroupsAreAllowed)
{
    select({m1}, m1);
    ASSERT_TRUE(duplicateLines(session));
    EXPECT_EQ(shape(session), "a G[D] m1[o] m1[o] m2[o] z ");
    select({m2}, m2);
    ASSERT_TRUE(insertLine(session, InsertWhere::After));
    ASSERT_TRUE(deleteLines(session));
    EXPECT_EQ(shape(session), "a G[D] m1[o] m1[o] m2[o] z ");
}

TEST_F(GroupsTest, AGroupBreakIsRefusedBeforeAnythingChanges)
{
    const auto steps = session.historySize();
    const auto revision = session.revision();
    EXPECT_EQ(sortLines(session, SortKey::Start, false).error(), CommandRefusal::GroupBreak);
    EXPECT_FALSE(session.lastGroupBreak().empty());
    select({g}, g);
    EXPECT_EQ(deleteLines(session).error(), CommandRefusal::GroupBreak);
    select({m2, z}, m2);
    EXPECT_EQ(swapLines(session).error(), CommandRefusal::GroupBreak);
    EXPECT_EQ(session.historySize(), steps);
    EXPECT_EQ(session.revision(), revision);
    EXPECT_EQ(shape(session), "a G[D] m1[o] m2[o] z ");
}

TEST_F(GroupsTest, MakeGroupsClosesTheMembersBehindADescription)
{
    select({a}, a);
    ASSERT_TRUE(makeGroups(session));
    EXPECT_EQ(session.history().back().name, "Adding tree");
    EXPECT_EQ(shape(session), "[D] a[c]h G[D] m1[o] m2[o] z ");
    EXPECT_EQ(session.document().lines()[0]->id, *session.selection().active);
    EXPECT_TRUE(session.document().lines()[0]->comment);
}

TEST_F(GroupsTest, MakingAGroupAcrossAHiddenOrdinaryLineIsRefused)
{
    EditSession hidden{load("[Events]\n"
                            "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,A\n"
                            "Dialogue: 0,0:00:03.00,0:00:04.00,Default,[hidden],0,0,0,,X\n"
                            "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,B\n")};
    hidden.setSelection(Selection{core::LineId{1}, {core::LineId{1}, core::LineId{3}}, {}, {}});
    EXPECT_EQ(makeGroups(hidden).error(), CommandRefusal::GroupBreak);
}

TEST_F(GroupsTest, ToggleRenameSelectAndRemove)
{
    ASSERT_TRUE(toggleGroup(session, g));
    EXPECT_EQ(shape(session), "a G[D] m1[c]h m2[c]h z ");
    ASSERT_TRUE(toggleGroup(session, g));
    EXPECT_EQ(shape(session), "a G[D] m1[o] m2[o] z ");
    ASSERT_TRUE(renameGroup(session, g, u8"Signs"));
    EXPECT_EQ(session.history().back().name, "Setting tree description");
    EXPECT_FALSE(renameGroup(session, g, u8""));
    ASSERT_TRUE(toggleGroup(session, g));
    ASSERT_TRUE(selectGroup(session, g));
    EXPECT_EQ(session.selection().selected, (std::set<core::LineId>{m1, m2}));
    EXPECT_EQ(shape(session), "a Signs[D] m1[o] m2[o] z ");
    ASSERT_TRUE(removeGroup(session, g));
    EXPECT_EQ(session.history().back().name, "Removing tree");
    EXPECT_EQ(shape(session), "a m1 m2 z ");
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(shape(session), "a Signs[D] m1[o] m2[o] z ");
}

TEST_F(GroupsTest, AddLinesMovesThemIntoTheGroup)
{
    select({a, z}, a);
    ASSERT_TRUE(addLinesToGroup(session, g));
    EXPECT_EQ(session.history().back().name, "Adding line to tree");
    EXPECT_EQ(shape(session), "G[D] a[o] m1[o] m2[o] z[o] ");
    EditSession closed{load(kGrouped)};
    ASSERT_TRUE(toggleGroup(closed, g));
    closed.setSelection(Selection{z, {z}, z, {}});
    ASSERT_TRUE(addLinesToGroup(closed, g));
    EXPECT_EQ(shape(closed), "a G[D] m1[c]h m2[c]h z[c]h ");
}

TEST_F(GroupsTest, CopyTakesTheDescriptionAndMembers)
{
    const auto text = copyGroup(session, g);
    EXPECT_EQ(text, u8"Comment: 0,0:00:00.00,0:00:00.00,Default,[tree_description],0,0,0,,G\r\n"
                    u8"Dialogue: 0,0:00:03.00,0:00:04.00,Default,[tree_opened],0,0,0,,m1\r\n"
                    u8"Dialogue: 0,0:00:05.00,0:00:06.00,Default,[tree_opened],0,0,0,,m2\r\n");
}

TEST_F(GroupsTest, GroupsSurviveSaveAndReopen)
{
    select({a}, a);
    ASSERT_TRUE(makeGroups(session));
    ASSERT_TRUE(renameGroup(session, session.document().lines()[0]->id, u8"New"));
    const auto bytes = core::encodeAss(session.document());
    EditSession reopened{core::loadAss(bytes).document};
    EXPECT_EQ(shape(reopened), shape(session));
    EXPECT_EQ(shape(reopened), "New[D] a[c]h G[D] m1[o] m2[o] z ");
}

TEST_F(NestedTest, AGroupIsItsDescriptionAndTheDeeperRunAfterIt)
{
    EXPECT_EQ(shape(session), kNestedShape);
    const auto owners = core::groupOwners(session.document());
    EXPECT_EQ(owners.at(s1.value), S);
    EXPECT_EQ(owners.at(C.value), S);
    EXPECT_EQ(owners.at(c1.value), C);
    EXPECT_EQ(owners.at(s2.value), S);
    EXPECT_FALSE(owners.contains(z.value));
    EXPECT_EQ(core::groupMembers(session.document(), S), (std::vector<core::LineId>{s1, C, s2}));
    EXPECT_EQ(core::groupContents(session.document(), S), (std::vector<core::LineId>{s1, C, c1, c2, s2}));
    EXPECT_EQ(core::groupContents(session.document(), C), (std::vector<core::LineId>{c1, c2}));
}

TEST_F(NestedTest, ClosingAGroupHidesEverythingInsideAndInnerGroupsKeepTheirState)
{
    ASSERT_TRUE(toggleGroup(session, C));
    EXPECT_EQ(shape(session), "a S[D] s1[o] C[o][D] c1[o][c]h c2[o][c]h s2[o] z ");
    ASSERT_TRUE(toggleGroup(session, S));
    EXPECT_EQ(shape(session), "a S[D] s1[c]h C[c][D]h c1[c][c]h c2[c][c]h s2[c]h z ");
    ASSERT_TRUE(toggleGroup(session, S));
    EXPECT_EQ(shape(session), "a S[D] s1[o] C[o][D] c1[o][c]h c2[o][c]h s2[o] z ");
}

TEST_F(NestedTest, SelectingAGroupOpensItAndSelectsEverythingInside)
{
    ASSERT_TRUE(toggleGroup(session, C));
    ASSERT_TRUE(toggleGroup(session, S));
    ASSERT_TRUE(selectGroup(session, S));
    EXPECT_EQ(session.selection().selected, (std::set<core::LineId>{s1, C, c1, c2, s2}));
    EXPECT_EQ(shape(session), "a S[D] s1[o] C[o][D] c1[o][c]h c2[o][c]h s2[o] z ");
}

TEST_F(NestedTest, MakingAGroupInsideAGroupNestsIt)
{
    select({s1}, s1);
    ASSERT_TRUE(makeGroups(session));
    EXPECT_EQ(shape(session), "a S[D] [o][D] s1[o][c]h C[o][D] c1[o][o] c2[o][o] s2[o] z ");
    EXPECT_TRUE(session.lastGroupBreak().empty());
}

TEST_F(NestedTest, MakingAGroupAroundWholeGroupsNestsThem)
{
    EditSession flat{load(kGrouped)};
    ASSERT_TRUE(toggleGroup(flat, core::LineId{2}));
    flat.setSelection(Selection{core::LineId{1}, {core::LineId{1}, core::LineId{2}}, core::LineId{1}, {}});
    ASSERT_TRUE(makeGroups(flat));
    EXPECT_EQ(shape(flat), "[D] a[c]h G[c][D]h m1[c][c]h m2[c][c]h z ");

    select({s1, C, c1, c2, s2}, s1);
    ASSERT_TRUE(makeGroups(session));
    EXPECT_EQ(shape(session), "a S[D] [o][D] s1[o][c]h C[o][c][D]h c1[o][c][o]h c2[o][c][o]h s2[o][c]h z ");
}

TEST_F(NestedTest, MakingAGroupThatCutsAGroupIsRefused)
{
    const auto revision = session.revision();
    select({s2, z}, s2);
    EXPECT_EQ(makeGroups(session).error(), CommandRefusal::GroupBreak);
    select({s1, C, c1}, s1);
    EXPECT_EQ(makeGroups(session).error(), CommandRefusal::GroupBreak);
    EXPECT_EQ(session.revision(), revision);
    EXPECT_EQ(shape(session), kNestedShape);
}

TEST_F(NestedTest, RemovingAGroupMovesWhatWasInsideUpOneLevel)
{
    ASSERT_TRUE(toggleGroup(session, S));
    ASSERT_TRUE(removeGroup(session, S));
    EXPECT_EQ(shape(session), "a s1 C[D] c1[o] c2[o] s2 z ");
    ASSERT_TRUE(session.undo());
    ASSERT_TRUE(session.undo());
    ASSERT_TRUE(removeGroup(session, C));
    EXPECT_EQ(shape(session), "a S[D] s1[o] c1[o] c2[o] s2[o] z ");
}

TEST_F(NestedTest, AddingLinesWorksAtTheGroupsOwnLevel)
{
    select({s2}, s2);
    ASSERT_TRUE(addLinesToGroup(session, C));
    EXPECT_EQ(shape(session), "a S[D] s1[o] C[o][D] c1[o][o] c2[o][o] s2[o][o] z ");
    ASSERT_TRUE(session.undo());
    const auto revision = session.revision();
    select({z}, z);
    EXPECT_EQ(addLinesToGroup(session, C).error(), CommandRefusal::GroupBreak);
    select({S}, S);
    EXPECT_EQ(addLinesToGroup(session, C).error(), CommandRefusal::Invalid);
    EXPECT_EQ(session.revision(), revision);
}

TEST_F(NestedTest, AddingASelectedGroupNestsItWhole)
{
    EditSession two{load("[Events]\n"
                         "Comment: 0,0:00:00.00,0:00:00.00,Default,[tree_description],0,0,0,,A\n"
                         "Dialogue: 0,0:00:01.00,0:00:02.00,Default,[tree_opened],0,0,0,,a1\n"
                         "Comment: 0,0:00:00.00,0:00:00.00,Default,[tree_description],0,0,0,,B\n"
                         "Dialogue: 0,0:00:03.00,0:00:04.00,Default,[tree_closed],0,0,0,,b1\n")};
    two.setSelection(Selection{core::LineId{3}, {core::LineId{3}}, core::LineId{3}, {}});
    ASSERT_TRUE(addLinesToGroup(two, core::LineId{1}));
    EXPECT_EQ(shape(two), "A[D] a1[o] B[o][D] b1[o][c]h ");
}

TEST_F(NestedTest, StructuralEditsKeepEveryDepthContiguous)
{
    select({c1}, c1);
    ASSERT_TRUE(duplicateLines(session));
    EXPECT_EQ(shape(session), "a S[D] s1[o] C[o][D] c1[o][o] c1[o][o] c2[o][o] s2[o] z ");
    ASSERT_TRUE(session.undo());
    select({c2, s2}, c2);
    EXPECT_EQ(swapLines(session).error(), CommandRefusal::GroupBreak);
    select({C}, C);
    EXPECT_EQ(deleteLines(session).error(), CommandRefusal::GroupBreak);
    EXPECT_EQ(shape(session), kNestedShape);
}

TEST_F(NestedTest, CopyTakesEverythingInsideTheGroup)
{
    EXPECT_EQ(copyGroup(session, C),
              u8"Comment: 0,0:00:00.00,0:00:00.00,Default,[tree_opened][tree_description],0,0,0,,C\r\n"
              u8"Dialogue: 0,0:00:05.00,0:00:06.00,Default,[tree_opened][tree_opened],0,0,0,,c1\r\n"
              u8"Dialogue: 0,0:00:07.00,0:00:08.00,Default,[tree_opened][tree_opened],0,0,0,,c2\r\n");
}

TEST_F(NestedTest, NestedGroupsSurviveSaveAndReopen)
{
    ASSERT_TRUE(toggleGroup(session, C));
    const auto bytes = core::encodeAss(session.document());
    const std::string_view text(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    EXPECT_NE(text.find("Default,[tree_opened][tree_description],0,0,0,,C\n"), std::string_view::npos) << text;
    EXPECT_NE(text.find("Default,[tree_opened][tree_closed],0,0,0,,c1\n"), std::string_view::npos) << text;
    EditSession reopened{core::loadAss(bytes).document};
    EXPECT_EQ(shape(reopened), "a S[D] s1[o] C[o][D] c1[o][c]h c2[o][c]h s2[o] z ");
}
