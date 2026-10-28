// Harness self-checks: these run with the ordinary tests (label gtest), so a
// benchmark below the contract minimums fails before any perf run.

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
    // Too few operations: 5 x 199 < 1,000.
    EXPECT_THROW(perf::run({"short", 5, 199, perf::kWarmWarmup, [&] { ++calls; }}), std::invalid_argument);
    // Too little warmup for a warm workload.
    EXPECT_THROW(perf::run({"cold", 5, 200, std::chrono::milliseconds(500), [&] { ++calls; }}),
                 std::invalid_argument);
    EXPECT_EQ(calls, 0); // rejected before running anything
}
