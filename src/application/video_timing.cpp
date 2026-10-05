#include "hikari/application/video_timing.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <set>

namespace hikari::application {

namespace {

constexpr std::int64_t kUsPerMs = 1000;

int msOf(core::DocumentTime t)
{
    return static_cast<int>(t.microseconds() / kUsPerMs);
}

// Legacy ZEROIT (SubsDialogue.h:20): down to 10 ms, truncated towards zero.
int zeroIt(int ms)
{
    return (ms / 10) * 10;
}

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

// SubsTime::NewTime (SubsTime.cpp:132-140): clamped at 0; MicroDVD frames
// follow as ceil(ms * fps / 1000) in float.
void newTime(core::TimeField &field, std::optional<std::int64_t> &frame, int ms, std::optional<double> fps)
{
    const int clamped = std::max(ms, 0);
    field.value = core::DocumentTime(std::int64_t{clamped} * kUsPerMs);
    if (fps && *fps > 0)
        frame = static_cast<std::int64_t>(std::ceil(static_cast<float>(clamped) * (static_cast<float>(*fps) / 1000.f)));
}

bool isShown(const ShownLine &shown, core::LineId id)
{
    return !shown || shown(id);
}

} // namespace

int legacyFrameTime(const LegacyTimebase &timebase, int tellMs, bool start)
{
    // VideoBox.cpp:1688-1693
    const int frame = timebase.frameShownAt(tellMs);
    return start ? timebase.startTimeFor(frame) : timebase.endTimeFor(frame);
}

int legacyInsertTimeFromVideo(const LegacyTimebase &timebase, int tellMs, bool start, int offsetMs)
{
    // HikariSubFrame.cpp:753-758: ZEROIT(GetFrameTime(start) + offset)
    return zeroIt(legacyFrameTime(timebase, tellMs, start) + offsetMs);
}

std::expected<void, CommandRefusal> setTimesFromVideo(EditSession &session, bool end, int ms)
{
    // SubsGridBase.cpp:1281: edit->Send(EDITBOX_LINE_EDITION, ...) commits the
    // editor's Line first, as its own step.
    if (session.draftLine())
        session.commitDraft();
    std::set<core::LineId> chosen;
    for (const auto *l : session.document().lines())
        if (session.selection().selected.contains(l->id))
            chosen.insert(l->id);
    if (chosen.empty())
        return std::unexpected(CommandRefusal::Invalid); // `if (sels.size())`: nothing recorded
    const auto fps = microDvdFps(session.document());
    const auto selection = session.selection();
    const auto ran = session.run(Command{end ? "Setting end time" : "Setting start time", session.revision(), chosen,
                                         [&](core::Document &d) {
                                             for (const auto id : chosen)
                                                 if (!d.editLine(id, [&](core::LineRecord &l) {
                                                         if (!end) {
                                                             // SubsGridBase.cpp:1287-1288
                                                             newTime(l.start, l.startFrame, ms, fps);
                                                             if (msOf(l.end.value) < ms) // `End < stime`: SubsTime(stime), unclamped
                                                                 newTime(l.end, l.endFrame, ms, fps);
                                                         } else {
                                                             // SubsGridBase.cpp:1303-1304
                                                             newTime(l.end, l.endFrame, ms, fps);
                                                             if (msOf(l.start.value) > ms)
                                                                 newTime(l.start, l.startFrame, ms, fps);
                                                         }
                                                     }))
                                                     return false;
                                             return true;
                                         }});
    if (!ran)
        return std::unexpected(ran.error());
    session.setSelection(selection);
    return {};
}

std::optional<core::LineId> lineAtVideoTime(const core::Document &document, const ShownLine &shown, int tellMs,
                                            int durationMs)
{
    // SubsGridWindow.cpp:1982-2022
    const auto lines = document.lines();
    if (lines.empty())
        return std::nullopt;
    const int time = tellMs;
    int prevtime = 0;
    int durtime = durationMs;
    std::size_t idr = 0, ip = 0;
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
    // SubsGridBase.cpp:1601-1625
    std::vector<core::LineId> found;
    for (const auto *line : document.lines()) {
        const core::LineRecord *dial = line;
        // `!ignoreFiltered && !dial->isVisible || dial->NonDialogue || dial->IsComment`
        if (!isShown(shown, dial->id) || dial->unparsed || dial->comment)
            continue;
        if (active && active->id == dial->id)
            dial = &*active; // the edit box's Line
        if (tellMs >= msOf(dial->start.value) - 5 && tellMs < msOf(dial->end.value) - 5)
            found.push_back(dial->id);
    }
    return found;
}

std::optional<int> legacyKeyframeSnap(const KeyframeSnapContext &context, int startMs, int endMs, bool snapStart)
{
    // HikariSubFrame.cpp:2533-2591
    const int time = snapStart ? startMs : endMs;
    const int time2 = snapStart ? endMs : startMs;
    int snaptime = time;
    int lastDifferents = INT_MAX; // MAXINT
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
        for (int j = std::max(shadeFrom, 0); j < shadeTo && j < count; ++j) {
            const auto &shade = context.lines[static_cast<std::size_t>(j)];
            if (!shade.visible || j == context.active)
                continue;
            const int start = shade.startMs, end = shade.endMs;
            const int startDiff = std::abs(time - start);
            const int endDiff = std::abs(time - end);
            if (startDiff < lastDifferents && startDiff > 0) {
                // legacy `continue`s here, so the End is not tried either
                if ((snapStart && start >= time2) || (!snapStart && start <= time2))
                    continue;
                snaptime = start;
                lastDifferents = startDiff;
            }
            if (endDiff < lastDifferents && endDiff > 0) {
                if ((snapStart && end >= time2) || (!snapStart && end <= time2))
                    continue;
                snaptime = end;
                lastDifferents = endDiff;
            }
        }
    }
    // HikariSubFrame.cpp:2593-2601
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
    // EditBox.cpp:454-460
    if (seek == SeekAfter::EveryLineChange && play < PlayAfter::VideoToEnd && !noChangeLine && rowChanged &&
        state != VideoState::None) {
        follow.pause = state == VideoState::Playing;
        follow.seekMs = line.startMs;
    }
    // EditBox.cpp:464-480
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
    // SubsGridWindow.cpp:1647: dclick || (click && lastActiveLine != row &&
    // (changeActive || !ctrl) && seekAfter < 4 && seekAfter > 0) && playAfter < 2
    const int choice = static_cast<int>(seek);
    const bool seeks = press.doubleClick ||
                       (press.rowChanged && (press.changeActive || !press.ctrl) && choice < 4 && choice > 0 &&
                        play < PlayAfter::VideoToEnd);
    if (!seeks || state == VideoState::None)
        return follow;
    // SetVideoLineTime, SubsGridWindow.cpp:1376-1397
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
    if (press.audioBox) // SubsGridWindow.cpp:1398
        follow.audioUpdate = getEndTime;
    return follow;
}

VideoFollow followEdit(SeekAfter seek, VideoState state, bool visualTool, const FollowedLine &line)
{
    VideoFollow follow;
    // SubsGridBase.cpp:1168-1177
    if (visualTool || seek <= SeekAfter::EveryLineChange)
        return follow;
    const bool playingSeeks = seek == SeekAfter::ClickOrEdit || seek == SeekAfter::Edit;
    if (state == VideoState::Paused || (state == VideoState::Playing && playingSeeks)) {
        follow.seekMs = line.startMs;
        follow.seekWhilePlaying = state == VideoState::Playing;
    }
    return follow;
}

} // namespace hikari::application
