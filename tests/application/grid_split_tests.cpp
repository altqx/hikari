// G4: the legacy Timebase helpers and the time-based splits (legacy
// SubsGrid::Split, Timebase and CalcMovePosition at 20d647c4).

#include "hikari/application/grid_split.h"
#include "hikari/core/ass_load.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <optional>
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

namespace {

// 640x480 script; Default is bottom-centre with margins of 10.
constexpr std::string_view kTextScript =
    "[Script Info]\n"
    "PlayResX: 640\n"
    "PlayResY: 480\n"
    "\n"
    "[V4+ Styles]\n"
    "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
    "Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, "
    "MarginR, MarginV, Encoding\n"
    "Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1\n"
    "\n"
    "[Events]\n"
    "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";

// Ten pixels a character, as tall as the font size.
struct FakeMeasure : TextMeasurePort {
    std::vector<std::string> measured;
    std::optional<TextExtents> measure(const std::vector<std::string> &style, const std::string &text) override
    {
        measured.push_back(text);
        return TextExtents{10.0 * static_cast<double>(text.size()), std::atof(style[2].c_str()), 0, 0};
    }
};

// Letter and digit runs are words; any other character is a segment of its own.
std::vector<std::pair<std::size_t, bool>> fakeWords(std::u16string_view text)
{
    std::vector<std::pair<std::size_t, bool>> out;
    auto alnum = [](char16_t c) { return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || (c >= u'0' && c <= u'9'); };
    std::size_t i = 0;
    while (i < text.size()) {
        if (alnum(text[i])) {
            while (i < text.size() && alnum(text[i]))
                ++i;
            out.emplace_back(i, true);
        } else {
            out.emplace_back(++i, false);
        }
    }
    return out;
}

std::vector<std::u8string> splitTexts(std::string_view dialogue, SplitText kind)
{
    EditSession session{load(std::string(kTextScript) + std::string(dialogue))};
    const auto a = session.document().lines()[0]->id;
    session.setSelection(Selection{a, {a}, a, {}});
    FakeMeasure measure;
    const auto steps = session.historySize();
    EXPECT_TRUE(splitByText(session, kind, measure, fakeWords));
    std::vector<std::u8string> out;
    for (const auto *l : session.document().lines())
        out.push_back(l->text);
    EXPECT_EQ(session.historySize(), steps + (out.size() > 1 ? 1 : 0));
    return out;
}

} // namespace

TEST(SplitLines, CharactersArePlacedAcrossTheLinesWidth)
{
    // Centred at 320 on the bottom margin: "ab" is 20 wide, so the characters
    // are centred at 315 and 325. \pos goes before the first tag.
    EXPECT_EQ(splitTexts("Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,ab\n", SplitText::Characters),
              (std::vector<std::u8string>{u8"{\\pos(315,470)\\an2}a", u8"{\\pos(325,470)\\an2}b"}));
}

TEST(SplitLines, WordsKeepTheirTagsAndSkipSpaces)
{
    // Left aligned at the Line's \pos: each word starts where the last ended.
    EXPECT_EQ(splitTexts("Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,{\\an7\\pos(100,50)}ab cd\n",
                         SplitText::Words),
              (std::vector<std::u8string>{u8"{\\an7\\pos(100,50)}ab", u8"{\\an7\\pos(130,50)}cd"}));
}

TEST(SplitLines, WrapsStackFromTheBottom)
{
    EXPECT_EQ(splitTexts("Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,ab\\Ncdef\n", SplitText::Wraps),
              (std::vector<std::u8string>{u8"{\\pos(320,450)\\an2}ab", u8"{\\pos(320,470)\\an2}cdef"}));
    // A Line without wraps stays as it is.
    EXPECT_EQ(splitTexts("Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,ab\n", SplitText::Wraps),
              (std::vector<std::u8string>{u8"ab"}));
}

TEST(SplitLines, LaterWrapsMergeTheirTagsIntoTheFirstBlock)
{
    EXPECT_EQ(splitTexts("Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,{\\b1}ab\\N{\\i1}cd\n", SplitText::Wraps),
              (std::vector<std::u8string>{u8"{\\pos(320,450)\\an2\\b1}ab", u8"{\\pos(320,470)\\an2\\b1\\i1}cd"}));
    // Legacy finds "\b" in "\bord2" and then drops the \b1 (Dialogue::MergeTagBlocks).
    EXPECT_EQ(splitTexts("Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,{\\bord2}ab\\N{\\b1}cd\n",
                         SplitText::Wraps),
              (std::vector<std::u8string>{u8"{\\pos(320,450)\\an2\\bord2}ab", u8"{\\pos(320,470)\\an2\\bord2}cd"}));
}

TEST(SplitLines, MoveKeepsItsDistanceAndEndsWithAComma)
{
    // Without move times legacy still writes the comma before them.
    EXPECT_EQ(splitTexts("Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,{\\move(10,20,110,20)}ab\n",
                         SplitText::Characters),
              (std::vector<std::u8string>{u8"{\\an2\\move(5,20,105,20,)}a", u8"{\\an2\\move(15,20,115,20,)}b"}));
    EXPECT_EQ(splitTexts("Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,{\\move(10,20,110,20,0,500)}ab\n",
                         SplitText::Characters)[0],
              u8"{\\an2\\move(5,20,105,20,0,500)}a");
}

TEST(SplitLines, TextSplitsAreOneUndoStepAndSkipTagOnlyLines)
{
    EditSession session{load(std::string(kTextScript) +
                             "Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,ab\n"
                             "Dialogue: 0,0:00:02.00,0:00:03.00,Default,,0,0,0,,{\\b1}\n"
                             "Dialogue: 0,0:00:04.00,0:00:05.00,Default,,0,0,0,,cd\n")};
    const auto lines = session.document().lines();
    const auto a = lines[0]->id, b = lines[1]->id, c = lines[2]->id;
    session.setSelection(Selection{a, {a, b, c}, a, {}});
    FakeMeasure measure;
    const auto steps = session.historySize();
    ASSERT_TRUE(splitByText(session, SplitText::Characters, measure, fakeWords));
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(session.history().back().name, "Splitting lines");
    const auto after = session.document().lines();
    ASSERT_EQ(after.size(), 5u);
    EXPECT_EQ(after[0]->id, a);
    EXPECT_EQ(after[2]->text, u8"{\\b1}");
    EXPECT_EQ(after[3]->id, c);
    EXPECT_EQ(after[4]->start.value.microseconds(), 4'000'000); // copies keep the Line's fields
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(session.document().lines().size(), 3u);
}

TEST(SplitLines, LegacyPieces)
{
    EXPECT_EQ(legacy::splitByChar(u"a{\\b1}b\\Nc"), (std::vector<std::u16string>{u"a", u"{\\b1}b", u"\\N", u"c"}));
    EXPECT_EQ(legacy::splitByWrap(u"a\\Nb{\\N}c"), (std::vector<std::u16string>{u"a", u"b{\\N}c"}));
    EXPECT_EQ(legacy::splitByWord(u"{\\i1}ab, cd", fakeWords),
              (std::vector<std::u16string>{u"{\\i1}ab,", u" ", u"cd"}));
}
