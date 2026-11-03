#include "hikari/application/aegisub_frame_times.h"

#include <gtest/gtest.h>

#include <cstdint>

namespace hikari::application {
namespace {

std::vector<std::int64_t> ntscStarts(std::int64_t offset = 0)
{
    std::vector<std::int64_t> starts;
    for (std::int64_t i = 0; i < 2398; ++i)
        starts.push_back(offset + i * 1001 / 24);
    return starts;
}

TEST(AegisubFrameTimes, FrameFromMsIsTheFirstFrameStartingAtOrAfterTheTime)
{
    const auto times = AegisubFrameTimes::fromFrameStarts(ntscStarts());
    ASSERT_TRUE(times);
    EXPECT_EQ(times->frameFromMs(0), 0);
    EXPECT_EQ(times->frameFromMs(1), 1);
    EXPECT_EQ(times->frameFromMs(41), 1);
    EXPECT_EQ(times->frameFromMs(42), 2);
    EXPECT_EQ(times->frameFromMs(-1), 0);
    EXPECT_EQ(times->frameFromMs(-50), -1);
    EXPECT_EQ(times->frameFromMs(14000), 336);
}

TEST(AegisubFrameTimes, MsFromFrameIsHalfwayFromThePreviousFrame)
{
    const auto times = AegisubFrameTimes::fromFrameStarts(ntscStarts());
    ASSERT_TRUE(times);
    EXPECT_EQ(times->msFromFrame(0), -20);
    EXPECT_EQ(times->msFromFrame(1), 21);
    EXPECT_EQ(times->msFromFrame(96), 3983);
    EXPECT_EQ(times->msFromFrame(-1), -62);
}

TEST(AegisubFrameTimes, TimesPastTheLastFrameFollowTheAverageRate)
{
    const auto times = AegisubFrameTimes::fromFrameStarts(ntscStarts());
    ASSERT_TRUE(times);
    EXPECT_EQ(times->msFromFrame(2397), 99954);
    EXPECT_EQ(times->msFromFrame(2398), 99995);
    EXPECT_EQ(times->msFromFrame(2500), 104249);
    EXPECT_EQ(times->frameFromMs(100'000'000), 2397624);
}

TEST(AegisubFrameTimes, EveryFrameTimeMapsBackToItsFrame)
{
    const auto times = AegisubFrameTimes::fromFrameStarts(ntscStarts());
    ASSERT_TRUE(times);
    for (std::int64_t frame = -100; frame < 5000; ++frame)
        ASSERT_EQ(times->frameFromMs(times->msFromFrame(frame)), frame) << frame;
}

TEST(AegisubFrameTimes, TheFirstFrameStartsAtZero)
{
    const auto shifted = AegisubFrameTimes::fromFrameStarts(ntscStarts(500));
    ASSERT_TRUE(shifted);
    EXPECT_EQ(shifted->msFromFrame(96), 3983);
    EXPECT_EQ(shifted->frameFromMs(14000), 336);
}

TEST(AegisubFrameTimes, NeedsTwoFramesInOrder)
{
    EXPECT_FALSE(AegisubFrameTimes::fromFrameStarts({}));
    EXPECT_FALSE(AegisubFrameTimes::fromFrameStarts({0}));
    EXPECT_FALSE(AegisubFrameTimes::fromFrameStarts({0, 0}));
    EXPECT_FALSE(AegisubFrameTimes::fromFrameStarts({0, 42, 41}));
}

TEST(AegisubFrameTimes, HugeInputsStayFinite)
{
    const auto times = AegisubFrameTimes::fromFrameStarts(ntscStarts());
    ASSERT_TRUE(times);
    EXPECT_GT(times->frameFromMs(INT64_MAX), 0);
    EXPECT_LT(times->frameFromMs(INT64_MIN), 0);
    EXPECT_GT(times->msFromFrame(INT64_MAX), 0);
    EXPECT_LT(times->msFromFrame(INT64_MIN), 0);
}

}
}
