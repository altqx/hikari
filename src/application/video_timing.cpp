#include "hikari/application/video_timing.h"

#include "line_retime.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>
#include <tuple>

namespace hikari::application {

namespace retiming {

namespace {

constexpr std::int64_t kUsPerMs = 1000;

// The Document's own MicroDVD rate; nullopt for other formats, 0 without one.
std::optional<double> microDvdFps(const core::Document &d)
{
    if (d.format() != core::SubtitleFormat::MicroDvd)
        return std::nullopt;
    if (!d.frameRate())
        return 0.0;
    const auto &fps = d.frameRate()->framesPerSecond();
    return static_cast<double>(fps.numerator()) / static_cast<double>(fps.denominator());
}

void newTime(core::TimeField &field, std::optional<std::int64_t> &frame, int ms, std::optional<double> fps)
{
    const int clamped = std::max(ms, 0);
    field.value = core::DocumentTime(std::int64_t{clamped} * kUsPerMs);
    if (fps && *fps > 0)
        frame = static_cast<std::int64_t>(std::ceil(static_cast<float>(clamped) * (static_cast<float>(*fps) / 1000.f)));
}

}

int msOf(core::DocumentTime t)
{
    return static_cast<int>(t.microseconds() / kUsPerMs);
}

int zeroIt(int ms)
{
    return (ms / 10) * 10;
}

std::expected<bool, CommandRefusal> retime(EditSession &session, const char *name,
                                           const std::map<core::LineId, Retime> &times)
{
    std::set<core::LineId> touches;
    for (const auto &[id, t] : times)
        touches.insert(id);
    const auto fps = microDvdFps(session.document());
    const auto selection = session.selection();
    bool changed = false, unchanged = false;
    const auto ran = session.run(Command{name, session.revision(), touches, [&](core::Document &d) {
                                             for (const auto &[id, t] : times)
                                                 if (!d.editLine(id, [&](core::LineRecord &l) {
                                                         const auto before = std::tuple(l.start.value, l.end.value,
                                                                                        l.startFrame, l.endFrame);
                                                         if (t.startMs)
                                                             newTime(l.start, l.startFrame, *t.startMs, fps);
                                                         if (t.endMs)
                                                             newTime(l.end, l.endFrame, *t.endMs, fps);
                                                         changed = changed || before != std::tuple(l.start.value, l.end.value,
                                                                                                   l.startFrame, l.endFrame);
                                                     }))
                                                     return false;
                                             unchanged = !changed;
                                             return changed;
                                         }});
    if (unchanged)
        return false;
    if (!ran)
        return std::unexpected(ran.error());
    session.setSelection(selection);
    return true;
}

}

namespace {

using retiming::msOf;
using retiming::Retime;
using retiming::zeroIt;

std::expected<void, CommandRefusal> retime(EditSession &session, const char *name,
                                           const std::map<core::LineId, Retime> &times)
{
    return retiming::retime(session, name, times).transform([](bool) {});
}

bool isShown(const ShownLine &shown, core::LineId id)
{
    return !shown || shown(id);
}

}

int legacyFrameTime(const LegacyTimebase &timebase, int tellMs, bool start)
{
    const int frame = timebase.frameShownAt(tellMs);
    return start ? timebase.startTimeFor(frame) : timebase.endTimeFor(frame);
}

int legacyInsertTimeFromVideo(const LegacyTimebase &timebase, int tellMs, bool start, int offsetMs)
{
    return zeroIt(legacyFrameTime(timebase, tellMs, start) + offsetMs);
}

std::expected<void, CommandRefusal> setTimesFromVideo(EditSession &session, bool end, int ms)
{
    if (session.draftLine())
        session.commitDraft();
    const auto lines = session.document().lines();
    const auto &selected = session.selection().selected;
    std::map<core::LineId, Retime> times;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto &l = *lines[i];
        if (!selected.contains(l.id))
            continue;
        auto &t = times[l.id];
        if (!end) {
            t.startMs = ms;
            if (msOf(l.end.value) < ms)
                t.endMs = ms;
        } else {
            t.endMs = ms;
            if (msOf(l.start.value) > ms)
                t.startMs = ms;
        }
    }
    if (times.empty())
        return std::unexpected(CommandRefusal::Invalid);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto &l = *lines[i];
        if (!selected.contains(l.id))
            continue;
        if (!end && i > 0) {
            const auto &before = *lines[i - 1];
            if (!selected.contains(before.id) && before.end.value == l.start.value) {
                auto &t = times[before.id];
                t.endMs = ms;
                if (msOf(before.start.value) > ms)
                    t.startMs = ms;
            }
        } else if (end && i + 1 < lines.size()) {
            const auto &after = *lines[i + 1];
            if (!selected.contains(after.id) && after.start.value == l.end.value) {
                auto &t = times[after.id];
                t.startMs = ms;
                if (msOf(after.end.value) < ms)
                    t.endMs = ms;
            }
        }
    }
    return retime(session, end ? "Setting end time" : "Setting start time", times);
}

