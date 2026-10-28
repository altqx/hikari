#include "hikari/application/reference_navigation.h"

#include <climits>
#include <cstdlib>

namespace hikari::application {

std::int64_t legacyMilliseconds(const core::TimeField &time)
{
    return time.value.microseconds() / 1000; // SubsTime::mstime
}

std::vector<Occurrence> seekOccurrences(const core::Document &reference, const core::LineRecord &editing)
{
    // SubsGridPreview.cpp:856-858: the editing tab's current Line's times.
    return seekOccurrences(reference, legacyMilliseconds(editing.start), legacyMilliseconds(editing.end));
}

std::vector<Occurrence> seekOccurrences(const core::Document &reference, std::int64_t startTime, std::int64_t endTime)
{
    // SubsGridPreview::SeekForOccurences, the loop over one tab
    // (SubsGridPreview.cpp:868-920), transcribed with its variable names.
    const auto lines = reference.lines();
    std::vector<Occurrence> previewData;
    if (lines.empty())
        return previewData; // the rewrite's Document never lacks Lines in practice; legacy read key 0
    std::int64_t lastLine = -2;
    std::int64_t startMin = INT_MAX, startMax = -1, endMin = INT_MAX, endMax = -1;
    std::size_t keyStartMin = 0, keyStartMax = 0, keyEndMin = 0, keyEndMax = 0;
    for (std::size_t j = 0; j < lines.size(); ++j) {
        const core::LineRecord *dial = lines[j];
        // !dial->isVisible: NOT_VISIBLE only (an opened block is visible).
        if (dial->visibility == core::LineVisibility::Hidden)
            continue;
        const std::int64_t start = legacyMilliseconds(dial->start), end = legacyMilliseconds(dial->end);
        if (start < endTime && end > startTime) {
            if (lastLine + 1 == static_cast<std::int64_t>(j)) {
                lastLine = static_cast<std::int64_t>(j);
                previewData.back().length = (j - previewData.back().row) + 1;
            } else {
                lastLine = static_cast<std::int64_t>(j);
                previewData.push_back(Occurrence{j, 1});
            }
        } else {
            if (start > startMax && start < startTime) {
                startMax = start;
                keyStartMax = j;
            } else if (start < startMin && start > startTime) {
                startMin = start;
                keyStartMin = j;
            }
            if (end > endMax && end < endTime) {
                endMax = end;
                keyEndMax = j;
            } else if (end < endMin && end > endTime) {
                endMin = end;
                keyEndMin = j;
            }
        }
    }
    if (lastLine == -2) {
        // SubsGridPreview.cpp:897-917: the start or end nearest to the
        // editing Line's, preferring the start on a tie.
        std::int64_t bestStart = 0, bestEnd = 0;
        std::size_t bestJ = 0, bestJE = 0;
        if (std::llabs(startTime - startMax) > std::llabs(startTime - startMin) || startMax < 0) {
            bestStart = startMin;
            bestJ = keyStartMin;
        } else {
            bestStart = startMax;
            bestJ = keyStartMax;
        }
        if (std::llabs(endTime - endMax) > std::llabs(endTime - endMin) || endMax < 0) {
            bestEnd = endMin;
            bestJE = keyEndMin;
        } else {
            bestEnd = endMax;
            bestJE = keyEndMax;
        }
        if (std::llabs(startTime - bestStart) > std::llabs(endTime - bestEnd))
            bestJ = bestJE;
        previewData.push_back(Occurrence{bestJ, 0});
    }
    return previewData;
}

void LinkedMatch::set(const std::vector<Occurrence> &occurrences)
{
    m_set = true;
    m_candidates.clear();
    m_index = 0;
    m_nearest.reset();
    for (const auto &occurrence : occurrences) {
        if (occurrence.length == 0)
            m_nearest = occurrence.row;
        else
            m_candidates.push_back(occurrence);
    }
}

void LinkedMatch::clear()
{
    *this = LinkedMatch{};
}

std::optional<Occurrence> LinkedMatch::current() const
{
    if (m_index < m_candidates.size())
        return m_candidates[m_index];
    return std::nullopt;
}

bool LinkedMatch::step(int delta)
{
    const auto next = static_cast<std::int64_t>(m_index) + delta;
    if (delta == 0 || next < 0 || next >= static_cast<std::int64_t>(m_candidates.size()))
        return false;
    m_index = static_cast<std::size_t>(next);
    return true;
}

bool LinkedMatch::choose(std::size_t row)
{
    for (std::size_t i = 0; i < m_candidates.size(); ++i)
        if (m_candidates[i].row == row) {
            m_index = i;
            return true;
        }
    return false;
}

} // namespace hikari::application
