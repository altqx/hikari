#include "hikari/application/timecodes_file.h"

#include "hikari/application/aegisub_frame_times.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace hikari::application {
namespace {

std::string fixture(const std::string &name)
{
    std::ifstream in(std::filesystem::path(HIKARI_TIMECODES_FIXTURES) / name, std::ios::binary);
    std::stringstream out;
    out << in.rdbuf();
    return out.str();
}

constexpr TimecodesRules kAegisub{.timestampHeaders = false, .v4 = false, .roundFractions = false, .mkvmergeLines = false};

std::vector<std::int64_t> starts(std::string_view text, const TimecodesRules &rules = {})
{
    const auto parsed = parseTimecodes(text, rules);
    EXPECT_TRUE(parsed) << static_cast<int>(parsed.error().problem) << " at line " << parsed.error().line;
    return parsed ? parsed->startsMs : std::vector<std::int64_t>{};
}

std::vector<std::int64_t> firstFrames(std::vector<std::int64_t> times, std::size_t frames)
{
    times.resize(std::min(times.size(), frames));
    return times;
}

TimecodesError errorOf(std::string_view text, const TimecodesRules &rules = {})
{
    const auto parsed = parseTimecodes(text, rules);
    EXPECT_FALSE(parsed);
    return parsed ? TimecodesError{} : parsed.error();
}

class MuxedByMkvmerge : public testing::TestWithParam<std::pair<const char *, std::size_t>> {};

TEST_P(MuxedByMkvmerge, FramesStartWhereMkvmergePutThem)
{
    const auto [name, frames] = GetParam();
    const auto muxed = starts(fixture(std::string(name) + ".mkvmerge.txt"));
    ASSERT_GT(muxed.size(), frames);
    const auto parsed = parseTimecodes(fixture(std::string(name) + ".txt"));
    ASSERT_TRUE(parsed);
    const auto times = AegisubFrameTimes::fromTimecodes(*parsed);
    std::vector<std::int64_t> ours;
    for (std::size_t frame = 0; frame < frames; ++frame)
        ours.push_back(times.startOf(static_cast<std::int64_t>(frame)));
    EXPECT_EQ(ours, firstFrames(muxed, frames));
}

INSTANTIATE_TEST_SUITE_P(Timecodes, MuxedByMkvmerge,
                         testing::Values(std::pair{"v1-gaps", std::size_t(40)}, std::pair{"v1-aegisub", std::size_t(40)},
                                         std::pair{"v2-fractions", std::size_t(8)},
                                         std::pair{"v4-unsorted", std::size_t(8)}));

TEST(Timecodes, AegisubTruncatesFractionsWhereMkvmergeRounds)
{
    EXPECT_EQ(starts(fixture("v2-fractions.txt"), {.roundFractions = false}),
              (std::vector<std::int64_t>{0, 41, 83, 125, 500, 541, 583, 625}));
    EXPECT_EQ(starts(fixture("v2-fractions.txt")), (std::vector<std::int64_t>{0, 42, 83, 125, 500, 542, 583, 625}));
}

TEST(Timecodes, OnlyAegisubsTimecodeHeadersWithoutMkvmergesRules)
{
    EXPECT_EQ(errorOf("# timestamp format v2\n0\n40\n", kAegisub), (TimecodesError{TimecodesProblem::UnknownFormat, 1}));
    EXPECT_EQ(errorOf("# timestamp format v4\n0\n40\n", kAegisub), (TimecodesError{TimecodesProblem::UnknownFormat, 1}));
    EXPECT_EQ(starts("# timecode format v2\n0\n40\n", kAegisub), (std::vector<std::int64_t>{0, 40}));
    EXPECT_EQ(starts("# timestamp format v2\n0\n40\n"), (std::vector<std::int64_t>{0, 40}));
    EXPECT_EQ(starts("# timestamp format v1\nassume 25\n"), (std::vector<std::int64_t>{0}));
}

TEST(Timecodes, V4IsReadInFrameOrder)
{
    EXPECT_EQ(starts("# timestamp format v4\n80\n0\n40\n"), (std::vector<std::int64_t>{0, 40, 80}));
    EXPECT_EQ(errorOf("# timestamp format v4\n0\n40\n", {.v4 = false}).problem, TimecodesProblem::UnknownFormat);
}

TEST(Timecodes, AegisubSkipsV2LinesThatDontStartWithANumber)
{
    EXPECT_EQ(starts("# timecode format v2\n0\nnot a time\n40\n80abc\n", kAegisub), (std::vector<std::int64_t>{0, 40, 80}));
    EXPECT_EQ(errorOf("# timecode format v2\n0\nnot a time\n40\n"), (TimecodesError{TimecodesProblem::MalformedLine, 3}));
    EXPECT_EQ(starts("# timecode format v2\n0\n   \n\t# note\n40\n"), (std::vector<std::int64_t>{0, 40}));
}

TEST(Timecodes, AegisubRejectsASpaceOnlyV1Line)
{
    EXPECT_EQ(errorOf("# timecode format v1\nAssume 25\n  \n", kAegisub), (TimecodesError{TimecodesProblem::MalformedLine, 3}));
    EXPECT_TRUE(parseTimecodes("# timecode format v1\nAssume 25\n  \n"));
}

TEST(Timecodes, GoodV1Files)
{
    EXPECT_EQ(starts("# timecode format v1\nAssume 25\n2,2,50\n"), (std::vector<std::int64_t>{0, 40, 80, 100}));
    EXPECT_EQ(starts("# timecode format v1\nAssume 25\n 1 , 1 , 50\n", kAegisub), (std::vector<std::int64_t>{0, 40, 60}));
    EXPECT_EQ(starts("Assume 25\n1,1,50\n"), (std::vector<std::int64_t>{0, 40, 60}));
    EXPECT_EQ(starts("# timecode format v1\nAssume 25\n3,3,50\n1,1,50\n"),
              (std::vector<std::int64_t>{0, 40, 60, 100, 120}));
}

TEST(Timecodes, BadV1Files)
{
    const auto problem = [](std::string_view ranges) {
        return errorOf("# timecode format v1\nAssume 25\n" + std::string(ranges));
    };
    EXPECT_EQ(problem("1;2;30\n"), (TimecodesError{TimecodesProblem::MalformedLine, 3}));
    EXPECT_EQ(problem("1,2\n"), (TimecodesError{TimecodesProblem::MalformedLine, 3}));
    EXPECT_EQ(problem("1,2,30,4\n"), (TimecodesError{TimecodesProblem::MalformedLine, 3}));
    EXPECT_EQ(problem("1.5,2,30\n"), (TimecodesError{TimecodesProblem::MalformedLine, 3}));
    EXPECT_EQ(problem("0,10,30\n5,15,30\n"), (TimecodesError{TimecodesProblem::Overlap, 4}));
    EXPECT_EQ(problem("0,10,30\n2,4,30\n"), (TimecodesError{TimecodesProblem::Overlap, 4}));
    EXPECT_EQ(problem("1,2,1000.5\n"), (TimecodesError{TimecodesProblem::FpsAbove1000, 3}));
    EXPECT_EQ(problem("1,2,0\n"), (TimecodesError{TimecodesProblem::FpsNotPositive, 3}));
    EXPECT_EQ(problem("1,2,nan\n"), (TimecodesError{TimecodesProblem::FpsNotPositive, 3}));
    EXPECT_EQ(problem("-1,2,30\n"), (TimecodesError{TimecodesProblem::NegativeFrame, 3}));
    EXPECT_EQ(problem("5,2,30\n"), (TimecodesError{TimecodesProblem::EndBeforeStart, 3}));
    EXPECT_EQ(problem("0,99999999,30\n"), (TimecodesError{TimecodesProblem::RangeTooLarge, 3}));
    EXPECT_EQ(problem("0,9999990,0.000001\n").problem, TimecodesProblem::TimeOutOfRange);
    EXPECT_EQ(errorOf("# timecode format v1\nAssume 1000.1\n"), (TimecodesError{TimecodesProblem::FpsAbove1000, 2}));
    EXPECT_EQ(errorOf("# timecode format v1\nAssume 0\n"), (TimecodesError{TimecodesProblem::FpsNotPositive, 2}));
    EXPECT_EQ(errorOf("# timecode format v1\n1,2,30\n"), (TimecodesError{TimecodesProblem::MalformedLine, 2}));
}

TEST(Timecodes, BadV2Files)
{
    EXPECT_EQ(errorOf("# timecode format v2\n"), (TimecodesError{TimecodesProblem::TooFewTimes, 0}));
    EXPECT_EQ(errorOf("# timecode format v2\n40\n"), (TimecodesError{TimecodesProblem::TooFewTimes, 0}));
    EXPECT_EQ(errorOf("# timecode format v2\n0\n80\n40\n"), (TimecodesError{TimecodesProblem::OutOfOrder, 0}));
    EXPECT_EQ(errorOf("# timecode format v2\n40\n40\n"), (TimecodesError{TimecodesProblem::AllIdentical, 0}));
    EXPECT_EQ(errorOf("# timecode format v2\n0\n1e300\n"), (TimecodesError{TimecodesProblem::TimeOutOfRange, 3}));
    EXPECT_EQ(errorOf(""), (TimecodesError{TimecodesProblem::UnknownFormat, 1}));
    EXPECT_EQ(errorOf("0\n40\n"), (TimecodesError{TimecodesProblem::UnknownFormat, 1}));
}

TEST(Timecodes, V2StartsAtZeroAndRunsOnAtItsAverageRate)
{
    const auto parsed = parseTimecodes("\xEF\xBB\xBF# timecode format v2\r\n1000\r\n1040\r\n1080\r\n");
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed->startsMs, (std::vector<std::int64_t>{0, 40, 80}));
    const auto times = AegisubFrameTimes::fromTimecodes(*parsed);
    EXPECT_DOUBLE_EQ(times.fps(), 25.);
    EXPECT_EQ(times.startOf(5), 200);
}

