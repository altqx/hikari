#pragma once

// R2: reference navigation (docs/qt/ux/translation-comparison.md) and the
// matching of legacy's in-Grid subtitles preview (SubsGridPreview at
// 20d647c4). The reference tray shows the protected reference Document; it
// is navigated on its own, and optionally follows the editing target's active
// Line one way ("linked matching"). The preview (GRID_SHOW_PREVIEW) opens
// another tab's Document there, linked, as legacy's preview followed the grid
// it was drawn on (SubsGridPreview::NewSeeking).

#include "hikari/core/document.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace hikari::application {

// One of legacy's MultiPreviewData (SubsGridPreview.h:28-41): a run of
// consecutive reference rows, or with `length` 0 the single Line legacy put
// in its place when no Line matched.
struct Occurrence {
    std::size_t row = 0;    // lineRangeStart: the Document row (0-based)
    std::size_t length = 1; // lineRangeLen
    bool operator==(const Occurrence &) const = default;
};

// SubsGridPreview::SeekForOccurences for one tab (SubsGridPreview.cpp:852-924):
// the reference Lines shown (not Hidden) whose times overlap the editing Line
// [startMs, endMs) (Start < endMs && End > startMs, in legacy's milliseconds),
// in runs of consecutive rows (a Hidden Line between two ends a run). With no
// such Line, legacy's nearest Line (its start or end closest to the editing
// Line's) with length 0; nothing for a Document without Lines.
std::vector<Occurrence> seekOccurrences(const core::Document &reference, std::int64_t startMs, std::int64_t endMs);
std::vector<Occurrence> seekOccurrences(const core::Document &reference, const core::LineRecord &editing);

// The editing Line's times as legacy compares them (SubsTime mstime).
std::int64_t legacyMilliseconds(const core::TimeField &time);

// The tray's linked matching: the candidates for the editing target's active
// Line, the one shown, and the empty no-match state. A no-match never shows a
// substitute (translation-comparison.md): legacy's nearest Line is kept
// apart and shown only on request.
class LinkedMatch {
public:
    // The occurrences seekOccurrences gave (or a comparison pairing): runs
    // become candidates, the first shown (NewSeeking takes the first of the
    // previewed grid, SubsGridPreview.cpp:917-924); a length-0 entry is the
    // nearest Line of a no-match.
    void set(const std::vector<Occurrence> &occurrences);
    void clear();

    bool active() const { return m_set; }
    bool noMatch() const { return m_set && m_candidates.empty(); }
    std::size_t count() const { return m_candidates.size(); }
    std::size_t index() const { return m_index; }
    const std::vector<Occurrence> &candidates() const { return m_candidates; }
    // The candidate shown, or nothing in the no-match state.
    std::optional<Occurrence> current() const;
    std::optional<std::size_t> nearest() const { return m_nearest; }
    // Shows the previous (-1) or next (+1) candidate; false at either end.
    bool step(int delta);
    // Shows the candidate starting at `row`, when there is one.
    bool choose(std::size_t row);

private:
    bool m_set = false;
    std::vector<Occurrence> m_candidates;
    std::size_t m_index = 0;
    std::optional<std::size_t> m_nearest;
};

} // namespace hikari::application
