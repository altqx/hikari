#pragma once

// Typed time (docs/qt/time-semantics.md): signed 64-bit microseconds for
// Document time and deltas, distinct integral frame and sample indices, exact
// rationals for rates and media timebases. Nothing wraps; nothing is a sentinel.

#include "hikari/core/checked.h"

#include <compare>
#include <cstdint>
#include <optional>

namespace hikari::core {

class TimeDelta {
public:
    constexpr TimeDelta() = default;
    constexpr explicit TimeDelta(std::int64_t microseconds) : m_us(microseconds) {}
    static Checked<TimeDelta> fromMilliseconds(std::int64_t milliseconds);

    constexpr std::int64_t microseconds() const { return m_us; }
    // Equal values compare equal under every operator (C01-equality).
    constexpr auto operator<=>(const TimeDelta &) const = default;

    Checked<TimeDelta> plus(TimeDelta other) const;
    Checked<TimeDelta> minus(TimeDelta other) const;
    Checked<TimeDelta> negated() const;

private:
    std::int64_t m_us = 0;
};

class DocumentTime {
public:
    constexpr DocumentTime() = default;
    constexpr explicit DocumentTime(std::int64_t microseconds) : m_us(microseconds) {}
    static Checked<DocumentTime> fromMilliseconds(std::int64_t milliseconds);

    constexpr std::int64_t microseconds() const { return m_us; }
    constexpr auto operator<=>(const DocumentTime &) const = default;

    Checked<DocumentTime> plus(TimeDelta delta) const;
    // Signed: an earlier `other` gives a positive delta.
    Checked<TimeDelta> minus(DocumentTime other) const;

private:
    std::int64_t m_us = 0;
};

class VideoFrameIndex {
public:
    constexpr VideoFrameIndex() = default;
    constexpr explicit VideoFrameIndex(std::int64_t index) : m_index(index) {}
    constexpr std::int64_t value() const { return m_index; }
    constexpr auto operator<=>(const VideoFrameIndex &) const = default;
    // Signed index difference, this - other.
    Checked<std::int64_t> minus(VideoFrameIndex other) const;

private:
    std::int64_t m_index = 0;
};

// One sample position across every channel.
class AudioSampleFrame {
public:
    constexpr AudioSampleFrame() = default;
    constexpr explicit AudioSampleFrame(std::int64_t index) : m_index(index) {}
    constexpr std::int64_t value() const { return m_index; }
    constexpr auto operator<=>(const AudioSampleFrame &) const = default;

private:
    std::int64_t m_index = 0;
};

// Half-open [start, end). Equal endpoints are empty; an imported reversed range
// is kept as authored and reported, never silently swapped.
struct TimeRange {
    DocumentTime start;
    DocumentTime end;

    constexpr bool isEmpty() const { return start == end; }
    constexpr bool isReversed() const { return end < start; }
    constexpr bool contains(DocumentTime t) const { return start <= t && t < end; }
    constexpr bool operator==(const TimeRange &) const = default;
};

// Exact fraction with a positive denominator, always in lowest terms.
class Rational {
public:
    static Checked<Rational> make(std::int64_t numerator, std::int64_t denominator);
    static constexpr Rational integer(std::int64_t value) { return Rational(value, 1); }

    constexpr std::int64_t numerator() const { return m_num; }
    constexpr std::int64_t denominator() const { return m_den; }
    constexpr bool operator==(const Rational &) const = default;
    // Exact comparison by cross-multiplication; never rounds.
    std::strong_ordering operator<=>(const Rational &other) const;

private:
    constexpr Rational(std::int64_t num, std::int64_t den) : m_num(num), m_den(den) {}
    std::int64_t m_num = 0;
    std::int64_t m_den = 1;
};

enum class Provenance {
    Exact,      // authored or indexed and validated
    Estimated,  // derived, extrapolated or from data with diagnostics
    Unknown,
};

// Frames per second as an exact positive rational: 24000/1001 stays that.
class FrameRate {
public:
    static Checked<FrameRate> make(std::int64_t numerator, std::int64_t denominator,
                                   Provenance provenance = Provenance::Exact);

    const Rational &framesPerSecond() const { return m_fps; }
    Provenance provenance() const { return m_provenance; }
    bool operator==(const FrameRate &) const = default;

    // Exact start of a frame in seconds relative to frame 0: index / fps.
    Checked<Rational> secondsAt(VideoFrameIndex index) const;

private:
    FrameRate(Rational fps, Provenance provenance) : m_fps(fps), m_provenance(provenance) {}
    Rational m_fps = Rational::integer(1);
    Provenance m_provenance = Provenance::Exact;
};

// Integer media ticks in a declared rational timebase (seconds per tick).
struct MediaTimestamp {
    std::int64_t ticks = 0;
    Rational secondsPerTick = Rational::integer(1);
};

struct DocumentTimeConversion {
    DocumentTime time;
    bool exact = true; // false when rounding to the nearest microsecond lost precision
};

// Nearest microsecond, ties away from zero; reports whether that lost precision.
Checked<DocumentTimeConversion> toDocumentTime(const MediaTimestamp &timestamp);

} // namespace hikari::core
