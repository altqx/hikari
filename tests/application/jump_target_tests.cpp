#include "hikari/application/jump_target.h"

#include <gtest/gtest.h>

using namespace hikari::application;
using Kind = JumpTarget::Kind;

namespace {

JumpTarget parsed(std::string_view text)
{
    const auto target = parseJumpTarget(text);
    EXPECT_TRUE(target) << text;
    return target.value_or(JumpTarget{});
}

JumpClock clock25(std::int64_t frames, std::int64_t shown)
{
    return {frames, shown, [](std::int64_t ms) { return ms / 40; }, [](std::int64_t frame) { return frame * 40; }};
}

}

TEST(JumpTarget, TimesInEveryForm)
{
    EXPECT_EQ(parsed("0:03:40.50").value, 220500);
    EXPECT_EQ(parsed("3:40").value, 220000);
    EXPECT_EQ(parsed("3:40.5").value, 220500);
    EXPECT_EQ(parsed("220.5").value, 220500);
    EXPECT_EQ(parsed("1:00:00").value, 3600000);
    EXPECT_EQ(parsed(" 3:40 ").kind, Kind::Time);
}

TEST(JumpTarget, ABareNumberIsAFrame)
{
    const JumpTarget t = parsed("5200");
    EXPECT_EQ(t.kind, Kind::Frame);
    EXPECT_EQ(t.value, 5200);
}

TEST(JumpTarget, StepsInSecondsAndFrames)
{
    EXPECT_EQ(parsed("+5s").kind, Kind::TimeStep);
    EXPECT_EQ(parsed("+5s").value, 5000);
    EXPECT_EQ(parsed("-1.5s").value, -1500);
    EXPECT_EQ(parsed("+1:00").value, 60000);
    EXPECT_EQ(parsed("+12f").kind, Kind::FrameStep);
    EXPECT_EQ(parsed("-12f").value, -12);
    EXPECT_EQ(parsed("+12F").value, 12);
}

TEST(JumpTarget, InputItCannotReadIsRefused)
{
    for (const std::string_view text : {"", "3:99", "1:60:00", "abc", "3:40:", "5.", ".5x", "+5", "+", "-f", "1:2:3:4",
                                        "+1.5f", "12 34", "3:4a", "0x10", "99999999999999"})
        EXPECT_FALSE(parseJumpTarget(text)) << text;
}

TEST(JumpTarget, FramesClampToTheVideo)
{
    const JumpClock clock = clock25(1000, 100);
    EXPECT_EQ(jumpFrame(parsed("5200"), clock), 999);
    EXPECT_EQ(jumpFrame(parsed("3:40"), clock), 999);
    EXPECT_EQ(jumpFrame(parsed("0:02"), clock), 50);
    EXPECT_EQ(jumpFrame(parsed("+12f"), clock), 112);
    EXPECT_EQ(jumpFrame(parsed("-500f"), clock), 0);
    EXPECT_EQ(jumpFrame(parsed("+5s"), clock), 225);
    EXPECT_EQ(jumpFrame(parsed("-1:00"), clock), 0);
}
