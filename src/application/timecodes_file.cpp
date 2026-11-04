// Ported from libaegisub/common/vfr.cpp (Aegisub 3.5.1):
// Copyright (c) 2010, Thomas Goyne <plorkyeran@aegisub.org>
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

#include "hikari/application/timecodes_file.h"

#include "hikari/application/aegisub_frame_times.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <optional>

namespace hikari::application {

namespace {

constexpr std::int64_t kMaxTimecodes = 10'000'000;

struct Line {
    std::string_view text;
    int number = 0;
};

std::vector<Line> linesOf(std::string_view text)
{
    if (text.starts_with("\xEF\xBB\xBF"))
        text.remove_prefix(3);
    std::vector<Line> lines;
    int number = 0;
    for (std::size_t at = 0; at < text.size();) {
        const auto end = std::min(text.find('\n', at), text.size());
        auto line = text.substr(at, end - at);
        if (line.ends_with('\r'))
            line.remove_suffix(1);
        lines.push_back({line, ++number});
        at = end + 1;
    }
    return lines;
}

bool isSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

std::string_view trimmed(std::string_view s)
{
    while (!s.empty() && isSpace(s.front()))
        s.remove_prefix(1);
    while (!s.empty() && isSpace(s.back()))
        s.remove_suffix(1);
    return s;
}

bool skippable(std::string_view line)
{
    const auto t = trimmed(line);
    return t.empty() || t.front() == '#';
}

std::string_view afterSpaces(std::string_view s)
{
    while (!s.empty() && isSpace(s.front()))
        s.remove_prefix(1);
    return s;
}

// istream's operator>> for an integer: leading spaces, an optional sign, then digits.
template <typename T>
std::optional<T> leadingInteger(std::string_view &s)
{
    s = afterSpaces(s);
    if (s.starts_with('+') && s.size() > 1 && s[1] != '-' && s[1] != '+')
        s.remove_prefix(1);
    T value{};
    const auto [end, error] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (error != std::errc{})
        return std::nullopt;
    s.remove_prefix(static_cast<std::size_t>(end - s.data()));
    return value;
}

std::optional<double> leadingDouble(std::string_view &s)
{
    s = afterSpaces(s);
    if (s.starts_with('+') && s.size() > 1 && s[1] != '-' && s[1] != '+')
        s.remove_prefix(1);
    double value = 0;
    const auto [end, error] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (error != std::errc{})
        return std::nullopt;
    s.remove_prefix(static_cast<std::size_t>(end - s.data()));
    return value;
}

std::optional<double> wholeDouble(std::string_view s)
{
    auto rest = s;
    const auto value = leadingDouble(rest);
    if (!value || !afterSpaces(rest).empty())
        return std::nullopt;
    return value;
}

std::optional<TimecodesFormat> headerFormat(std::string_view header, const TimecodesRules &rules)
{
    if (rules.mkvmergeLines)
        header = trimmed(header);
    for (const std::string_view word : {"timecode", "timestamp"}) {
        if (word == "timestamp" && !rules.timestampHeaders)
            continue;
        const std::string prefix = "# " + std::string(word) + " format v";
        if (header == prefix + "1")
            return TimecodesFormat::V1;
        if (header == prefix + "2")
            return TimecodesFormat::V2;
        if (rules.v4 && header == prefix + "4")
            return TimecodesFormat::V4;
    }
    return std::nullopt;
}

std::expected<Timecodes, TimecodesError> fromTimes(Timecodes out)
{
    auto &times = out.startsMs;
    if (out.format == TimecodesFormat::V4)
        std::ranges::stable_sort(times);
    if (times.size() <= 1)
        return std::unexpected(TimecodesError{TimecodesProblem::TooFewTimes});
    if (!std::ranges::is_sorted(times))
        return std::unexpected(TimecodesError{TimecodesProblem::OutOfOrder});
    if (times.front() == times.back())
        return std::unexpected(TimecodesError{TimecodesProblem::AllIdentical});
    const std::int64_t first = times.front();
    for (auto &time : times)
        time -= first;
    const auto frames = static_cast<std::int64_t>(times.size()) - 1;
    if (frames > std::numeric_limits<std::int64_t>::max() / (kTimecodesDenominator * 1000))
        return std::unexpected(TimecodesError{TimecodesProblem::TooManyTimes});
    out.numerator = frames * kTimecodesDenominator * 1000 / times.back();
    out.last = frames * kTimecodesDenominator * 1000;
    return out;
}

std::expected<Timecodes, TimecodesError> parseV2(const std::vector<Line> &lines, TimecodesFormat format,
                                                 const TimecodesRules &rules)
{
    Timecodes out{format};
    for (std::size_t i = 1; i < lines.size(); ++i) {
        std::optional<std::int64_t> time;
        if (rules.mkvmergeLines) {
            if (skippable(lines[i].text))
                continue;
            const auto value = wholeDouble(lines[i].text);
            if (!value || !std::isfinite(*value))
                return std::unexpected(TimecodesError{TimecodesProblem::MalformedLine, lines[i].number});
            const double ms = rules.roundFractions ? std::round(*value) : std::trunc(*value);
            if (std::abs(ms) > std::numeric_limits<int>::max())
                return std::unexpected(TimecodesError{TimecodesProblem::TimeOutOfRange, lines[i].number});
            time = static_cast<std::int64_t>(ms);
        } else {
            auto rest = lines[i].text;
            const auto value = leadingInteger<int>(rest);
            if (!value)
                continue;
            time = *value;
        }
        if (static_cast<std::int64_t>(out.startsMs.size()) >= kMaxTimecodes)
            return std::unexpected(TimecodesError{TimecodesProblem::TooManyTimes, lines[i].number});
        out.startsMs.push_back(*time);
    }
    return fromTimes(std::move(out));
}

struct Range {
    int start = 0;
    int end = 0;
    double fps = 0;
    int line = 0;
};

std::expected<std::optional<Range>, TimecodesError> rangeOf(const Line &line, const TimecodesRules &rules)
{
    if (line.text.empty() || line.text.front() == '#' || (rules.mkvmergeLines && skippable(line.text)))
        return std::nullopt;
    const auto malformed = std::unexpected(TimecodesError{TimecodesProblem::MalformedLine, line.number});
    auto rest = rules.mkvmergeLines ? trimmed(line.text) : line.text;
    Range range{.line = line.number};
    const auto start = leadingInteger<int>(rest);
    if (!start || !(rest = afterSpaces(rest)).starts_with(','))
        return malformed;
    rest.remove_prefix(1);
    const auto end = leadingInteger<int>(rest);
    if (!end || !(rest = afterSpaces(rest)).starts_with(','))
        return malformed;
    rest.remove_prefix(1);
    const auto fps = leadingDouble(rest);
    if (!fps || !rest.empty())
        return malformed;
    range.start = *start;
    range.end = *end;
    range.fps = *fps;
    const auto invalid = [&](TimecodesProblem problem) { return std::unexpected(TimecodesError{problem, line.number}); };
    if (range.start < 0 || range.end < 0)
        return invalid(TimecodesProblem::NegativeFrame);
    if (range.end < range.start)
        return invalid(TimecodesProblem::EndBeforeStart);
    if (range.end > kMaxTimecodes - 2)
        return invalid(TimecodesProblem::RangeTooLarge);
    if (!std::isfinite(range.fps) || range.fps <= 0.)
        return invalid(TimecodesProblem::FpsNotPositive);
    if (range.fps > 1000.)
        return invalid(TimecodesProblem::FpsAbove1000);
    return range;
}

bool appendV1(std::vector<std::int64_t> &times, double time)
{
    if (!std::isfinite(time) || time < 0 || time > std::numeric_limits<int>::max() - .5)
        return false;
    times.push_back(static_cast<int>(time + .5));
    return true;
}

std::optional<std::string_view> assumedFpsText(const Line &line, const TimecodesRules &rules)
{
    if (!rules.mkvmergeLines)
        return line.text.size() < 7 ? std::string_view() : trimmed(line.text.substr(7));
    const auto t = trimmed(line.text);
    constexpr std::string_view word = "assume";
    if (t.size() <= word.size() || !isSpace(t[word.size()]) ||
        !std::ranges::equal(t.substr(0, word.size()), word,
                            [](char a, char b) { return (a | 0x20) == b; }))
        return std::nullopt;
    return trimmed(t.substr(word.size()));
}

std::expected<Timecodes, TimecodesError> parseV1(const std::vector<Line> &lines, const TimecodesRules &rules)
{
    std::size_t at = 0;
    if (lines.front().text.starts_with('#')) {
        at = 1;
        while (rules.mkvmergeLines && at < lines.size() && skippable(lines[at].text))
            ++at;
    }
    const Line assume = at < lines.size() ? lines[at] : Line{{}, lines.back().number + 1};
    const auto fpsText = assumedFpsText(assume, rules);
    if (!fpsText)
        return std::unexpected(TimecodesError{TimecodesProblem::MalformedLine, assume.number});
    const auto fps = wholeDouble(*fpsText);
    if (!fps || !std::isfinite(*fps) || *fps <= 0.)
        return std::unexpected(TimecodesError{TimecodesProblem::FpsNotPositive, assume.number});
    if (*fps > 1000.)
        return std::unexpected(TimecodesError{TimecodesProblem::FpsAbove1000, assume.number});

    std::vector<Range> ranges;
    for (std::size_t i = at + 1; i < lines.size(); ++i) {
        auto range = rangeOf(lines[i], rules);
        if (!range)
            return std::unexpected(range.error());
        if (*range)
            ranges.push_back(**range);
    }
    std::ranges::stable_sort(ranges, {}, &Range::start);

    Timecodes out{TimecodesFormat::V1};
    const auto outOfRange = std::unexpected(TimecodesError{TimecodesProblem::TimeOutOfRange});
    if (!ranges.empty())
        out.startsMs.reserve(static_cast<std::size_t>(ranges.back().end) + 2);
    double time = 0.;
    int frame = 0;
    for (const auto &range : ranges) {
        if (frame > range.start)
            return std::unexpected(TimecodesError{TimecodesProblem::Overlap, range.line});
        for (; frame < range.start; ++frame) {
            if (!appendV1(out.startsMs, time))
                return outOfRange;
            time += 1000. / *fps;
        }
        for (; frame <= range.end; ++frame) {
            if (!appendV1(out.startsMs, time))
                return outOfRange;
            time += 1000. / range.fps;
        }
    }
    if (!appendV1(out.startsMs, time))
        return outOfRange;
    const double last = time * *fps * static_cast<double>(kTimecodesDenominator);
    if (!std::isfinite(last) || last >= std::ldexp(1.0, 63))
        return outOfRange;
    out.last = static_cast<std::int64_t>(last);
    out.numerator = static_cast<std::int64_t>(*fps * static_cast<double>(kTimecodesDenominator));
    return out;
}

}

std::expected<Timecodes, TimecodesError> parseTimecodes(std::string_view text, const TimecodesRules &rules)
{
    const auto lines = linesOf(text);
    if (lines.empty())
        return std::unexpected(TimecodesError{TimecodesProblem::UnknownFormat, 1});
    const auto format = headerFormat(lines.front().text, rules);
    if (format == TimecodesFormat::V2 || format == TimecodesFormat::V4)
        return parseV2(lines, *format, rules);
    if (format == TimecodesFormat::V1 || lines.front().text.starts_with("Assume "))
        return parseV1(lines, rules);
    return std::unexpected(TimecodesError{TimecodesProblem::UnknownFormat, 1});
}

std::string timecodesText(const AegisubFrameTimes &times, int frameCount)
{
    std::string out = "# timecode format v2\n";
    const auto count = std::max<std::int64_t>(times.heldFrames(), frameCount);
    for (std::int64_t frame = 0; frame < count; ++frame)
        out += std::to_string(times.startOf(frame)) + '\n';
    return out;
}

}
