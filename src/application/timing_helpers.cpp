#include "hikari/application/timing_helpers.h"

#include "line_retime.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <vector>

namespace hikari::application {

namespace {

using retiming::msOf;
using retiming::Retime;

using Lines = std::vector<const core::LineRecord *>;

Lines linesOf(const EditSession &session)
{
    const auto lines = session.document().lines();
    return Lines(lines.begin(), lines.end());
}

void commitPending(EditSession &session)
{
    if (session.draftLine())
        session.commitDraft();
}

int frameStart(const LegacyTimebase &timebase, int frame)
{
    return retiming::zeroIt(timebase.startTimeFor(std::max(frame, 0)));
}

const core::LineRecord *uncommentedNeighbour(const Lines &lines, std::size_t at, TimingToward toward)
{
    if (toward == TimingToward::Previous) {
        for (std::size_t i = at; i-- > 0;)
            if (!lines[i]->comment)
                return lines[i];
    } else {
        for (std::size_t i = at + 1; i < lines.size(); ++i)
            if (!lines[i]->comment)
                return lines[i];
    }
    return nullptr;
}

std::expected<void, CommandRefusal> apply(EditSession &session, const char *name,
                                          const std::map<core::LineId, Retime> &times)
{
    if (times.empty())
        return std::unexpected(CommandRefusal::Invalid);
    const auto ran = retiming::retime(session, name, times);
    if (!ran)
        return std::unexpected(ran.error());
    if (!*ran)
        return std::unexpected(CommandRefusal::Invalid);
    return {};
}

std::optional<int> keyframeBefore(std::span<const int> sorted, int frame)
{
    const auto it = std::lower_bound(sorted.begin(), sorted.end(), frame);
    if (it == sorted.begin())
        return std::nullopt;
    return *(it - 1);
}

std::optional<int> keyframeAfter(std::span<const int> sorted, int frame)
{
    const auto it = std::upper_bound(sorted.begin(), sorted.end(), frame);
    if (it == sorted.end())
        return std::nullopt;
    return *it;
}

std::vector<int> sortedKeyframes(std::span<const int> keyframes)
{
    std::vector<int> sorted(keyframes.begin(), keyframes.end());
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    return sorted;
}

}

std::expected<void, CommandRefusal> snapToAdjacentKeyframe(EditSession &session, const LegacyTimebase &timebase,
                                                           std::span<const int> keyframes, TimingEdge edge,
                                                           TimingToward toward)
{
    commitPending(session);
    const auto sorted = sortedKeyframes(keyframes);
    const auto &selected = session.selection().selected;
    std::map<core::LineId, Retime> times;
    for (const auto *l : linesOf(session)) {
        if (!selected.contains(l->id))
            continue;
        const int first = timebase.frameAt(msOf(l->start.value));
        const int after = timebase.frameAt(msOf(l->end.value));
        const int from = edge == TimingEdge::Start ? first : after;
        const auto kf = toward == TimingToward::Previous ? keyframeBefore(sorted, from) : keyframeAfter(sorted, from);
        if (!kf)
            continue;
        if (edge == TimingEdge::Start && *kf < after)
            times[l->id].startMs = frameStart(timebase, *kf);
        else if (edge == TimingEdge::End && *kf > first)
            times[l->id].endMs = frameStart(timebase, *kf);
    }
    return apply(session, "Snapping to the adjacent keyframe", times);
}

std::expected<void, CommandRefusal> changeLead(EditSession &session, const LegacyTimebase &timebase, TimingEdge edge,
                                               bool add, int amountMs)
{
    commitPending(session);
    const auto lines = linesOf(session);
    const auto &selected = session.selection().selected;
    std::map<core::LineId, Retime> times;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto &l = *lines[i];
        if (!selected.contains(l.id))
            continue;
        const int start = msOf(l.start.value), end = msOf(l.end.value);
        const int first = timebase.frameAt(start), after = timebase.frameAt(end);
        if (edge == TimingEdge::Start) {
            int frame = timebase.frameAt(std::max(add ? start - amountMs : start + amountMs, 0));
            if (add) {
                if (const auto *before = uncommentedNeighbour(lines, i, TimingToward::Previous);
                    before && msOf(before->end.value) <= start)
                    frame = std::max(frame, timebase.frameAt(msOf(before->end.value)));
            } else {
                frame = std::min(frame, after - 1);
            }
            times[l.id].startMs = frameStart(timebase, frame);
        } else {
            int frame = timebase.frameAt(std::max(add ? end + amountMs : end - amountMs, 0));
            if (add) {
                if (const auto *next = uncommentedNeighbour(lines, i, TimingToward::Next);
                    next && msOf(next->start.value) >= end)
                    frame = std::min(frame, timebase.frameAt(msOf(next->start.value)));
            } else {
                frame = std::max(frame, first + 1);
            }
            times[l.id].endMs = frameStart(timebase, frame);
        }
    }
    const char *name = edge == TimingEdge::Start ? (add ? "Adding lead-in" : "Removing lead-in")
                                                 : (add ? "Adding lead-out" : "Removing lead-out");
    return apply(session, name, times);
}

