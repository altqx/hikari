#include "hikari/application/video_session.h"

#include <algorithm>
#include <utility>

namespace hikari::application {

VideoSession::VideoSession(IndexedSourcePort &source, SubtitleRendererPort &renderer)
    : m_source(source), m_renderer(renderer)
{
}

void VideoSession::notify()
{
    if (m_observer)
        m_observer();
}

void VideoSession::setPresenter(PresenterPort *presenter)
{
    m_presenter = presenter;
    present();
}

void VideoSession::open(const std::string &path, IndexRequest index)
{
    // A Line seek made before the video opened still applies to it.
    const auto pendingSeek = m_pendingSeek;
    close();
    m_pendingSeek = pendingSeek;
    m_path = path;
    m_state = State::Opening;
    const std::weak_ptr<bool> alive = m_alive;
    const int chosen = index.audioTrack;
    m_source.openIndexed(path, index, nullptr, [this, alive, chosen](std::expected<SourceTimeline, SourceError> opened) {
        if (alive.expired() || m_state != State::Opening)
            return;
        if (!opened) {
            m_state = State::Failed;
            m_error = opened.error();
            return notify();
        }
        if (opened->generation != m_source.generation())
            return; // a later open replaced it
        m_starts.clear();
        bool exact = opened->timeBaseDenominator > 0;
        for (const std::int64_t pts : opened->pts) {
            const auto us = core::mulDiv(pts, opened->timeBaseNumerator * 1'000'000, opened->timeBaseDenominator, 1,
                                         core::Rounding::Floor);
            exact = exact && us.has_value();
            m_starts.emplace_back(us.value_or(0));
        }
        if (!exact || m_starts.empty()) {
            m_state = State::Failed;
            m_error = SourceError::Unsupported;
            m_starts.clear();
            return notify();
        }
        m_timeline = core::FrameTimeline::indexed(m_starts);
        m_keyframes = opened->keyframes;
        m_hasAudio = opened->firstAudioTrack >= 0;
        m_audioTrack = chosen >= 0 ? chosen : opened->firstAudioTrack;
        const auto ordinal = std::find(opened->audioTracks.begin(), opened->audioTracks.end(), m_audioTrack);
        m_audioOrdinal = ordinal == opened->audioTracks.end() ? -1
                                                              : static_cast<int>(ordinal - opened->audioTracks.begin());
        m_newIndex = opened->newIndex;
        m_fps = opened->fpsDenominator > 0 ? static_cast<double>(opened->fpsNumerator) / static_cast<double>(opened->fpsDenominator) : 0;
        m_state = State::Ready;
        notify();
        if (const auto seek = std::exchange(m_pendingSeek, std::nullopt))
            seekTo(*seek);
        else
            showFrame(m_requested.value_or(0));
    });
    notify();
}

void VideoSession::close()
{
    if (m_player && m_playing)
        m_player->stop();
    m_playing = false;
    m_stopped = false;
    ++m_playEpoch;
    m_lastGeneralUs.reset();
    m_overlayTime.reset();
    if (m_state == State::Opening)
        m_source.cancelOpen();
    m_source.cancelReads();
    m_state = State::Closed;
    m_path.clear();
    m_error.reset();
    m_starts.clear();
    m_keyframes.clear();
    m_hasAudio = false;
    m_audioTrack = -1;
    m_audioOrdinal = -1;
    m_newIndex = true;
    m_timeline.reset();
    m_requested.reset();
    m_pendingSeek.reset();
    m_shown.reset();
    m_overlay.reset();
    m_lastPresent.reset();
    ++m_request;
    notify();
}

void VideoSession::setSubtitles(std::vector<std::byte> script)
{
    RenderSnapshot snapshot;
    snapshot.script = std::move(script);
    m_hasSubtitles = m_renderer.prepare(std::move(snapshot)).has_value();
    if (m_shown) {
        render();
        present();
    }
}

std::optional<core::DocumentTime> VideoSession::frameStart(int index) const
{
    if (index < 0 || index >= frameCount())
        return std::nullopt;
    return m_starts[static_cast<std::size_t>(index)];
}

void VideoSession::seekTo(core::DocumentTime start)
{
    if (!m_timeline) {
        m_pendingSeek = start; // applied once the source is indexed
        return;
    }
    const auto frame = m_timeline->frameAtOrAfter(start);
    // Past the last frame start: the last frame.
    showFrame(frame ? static_cast<int>(frame->value()) : frameCount() - 1);
}

bool VideoSession::step(int frames)
{
    if (m_state != State::Ready || !m_requested)
        return false;
    const int target = *m_requested + frames;
    if (target < 0 || target >= frameCount())
        return false;
    showFrame(target);
    return true;
}

void VideoSession::showFrame(int index)
{
    m_requested = index;
    if (m_state != State::Ready || index < 0 || index >= frameCount())
        return notify();
    const std::uint64_t request = ++m_request;
    const std::weak_ptr<bool> alive = m_alive;
    m_source.frame(index, [this, alive, request](std::expected<IndexedFrame, SourceError> frame) {
        if (alive.expired() || request != m_request)
            return; // superseded or closed
        if (!frame) {
            if (frame.error() != SourceError::Stale && frame.error() != SourceError::Cancelled) {
                m_error = frame.error();
                notify();
            }
            return;
        }
        if (m_playing)
            return; // playback shows the player's frames
        m_shown = std::make_shared<const IndexedFrame>(std::move(*frame));
        m_overlayTime.reset();
        m_lastPresent.reset();
        render();
        present();
        notify();
    });
    notify();
}

void VideoSession::requestFrame(int index, bool withSubtitles,
                                std::function<void(std::shared_ptr<const IndexedFrame>)> done)
{
    if (m_state != State::Ready || index < 0 || index >= frameCount())
        return done(nullptr);
    const std::weak_ptr<bool> alive = m_alive;
    m_source.frame(index, [this, alive, index, withSubtitles, done](std::expected<IndexedFrame, SourceError> frame) {
        if (alive.expired() || !frame)
            return done(nullptr);
        if (withSubtitles && m_hasSubtitles)
            if (const auto start = frameStart(index))
                if (auto overlay = m_renderer.render(*start, frame->width, frame->height)) {
                    // Premultiplied overlay over the opaque frame.
                    for (int y = 0; y < frame->height; ++y) {
                        auto *dst = reinterpret_cast<std::uint8_t *>(frame->bgra.data()) + y * frame->stride;
                        const auto *src = overlay->pixels.data() + y * overlay->stride;
                        for (int x = 0; x < frame->width * 4; x += 4) {
                            const unsigned a = src[x + 3];
                            for (int c = 0; c < 3; ++c)
                                dst[x + c] = static_cast<std::uint8_t>(src[x + c] + dst[x + c] * (255 - a) / 255);
                        }
                    }
                }
        done(std::make_shared<const IndexedFrame>(std::move(*frame)));
    });
}

void VideoSession::render()
{
    m_overlay.reset();
    if (!m_hasSubtitles || !m_shown)
        return;
    const auto start = m_overlayTime ? m_overlayTime : frameStart(m_shown->index);
    if (!start)
        return;
    if (auto overlay = m_renderer.render(*start, m_shown->width, m_shown->height))
        m_overlay = std::make_shared<const OverlayFrame>(std::move(*overlay));
}

void VideoSession::present()
{
    if (!m_presenter || !m_shown)
        return;
    Presentation p;
    p.generation = m_shown->generation;
    p.frame = m_shown;
    p.overlay = m_overlay;
    const std::weak_ptr<bool> alive = m_alive;
    const auto shown = m_shown;
    m_presenter->present(std::move(p), [this, alive, shown](PresentResult result) {
        if (alive.expired() || shown != m_shown)
            return;
        m_lastPresent = result;
        notify();
    });
}

bool VideoSession::play()
{
    if (!m_player || m_state != State::Ready || m_playing)
        return false;
    m_playing = true;
    m_stopped = false;
    const std::uint64_t epoch = ++m_playEpoch;
    const std::int64_t fromUs = m_shown ? frameStart(m_shown->index).value_or(core::DocumentTime(0)).microseconds()
                                        : 0;
    const std::weak_ptr<bool> alive = m_alive;
    auto start = [this, alive, epoch, fromUs] {
        m_player->seek(fromUs, [this, alive, epoch](std::expected<SeekResult, PlayerError> sought) {
            if (alive.expired() || epoch != m_playEpoch || !m_playing)
                return;
            if (!sought) {
                m_playing = false;
                return notify();
            }
            m_player->play();
        });
    };
    if (m_playerPath == m_path) {
        if (m_audioOrdinal >= 0) // the same file reopened may have another track chosen
            m_player->selectAudioTrack(m_audioOrdinal);
        start();
    } else {
        m_player->open(m_path, [this, alive, epoch, start](std::expected<MediaDescription, PlayerError> opened) {
            if (alive.expired() || epoch != m_playEpoch || !m_playing)
                return;
            if (!opened) {
                m_playing = false;
                return notify();
            }
            m_playerPath = m_path;
            // A1: the video's chosen audio track (legacy played the box's)
            if (m_audioOrdinal >= 0 && m_audioOrdinal < static_cast<int>(opened->audioTracks.size()))
                m_player->selectAudioTrack(m_audioOrdinal);
            start();
        });
    }
    notify();
    return true;
}

void VideoSession::generalFrame(IndexedFrame frame, std::int64_t startUs)
{
    if (!m_playing)
        return; // a late frame after a pause or stop
    m_lastGeneralUs = startUs;
    frame.generation = m_source.generation();
    m_shown = std::make_shared<const IndexedFrame>(std::move(frame));
    m_overlayTime = core::DocumentTime(startUs);
    m_lastPresent.reset();
    render();
    present();
    notify();
}

bool VideoSession::pause()
{
    if (!m_playing)
        return false;
    m_player->pause();
    m_playing = false;
    ++m_playEpoch;
    // The indexed frame whose interval holds the last delivered frame's start.
    int index = m_requested.value_or(0);
    if (m_lastGeneralUs && m_timeline)
        if (const auto frame = m_timeline->frameContaining(core::DocumentTime(*m_lastGeneralUs)))
            index = static_cast<int>(frame->value());
    showFrame(index);
    return true;
}

bool VideoSession::stop()
{
    if (m_state != State::Ready)
        return false;
    if (m_playing) {
        m_player->pause();
        m_playing = false;
        m_stopped = true; // legacy Stop acts only while Playing
        ++m_playEpoch;
    }
    showFrame(0);
    return true;
}

namespace {

std::int64_t msOf(core::DocumentTime t)
{
    return t.microseconds() / 1000;
}

} // namespace

void VideoSession::seekToEnd(core::DocumentTime end)
{
    if (!m_timeline)
        return;
    // FrameShownAt(end - 1 ms), clamped.
    const core::DocumentTime before(std::max<std::int64_t>(0, end.microseconds() - 1000));
    const auto frame = m_timeline->frameContaining(before);
    showFrame(frame ? static_cast<int>(frame->value()) : (before.microseconds() <= 0 ? 0 : frameCount() - 1));
}

bool VideoSession::seekBy(std::int64_t ms)
{
    if (m_state != State::Ready || !m_requested)
        return false;
    // Tell() is the shown frame's start in whole milliseconds.
    const std::int64_t target = msOf(m_starts[static_cast<std::size_t>(*m_requested)]) + ms;
    if (target <= 0) {
        showFrame(0);
        return true;
    }
    seekTo(core::DocumentTime(target * 1000));
    return true;
}

LegacyTimebase VideoSession::legacyTimebase() const
{
    if (m_state != State::Ready)
        return {};
    std::vector<int> timecodes;
    timecodes.reserve(m_starts.size());
    for (const auto start : m_starts)
        timecodes.push_back(static_cast<int>(start.microseconds() / 1000));
    return LegacyTimebase(std::move(timecodes), m_fps);
}

bool VideoSession::isKeyframe(int index) const
{
    return std::binary_search(m_keyframes.begin(), m_keyframes.end(), index);
}

bool VideoSession::nextKeyframe()
{
    if (m_state != State::Ready || !m_requested || m_keyframes.empty())
        return false;
    // Timebase::NextKeyframe on keyframe times in ms: the first after now, else the first.
    const std::int64_t now = msOf(m_starts[static_cast<std::size_t>(*m_requested)]);
    int target = m_keyframes.front();
    for (const int k : m_keyframes)
        if (msOf(m_starts[static_cast<std::size_t>(k)]) > now) {
            target = k;
            break;
        }
    showFrame(target);
    return true;
}

bool VideoSession::previousKeyframe()
{
    if (m_state != State::Ready || !m_requested || m_keyframes.empty())
        return false;
    // Timebase::PrevKeyframe: the last before now, else the last.
    const std::int64_t now = msOf(m_starts[static_cast<std::size_t>(*m_requested)]);
    int target = m_keyframes.back();
    for (auto it = m_keyframes.rbegin(); it != m_keyframes.rend(); ++it)
        if (msOf(m_starts[static_cast<std::size_t>(*it)]) < now) {
            target = *it;
            break;
        }
    showFrame(target);
    return true;
}

void VideoSession::setKeyframes(std::vector<int> frames)
{
    std::sort(frames.begin(), frames.end());
    frames.erase(std::unique(frames.begin(), frames.end()), frames.end());
    m_keyframes = std::move(frames);
}

} // namespace hikari::application
