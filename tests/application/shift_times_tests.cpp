#include "hikari/application/shift_times.h"
#include "hikari/core/ass_load.h"

#include <gtest/gtest.h>

#include <cstring>
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

constexpr std::string_view kLines =
    "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,a\n"
    "Dialogue: 0,0:00:03.00,0:00:04.00,Sign,,0,0,0,,b\n"
    "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,c\n";

std::vector<std::pair<int, int>> times(const EditSession &s)
{
    std::vector<std::pair<int, int>> out;
    for (const auto *l : s.document().lines())
        out.emplace_back(static_cast<int>(l->start.value.microseconds() / 1000), static_cast<int>(l->end.value.microseconds() / 1000));
    return out;
}

LegacyTimebase timebase()
{
    std::vector<int> t;
    for (int i = 0; i < 400; ++i)
        t.push_back(i * 40);
    return LegacyTimebase(std::move(t), 25.0);
}

struct Shift : ::testing::Test {
    EditSession session{load(kLines)};
    core::LineId row(std::size_t i) { return session.document().lines()[i]->id; }
    void select(std::set<std::size_t> rows, std::size_t active)
    {
        std::set<core::LineId> ids;
        for (auto r : rows)
            ids.insert(row(r));
        session.setSelection(Selection{row(active), ids, row(active), {}});
    }
};

}

TEST_F(Shift, AllLinesForwardAndBackwardAsOneStep)
{
    ShiftTimesSettings s;
    s.timeMs = 1500;
    const auto steps = session.historySize();
    ASSERT_TRUE(shiftTimes(session, s, {}));
    EXPECT_EQ(times(session), (std::vector<std::pair<int, int>>{{2500, 3500}, {4500, 5500}, {6500, 7500}}));
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(session.history().back().name, "Shifting times");
    s.forward = false;
    s.timeMs = 4000;
    ASSERT_TRUE(shiftTimes(session, s, {}));
    EXPECT_EQ(times(session)[0], (std::pair{0, 0}));
    ASSERT_TRUE(session.undo());
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(times(session)[0], (std::pair{1000, 2000}));
}

TEST_F(Shift, WhichLinesAndWhichTimes)
{
    select({1}, 1);
    ShiftTimesSettings s;
    s.timeMs = 100;
    s.whichLines = 1;
    ASSERT_TRUE(shiftTimes(session, s, {}));
    EXPECT_EQ(times(session), (std::vector<std::pair<int, int>>{{1000, 2000}, {3100, 4100}, {5000, 6000}}));
    s.whichLines = 2;
    s.whichTimes = 1;
    ASSERT_TRUE(shiftTimes(session, s, {}));
    EXPECT_EQ(times(session), (std::vector<std::pair<int, int>>{{1000, 2000}, {3200, 4100}, {5100, 6000}}));
    s.whichLines = 4;
    s.whichTimes = 2;
    ASSERT_TRUE(shiftTimes(session, s, {}));
    EXPECT_EQ(times(session), (std::vector<std::pair<int, int>>{{1000, 2100}, {3200, 4200}, {5100, 6000}}));
    s.whichLines = 5;
    s.whichTimes = 0;
    s.styles = u8"Default";
    ASSERT_TRUE(shiftTimes(session, s, {}));
    EXPECT_EQ(times(session), (std::vector<std::pair<int, int>>{{1100, 2200}, {3200, 4200}, {5200, 6100}}));
    s.styles.clear();
    EXPECT_EQ(std::get<ShiftProblem>(shiftTimes(session, s, {}).error()), ShiftProblem::NoStylesChosen);
    session.setSelection({});
    s.whichLines = 1;
    EXPECT_EQ(std::get<ShiftProblem>(shiftTimes(session, s, {}).error()), ShiftProblem::NoLinesSelected);
}

TEST_F(Shift, ByFramesOnTheTimebase)
{
    const auto tb = timebase();
    ShiftTimesSettings s;
    s.byFrames = true;
    s.frames = 2;
    EXPECT_EQ(std::get<ShiftProblem>(shiftTimes(session, s, {}).error()), ShiftProblem::NoExactTimebase);
    ShiftContext c;
    c.timebase = &tb;
    ASSERT_TRUE(shiftTimes(session, s, c));
    EXPECT_EQ(times(session)[0], (std::pair{1060, 2060}));
}

