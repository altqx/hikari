#include "hikari/application/position_commands.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/text_projection.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <string_view>

using namespace hikari;
using namespace hikari::application;
using namespace hikari::application::position;

namespace {

core::Document load(std::string_view text)
{
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return core::loadAss(bytes).document;
}

Geometry geometry()
{
    Geometry g;
    g.width = 1920;
    g.height = 1080;
    g.style = [](const core::LineRecord &) {
        core::StyleValues s;
        s.alignment = u8"2";
        s.angle = u8"0";
        s.marginLeft = u8"10";
        s.marginRight = u8"10";
        s.marginVertical = u8"10";
        return s;
    };
    g.size = [](const core::LineRecord &) { return std::optional<Size>(Size{100, 40}); };
    return g;
}

core::LineRecord line(std::u8string text)
{
    core::LineRecord l;
    l.text = std::move(text);
    return l;
}

std::string str(std::u8string_view s) { return {s.begin(), s.end()}; }

std::string phantomOf(std::u8string_view text, Keep keep, std::optional<std::pair<std::size_t, std::size_t>> sel = {})
{
    return str(core::toUtf8(phantom(core::toUtf16(text), keep, sel)));
}

}

TEST(PositionCommands, ChangeAlignmentKeepsTheLineInPlace)
{
    const auto g = geometry();
    EXPECT_EQ(str(changeAlignment(line(u8"{\\pos(500,500)}a"), 7, g)), "{\\pos(450,460)\\an7}a");
    EXPECT_EQ(str(changeAlignment(line(u8"{\\an7\\pos(450,460)}a"), 2, g)), "{\\an2\\pos(500,500)}a");
    EXPECT_EQ(str(changeAlignment(line(u8"{\\move(100,100,200,200,0,500)}a"), 5, g)),
              "{\\move(100,80,200,180,0,500)\\an5}a");
    EXPECT_EQ(str(changeAlignment(line(u8"{\\pos(500,500)}a"), 2, g)), "{\\pos(500,500)}a");
}

TEST(PositionCommands, ChangeAlignmentPinsTheOriginOfARotatedLine)
{
    const auto g = geometry();
    EXPECT_EQ(str(changeAlignment(line(u8"{\\pos(500,500)\\frz30}a"), 8, g)),
              "{\\pos(500,460)\\frz30\\org(500,500)\\an8}a");
    EXPECT_EQ(str(changeAlignment(line(u8"{\\pos(500,500)\\frz30\\org(1,2)}a"), 8, g)),
              "{\\pos(500,460)\\frz30\\org(1,2)\\an8}a");
}

TEST(PositionCommands, ChangeAlignmentPutsInAPositionWhenThereIsNone)
{
    const auto g = geometry();
    const auto text = str(changeAlignment(line(u8"a"), 8, g));
    EXPECT_TRUE(text.starts_with("{\\pos(")) << text;
    EXPECT_TRUE(text.ends_with("\\an8}a")) << text;
}

TEST(PositionCommands, ShiftMovesPositionOriginAndClips)
{
    const auto g = geometry();
    EXPECT_EQ(str(shifted(line(u8"{\\pos(10,20)\\org(1,1)\\clip(0,0,100,100)}a"), 5, -5, g)),
              "{\\pos(15,15)\\org(6,-4)\\clip(5,-5,105,95)}a");
    EXPECT_EQ(str(shifted(line(u8"{\\pos(0,0)\\iclip(m 0 0 l 10 0 10 10)}a"), 5, 5, g)),
              "{\\pos(5,5)\\iclip(m 5 5 l 15 5 15 15)}a");
    EXPECT_EQ(str(shifted(line(u8"{\\pos(0,0)\\clip(2,m 0 0 l 4 4)}a"), 1.5, 1, g)),
              "{\\pos(1.5,1)\\clip(2,m 3 2 l 7 6)}a");
    EXPECT_EQ(str(shifted(line(u8"{\\move(0,0,10,10)}a"), 1, 2, g)), "{\\move(1,2,11,12)}a");
}

