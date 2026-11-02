#include "hikari/application/center_times.h"
#include "hikari/core/ass_load.h"

#include <gtest/gtest.h>

#include <cstring>
#include <set>
#include <string_view>
#include <vector>

using namespace hikari;
using namespace hikari::application;

namespace {

core::Document load(std::string_view text)
{
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return core::loadAss(bytes).document;
}

int ms(core::DocumentTime t)
{
    return static_cast<int>(t.microseconds() / 1000);
}

LegacyTimebase ntsc()
{
    std::vector<int> timecodes;
    for (int f = 0; f < 2000; ++f)
        timecodes.push_back(static_cast<int>(static_cast<long long>(f) * 1001 / 24));
    return LegacyTimebase(timecodes, 24000.0 / 1001.0);
}

LegacyTimebase variable()
{
    std::vector<int> timecodes;
    for (int f = 0, t = 0; f < 2000; ++f, t += f % 3 == 0 ? 50 : 33)
        timecodes.push_back(t);
    return LegacyTimebase(timecodes, 0.0);
}

void expectEveryCentisecondKeepsItsFrame(const LegacyTimebase &timebase)
{
    for (int t = 0; t < 30000; t += 10) {
        const int centered = centeredTime(timebase, t);
        ASSERT_EQ(timebase.frameAt(centered), timebase.frameAt(t)) << t;
        ASSERT_EQ(centered % 10, 0) << t;
        ASSERT_EQ(centeredTime(timebase, centered), centered) << t;
    }
}

}

TEST(CenterTimes, EveryTimeStaysOnItsFrameWithConstantTimecodes)
{
    expectEveryCentisecondKeepsItsFrame(ntsc());
}

TEST(CenterTimes, EveryTimeStaysOnItsFrameWithVariableTimecodes)
{
    expectEveryCentisecondKeepsItsFrame(variable());
}

TEST(CenterTimes, ATimeMovesToTheMiddleOfItsFrameGap)
{
    const auto timebase = ntsc();
    EXPECT_EQ(timebase.msAt(23), 959);
    EXPECT_EQ(timebase.msAt(24), 1001);
    EXPECT_EQ(centeredTime(timebase, 960), 980);
    EXPECT_EQ(centeredTime(timebase, 1000), 980);
    EXPECT_EQ(centeredTime(timebase, 0), 0);
    EXPECT_EQ(centeredTime(timebase, 30), 20);
}

TEST(CenterTimes, AShiftByTimeMovesCenteredLinesByWholeFrames)
{
    const auto timebase = ntsc();
    const int shift = 4171;
    std::set<int> moved;
    for (int t = 1000; t < 20000; t += 10) {
        const int centered = centeredTime(timebase, t);
        moved.insert(timebase.frameAt(centered + shift) - timebase.frameAt(centered));
    }
    EXPECT_EQ(moved, (std::set<int>{100}));
}

TEST(CenterTimes, TheSelectedLinesAreCenteredInOneStep)
{
    EditSession session{load("[Events]\n"
                             "Dialogue: 0,0:00:00.96,0:00:02.00,Default,,0,0,0,,a\n"
                             "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,b\n"
                             "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,c\n")};
    const core::LineId a{1}, c{3};
    session.setSelection(Selection{a, {a, c}, a, {}});
    const auto timebase = ntsc();
    const auto steps = session.historySize();
    ASSERT_TRUE(centerTimesInFrames(session, timebase));
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(session.history().back().name, "Centering times");
    const auto line = [&](int i) { return session.document().lines()[static_cast<std::size_t>(i)]; };
    EXPECT_EQ(ms(line(0)->start.value), 980);
    EXPECT_EQ(ms(line(0)->end.value), centeredTime(timebase, 2000));
    EXPECT_EQ(ms(line(1)->start.value), 3000);
    EXPECT_EQ(ms(line(2)->start.value), centeredTime(timebase, 5000));
    EXPECT_EQ(session.selection().selected, (std::set<core::LineId>{a, c}));
    const auto revision = session.revision();
    ASSERT_TRUE(centerTimesInFrames(session, timebase));
    EXPECT_EQ(session.revision(), revision);
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(ms(line(0)->start.value), 960);
}

TEST(CenterTimes, NothingSelectedOrNoTimebaseIsRefused)
{
    EditSession session{load("[Events]\nDialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,a\n")};
    EXPECT_FALSE(centerTimesInFrames(session, ntsc()));
    session.setSelection(Selection{core::LineId{1}, {core::LineId{1}}, core::LineId{1}, {}});
    EXPECT_FALSE(centerTimesInFrames(session, LegacyTimebase{}));
}
