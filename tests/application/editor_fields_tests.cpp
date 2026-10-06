// E4: the Line editor's time fields, Layer and \an choice against legacy
// EditBox::SetLine/OnEdit/SetAlignment, TimeCtrl::SetTime/GetTime, SubsTime
// raw/ParseMS/operator- and NumCtrl at 20d647c4.

#include "hikari/application/editor_fields.h"

#include <gtest/gtest.h>

#include <climits>
#include <vector>

using namespace hikari;
using namespace hikari::application;

namespace {

core::LineRecord line(std::int64_t startMs, std::int64_t endMs)
{
    core::LineRecord l;
    l.start.value = core::DocumentTime(startMs * 1000);
    l.end.value = core::DocumentTime(endMs * 1000);
    return l;
}

// 25 fps from 0 ms, as an indexed CFR video's frame starts.
LegacyTimebase cfr()
{
    std::vector<int> t;
    for (int i = 0; i < 500; ++i)
        t.push_back(i * 40);
    return LegacyTimebase(std::move(t), 25.0);
}

// time-semantics.md's VFR example (0, 40, 81, 120 ms) and a few more frames.
LegacyTimebase vfr()
{
    return LegacyTimebase({0, 40, 81, 120, 200, 210, 300}, 23.976);
}

} // namespace

// SubsTime::raw per format; the Duration is End - Start (EditBox.cpp:421).
TEST(EditorTimes, EachFormatShowsItsOwnFieldText)
{
    const auto l = line(1234, 5678);
    auto t = editorTimeTexts(l, core::SubtitleFormat::Ass, nullptr);
    EXPECT_EQ(t.start, u8"0:00:01.23");
    EXPECT_EQ(t.end, u8"0:00:05.67");
    EXPECT_EQ(t.duration, u8"0:00:04.44");
    t = editorTimeTexts(l, core::SubtitleFormat::Srt, nullptr);
    EXPECT_EQ(t.start, u8"00:00:01,234");
    EXPECT_EQ(t.duration, u8"00:00:04,444");
    t = editorTimeTexts(l, core::SubtitleFormat::TMPlayer, nullptr);
    EXPECT_EQ(t.start, u8"00:00:01");
    EXPECT_EQ(t.end, u8"00:00:05");
    // MPL2 deciseconds rounded up (raw's ceil(ms * 10 / 1000)).
    t = editorTimeTexts(l, core::SubtitleFormat::Mpl2, nullptr);
    EXPECT_EQ(t.start, u8"13");
    EXPECT_EQ(t.end, u8"57");
    EXPECT_EQ(t.duration, u8"45");
    // MicroDVD shows the authored frames; the duration their difference.
    auto m = l;
    m.startFrame = 30;
    m.endFrame = 136;
    t = editorTimeTexts(m, core::SubtitleFormat::MicroDvd, nullptr);
    EXPECT_EQ(t.start, u8"30");
    EXPECT_EQ(t.end, u8"136");
    EXPECT_EQ(t.duration, u8"106");
    // An End before the Start: the Duration is clamped at 0 (SubsTime::operator-).
    t = editorTimeTexts(line(5000, 1000), core::SubtitleFormat::Ass, nullptr);
    EXPECT_EQ(t.duration, u8"0:00:00.00");
}

