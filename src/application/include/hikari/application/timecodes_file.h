#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

class AegisubFrameTimes;

enum class TimecodesFormat { V1, V2, V4 };

inline constexpr std::int64_t kTimecodesDenominator = 1'000'000'000;

// A timecodes file's frame times as Aegisub 3.5.1 holds them: each listed frame's start in ms,
// from 0. Frames past the list continue at numerator / kTimecodesDenominator fps from `last`,
// the unrounded end of the listed frames in the same units.
struct Timecodes {
    TimecodesFormat format = TimecodesFormat::V2;
    std::vector<std::int64_t> startsMs;
    std::int64_t numerator = 0;
    std::int64_t last = 0;
};

enum class TimecodesProblem {
    UnknownFormat,
    MalformedLine,
    TooFewTimes,
    TooManyTimes,
    OutOfOrder,
    AllIdentical,
    NegativeFrame,
    EndBeforeStart,
    RangeTooLarge,
    FpsNotPositive,
    FpsAbove1000,
    Overlap,
    TimeOutOfRange,
};

struct TimecodesError {
    TimecodesProblem problem = TimecodesProblem::UnknownFormat;
    int line = 0; // 1-based; 0 for the file as a whole
    friend bool operator==(const TimecodesError &, const TimecodesError &) = default;
};

// mkvmerge's rules that Aegisub 3.5.1 doesn't have (FA21 #648 design gate rows 1–4).
struct TimecodesRules {
    bool timestampHeaders = true;  // "# timestamp format v1/v2", which mkvmerge writes
    bool v4 = true;                // v2 with unsorted times, read in frame order
    bool roundFractions = true;    // "41.7" is 42, as mkvmerge muxes it, rather than Aegisub's 41
    bool mkvmergeLines = true;     // blank, space-only and "#" lines skipped anywhere; others must parse
};

std::expected<Timecodes, TimecodesError> parseTimecodes(std::string_view text, const TimecodesRules &rules = {});

// Aegisub's Save: "# timecode format v2", every held start, then later starts up to `frameCount`.
std::string timecodesText(const AegisubFrameTimes &times, int frameCount);

}