TEST(Timecodes, V1RunsOnAtItsAssumedRate)
{
    const auto parsed = parseTimecodes("# timecode format v1\nAssume 25\n0,1,50\n");
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed->startsMs, (std::vector<std::int64_t>{0, 20, 40}));
    const auto times = AegisubFrameTimes::fromTimecodes(*parsed);
    EXPECT_DOUBLE_EQ(times.fps(), 25.);
    EXPECT_EQ(times.startOf(3), 80);
    EXPECT_EQ(times.frameFromMs(81), 4);
}

TEST(Timecodes, SavingWritesAegisubsV2AndRunsOnToTheVideosLength)
{
    const auto parsed = parseTimecodes(fixture("v2-fractions.txt"));
    ASSERT_TRUE(parsed);
    const auto times = AegisubFrameTimes::fromTimecodes(*parsed);
    const auto text = timecodesText(times, 10);
    EXPECT_TRUE(text.starts_with("# timecode format v2\n0\n42\n83\n125\n500\n"));
    const auto again = parseTimecodes(text, kAegisub);
    ASSERT_TRUE(again);
    EXPECT_EQ(firstFrames(again->startsMs, 8), parsed->startsMs);
    EXPECT_EQ(again->startsMs.size(), 10u);
    EXPECT_EQ(timecodesText(times, -1), timecodesText(times, 8));
}

TEST(Timecodes, SavingAVideosFramesStartsThemAtZero)
{
    const auto times = AegisubFrameTimes::fromFrameStarts({1000, 1042, 1083});
    ASSERT_TRUE(times);
    EXPECT_EQ(timecodesText(*times, 3), "# timecode format v2\n0\n42\n83\n");
}

}
}