// TimeCtrl::SetTime with frames shown on an exact timebase (TimeCtrl.cpp:247-266),
// SetLine's Duration as the frame count both ends included (EditBox.cpp:416-419).
TEST(EditorTimes, FramesOnACfrTimebase)
{
    const auto tb = cfr();
    const auto t = editorTimeTexts(line(1000, 2000), core::SubtitleFormat::Ass, &tb);
    EXPECT_EQ(t.start, u8"25"); // the frame at or after 1000 ms
    EXPECT_EQ(t.end, u8"49");   // the frame at or after 2000 ms, less one
    EXPECT_EQ(t.duration, u8"25");
    // Start inside a frame: the next one is the first shown.
    EXPECT_EQ(editorTimeTexts(line(1001, 2000), core::SubtitleFormat::Ass, &tb).start, u8"26");
    // An End before the Start: the frame difference clamps, plus one.
    EXPECT_EQ(editorTimeTexts(line(2000, 1000), core::SubtitleFormat::Ass, &tb).duration, u8"1");
    // After a Start or End edit OnEdit shows SetTime(End - Start, false, 1): the
    // frame at that many milliseconds (EditBox.cpp:1541-1543).
    EXPECT_EQ(editedDurationText(line(1000, 2000), core::SubtitleFormat::Ass, &tb), u8"25");
    EXPECT_EQ(editedDurationText(line(1000, 1050), core::SubtitleFormat::Ass, &tb), u8"2");
    EXPECT_EQ(editedDurationText(line(1000, 2000), core::SubtitleFormat::Ass, nullptr), u8"0:00:01.00");
}

TEST(EditorTimes, FramesOnAVfrTimebase)
{
    const auto tb = vfr();
    const auto t = editorTimeTexts(line(41, 200), core::SubtitleFormat::Ass, &tb);
    EXPECT_EQ(t.start, u8"2"); // frameAtOrAfter(41) = 2
    EXPECT_EQ(t.end, u8"3");   // the frame at 200 ms is 4: the last shown is 3
    EXPECT_EQ(t.duration, u8"2");
    // Typed frames: Timebase::StartTimeFor/EndTimeFor (midpoint + 5 ms), ZEROIT.
    auto start = typedTime(u8"2", TimeFieldRole::Start, core::SubtitleFormat::Ass, std::nullopt, &tb);
    ASSERT_TRUE(start);
    EXPECT_EQ(start->ms, 60); // min(40 + 41 / 2 + 5, 81) = 65 -> 60
    auto end = typedTime(u8"3", TimeFieldRole::End, core::SubtitleFormat::Ass, std::nullopt, &tb);
    ASSERT_TRUE(end);
    EXPECT_EQ(end->ms, 160); // min(120 + 80 / 2 + 5, 200) = 165 -> 160
    // A typed Duration is the time of that frame number (TimeCtrl::GetTime opt 0: MsAt).
    auto duration = typedTime(u8"3", TimeFieldRole::Duration, core::SubtitleFormat::Ass, std::nullopt, &tb);
    ASSERT_TRUE(duration);
    EXPECT_EQ(duration->ms, 120);
}

// Without an exact timebase (no indexed video) the switch shows times.
TEST(EditorTimes, FramesNeedAnExactTimebase)
{
    const LegacyTimebase estimated({}, 25.0);
    EXPECT_EQ(editorTimeTexts(line(1000, 2000), core::SubtitleFormat::Ass, &estimated).start, u8"0:00:01.00");
    EXPECT_EQ(typedTime(u8"0:00:03.00", TimeFieldRole::Start, core::SubtitleFormat::Ass, std::nullopt, &estimated)->ms,
              3000);
}

TEST(EditorTimes, TypedTimesInEachFormat)
{
    const auto at = [](const char8_t *text, core::SubtitleFormat format) {
        return typedTime(text, TimeFieldRole::Start, format, std::nullopt, nullptr);
    };
    EXPECT_EQ(at(u8"0:01:02.34", core::SubtitleFormat::Ass)->ms, 62340);
    EXPECT_EQ(at(u8"01:01:02,345", core::SubtitleFormat::Srt)->ms, 3662345);
    EXPECT_EQ(at(u8"00:01:02", core::SubtitleFormat::TMPlayer)->ms, 62000);
    EXPECT_EQ(at(u8"15", core::SubtitleFormat::Mpl2)->ms, 1500); // ParseMS: result * 100
    // Not the field's form: refused (the legacy control cannot hold it).
    EXPECT_FALSE(at(u8"0:01:02,34", core::SubtitleFormat::Ass));
    EXPECT_FALSE(at(u8"0:01:02.34", core::SubtitleFormat::Srt));
    EXPECT_FALSE(at(u8"00:61:02", core::SubtitleFormat::TMPlayer));
    EXPECT_FALSE(at(u8"1.5", core::SubtitleFormat::Mpl2));
    EXPECT_FALSE(at(u8"-3", core::SubtitleFormat::MicroDvd));
}

