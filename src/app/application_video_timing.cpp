// V6: timing and selecting Lines from the video, and the video following the
// active Line (legacy at 20d647c4; the rules are in
// hikari/application/video_timing.h, this file feeds them the shell's state
// and carries out what they return).

#include "hikari/app/application.h"

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

} // namespace

application::VideoState Application::videoState() const
{
    // Legacy PlaybackState: None until a video is open, Paused once it is,
    // Stopped after Stop while playing.
    const auto &video = m_video->session();
    if (video.state() != VideoSession::State::Ready)
        return application::VideoState::None;
    if (video.playing())
        return application::VideoState::Playing;
    return video.stopped() ? application::VideoState::Stopped : application::VideoState::Paused;
}

// The edit box's Line (legacy edit->line: the active Line with its pending
// draft) and the next shown Line's start (GetKeyFromPosition(currentLine, 1)).
std::optional<application::FollowedLine> Application::followedLine() const
{
    auto *session = targetSession();
    if (!session || !session->selection().active)
        return std::nullopt;
    return followedLine(*session->selection().active);
}

// The same for any Line (the one an edit was committed on leaving).
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
    auto &video = m_video->session();
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
    if (follow.audioUpdate && m_audio)
        m_audio->followLine(*follow.audioUpdate);
    if (follow.playAudio && m_audio) {
        refreshAudio(); // the box shows the new Line before it plays its selection
        m_audio->playSelection();
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
                                                  origin.autoPlay, m_audio && m_audio->hasAudio(), *line));
}

void Application::followActiveLine(bool rowChanged, bool edited, std::optional<core::LineId> left)
{
    // An edit committed because the active Line changed. EditBox::SetLine
    // sends the old Line first (EditBox.cpp:383-384), so ShowEditOnVideo
    // follows the edited Line before the new one is shown. Enter is the
    // other way round: SubsGrid::ChangeLine runs NextLine before SetModified
    // (SubsGridBase.cpp:150-154), so ShowEditOnVideo follows the new Line.
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
    // ShowEditOnVideo: `edit->Visual < CHANGEPOS` (no tool, or the crosshair)
    const bool visualTool = m_visualTools && m_visualTools->activeFamily() > 0;
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
    press.audioBox = m_audio && m_audio->hasAudio();
    const auto seek = static_cast<application::SeekAfter>(m_settings->integer("video.moveToActiveLine"));
    const auto play = static_cast<application::PlayAfter>(m_settings->integer("video.playAfterSelection"));
    applyVideoFollow(application::followGridPress(seek, play, videoState(), press, *followed));
}

void Application::trackVideoFollow()
{
    // OPEN_VIDEO_AT_ACTIVE_LINE (VideoBox::LoadVideo, VideoBox.cpp:407-410):
    // a video opened for a Document with a file starts at the active Line's
    // start; otherwise at its first frame. The seek is made when the open
    // starts and applies once the video is indexed.
    auto last = std::make_shared<VideoSession::State>(VideoSession::State::Closed);
    connect(m_video.get(), &ui::VideoController::changed, this, [this, last] {
        auto &video = m_video->session();
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
    // HikariSubFrame.cpp:750-751: only with a video (the menu item: also the editor)
    if (!session || videoState() == application::VideoState::None)
        return false;
    const auto &video = m_video->session();
    const int ms = application::legacyInsertTimeFromVideo(
        video.legacyTimebase(), video.tellMs(), !end,
        m_settings->integer(end ? "grid.insertEndOffset" : "grid.insertStartOffset"));
    const auto done = application::setTimesFromVideo(*session, end, ms);
    m_editor->reloadFromSession();
    refreshViews();
    return done.has_value();
}

bool Application::selectLineFromVideo()
{
    auto *session = targetSession();
    // SubsGrid::SelVideoLine: nothing without a video
    if (!session || videoState() == application::VideoState::None)
        return false;
    const auto &video = m_video->session();
    const auto id = application::lineAtVideoTime(session->document(), shownLines(), video.tellMs(), video.durationMs());
    if (!id)
        return false;
    // edit->SetLine(i); SelectRow(i); MakeVisible(i)
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
    // VideoBox::Tell is 0 without a video; "Ignore filtering in some actions"
    // takes the hidden Lines too.
    const application::ShownLine shown = m_gridFilter->ignoreInActions() ? application::ShownLine{} : shownLines();
    const auto found = application::linesShownOnVideo(session->document(), shown, editorLine, m_video->session().tellMs());
    application::Selection next = session->selection();
    next.selected = std::set<core::LineId>(found.begin(), found.end());
    if (!found.empty())
        next.active = found.front(); // edit->SetLine(FirstSelection())
    return applySelection(std::move(next));
}

bool Application::snapToKeyframe(bool start)
{
    auto *session = targetSession();
    // HikariSubFrame.cpp:2531: an audio box and an exact (FFMS2) timebase
    if (!session || !m_audio || !m_audio->hasAudio() || !exactTimebase() || !session->selection().active)
        return false;
    const auto line = followedLine();
    if (!line)
        return false;
    const auto &video = m_video->session();
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
    // StartEdit / EndEdit get the time, then Send(SNAP_TO_KEYFRAME_OR_LINE_TIME):
    // the edit box's Line is committed as "Snapping to keyframe".
    application::DraftChange change;
    (start ? change.start : change.end) = core::DocumentTime(std::int64_t{*snapped} * 1000);
    if (!session->editDraft(active, change))
        return false;
    const bool done = session->commitDraftAs("Snapping to keyframe");
    m_editor->reloadFromSession();
    refreshViews();
    return done;
}

bool Application::commitAndAdvance()
{
    m_lineChangeOrigin = LineChangeOrigin{false, true}; // SubsGrid::NextLine: SetLine(..., autoPlay)
    // E4 sends the draft before the move (EditBox::Send, then NextLine); the
    // video sees both at once, as ShowEditOnVideo ran after NextLine
    // (SubsGrid::ChangeLine, SubsGridBase.cpp:150-154).
    m_holdVideoRefresh = true;
    const bool done = m_editor->commitAndAdvance();
    m_holdVideoRefresh = false;
    refreshVideo();
    m_lineChangeOrigin.reset();
    return done;
}

} // namespace hikari::app

namespace hikari::app {

bool Application::selectVisibleLineAfterFullScreen()
{
    // VideoBox.cpp:555-559: `tab->SubsPath != emptyString &&
    // Options.GetBool(GRID_SET_VISIBLE_LINE_AFTER_FULL_SCREEN)`.
    if (!m_settings->boolean("grid.setVisibleLineAfterFullScreen") || targetUntitled())
        return false;
    m_editor->commit(); // tab->edit->Send(EDITBOX_LINE_EDITION, false)
    return selectLineFromVideo();
}

bool Application::openEditorFromFullScreen()
{
    // VideoBox::OpenEditor (esc defaults to true: the window is not minimized).
    if (!m_videoFullscreen->active())
        return false;
    if (m_video->playing())
        m_video->pause();
    // Legacy set EDITOR_ON only, leaving a player layout (tab->editor off)
    // until the next start; the rewrite's switch is that setting (D2), so
    // the editor comes back now.
    if (!editorOn())
        toggleEditor();
    if (!targetUntitled())
        selectLineFromVideo(); // tab->grid->SelVideoLine()
    return m_videoFullscreen->leave();
}

} // namespace hikari::app
