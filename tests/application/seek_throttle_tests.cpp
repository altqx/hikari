#include "hikari/application/seek_throttle.h"

#include <gtest/gtest.h>

#include <vector>

using namespace hikari::application;
using namespace std::chrono_literals;

namespace {

using Clock = SeekThrottle::Clock;

struct Drag {
    SeekThrottle throttle;
    Clock::time_point now{};
    std::vector<int> seeks;
    void record(std::optional<int> frame)
    {
        if (frame)
            seeks.push_back(*frame);
    }
    void advance(std::chrono::milliseconds by)
    {
        const auto until = now + by;
        while (const auto due = throttle.due()) {
            if (*due > until)
                break;
            now = *due;
            record(throttle.tick(now));
        }
        now = until;
    }
    void move(int frame) { record(throttle.move(frame, now)); }
    void release() { record(throttle.release(now)); }
};

}

TEST(SeekThrottle, AFastDragSeeksAtMostEveryIntervalAndExactlyWhereItIsLetGo)
{
    Drag drag;
    for (int i = 0; i < 500; ++i) {
        drag.move(i * 2);
        drag.advance(2ms);
    }
    drag.move(997);
    drag.release();
    EXPECT_GE(drag.seeks.size(), 20u);
    EXPECT_LE(drag.seeks.size(), 22u);
    EXPECT_EQ(drag.seeks.front(), 0);
    EXPECT_EQ(drag.seeks.back(), 997);
    for (std::size_t i = 1; i + 1 < drag.seeks.size(); ++i)
        EXPECT_GT(drag.seeks[i], drag.seeks[i - 1]);
    EXPECT_FALSE(drag.throttle.waiting());
}

TEST(SeekThrottle, TheFirstMoveSeeksAtOnceAndAWaitingFrameWhenTheIntervalIsUp)
{
    Drag drag;
    drag.move(10);
    EXPECT_EQ(drag.seeks, (std::vector<int>{10}));
    drag.advance(10ms);
    drag.move(11);
    drag.advance(10ms);
    drag.move(12);
    EXPECT_EQ(drag.seeks, (std::vector<int>{10}));
    ASSERT_TRUE(drag.throttle.due());
    EXPECT_EQ(*drag.throttle.due(), Clock::time_point{} + 50ms);
    drag.advance(29ms);
    EXPECT_EQ(drag.seeks, (std::vector<int>{10}));
    drag.advance(1ms);
    EXPECT_EQ(drag.seeks, (std::vector<int>{10, 12}));
    drag.advance(100ms);
    drag.move(20);
    EXPECT_EQ(drag.seeks, (std::vector<int>{10, 12, 20}));
}

TEST(SeekThrottle, TheReleaseSeeksItsFrameUnlessJustSought)
{
    Drag click;
    click.move(30);
    click.advance(5ms);
    click.release();
    EXPECT_EQ(click.seeks, (std::vector<int>{30}));
    Drag waiting;
    waiting.move(1);
    waiting.advance(5ms);
    waiting.move(2);
    waiting.advance(5ms);
    waiting.move(3);
    waiting.release();
    EXPECT_EQ(waiting.seeks, (std::vector<int>{1, 3}));
    EXPECT_FALSE(waiting.throttle.due());
    Drag held;
    held.move(40);
    held.advance(300ms);
    held.release();
    EXPECT_EQ(held.seeks, (std::vector<int>{40, 40}));
    Drag back;
    back.move(5);
    back.advance(10ms);
    back.move(6);
    back.move(5);
    EXPECT_FALSE(back.throttle.waiting());
    back.advance(100ms);
    EXPECT_EQ(back.seeks, (std::vector<int>{5}));
    Drag none;
    none.release();
    EXPECT_TRUE(none.seeks.empty());
    none.move(7);
    none.advance(1ms);
    none.release();
    none.move(8);
    EXPECT_EQ(none.seeks, (std::vector<int>{7, 8}));
}