TEST_F(Shift, MoveToVideoTime)
{
    select({0}, 0);
    const auto tb = timebase();
    ShiftTimesSettings s;
    s.moveToVideoTime = true;
    s.fromStartTime = true;
    ShiftContext c;
    c.timebase = &tb;
    c.videoFrame = 75;
    c.videoFrameStartMs = 2985;
    c.videoFrameEndMs = 3025;
    ASSERT_TRUE(shiftTimes(session, s, c));
    EXPECT_EQ(times(session)[0], (std::pair{2980, 3980}));
}

TEST(ShiftTagTimes, LegacyChangeTimes)
{
    EXPECT_EQ(legacy::changeTagTimes(u8"{\\move(1,2,3,4,100,200)\\t(0,500,\\fs20)\\fad(100,200)}x", 10, 20),
              u8"{\\move(1,2,3,4,110,220)\\t(10,520,\\fs20)\\fad(110,220)}x");
    EXPECT_EQ(legacy::changeTagTimes(u8"{\\t(5,50,\\b1)\\move(1,2,3,4)\\fade(1,2,3,4,5,6,7)}x", -10, -100),
              u8"{\\t(0,0,\\b1)\\move(1,2,3,4)\\fade(1,2,3,4,5,6,7)}x");
}

TEST_F(Shift, TagTimesFollowTheFrames)
{
    EditSession tagged{load("[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                            "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\move(1,2,3,4,100,900)}x\n")};
    const auto tb = timebase();
    ShiftTimesSettings s;
    s.timeMs = 15;
    s.tagTimes = true;
    ShiftContext c;
    c.timebase = &tb;
    ASSERT_TRUE(shiftTimes(tagged, s, c));
    EXPECT_EQ(tagged.document().lines()[0]->text, u8"{\\move(1,2,3,4,125,925)}x");
}

TEST(ShiftEndCorrection, OverlapsAndTextLength)
{
    const auto tb = timebase();
    ShiftContext c;
    c.timebase = &tb;
    EditSession a{load("[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                       "Dialogue: 0,0:00:01.00,0:00:03.50,Default,,0,0,0,,a\n"
                       "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,b\n")};
    ShiftTimesSettings s;
    s.timeMs = 10;
    s.correctEndTimes = 1;
    ASSERT_TRUE(shiftTimes(a, s, c));
    EXPECT_EQ(times(a), (std::vector<std::pair<int, int>>{{1010, 3010}, {3010, 4010}}));
    EditSession b{load("[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                       "Dialogue: 0,0:00:01.00,0:00:01.50,Default,,0,0,0,,a\n"
                       "Dialogue: 0,0:00:03.00,0:00:03.20,Default,,0,0,0,,abc\n"
                       "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,c\n")};
    s.timeMs = 0;
    s.correctEndTimes = 2;
    s.timePerCharacter = 100;
    ASSERT_TRUE(shiftTimes(b, s, c));
    EXPECT_EQ(times(b), (std::vector<std::pair<int, int>>{{1000, 1500}, {3000, 4000}, {5000, 6000}}));
    EditSession d{load("[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                       "Dialogue: 0,0:00:01.00,0:00:03.50,Default,,0,0,0,,a\n"
                       "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,b\n")};
    s.timeMs = 10;
    s.correctEndTimes = 1;
    const auto r = shiftTimes(d, s, {});
    ASSERT_TRUE(r);
    EXPECT_TRUE(r->endCorrectionSkipped);
    EXPECT_EQ(times(d), (std::vector<std::pair<int, int>>{{1010, 3510}, {3010, 4010}}));
}

TEST(ShiftProfiles, LegacyProfileText)
{
    ShiftTimesSettings s;
    s.timeMs = 1500;
    s.forward = false;
    s.tagTimes = true;
    s.whichLines = 5;
    s.styles = u8"Default,Sign";
    s.correctEndTimes = 1;
    const std::string text = shiftProfileText("Fix", s);
    EXPECT_EQ(text, "Fix: Time: 1500 Forward: 0 Frames: 0 MoveTagTimes: 1 MoveToStartTimes: 0 MoveToVideoTime: 0 "
                    "MoveToVideoTime: 0 WhichLines: 5 StylesText: Default,Sign WhichTimes: 0 EndTimeCorrection: 1");
    EXPECT_EQ(shiftProfileName(text), "Fix");
    const auto back = applyShiftProfile(text, {});
    EXPECT_EQ(back.timeMs, 1500);
    EXPECT_FALSE(back.forward);
    EXPECT_TRUE(back.tagTimes);
    EXPECT_EQ(back.whichLines, 5);
    EXPECT_EQ(back.styles, u8"Default,Sign");
    EXPECT_EQ(back.correctEndTimes, 1);
    s.styles = u8"Main Dialogue";
    const auto shifted = applyShiftProfile(shiftProfileText("Odd", s), {});
    EXPECT_EQ(shifted.styles, u8"Main");
    EXPECT_EQ(shifted.whichTimes, 0);
}

namespace {

EditSession twoLines(std::string_view a, std::string_view b)
{
    const std::string text = std::string("[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n") +
                             "Dialogue: 0," + std::string(a) + ",Default,,0,0,0,,a\n" + "Dialogue: 0," + std::string(b) +
                             ",Default,,0,0,0,,b\n";
    return EditSession{load(text)};
}

}

TEST(Postprocessor, LeadInAndOutReplaceTheShift)
{
    auto session = twoLines("0:00:01.00,0:00:02.00", "0:00:03.00,0:00:04.00");
    const auto tb = timebase();
    ShiftContext c;
    c.timebase = &tb;
    ShiftTimesSettings s;
    s.timeMs = 5000;
    s.postprocessor = 16 | 1 | 2;
    s.leadIn = 200;
    s.leadOut = 300;
    ASSERT_TRUE(shiftTimes(session, s, c));
    EXPECT_EQ(times(session), (std::vector<std::pair<int, int>>{{800, 2300}, {2800, 4300}}));
    EXPECT_EQ(session.history().back().name, "Shifting times");
    auto plain = twoLines("0:00:01.00,0:00:02.00", "0:00:03.00,0:00:04.00");
    s.postprocessor = 1 | 2;
    s.timeMs = 100;
    ASSERT_TRUE(shiftTimes(plain, s, c));
    EXPECT_EQ(times(plain)[0], (std::pair{1100, 2100}));
    s.postprocessor = 16 | 1;
    EXPECT_EQ(std::get<ShiftProblem>(shiftTimes(plain, s, {}).error()), ShiftProblem::NoExactTimebase);
}

TEST(Postprocessor, ContinuousTimesCloseSmallGaps)
{
    auto session = twoLines("0:00:01.00,0:00:02.00", "0:00:02.30,0:00:03.00");
    const auto tb = timebase();
    ShiftContext c;
    c.timebase = &tb;
    ShiftTimesSettings s;
    s.postprocessor = 16 | 4;
    s.thresholdStart = 200;
    s.thresholdEnd = 300;
    ASSERT_TRUE(shiftTimes(session, s, c));
    EXPECT_EQ(times(session), (std::vector<std::pair<int, int>>{{1000, 2180}, {2180, 3000}}));
}

TEST(Postprocessor, SnapToKeyframes)
{
    EditSession session{load("[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                             "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,a\n")};
    const auto tb = timebase();
    ShiftContext c;
    c.timebase = &tb;
    c.keyframes = {25, 50};
    ShiftTimesSettings s;
    s.postprocessor = 16 | 8;
    s.keyframeBeforeStart = s.keyframeAfterStart = s.keyframeBeforeEnd = s.keyframeAfterEnd = 100;
    ASSERT_TRUE(shiftTimes(session, s, c));
    EXPECT_EQ(times(session)[0], (std::pair{980, 1980}));
}

#include "hikari/application/keyframe_files.h"

TEST(KeyframeFiles, LegacyFormats)
{
    EXPECT_EQ(parseKeyframes("# keyframe format v1\nfps 23.976\n0\n120\r\n240\n"), (std::vector<int>{0, 0, 120, 240}));
    EXPECT_EQ(parseKeyframes("# XviD 2pass stat file\ni 1 2\np 3\nb 4\ni 5\n#comment\np\ni\n"), (std::vector<int>{0, 3, 5}));
    EXPECT_EQ(parseKeyframes("#options: preset=x\nin:0 out:0 type:I\nin:1 out:1 type:P\nin:2 out:2 type:b\nin:3 out:3 type:i\n"),
              (std::vector<int>{0, 3}));
    EXPECT_TRUE(parseKeyframes("##map version 1\nI frame\nP frame\n").empty());
    EXPECT_TRUE(parseKeyframes("not a keyframes file\n1\n").empty());
}
