#include "hikari/application/subtitle_boundary.h"

#include <gtest/gtest.h>

using namespace hikari::application;

TEST(SubtitleBoundary, NextGoesToTheStartThenTheEndThenLeavesTheLine)
{
    const LineFrames line{100, 150};
    EXPECT_EQ(nextBoundaryInLine(line, 40), 100);
    EXPECT_EQ(nextBoundaryInLine(line, 100), 150);
    EXPECT_EQ(nextBoundaryInLine(line, 120), 150);
    EXPECT_EQ(nextBoundaryInLine(line, 150), std::nullopt);
    EXPECT_EQ(nextBoundaryInLine(line, 400), std::nullopt);
}

TEST(SubtitleBoundary, PreviousGoesToTheEndThenTheStartThenLeavesTheLine)
{
    const LineFrames line{100, 150};
    EXPECT_EQ(previousBoundaryInLine(line, 400), 150);
    EXPECT_EQ(previousBoundaryInLine(line, 150), 100);
    EXPECT_EQ(previousBoundaryInLine(line, 120), 100);
    EXPECT_EQ(previousBoundaryInLine(line, 100), std::nullopt);
    EXPECT_EQ(previousBoundaryInLine(line, 40), std::nullopt);
}

TEST(SubtitleBoundary, AOneFrameLineStepsStraightOut)
{
    const LineFrames line{100, 100};
    EXPECT_EQ(nextBoundaryInLine(line, 100), std::nullopt);
    EXPECT_EQ(previousBoundaryInLine(line, 100), std::nullopt);
}