std::expected<void, CommandRefusal> nudgeEdgeByFrames(EditSession &session, const LegacyTimebase &timebase,
                                                      TimingEdge edge, int frames)
{
    commitPending(session);
    const auto &selected = session.selection().selected;
    std::map<core::LineId, Retime> times;
    for (const auto *l : linesOf(session)) {
        if (!selected.contains(l->id))
            continue;
        const int first = timebase.frameAt(msOf(l->start.value));
        const int after = timebase.frameAt(msOf(l->end.value));
        if (edge == TimingEdge::Start)
            times[l->id].startMs = frameStart(timebase, std::clamp(first + frames, 0, std::max(after - 1, 0)));
        else
            times[l->id].endMs = frameStart(timebase, std::max(after + frames, first + 1));
    }
    return apply(session, edge == TimingEdge::Start ? "Moving the start by frames" : "Moving the end by frames", times);
}

std::expected<void, CommandRefusal> extendNeighbour(EditSession &session, TimingToward toward)
{
    commitPending(session);
    const auto active = session.selection().active;
    if (!active)
        return std::unexpected(CommandRefusal::Invalid);
    const auto lines = linesOf(session);
    const auto at = std::find_if(lines.begin(), lines.end(), [&](const core::LineRecord *l) { return l->id == *active; });
    if (at == lines.end())
        return std::unexpected(CommandRefusal::Invalid);
    const auto *other = uncommentedNeighbour(lines, static_cast<std::size_t>(at - lines.begin()), toward);
    if (!other)
        return std::unexpected(CommandRefusal::Invalid);
    std::map<core::LineId, Retime> times;
    if (toward == TimingToward::Previous) {
        const int to = msOf((*at)->start.value);
        if (msOf(other->start.value) > to)
            return std::unexpected(CommandRefusal::Invalid);
        times[other->id].endMs = to;
    } else {
        const int to = msOf((*at)->end.value);
        if (msOf(other->end.value) < to)
            return std::unexpected(CommandRefusal::Invalid);
        times[other->id].startMs = to;
    }
    return apply(session, toward == TimingToward::Previous ? "Extending the previous line" : "Extending the next line",
                 times);
}

