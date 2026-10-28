// G9: Line groups (legacy trees at 20d647c4) and the accepted
// G56-contiguity rule across commands.

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

// a, then group G (description, members m1 m2, open), then z.
constexpr std::string_view kGrouped = "[Events]\n"
                                      "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,a\n"
                                      "Comment: 0,0:00:00.00,0:00:00.00,Default,[tree_description],0,0,0,,G\n"
                                      "Dialogue: 0,0:00:03.00,0:00:04.00,Default,[tree_opened],0,0,0,,m1\n"
                                      "Dialogue: 0,0:00:05.00,0:00:06.00,Default,[tree_opened],0,0,0,,m2\n"
                                      "Dialogue: 0,0:00:00.50,0:00:07.00,Default,,0,0,0,,z\n";

std::string shape(const EditSession &s)
{
    std::string out;
    for (const auto *l : s.document().lines()) {
        out += std::string(l->text.begin(), l->text.end());
        out += l->group == core::GroupMarker::Description ? "[D]"
               : l->group == core::GroupMarker::Opened     ? "[o]"
               : l->group == core::GroupMarker::Closed     ? "[c]"
                                                           : "";
        out += l->visibility == core::LineVisibility::Hidden ? "h " : " ";
    }
    return out;
}

struct GroupsTest : ::testing::Test {
    EditSession session{load(kGrouped)};
    core::LineId a{1}, g{2}, m1{3}, m2{4}, z{5};
    void select(std::set<core::LineId> lines, core::LineId active) { session.setSelection(Selection{active, lines, active, {}}); }
};

} // namespace

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
    ASSERT_TRUE(duplicateLines(session)); // a member copy inside the run
    EXPECT_EQ(shape(session), "a G[D] m1[o] m1[o] m2[o] z ");
    select({m2}, m2);
    ASSERT_TRUE(insertLine(session, InsertWhere::After)); // the copy is a member
    ASSERT_TRUE(deleteLines(session));                    // a member deleted, the run stays valid
    EXPECT_EQ(shape(session), "a G[D] m1[o] m1[o] m2[o] z ");
}

TEST_F(GroupsTest, AGroupBreakIsRefusedBeforeAnythingChanges)
{
    const auto steps = session.historySize();
    const auto revision = session.revision();
    // Sorting by Start would move z (0.5 s) between... and orphan members.
    EXPECT_EQ(sortLines(session, SortKey::Start, false).error(), CommandRefusal::GroupBreak);
    EXPECT_FALSE(session.lastGroupBreak().empty());
    // Deleting the description orphans its members.
    select({g}, g);
    EXPECT_EQ(deleteLines(session).error(), CommandRefusal::GroupBreak);
    // Swapping a member with an ordinary Line breaks the run.
    select({m2, z}, m2);
    EXPECT_EQ(swapLines(session).error(), CommandRefusal::GroupBreak);
    EXPECT_EQ(session.historySize(), steps); // C07: nothing changed
    EXPECT_EQ(session.revision(), revision);
    EXPECT_EQ(shape(session), "a G[D] m1[o] m2[o] z ");
}

TEST_F(GroupsTest, MakeGroupsClosesTheMembersBehindADescription)
{
    select({a}, a);
    ASSERT_TRUE(makeGroups(session));
    EXPECT_EQ(session.history().back().name, "Adding tree");
    EXPECT_EQ(shape(session), "[D] a[c]h G[D] m1[o] m2[o] z ");
    // The hidden member gave way to its description as the active Line.
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
    // Legacy would give description, A(member), X(ordinary), B(member).
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
    EXPECT_FALSE(renameGroup(session, g, u8"")); // the dialog needs a name
    ASSERT_TRUE(toggleGroup(session, g));
    ASSERT_TRUE(selectGroup(session, g)); // opens, then selects the members
    EXPECT_EQ(session.selection().selected, (std::set<core::LineId>{m1, m2}));
    EXPECT_EQ(shape(session), "a Signs[D] m1[o] m2[o] z ");
    ASSERT_TRUE(removeGroup(session, g));
    EXPECT_EQ(session.history().back().name, "Removing tree");
    EXPECT_EQ(shape(session), "a m1 m2 z "); // the description Line is deleted
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(shape(session), "a Signs[D] m1[o] m2[o] z ");
}

TEST_F(GroupsTest, AddLinesMovesThemIntoTheGroup)
{
    select({a, z}, a);
    ASSERT_TRUE(addLinesToGroup(session, g));
    EXPECT_EQ(session.history().back().name, "Adding line to tree");
    EXPECT_EQ(shape(session), "G[D] a[o] m1[o] m2[o] z[o] ");
    // Into a closed group, they are closed too.
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
