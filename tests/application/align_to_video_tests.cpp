#include "hikari/application/align_to_video.h"
#include "hikari/core/ass_load.h"

#include <gtest/gtest.h>

#include <cstring>
#include <set>
#include <string_view>
#include <vector>

using namespace hikari;
using namespace hikari::application;

namespace {

constexpr KeyColour kBlack{0, 0, 0}, kWhite{255, 255, 255}, kRed{200, 30, 30};

struct Box {
    int x = 0, y = 0, width = 0, height = 0;
    KeyColour colour;
};

IndexedFrame picture(int width, int height, KeyColour background, std::vector<Box> boxes = {})
{
    IndexedFrame f;
    f.width = width;
    f.height = height;
    f.stride = width * 4 + 12;
    f.bgra.resize(static_cast<std::size_t>(f.stride) * height);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            KeyColour c = background;
            for (const auto &b : boxes)
                if (x >= b.x && x < b.x + b.width && y >= b.y && y < b.y + b.height)
                    c = b.colour;
            auto *p = f.bgra.data() + static_cast<std::size_t>(y) * f.stride + static_cast<std::size_t>(x) * 4;
            p[0] = std::byte{c.b};
            p[1] = std::byte{c.g};
            p[2] = std::byte{c.r};
            p[3] = std::byte{255};
        }
    return f;
}

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

AlignSearch searchOver(const std::set<int> &holding, int picked, int frameCount, std::vector<int> *asked = nullptr)
{
    AlignSearch search(picked, frameCount);
    while (const auto frame = search.next()) {
        if (asked)
            asked->push_back(*frame);
        search.report(holding.contains(*frame));
    }
    return search;
}

}

TEST(AlignToVideo, PixelColourReadsBgraRowsByStride)
{
    const auto f = picture(8, 4, kBlack, {{5, 2, 1, 1, kRed}});
    EXPECT_EQ(pixelColour(f, 5, 2), kRed);
    EXPECT_EQ(pixelColour(f, 4, 2), kBlack);
    EXPECT_EQ(pixelColour(f, 8, 0), std::nullopt);
    EXPECT_EQ(pixelColour(f, 0, -1), std::nullopt);
    auto planes = f;
    planes.ycbcr = YcbcrPlanes{};
    EXPECT_EQ(pixelColour(planes, 5, 2), std::nullopt);
}

TEST(AlignToVideo, ColoursMatchWithinTheirLabDistance)
{
    EXPECT_TRUE(coloursMatch(kRed, kRed, 0));
    EXPECT_FALSE(coloursMatch(kRed, KeyColour{201, 30, 30}, 0));
    EXPECT_TRUE(coloursMatch(kRed, KeyColour{205, 35, 30}, kDefaultAlignTolerance));
    EXPECT_FALSE(coloursMatch(kRed, KeyColour{100, 30, 30}, kDefaultAlignTolerance));
    EXPECT_FALSE(coloursMatch(kBlack, kWhite, 99));
    EXPECT_TRUE(coloursMatch(kBlack, kWhite, 101));
}

TEST(AlignToVideo, TheShapeEndsAtTheFirstMismatchOrTheEdge)
{
    const auto f = picture(64, 32, kBlack, {{10, 5, 20, 10, kWhite}, {50, 0, 14, 32, kRed}});
    EXPECT_EQ(keyShape(f, {15, 8, kWhite}), (KeyShape{9, 30, 4, 15}));
    EXPECT_EQ(keyShape(f, {55, 8, kRed}), (KeyShape{49, 64, -1, 32}));
    EXPECT_EQ(keyShape(f, {5, 8, kWhite}), std::nullopt);
}

