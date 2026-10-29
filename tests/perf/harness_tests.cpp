#include "perf_harness.h"

#include <gtest/gtest.h>

#include <stdexcept>

using namespace hikari;

TEST(PerfHarness, NearestRankPercentiles)
{
    const std::vector<double> samples{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    EXPECT_EQ(perf::nearestRank(samples, 50), 5);
    EXPECT_EQ(perf::nearestRank(samples, 95), 10);
    EXPECT_EQ(perf::nearestRank(samples, 10), 1);
    EXPECT_EQ(perf::nearestRank({}, 95), 0);
}

TEST(PerfHarness, RejectsSpecsBelowContractMinimums)
{
    int calls = 0;
    EXPECT_THROW(perf::run({"short", 5, 199, perf::kWarmWarmup, [&] { ++calls; }}), std::invalid_argument);
    EXPECT_THROW(perf::run({"cold", 5, 200, std::chrono::milliseconds(500), [&] { ++calls; }}),
                 std::invalid_argument);
    EXPECT_EQ(calls, 0);
}
