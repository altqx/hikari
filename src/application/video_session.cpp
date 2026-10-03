#include "hikari/application/video_session.h"

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

void VideoSession::open(const std::string &path)
{
    // A Line seek made before the video opened still applies to it.
    const auto pendingSeek = m_pendingSeek;
    close();
    m_pendingSeek = pendingSeek;
    m_path = path;
    m_state = State::Opening;
    const std::weak_ptr<bool> alive = m_alive;
    m_source.open(path, nullptr, [this, alive](std::expected<SourceTimeline, SourceError> opened) {
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
    if (m_state == State::Opening)
        m_source.cancelOpen();
    m_source.cancelReads();
    m_state = State::Closed;
    m_path.clear();
    m_error.reset();
    m_starts.clear();
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
        m_shown = std::make_shared<const IndexedFrame>(std::move(*frame));
        m_lastPresent.reset();
        render();
        present();
        notify();
    });
    notify();
}

void VideoSession::render()
{
    m_overlay.reset();
    if (!m_hasSubtitles || !m_shown)
        return;
    const auto start = frameStart(m_shown->index);
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

} // namespace hikari::application