std::expected<void, CommandRefusal> joinPreviousToAudioSelection(EditSession &session, int startMs, int endMs)
{
    if (session.draftLine())
        session.commitDraft();
    const auto active = session.selection().active;
    if (!active || endMs < startMs)
        return std::unexpected(CommandRefusal::Invalid);
    const auto lines = session.document().lines();
    const auto at = std::find_if(lines.begin(), lines.end(), [&](const core::LineRecord *l) { return l->id == *active; });
    if (at == lines.end())
        return std::unexpected(CommandRefusal::Invalid);
    std::map<core::LineId, Retime> times{{*active, Retime{startMs, endMs}}};
    if (at != lines.begin()) {
        const auto &before = **(at - 1);
        auto &t = times[before.id];
        t.endMs = startMs;
        if (msOf(before.start.value) > startMs)
            t.startMs = startMs;
    }
    return retime(session, "Joining with previous", times);
}

std::expected<void, CommandRefusal> shiftFramesToVideo(EditSession &session, const LegacyTimebase &timebase,
                                                       const FrameShift &shift)
{
    if (session.draftLine())
        session.commitDraft();
    const auto active = session.selection().active;
    if (!active)
        return std::unexpected(CommandRefusal::Invalid);
    const auto lines = session.document().lines();
    const auto at = std::find_if(lines.begin(), lines.end(), [&](const core::LineRecord *l) { return l->id == *active; });
    if (at == lines.end())
        return std::unexpected(CommandRefusal::Invalid);
    const auto frameOf = [&](core::DocumentTime t) { return timebase.frameAt(msOf(t)); };
    const int frames = shift.byEnd ? shift.videoFrame + 1 - frameOf((*at)->end.value)
                                   : shift.videoFrame - frameOf((*at)->start.value);
    const auto shifted = [&](core::DocumentTime t) { return zeroIt(timebase.startTimeFor(frameOf(t) + frames)); };
    const auto activeStart = (*at)->start.value;
    std::map<core::LineId, Retime> times;
    for (const auto *l : lines) {
        const bool moves = shift.following ? l->start.value >= activeStart : session.selection().selected.contains(l->id);
        if (moves || l->id == *active)
            times[l->id] = Retime{shifted(l->start.value), shifted(l->end.value)};
    }
    return retime(session, "Shifting times by frames", times);
}

std::optional<core::LineId> lineAtVideoTime(const core::Document &document, const ShownLine &shown, int tellMs,
                                            int durationMs)
{
    const auto lines = document.lines();
    const auto firstShown =
        std::find_if(lines.begin(), lines.end(), [&](const core::LineRecord *l) { return isShown(shown, l->id); });
    if (firstShown == lines.end())
        return std::nullopt;
    const int time = tellMs;
    int prevtime = 0;
    int durtime = durationMs;
    std::size_t idr = static_cast<std::size_t>(firstShown - lines.begin()), ip = idr;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto &dial = *lines[i];
        if (!isShown(shown, dial.id))
            continue;
        if (!dial.comment && (!dial.text.empty() || !dial.translation.empty())) {
            const int start = msOf(dial.start.value), end = msOf(dial.end.value);
            if (time >= start && time <= end)
                return dial.id;
            if (start > prevtime && start < time) {
                prevtime = start;
                ip = i;
            }
            if (start < durtime && start > time) {
                durtime = start;
                idr = i;
            }
        }
    }
    return (time - prevtime) > (durtime - time) ? lines[idr]->id : lines[ip]->id;
}

std::vector<core::LineId> linesShownOnVideo(const core::Document &document, const ShownLine &shown,
                                            const std::optional<core::LineRecord> &active, int tellMs)
{
    std::vector<core::LineId> found;
    for (const auto *line : document.lines()) {
        const core::LineRecord *dial = line;
        if (!isShown(shown, dial->id) || dial->unparsed || dial->comment)
            continue;
        if (active && active->id == dial->id)
            dial = &*active;
        if (tellMs >= msOf(dial->start.value) - 5 && tellMs < msOf(dial->end.value) - 5)
            found.push_back(dial->id);
    }
    return found;
}

