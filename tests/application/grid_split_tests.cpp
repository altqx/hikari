// G4: the legacy Timebase helpers and the time-based splits (legacy
// SubsGrid::Split, Timebase and CalcMovePosition at 20d647c4).

#include "hikari/application/grid_split.h"
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

// 25 frames per second: frame n starts at 40n ms.
LegacyTimebase timebase()
{
    std::vector<int> t;
    for (int i = 0; i < 50; ++i)
        t.push_back(i * 40);
    return LegacyTimebase(std::move(t), 25.0);
}

std::int64_t ms(core::DocumentTime t)
{
    return t.microseconds() / 1000;
}

} // namespace

TEST(LegacyTimebase, FramesAndMidpointTimes)
{
    const auto tb = timebase();
    EXPECT_EQ(tb.frameAt(0), 0);
    EXPECT_EQ(tb.frameAt(50), 2);     // the frame at or after
    EXPECT_EQ(tb.frameAt(80), 2);
    EXPECT_EQ(tb.frameShownAt(79), 1);
    EXPECT_EQ(tb.startTimeFor(2), 65); // halfway to the previous frame, plus 5 ms
    EXPECT_EQ(tb.endTimeFor(2), 105);
    EXPECT_EQ(tb.startTimeFor(0), 0);
    EXPECT_EQ(tb.msAt(52), 2080);      // past the last: extrapolated at the frame rate
    EXPECT_EQ(tb.clampFrame(70), 49);
}

TEST(SplitLines, AtVideoTimeEndsAtTheFramesEndTime)
{
    EditSession session{load("[Events]\n"
                             "Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,a\n"
                             "Dialogue: 0,0:00:02.00,0:00:03.00,Default,,0,0,0,,b\n")};
    const auto a = session.document().lines()[0]->id;
    session.setSelection(Selection{a, {a}, a, {}});
    // 410 ms: the frame at or after is 11 (440 ms); its end time is 465, to centiseconds 460.
    ASSERT_TRUE(splitAtVideoTime(session, timebase(), 410));
    const auto lines = session.document().lines();
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_EQ(ms(lines[0]->end.value), 460);
    EXPECT_EQ(ms(lines[1]->start.value), 460);
    EXPECT_EQ(ms(lines[1]->end.value), 1000);
    EXPECT_EQ(lines[1]->text, u8"a");
    EXPECT_EQ(session.history().back().name, "Splitting lines");
    // Exactly one shown selected Line.
    session.setSelection(Selection{a, {a, lines[2]->id}, a, {}});
    EXPECT_FALSE(splitAtVideoTime(session, timebase(), 410));
}

TEST(SplitLines, IntoFramesWithTheMoveAsAPositionPerFrame)
{
    EditSession session{load("[Events]\n"
                             "Dialogue: 0,0:00:00.05,0:00:00.17,Default,,0,0,0,,{\\move(0,0,100,100)\\bord2}x\n")};
    const auto a = session.document().lines()[0]->id;
    session.setSelection(Selection{a, {a}, a, {}});
    ASSERT_TRUE(splitIntoFrames(session, timebase()));
    const auto lines = session.document().lines();
    // Frames 2, 3 and 4: from the frame at 50 ms up to the one before the frame at 170 ms.
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_EQ(lines[0]->id, a);
    EXPECT_EQ(ms(lines[0]->start.value), 60);
    EXPECT_EQ(ms(lines[0]->end.value), 100);
    EXPECT_EQ(ms(lines[1]->start.value), 100);
    EXPECT_EQ(ms(lines[2]->end.value), 180);
    // The move runs over the Line's own times (50-170 ms): at 80, 120 and 160 ms.
    EXPECT_EQ(lines[0]->text, u8"{\\pos(25,25)\\bord2}x");
    EXPECT_EQ(lines[1]->text, u8"{\\pos(58.333,58.333)\\bord2}x");
    EXPECT_EQ(lines[2]->text, u8"{\\pos(91.667,91.667)\\bord2}x");
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(session.document().lines().size(), 1u);
    // Without an exact (indexed) timebase there is nothing to split by.
    EXPECT_FALSE(splitIntoFrames(session, LegacyTimebase({}, 25.0)));
}

TEST(SplitLines, MoveTimesAreRelativeToTheLine)
{
    // \move with its own times (relative to the Line's start).
    EXPECT_EQ(legacy::moveToPos(u8"{\\move(10,20,110,220,100,300)}x", 1000, 2000, 1050), u8"{\\pos(10,20)}x");
    EXPECT_EQ(legacy::moveToPos(u8"{\\move(10,20,110,220,100,300)}x", 1000, 2000, 1200), u8"{\\pos(60,120)}x");
    EXPECT_EQ(legacy::moveToPos(u8"{\\move(10,20,110,220,100,300)}x", 1000, 2000, 1400), u8"{\\pos(110,220)}x");
    EXPECT_EQ(legacy::moveToPos(u8"no tags", 0, 100, 50), u8"no tags");
    EXPECT_EQ(legacy::floatText(2.5f), u8"2.5");
    EXPECT_EQ(legacy::floatText(3.0f), u8"3");
}