TEST(PositionCommands, PositionToMarginsMatchesPetzku)
{
    const auto g = geometry();
    auto centre = line(u8"{\\pos(960,1070)}a");
    ASSERT_TRUE(positionToMargins(centre, g));
    EXPECT_EQ(str(centre.text), "a");
    EXPECT_EQ(centre.marginLeft.value, 0);
    EXPECT_EQ(centre.marginRight.value, 0);
    EXPECT_EQ(centre.marginVertical.value, 10);

    auto left = line(u8"{\\an1\\pos(100,1000)}a");
    ASSERT_TRUE(positionToMargins(left, g));
    EXPECT_EQ(str(left.text), "{\\an1}a");
    EXPECT_EQ(left.marginLeft.value, 100);
    EXPECT_EQ(left.marginRight.value, -80);
    EXPECT_EQ(left.marginVertical.value, 80);

    auto top = line(u8"{\\an9\\pos(1900,30.4)}a");
    ASSERT_TRUE(positionToMargins(top, g));
    EXPECT_EQ(top.marginLeft.value, -1);
    EXPECT_EQ(top.marginRight.value, 19);
    EXPECT_EQ(top.marginVertical.value, 30);

    auto moving = line(u8"{\\move(1,2,3,4)}a");
    EXPECT_FALSE(positionToMargins(moving, g));
    auto none = line(u8"a");
    EXPECT_FALSE(positionToMargins(none, g));
}

TEST(PositionCommands, MarginsToPositionMatchesPetzku)
{
    const auto g = geometry();
    auto plain = line(u8"a");
    ASSERT_TRUE(marginsToPosition(plain, g));
    EXPECT_EQ(str(plain.text), "{\\pos(960,1070)}a");

    auto tagged = line(u8"{\\an7\\i1}a");
    tagged.marginLeft.value = 30;
    ASSERT_TRUE(marginsToPosition(tagged, g));
    EXPECT_EQ(str(tagged.text), "{\\pos(30,10)\\an7\\i1}a");

    auto placed = line(u8"{\\pos(1,1)}a");
    EXPECT_FALSE(marginsToPosition(placed, g));
}

TEST(PositionCommands, PhantomUsesTheMarkersOnEachRow)
{
    EXPECT_EQ(phantomOf(u8"a{}b{}c", Keep::Start), "{\\alpha&HFF&}b{\\alpha}ab{\\alpha&HFF&}c");
    EXPECT_EQ(phantomOf(u8"a{}b{}c", Keep::End), "{\\alpha&HFF&}a{\\alpha}bc{\\alpha&HFF&}b");
    EXPECT_EQ(phantomOf(u8"{\\an5}a{}b{}c\\Nplain", Keep::Start),
              "{\\an5}{\\alpha&HFF&}b{\\alpha}ab{\\alpha&HFF&}c\\Nplain");
}

TEST(PositionCommands, PhantomUsesTheSelection)
{
    EXPECT_EQ(phantomOf(u8"abc", Keep::Start, std::pair<std::size_t, std::size_t>{1, 2}),
              "{\\alpha&HFF&}b{\\alpha}ab{\\alpha&HFF&}c");
    EXPECT_EQ(phantomOf(u8"abc\\Nxyz", Keep::End, std::pair<std::size_t, std::size_t>{6, 7}),
              "abc\\N{\\alpha&HFF&}x{\\alpha}yz{\\alpha&HFF&}y");
}

