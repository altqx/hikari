#pragma once

#include "hikari/core/document.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace hikari::application {

// A run of consecutive reference rows; `length` 0 marks the nearest Line of a no-match.
struct Occurrence {
    std::size_t row = 0; // 0-based Document row
    std::size_t length = 1;
    bool operator==(const Occurrence &) const = default;
};

// Non-Hidden reference Lines overlapping [startMs, endMs), in runs of consecutive rows (a Hidden
// Line ends a run). No overlap: the nearest Line with length 0. Empty for a Document without Lines.
std::vector<Occurrence> seekOccurrences(const core::Document &reference, std::int64_t startMs, std::int64_t endMs);
std::vector<Occurrence> seekOccurrences(const core::Document &reference, const core::LineRecord &editing);

// Truncated to whole milliseconds.
std::int64_t legacyMilliseconds(const core::TimeField &time);

// A no-match shows no substitute; the nearest Line is kept apart, shown only on request.
class LinkedMatch {
public:
    // Runs become candidates, the first shown; a length-0 entry is the nearest Line of a no-match.
    void set(const std::vector<Occurrence> &occurrences);
    void clear();

    bool active() const { return m_set; }
    bool noMatch() const { return m_set && m_candidates.empty(); }
    std::size_t count() const { return m_candidates.size(); }
    std::size_t index() const { return m_index; }
    const std::vector<Occurrence> &candidates() const { return m_candidates; }
    // nullopt in the no-match state.
    std::optional<Occurrence> current() const;
    std::optional<std::size_t> nearest() const { return m_nearest; }
    // -1 previous, +1 next; false at either end.
    bool step(int delta);
    bool choose(std::size_t row);

private:
    bool m_set = false;
    std::vector<Occurrence> m_candidates;
    std::size_t m_index = 0;
    std::optional<std::size_t> m_nearest;
};

}
