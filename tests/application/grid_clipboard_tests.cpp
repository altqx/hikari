// G2: Grid clipboard commands (legacy SubsGrid::CopyRows/OnPaste at
// 20d647c4): what is copied, where pasted Lines go, one undo step each and
// the selection afterwards.

#include "hikari/application/grid_clipboard.h"
#include "hikari/core/ass_load.h"

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

constexpr std::string_view kThree = "[Events]\n"
                                    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,a\n"
                                    "Dialogue: 2,0:00:05.00,0:00:06.00,Sign,B,7,0,0,fx,b\n"
                                    "Dialogue: 0,0:00:06.50,0:00:08.00,Default,,0,0,0,,c\n";

struct GridClipboardTest : ::testing::Test {
    EditSession session{load(kThree)};
    core::LineId a{1}, b{2}, c{3};
    void select(std::set<core::LineId> lines, core::LineId active) { session.setSelection(Selection{active, lines, active, {}}); }
    std::vector<std::u8string> texts() const
    {
        std::vector<std::u8string> out;
        for (const auto *l : session.document().lines())
            out.push_back(l->text);
        return out;
    }
    const core::LineRecord &line(std::size_t i) const { return *session.document().lines()[i]; }
};

} // namespace

TEST_F(GridClipboardTest, CopyTakesEverySelectedLineInDocumentOrder)
{
    select({c, a}, c);
    EXPECT_EQ(copyRows(session), u8"Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,a\r\n"
                                 u8"Dialogue: 0,0:00:06.50,0:00:08.00,Default,,0,0,0,,c\r\n");
    EXPECT_EQ(copyColumns(session, core::column::Text), u8"a\r\nc\r\n");
}

TEST_F(GridClipboardTest, PasteGoesBeforeTheFirstSelectedLineAsOneStep)
{
    select({c, b}, c);
    const auto steps = session.historySize();
    ASSERT_TRUE(pasteRows(session, u8"Dialogue: 0,0:00:09.00,0:00:10.00,Default,,0,0,0,,x\r\n"
                                   u8"Dialogue: 0,0:00:11.00,0:00:12.00,Default,,0,0,0,,y\r\n"));
    EXPECT_EQ(texts(), (std::vector<std::u8string>{u8"a", u8"x", u8"y", u8"b", u8"c"}));
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(session.history().back().name, "Pasting lines");
    // The pasted Lines are the selection, the first one active.
    EXPECT_EQ(session.selection().active, line(1).id);
    EXPECT_EQ(session.selection().selected, (std::set<core::LineId>{line(1).id, line(2).id}));
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(texts(), (std::vector<std::u8string>{u8"a", u8"b", u8"c"}));
}

TEST_F(GridClipboardTest, NothingToPasteChangesNothing)
{
    select({b}, b);
    const auto steps = session.historySize();
    EXPECT_TRUE(pasteRows(session, u8"\r\n\r\n"));
    EXPECT_EQ(session.historySize(), steps);
    session.setSelection(Selection{});
    EXPECT_FALSE(pasteRows(session, u8"Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,x"));
}

TEST_F(GridClipboardTest, HiddenSelectedLinesAreNotThePastePosition)
{
    select({a, c}, a);
    ASSERT_TRUE(pasteRows(session, u8"plain", [&](core::LineId id) { return id != a; }));
    EXPECT_EQ(texts(), (std::vector<std::u8string>{u8"a", u8"b", u8"plain", u8"c"}));
}

TEST_F(GridClipboardTest, PastedLinesJoinTheGroupTheyLandIn)
{
    EditSession grouped{load("[Events]\n"
                             "Dialogue: 0,0:00:01.00,0:00:02.00,Default,[tree_description],0,0,0,,group\n"
                             "Dialogue: 0,0:00:01.00,0:00:02.00,Default,[tree_opened],0,0,0,,member\n"
                             "Dialogue: 0,0:00:01.00,0:00:02.00,Default,[hidden],0,0,0,,hidden\n")};
    const auto lines = grouped.document().lines();
    grouped.setSelection(Selection{lines[1]->id, {lines[1]->id}, lines[1]->id, {}});
    ASSERT_TRUE(pasteRows(grouped, u8"x"));
    EXPECT_EQ(grouped.document().lines()[1]->group, core::GroupMarker::Opened);
    // A hidden Line after the position hides the pasted Line.
    const auto hiddenLine = grouped.document().lines()[3]->id;
    grouped.setSelection(Selection{hiddenLine, {hiddenLine}, hiddenLine, {}});
    ASSERT_TRUE(pasteRows(grouped, u8"y"));
    EXPECT_EQ(grouped.document().lines()[3]->visibility, core::LineVisibility::Hidden);
    EXPECT_EQ(grouped.document().lines()[3]->group, core::GroupMarker::None);
}

TEST_F(GridClipboardTest, PasteColumnsFillsTheSelectedLinesInOrder)
{
    select({a, c}, c);
    const auto steps = session.historySize();
    ASSERT_TRUE(pasteColumns(session,
                             u8"Dialogue: 4,0:00:20.00,0:00:21.00,Other,Z,1,1,1,e,first\r\n"
                             u8"Dialogue: 5,0:00:30.00,0:00:31.00,Other,Z,1,1,1,e,second\r\n"
                             u8"Dialogue: 6,0:00:40.00,0:00:41.00,Other,Z,1,1,1,e,ignored\r\n",
                             core::column::Text | core::column::Start));
    EXPECT_EQ(texts(), (std::vector<std::u8string>{u8"first", u8"b", u8"second"}));
    EXPECT_EQ(line(0).start.value.microseconds(), 20'000'000);
    EXPECT_EQ(line(0).end.value.microseconds(), 2'000'000); // End was not chosen
    EXPECT_EQ(line(0).layer.value, 0);
    EXPECT_EQ(line(0).style, u8"Default");
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(session.history().back().name, "Pasting columns");
    // The selection stays; its first Line becomes active.
    EXPECT_EQ(session.selection().selected, (std::set<core::LineId>{a, c}));
    EXPECT_EQ(session.selection().active, a);
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(texts(), (std::vector<std::u8string>{u8"a", u8"b", u8"c"}));
}

TEST_F(GridClipboardTest, CommandsAreRefusedOnAReadOnlyTarget)
{
    select({b}, b);
    session.setReadOnly(true);
    EXPECT_EQ(pasteRows(session, u8"x").error(), CommandRefusal::ReadOnly);
    EXPECT_EQ(pasteColumns(session, u8"x", core::column::Text).error(), CommandRefusal::ReadOnly);
    EXPECT_FALSE(copyRows(session).empty()); // copying needs no write
}
