#pragma once

// Frame lookup over presentation starts B[i] (docs/qt/time-semantics.md).
// Constant rate: B[i] = origin + i / fps, compared exactly as rationals.
// Indexed (VFR): authored starts in presentation order; frame identity is never
// renumbered, and disordered starts make the timeline Estimated, not Exact.

#include "hikari/core/time.h"

#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace hikari::core {

struct TimelineDiagnostic {
    enum class Kind {
        NotStrictlyIncreasing, // start i is not after start i-1
    };
    Kind kind;
    VideoFrameIndex frame;
};

class FrameTimeline {
public:
    // frameCount bounds the timeline; without it the timeline is unbounded above.
    static FrameTimeline constantRate(FrameRate rate, DocumentTime origin,
                                      std::optional<std::int64_t> frameCount = std::nullopt);
    // lastFrameEnd is the known end of the final frame; without it the final
    // frame's extent is unknown and lookups past its start are out of range.
    static FrameTimeline indexed(std::vector<DocumentTime> starts,
                                 std::optional<DocumentTime> lastFrameEnd = std::nullopt);

    Provenance provenance() const;
    const std::vector<TimelineDiagnostic> &diagnostics() const { return m_diagnostics; }

    // First frame with B[i] >= t: the first sampled frame showing a subtitle
    // that starts at t.
    Checked<VideoFrameIndex> frameAtOrAfter(DocumentTime t) const;
    // The frame with B[i] <= t < B[i+1]: the frame displayed at t.
    Checked<VideoFrameIndex> frameContaining(DocumentTime t) const;
    // Last frame with B[i] < end: the last frame showing a subtitle ending at end.
    Checked<VideoFrameIndex> lastFrameStartingBefore(DocumentTime end) const;
    // Exact start of a frame. Constant rate gives an exact rational number of
    // microseconds; indexed timelines give the authored start.
    Checked<Rational> frameStartMicroseconds(VideoFrameIndex frame) const;

private:
    struct Constant {
        FrameRate rate;
        DocumentTime origin;
        std::optional<std::int64_t> frameCount;
    };
    struct Indexed {
        std::vector<DocumentTime> starts;
        std::optional<DocumentTime> lastFrameEnd;
        bool ordered = true;
    };
    explicit FrameTimeline(std::variant<Constant, Indexed> data) : m_data(std::move(data)) {}

    std::variant<Constant, Indexed> m_data;
    std::vector<TimelineDiagnostic> m_diagnostics;
};

// T42-A: the signed frame offset between two times on one timeline, as the
// difference of their frameAtOrAfter indices (never the frame of target-anchor).
Checked<std::int64_t> frameOffset(const FrameTimeline &timeline, DocumentTime anchor, DocumentTime target);

} // namespace hikari::core
