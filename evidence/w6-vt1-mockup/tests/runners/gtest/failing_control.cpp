#include <gtest/gtest.h>

// Must fail. CTest registers this binary with WILL_FAIL.
TEST(RunnerControl, FailsOnPurpose)
{
    EXPECT_EQ(1 + 1, 3);
}