// MicroDVD: the frame is kept; its time comes from the Document's own rate
// (C01-fps-isolation in place of legacy's frame / 23.976), else unresolved.
TEST(EditorTimes, MicroDvdFramesUseTheDocumentRate)
{
    auto t = typedTime(u8"50", TimeFieldRole::Start, core::SubtitleFormat::MicroDvd, std::nullopt, nullptr);
    ASSERT_TRUE(t);
    EXPECT_EQ(t->frame, 50);
    EXPECT_FALSE(t->timeResolved);
    const auto rate = core::FrameRate::make(25, 1);
    ASSERT_TRUE(rate);
    t = typedTime(u8"50", TimeFieldRole::Start, core::SubtitleFormat::MicroDvd, *rate, nullptr);
    ASSERT_TRUE(t);
    EXPECT_TRUE(t->timeResolved);
    EXPECT_EQ(t->ms, 2000);
    // A time put into the field: the frame rounded up at the rate (SubsTime::raw).
    const auto f = microDvdFieldTime(2001, *rate);
    ASSERT_TRUE(f);
    EXPECT_EQ(f->frame, 51);
    EXPECT_EQ(f->ms, 2040);
    EXPECT_FALSE(microDvdFieldTime(2001, std::nullopt));
}

// A3's commit precision through the fields (SubsTime::NewTime, raw, SetRaw).
TEST(EditorTimes, FieldPrecision)
{
    EXPECT_EQ(fieldPrecisionTime(core::SubtitleFormat::Ass, 1239), 1230);
    EXPECT_EQ(fieldPrecisionTime(core::SubtitleFormat::Srt, 1239), 1239);
    EXPECT_EQ(fieldPrecisionTime(core::SubtitleFormat::TMPlayer, 1999), 1000);
    EXPECT_EQ(fieldPrecisionTime(core::SubtitleFormat::Mpl2, 1201), 1300);
    EXPECT_EQ(fieldPrecisionTime(core::SubtitleFormat::Mpl2, 1200), 1200);
    EXPECT_EQ(fieldPrecisionTime(core::SubtitleFormat::Mpl2, -5), 0);
}

// NumCtrl for the Layer: -10000000..10000000, clamped; text it cannot read
// keeps the previous value (nullopt).
TEST(EditorFields, LayerFollowsNumCtrl)
{
    EXPECT_EQ(layerValue(u8"12"), 12);
    EXPECT_EQ(layerValue(u8"-3"), -3);
    EXPECT_EQ(layerValue(u8"99999999"), 10000000);
    EXPECT_EQ(layerValue(u8"-99999999"), -10000000);
    EXPECT_EQ(layerValue(u8"999999999999999999999999"), 10000000);
    EXPECT_FALSE(layerValue(u8"-"));
    EXPECT_FALSE(layerValue(u8""));
    EXPECT_FALSE(layerValue(u8"1-2"));
    EXPECT_FALSE(layerValue(u8"abc"));
}

// EditBox::SetAlignment: the first \an of the translation (or the text), else the Style's.
TEST(EditorFields, AlignmentFromTheTextOrTheStyle)
{
    core::LineRecord l;
    l.text = u8"{\\an8}top";
    EXPECT_EQ(legacyAlignment(l, 2), 8);
    l.text = u8"plain";
    EXPECT_EQ(legacyAlignment(l, 2), 2);
    l.text = u8"{\\an7}original";
    l.translation = u8"translated";
    EXPECT_EQ(legacyAlignment(l, 3), 3); // the translation has none: the Style's
    l.translation = u8"{\\pos(1,2)\\an9}x{\\an1}";
    EXPECT_EQ(legacyAlignment(l, 3), 9);
}