std::optional<TimingSuggestion> suggestTiming(const core::Document &document, core::LineId line,
                                              const LegacyTimebase &timebase, std::span<const int> keyframes,
                                              const ShiftTimesSettings &settings)
{
    const auto docLines = document.lines();
    const Lines lines(docLines.begin(), docLines.end());
    const auto at = std::find_if(lines.begin(), lines.end(), [&](const core::LineRecord *l) { return l->id == line; });
    if (at == lines.end() || (*at)->comment)
        return std::nullopt;
    const int rules = settings.postprocessor;
    const bool leadIn = (rules & 1) != 0, leadOut = (rules & 2) != 0;
    const bool link = (rules & 4) != 0, snap = (rules & 8) != 0;
    std::vector<std::pair<int, int>> keys; // {time, frame}
    for (const int kf : sortedKeyframes(keyframes))
        keys.emplace_back(frameStart(timebase, kf), kf);
    const auto onKeyframe = [&](int ms) {
        return std::any_of(keys.begin(), keys.end(), [&](const auto &k) { return k.first == ms; });
    };
    const auto rounded = [&](int ms) { return frameStart(timebase, timebase.frameAt(std::max(ms, 0))); };

    TimingSuggestion s;
    s.line = line;
    s.startBefore = s.startMs = msOf((*at)->start.value);
    s.endBefore = s.endMs = msOf((*at)->end.value);
    const int start = s.startBefore, end = s.endBefore;
    const int startFrame = timebase.frameAt(start), endFrame = timebase.frameAt(end);

    if (snap && onKeyframe(start)) {
        s.startReason = TimingReason::OnKeyframe;
    } else {
        if (snap) {
            for (auto k = keys.rbegin(); k != keys.rend(); ++k)
                if (k->first < start && start - k->first <= settings.keyframeBeforeStart) {
                    s.startMs = k->first;
                    s.startReason = TimingReason::KeyframeBefore;
                    s.startFrames = startFrame - k->second;
                    break;
                }
            if (s.startReason == TimingReason::Kept)
                for (const auto &[time, frame] : keys)
                    if (time > start && time - start <= settings.keyframeAfterStart && frame < endFrame) {
                        s.startMs = time;
                        s.startReason = TimingReason::KeyframeAfter;
                        s.startFrames = frame - startFrame;
                        break;
                    }
        }
        const auto *before = uncommentedNeighbour(lines, static_cast<std::size_t>(at - lines.begin()), TimingToward::Previous);
        if (s.startReason == TimingReason::Kept && link && before) {
            const int beforeEnd = msOf(before->end.value);
            const int linked = leadIn ? rounded(start - settings.leadIn) : start;
            if (std::abs(start - beforeEnd) <= settings.thresholdStart && !onKeyframe(beforeEnd) &&
                linked > msOf(before->start.value)) {
                s.startMs = linked;
                s.startReason = TimingReason::Linked;
                s.linked = before->id;
                s.linkedEndBefore = beforeEnd;
            }
        }
        if (s.startReason == TimingReason::Kept && leadIn) {
            s.startMs = rounded(start - settings.leadIn);
            s.startReason = TimingReason::LeadIn;
        }
    }

    if (snap && onKeyframe(end)) {
        s.endReason = TimingReason::OnKeyframe;
    } else {
        if (snap) {
            for (auto k = keys.rbegin(); k != keys.rend(); ++k)
                if (k->first < end && end - k->first <= settings.keyframeBeforeEnd && k->first > s.startMs) {
                    s.endMs = k->first;
                    s.endReason = TimingReason::KeyframeBefore;
                    s.endFrames = endFrame - k->second;
                    break;
                }
            if (s.endReason == TimingReason::Kept)
                for (const auto &[time, frame] : keys)
                    if (time > end && time - end <= settings.keyframeAfterEnd) {
                        s.endMs = time;
                        s.endReason = TimingReason::KeyframeAfter;
                        s.endFrames = frame - endFrame;
                        break;
                    }
        }
        if (s.endReason == TimingReason::Kept && leadOut) {
            s.endMs = rounded(end + settings.leadOut);
            s.endReason = TimingReason::LeadOut;
        }
    }
    return s;
}

std::expected<void, CommandRefusal> acceptTiming(EditSession &session, const TimingSuggestion &suggestion)
{
    if (suggestion.endMs < suggestion.startMs)
        return std::unexpected(CommandRefusal::Invalid);
    commitPending(session);
    std::map<core::LineId, Retime> times{{suggestion.line, Retime{suggestion.startMs, suggestion.endMs}}};
    if (suggestion.linked)
        times[*suggestion.linked].endMs = suggestion.startMs;
    const auto ran = retiming::retime(session, "Timing assistant", times);
    if (!ran)
        return std::unexpected(ran.error());
    return {};
}

}
