#include "hikari/app/application.h"
#include "hikari/application/center_times.h"

#include <memory>

namespace hikari::app {

using application::VideoSession;

namespace {

std::optional<core::LineRecord> committedLine(const application::EditSession &session, core::LineId id)
{
    for (const auto *line : session.document().lines())
        if (line->id == id)
            return *line;
    return std::nullopt;
}

int msOf(core::DocumentTime t)
{
    return static_cast<int>(t.microseconds() / 1000);
}

}

application::VideoState Application::videoState() const
{
    const auto &video = activeMedia().video->session();
    if (video.state() != VideoSession::State::Ready)
        return application::VideoState::None;
    if (video.playing())
        return application::VideoState::Playing;
    return video.stopped() ? application::VideoState::Stopped : application::VideoState::Paused;
}

std::optional<application::FollowedLine> Application::followedLine() const
{
    auto *session = targetSession();
    if (!session || !session->selection().active)
        return std::nullopt;
    return followedLine(*session->selection().active);
}

std::optional<application::FollowedLine> Application::followedLine(core::LineId id) const
{
    auto *session = targetSession();
    if (!session)
        return std::nullopt;
    auto record = session->draftLine() == id ? session->draftRecord() : committedLine(*session, id);
    if (!record)
        return std::nullopt;
    const auto shown = shownLines();
    std::vector<application::AudioLineSpan> spans;
    int position = -1;
    for (const auto *line : session->document().lines()) {
        if (line->id == id)
            position = static_cast<int>(spans.size());
        spans.push_back({msOf(line->start.value), msOf(line->end.value), shown(line->id)});
    }
    application::FollowedLine followed{msOf(record->start.value), msOf(record->end.value), 0};
    const int next = application::legacyKeyFromPosition(spans, position, 1);
    followed.nextStartMs = next >= 0 && next < static_cast<int>(spans.size()) ? spans[static_cast<std::size_t>(next)].startMs
                                                                             : followed.startMs;
    return followed;
}

void Application::applyVideoFollow(const application::VideoFollow &follow)
{
    auto &video = activeMedia().video->session();
    if (follow.playThenPause)
        video.unstop();
    if (follow.pause)
        video.pause();
    if (follow.seekMs) {
        const core::DocumentTime time(std::int64_t{*follow.seekMs} * 1000);
        if (follow.seekWhilePlaying)
            video.seekKeepPlaying(time, follow.seekStart);
        else if (!follow.seekStart)
            video.seekToEnd(time);
        else if (*follow.seekMs <= 0)
            video.showFrame(0);
        else
            video.seekTo(time);
    }
    if (follow.playVideo)
        video.playLine(follow.playVideo->first, follow.playVideo->second);
    if (follow.audioUpdate && activeMedia().audio)
        activeMedia().audio->followLine(*follow.audioUpdate);
    if (follow.playAudio && activeMedia().audio) {
        refreshAudio();
        activeMedia().audio->playSelection();
    }
}

void Application::followShownLine(bool rowChanged, LineChangeOrigin origin)
{
    const auto line = followedLine();
    if (!line)
        return;
    const auto seek = static_cast<application::SeekAfter>(m_settings->integer("video.moveToActiveLine"));
    const auto play = static_cast<application::PlayAfter>(m_settings->integer("video.playAfterSelection"));
    applyVideoFollow(application::followShownLine(seek, play, videoState(), rowChanged, origin.gridClick,
                                                  origin.autoPlay, activeMedia().audio && activeMedia().audio->hasAudio(), *line));
}

void Application::followActiveLine(bool rowChanged, bool edited, std::optional<core::LineId> left)
{
    // MUST KILL: the Line an edit left is followed before the new one, except on Enter.
    const bool nextLine = m_lineChangeOrigin && !m_lineChangeOrigin->gridClick && m_lineChangeOrigin->autoPlay;
    if (rowChanged && edited && left && !nextLine) {
        if (const auto line = followedLine(*left))
            followEditOn(*line);
        edited = false;
    }
    if (rowChanged) {
        const auto origin = std::exchange(m_lineChangeOrigin, std::nullopt);
        followShownLine(true, origin.value_or(LineChangeOrigin{}));
    }
    if (edited)
        if (const auto line = followedLine())
            followEditOn(*line);
}

void Application::followEditOn(const application::FollowedLine &line)
{
    const bool visualTool = activeViews().visualTools && visualTools().activeFamily() > 0;
    const auto seek = static_cast<application::SeekAfter>(m_settings->integer("video.moveToActiveLine"));
    applyVideoFollow(application::followEdit(seek, videoState(), visualTool, line));
}

void Application::followGridPress(std::optional<core::LineId> before, core::LineId line, int modifiers,
                                  bool endColumn, bool doubleClick)
{
    auto *session = targetSession();
    const auto followed = followedLine();
    if (!session || !followed)
        return;
    application::GridPress press;
    press.doubleClick = doubleClick;
    press.ctrl = modifiers & Qt::ControlModifier;
    press.rowChanged = before != line;
    press.changeActive = m_settings->boolean("grid.changeActiveOnSelection");
    press.endColumn = endColumn;
    press.tmPlayer = session->document().format() == core::SubtitleFormat::TMPlayer;
    press.audioBox = activeMedia().audio && activeMedia().audio->hasAudio();
    const auto seek = static_cast<application::SeekAfter>(m_settings->integer("video.moveToActiveLine"));
    const auto play = static_cast<application::PlayAfter>(m_settings->integer("video.playAfterSelection"));
    applyVideoFollow(application::followGridPress(seek, play, videoState(), press, *followed));
}

void Application::trackVideoFollow(WindowMedia &media)
{
    auto last = std::make_shared<VideoSession::State>(VideoSession::State::Closed);
    connect(media.video.get(), &ui::VideoController::changed, this, [this, last] {
        auto &video = activeMedia().video->session();
        const auto state = std::exchange(*last, video.state());
        if (video.state() != VideoSession::State::Opening || state == VideoSession::State::Opening)
            return;
        if (!m_settings->boolean("video.openAtActiveLine") || targetUntitled())
            return;
        if (const auto line = followedLine())
            video.seekTo(core::DocumentTime(std::int64_t{line->startMs} * 1000));
    });
}

bool Application::setTimeFromVideo(bool end)
{
    auto *session = targetSession();
    if (!session || videoState() == application::VideoState::None)
        return false;
    const auto &video = activeMedia().video->session();
    const int ms = application::legacyInsertTimeFromVideo(
        video.legacyTimebase(), video.tellMs(), !end,
        m_settings->integer(end ? "grid.insertEndOffset" : "grid.insertStartOffset"));
    const auto done = application::setTimesFromVideo(*session, end, ms);
    editor().reloadFromSession();
    refreshViews();
    return done.has_value();
}

bool Application::joinPreviousToAudioSelection()
{
    auto *session = targetSession();
    if (!session || !activeMedia().audio || !activeMedia().audio->ready())
        return false;
    const auto done =
        application::joinPreviousToAudioSelection(*session, activeMedia().audio->selectionStart(), activeMedia().audio->selectionEnd());
    editor().reloadFromSession();
    refreshViews();
    return done.has_value();
}

bool Application::shiftFramesToVideo(bool byEnd, bool following)
{
    auto *session = targetSession();
    if (!session || videoState() == application::VideoState::None)
        return false;
    const auto &video = activeMedia().video->session();
    const auto timebase = video.legacyTimebase();
    const auto done = application::shiftFramesToVideo(
        *session, timebase, application::FrameShift{timebase.frameShownAt(video.tellMs()), byEnd, following});
    editor().reloadFromSession();
    refreshViews();
    return done.has_value();
}

bool Application::selectLineFromVideo()
{
    auto *session = targetSession();
    if (!session || videoState() == application::VideoState::None)
        return false;
    const auto &video = activeMedia().video->session();
    const auto id = application::lineAtVideoTime(session->document(), shownLines(), video.tellMs(), video.durationMs());
    if (!id)
        return false;
    return applySelection(gridSelection().plain(session->selection(), *id));
}

bool Application::selectLinesVisibleOnVideo()
{
    auto *session = targetSession();
    if (!session)
        return false;
    const auto active = session->selection().active;
    std::optional<core::LineRecord> editorLine;
    if (active)
        editorLine = session->draftLine() == active ? session->draftRecord() : committedLine(*session, *active);
    const application::ShownLine shown = m_gridFilter->ignoreInActions() ? application::ShownLine{} : shownLines();
    const auto found = application::linesShownOnVideo(session->document(), shown, editorLine, activeMedia().video->session().tellMs());
    application::Selection next = session->selection();
    next.selected = std::set<core::LineId>(found.begin(), found.end());
    if (!found.empty())
        next.active = found.front();
    return applySelection(std::move(next));
}

bool Application::snapToKeyframe(bool start)
{
    auto *session = targetSession();
    if (!session || !activeMedia().audio || !activeMedia().audio->hasAudio() || !exactTimebase() || !session->selection().active)
        return false;
    const auto line = followedLine();
    if (!line)
        return false;
    const auto &video = activeMedia().video->session();
    const auto timebase = video.legacyTimebase();
    std::vector<int> keyframes, snapTimes;
    for (const int frame : video.keyframes()) {
        keyframes.push_back(timebase.msAt(frame));
        snapTimes.push_back(timebase.startTimeFor(timebase.frameAt(keyframes.back())));
    }
    const auto shown = shownLines();
    const auto active = *session->selection().active;
    std::vector<application::AudioLineSpan> spans;
    int position = -1;
    for (const auto *l : session->document().lines()) {
        if (l->id == active)
            position = static_cast<int>(spans.size());
        spans.push_back({msOf(l->start.value), msOf(l->end.value), shown(l->id)});
    }
    application::KeyframeSnapContext context;
    context.keyframesMs = keyframes;
    context.keyframeSnapMs = snapTimes;
    context.lines = spans;
    context.active = position;
    context.inactiveLinesMode = m_settings->integer("audio.inactiveLinesDisplayMode");
    const auto snapped = application::legacyKeyframeSnap(context, line->startMs, line->endMs, start);
    if (!snapped)
        return false;
    application::DraftChange change;
    (start ? change.start : change.end) = core::DocumentTime(std::int64_t{*snapped} * 1000);
    if (!session->editDraft(active, change))
        return false;
    const bool done = session->commitDraftAs("Snapping to keyframe");
    editor().reloadFromSession();
    refreshViews();
    return done;
}

bool Application::commitAndAdvance()
{
    m_lineChangeOrigin = LineChangeOrigin{false, true};
    // MUST KILL: the video refresh is held so the draft and the move reach the video at once.
    m_holdVideoRefresh = true;
    const bool done = editor().commitAndAdvance();
    m_holdVideoRefresh = false;
    refreshVideo();
    m_lineChangeOrigin.reset();
    return done;
}

}

namespace hikari::app {

bool Application::selectVisibleLineAfterFullScreen()
{
    if (!m_settings->boolean("grid.setVisibleLineAfterFullScreen") || targetUntitled())
        return false;
    editor().commit();
    return selectLineFromVideo();
}

bool Application::openEditorFromFullScreen()
{
    if (!m_videoFullscreen->active())
        return false;
    if (activeMedia().video->playing())
        activeMedia().video->pause();
    if (!targetUntitled())
        selectLineFromVideo();
    return m_videoFullscreen->leave();
}

bool Application::centerTimesInFrames()
{
    auto *session = targetSession();
    if (!session || videoState() == application::VideoState::None)
        return false;
    const auto done = application::centerTimesInFrames(*session, activeMedia().video->session().legacyTimebase());
    editor().reloadFromSession();
    refreshViews();
    return done.has_value();
}

}