std::optional<int> legacyKeyframeSnap(const KeyframeSnapContext &context, int startMs, int endMs, bool snapStart)
{
    const int time = snapStart ? startMs : endMs;
    const int time2 = snapStart ? endMs : startMs;
    int snaptime = time;
    int lastDifferents = INT_MAX;
    for (std::size_t k = 0; k < context.keyframesMs.size(); ++k) {
        const int keyMS = context.keyframesMs[k];
        if (keyMS >= time - 5000 && keyMS < time + 5000) {
            const int frameTime = zeroIt(k < context.keyframeSnapMs.size() ? context.keyframeSnapMs[k] : keyMS);
            const int actualDiff = std::abs(time - frameTime);
            if (actualDiff < lastDifferents && actualDiff > 0) {
                if ((snapStart && frameTime >= time2) || (!snapStart && frameTime <= time2))
                    continue;
                snaptime = frameTime;
                lastDifferents = actualDiff;
            }
        }
    }
    const int count = static_cast<int>(context.lines.size());
    if (context.inactiveLinesMode > 0) {
        int shadeFrom = 0, shadeTo = count;
        if (context.inactiveLinesMode == 1) {
            shadeFrom = legacyKeyFromPosition(context.lines, context.active, -1);
            shadeTo = legacyKeyFromPosition(context.lines, context.active, 1);
        }
        const int last = context.inactiveLinesMode == 1 ? shadeTo : shadeTo - 1;
        auto blocked = [&](int target) { return snapStart ? target >= time2 : target <= time2; };
        for (int j = std::max(shadeFrom, 0); j <= last && j < count; ++j) {
            const auto &shade = context.lines[static_cast<std::size_t>(j)];
            if (!shade.visible || j == context.active)
                continue;
            const int start = shade.startMs, end = shade.endMs;
            const int startDiff = std::abs(time - start);
            const int endDiff = std::abs(time - end);
            if (startDiff < lastDifferents && startDiff > 0 && !blocked(start)) {
                snaptime = start;
                lastDifferents = startDiff;
            }
            if (endDiff < lastDifferents && endDiff > 0 && !blocked(end)) {
                snaptime = end;
                lastDifferents = endDiff;
            }
        }
    }
    if (time != snaptime && std::abs(time - snaptime) < 5000) {
        if (snapStart ? snaptime >= time2 : snaptime <= time2)
            return std::nullopt;
        return snaptime;
    }
    return std::nullopt;
}

VideoFollow followShownLine(SeekAfter seek, PlayAfter play, VideoState state, bool rowChanged, bool noChangeLine,
                            bool autoPlay, bool audioBox, const FollowedLine &line)
{
    VideoFollow follow;
    if (seek == SeekAfter::EveryLineChange && play < PlayAfter::VideoToEnd && !noChangeLine && rowChanged &&
        state != VideoState::None) {
        follow.pause = state == VideoState::Playing;
        follow.seekMs = line.startMs;
    }
    if (play > PlayAfter::Nothing && autoPlay) {
        if (play == PlayAfter::AudioToEnd) {
            follow.playAudio = audioBox;
        } else if (state != VideoState::None) {
            const int ed = line.endMs, nst = line.nextStartMs;
            const int playend = (nst > ed && play > PlayAfter::VideoToEnd) ? nst : ed;
            follow.playVideo = std::pair(line.startMs, playend);
        }
    }
    return follow;
}

VideoFollow followGridPress(SeekAfter seek, PlayAfter play, VideoState state, const GridPress &press,
                            const FollowedLine &line)
{
    VideoFollow follow;
    const int choice = static_cast<int>(seek);
    const bool seeks = press.doubleClick ||
                       (press.rowChanged && (press.changeActive || !press.ctrl) && choice < 4 && choice > 0 &&
                        play < PlayAfter::VideoToEnd);
    if (!seeks || state == VideoState::None)
        return follow;
    if (state == VideoState::Stopped)
        follow.playThenPause = true;
    else if (state == VideoState::Playing && choice != 0)
        follow.pause = true;
    const bool getEndTime = press.endColumn && !press.tmPlayer;
    int vczas = getEndTime ? line.endMs : line.startMs;
    if (press.doubleClick && press.ctrl)
        vczas -= 1000;
    follow.seekMs = std::max(0, vczas);
    follow.seekStart = !getEndTime;
    follow.seekWhilePlaying = state == VideoState::Playing && !follow.pause;
    if (press.audioBox)
        follow.audioUpdate = getEndTime;
    return follow;
}

VideoFollow followEdit(SeekAfter seek, VideoState state, bool visualTool, const FollowedLine &line)
{
    VideoFollow follow;
    if (visualTool || seek <= SeekAfter::EveryLineChange)
        return follow;
    const bool playingSeeks = seek == SeekAfter::ClickOrEdit || seek == SeekAfter::Edit;
    if (state == VideoState::Paused || (state == VideoState::Playing && playingSeeks)) {
        follow.seekMs = line.startMs;
        follow.seekWhilePlaying = state == VideoState::Playing;
    }
    return follow;
}

}