TEST(PositionCommands, CycleStepsThroughTheList)
{
    const auto blur = defaultCycleValues(CycleTag::Blur);
    EXPECT_EQ(str(cycle(u8"{\\blur1}a", CycleTag::Blur, blur, false)), "{\\blur1.2}a");
    EXPECT_EQ(str(cycle(u8"{\\blur1}a", CycleTag::Blur, blur, true)), "{\\blur0.8}a");
    EXPECT_EQ(str(cycle(u8"{\\blur0.5}a", CycleTag::Blur, blur, false)), "{\\blur0.6}a");
    EXPECT_EQ(str(cycle(u8"{\\blur0.6}a", CycleTag::Blur, blur, true)), "{\\blur0.5}a");
    EXPECT_EQ(str(cycle(u8"{\\blur1.1}a", CycleTag::Blur, blur, false)), "{\\blur1.2}a");
    EXPECT_EQ(str(cycle(u8"{\\blur1.1}a", CycleTag::Blur, blur, true)), "{\\blur1}a");
    EXPECT_EQ(str(cycle(u8"a", CycleTag::Blur, blur, false)), "{\\blur0.6}a");
    EXPECT_EQ(str(cycle(u8"{\\t(\\blur5)}a", CycleTag::Blur, blur, false)), "{\\t(\\blur5)\\blur0.6}a");
    EXPECT_EQ(str(cycle(u8"{\\be1\\bord2}a", CycleTag::Bord, defaultCycleValues(CycleTag::Bord), false)),
              "{\\be1\\bord2.5}a");
}

TEST(PositionCommands, CycleWritesAlphaAsHex)
{
    const auto alpha = defaultCycleValues(CycleTag::Alpha);
    EXPECT_EQ(str(cycle(u8"{\\alpha&HFF&}a", CycleTag::Alpha, alpha, false)), "{\\alpha&H00&}a");
    EXPECT_EQ(str(cycle(u8"{\\alpha&HFF&}a", CycleTag::Alpha, alpha, true)), "{\\alpha&HE0&}a");
    EXPECT_EQ(str(cycle(u8"{\\alpha&H20&}a", CycleTag::Alpha, alpha, false)), "{\\alpha&H30&}a");
}

namespace {

constexpr std::string_view kPlaced =
    "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\pos(100,100)}a\n"
    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\pos(300,200)}b\n"
    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\pos(240,300)}c\n";

struct Session : ::testing::Test {
    EditSession session{load(kPlaced)};
    core::LineId row(std::size_t i) { return session.document().lines()[i]->id; }
    std::string text(std::size_t i) { return str(session.document().lines()[i]->text); }
    void select(std::set<std::size_t> rows, std::size_t active)
    {
        std::set<core::LineId> ids;
        for (auto r : rows)
            ids.insert(row(r));
        session.setSelection(Selection{row(active), ids, row(active), {}});
    }
};

}

TEST_F(Session, AlignToTheActiveLineIsOneUndoStep)
{
    select({0, 1, 2}, 0);
    ASSERT_TRUE(align(session, Edge::Left, RelativeTo::ActiveLine, geometry()));
    EXPECT_EQ(text(0), "{\\pos(100,100)}a");
    EXPECT_EQ(text(1), "{\\pos(100,200)}b");
    EXPECT_EQ(text(2), "{\\pos(100,300)}c");
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(text(1), "{\\pos(300,200)}b");
    EXPECT_EQ(text(2), "{\\pos(240,300)}c");
    EXPECT_FALSE(session.canUndo());
}

TEST_F(Session, AlignToTheVideoFrame)
{
    select({1}, 1);
    ASSERT_TRUE(align(session, Edge::Top, RelativeTo::VideoFrame, geometry()));
    EXPECT_EQ(text(1), "{\\pos(300,40)}b");
}

TEST_F(Session, DistributeSpacesTheMiddleLines)
{
    select({0, 1, 2}, 0);
    ASSERT_TRUE(distribute(session, Axis::Horizontal, geometry()));
    EXPECT_EQ(text(0), "{\\pos(100,100)}a");
    EXPECT_EQ(text(1), "{\\pos(300,200)}b");
    EXPECT_EQ(text(2), "{\\pos(200,300)}c");
    select({0, 1}, 0);
    EXPECT_EQ(distribute(session, Axis::Vertical, geometry()).error(), CommandRefusal::Invalid);
}

TEST_F(Session, CycleSkipsHiddenLines)
{
    select({0, 1}, 0);
    const auto hidden = row(1);
    ASSERT_TRUE(cycle(session, CycleTag::Bord, defaultCycleValues(CycleTag::Bord), false,
                      [&](core::LineId id) { return id != hidden; }));
    EXPECT_EQ(text(0), "{\\pos(100,100)\\bord0}a");
    EXPECT_EQ(text(1), "{\\pos(300,200)}b");
}