TEST(AlignToVideo, TheKeyPointHoldsWhileItsShapeMovesAtMostFivePixels)
{
    const KeyPoint point{20, 10, kWhite};
    const auto reference = *keyShape(picture(64, 32, kBlack, {{10, 5, 20, 10, kWhite}}), point);
    EXPECT_TRUE(keyPointHolds(picture(64, 32, kBlack, {{15, 5, 20, 10, kWhite}}), point, reference));
    EXPECT_TRUE(keyPointHolds(picture(64, 32, kBlack, {{10, 0, 20, 15, kWhite}}), point, reference));
    EXPECT_FALSE(keyPointHolds(picture(64, 32, kBlack, {{16, 5, 20, 10, kWhite}}), point, reference));
    EXPECT_FALSE(keyPointHolds(picture(64, 32, kBlack, {{10, 5, 20, 10, kRed}}), point, reference));
    EXPECT_FALSE(keyPointHolds(picture(64, 32, kBlack), point, reference));
}

TEST(AlignToVideo, TheSearchStepsOneFrameBackThenForward)
{
    std::vector<int> asked;
    const auto search = searchOver({3, 4, 5, 6, 7, 8}, 5, 12, &asked);
    EXPECT_EQ(asked, (std::vector<int>{4, 3, 2, 6, 7, 8, 9}));
    EXPECT_EQ(search.first(), 3);
    EXPECT_EQ(search.last(), 8);
    EXPECT_EQ(search.checked(), 7);
}

TEST(AlignToVideo, AOneFrameGapEndsTheRun)
{
    const auto search = searchOver({2, 3, 5, 6, 8}, 5, 12);
    EXPECT_EQ(search.first(), 5);
    EXPECT_EQ(search.last(), 6);
}

TEST(AlignToVideo, TheSearchStopsAtTheVideosEnds)
{
    std::vector<int> asked;
    const auto search = searchOver({0, 1, 2, 3}, 0, 4, &asked);
    EXPECT_EQ(asked, (std::vector<int>{1, 2, 3}));
    EXPECT_EQ(search.first(), 0);
    EXPECT_EQ(search.last(), 3);
    EXPECT_EQ(searchOver({}, 0, 1).checked(), 0);
}

TEST(AlignToVideo, TheActiveLineSpansTheFramesInOneStep)
{
    EditSession session{load("[Events]\n"
                             "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,a\n"
                             "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,b\n")};
    const core::LineId b{2};
    session.setSelection(Selection{b, {b}, b, {}});
    const auto timebase = ntsc();
    const auto steps = session.historySize();
    ASSERT_TRUE(alignActiveLine(session, timebase, 10, 21));
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(session.history().back().name, "Aligning to video");
    const auto *line = session.document().lines()[1];
    EXPECT_EQ(ms(line->start.value), timebase.startTimeFor(10));
    EXPECT_EQ(ms(line->end.value), timebase.endTimeFor(21));
    EXPECT_EQ(timebase.frameAt(ms(line->start.value)), 10);
    EXPECT_EQ(timebase.frameAt(ms(line->end.value)), 22);
    EXPECT_EQ(ms(session.document().lines()[0]->start.value), 1000);
    EXPECT_EQ(session.selection().active, b);
    const auto revision = session.revision();
    ASSERT_TRUE(alignActiveLine(session, timebase, 10, 21));
    EXPECT_EQ(session.revision(), revision);
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(ms(session.document().lines()[1]->start.value), 3000);
}

TEST(AlignToVideo, NoActiveLineNoTimebaseOrNoFramesIsRefused)
{
    EditSession session{load("[Events]\nDialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,a\n")};
    EXPECT_FALSE(alignActiveLine(session, ntsc(), 1, 2));
    session.setSelection(Selection{core::LineId{1}, {core::LineId{1}}, core::LineId{1}, {}});
    EXPECT_FALSE(alignActiveLine(session, LegacyTimebase{}, 1, 2));
    EXPECT_FALSE(alignActiveLine(session, ntsc(), 3, 2));
    EXPECT_TRUE(alignActiveLine(session, ntsc(), 2, 2));
}
