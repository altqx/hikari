#include "hikari/backends/transport_coordinator.h"

#include <algorithm>
#include <memory>

namespace hikari::backends {

using application::IndexedFrame;
using application::PresentOutcome;
using application::PresentResult;
using application::SourceError;

TransportCoordinator::TransportCoordinator(application::GeneralPlayerPort &general,
                                           application::IndexedSourcePort &indexed,
                                           application::AudioOutputPort &output,
                                           application::PresenterPort *presenter, QObject *parent)
    : QObject(parent), m_general(general), m_indexed(indexed), m_output(output), m_presenter(presenter)
{
}

std::int64_t TransportCoordinator::frameStartUs(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_timeline.pts.size()))
        return 0;
    // pts * num / den seconds, in microseconds, rounded.
    const long double us = static_cast<long double>(m_timeline.pts[static_cast<std::size_t>(index)]) *
                           m_timeline.timeBaseNumerator * 1'000'000.0L / m_timeline.timeBaseDenominator;
    return static_cast<std::int64_t>(us + (us < 0 ? -0.5L : 0.5L));
}

int TransportCoordinator::frameAt(std::int64_t us) const
{
    const int count = static_cast<int>(m_timeline.pts.size());
    if (count == 0 || us < frameStartUs(0))
        return -1;
    // The last frame starting at or before `us` (pts are presentation order).
    int lo = 0, hi = count - 1;
    while (lo < hi) {
        const int mid = (lo + hi + 1) / 2;
        if (frameStartUs(mid) <= us)
            lo = mid;
        else
            hi = mid - 1;
    }
    return lo;
}

void TransportCoordinator::settle(Acked &done, std::expected<HandoffAck, HandoffError> result)
{
    if (auto callback = std::exchange(done, nullptr))
        callback(std::move(result));
}

void TransportCoordinator::open(const std::string &path, int audioOrdinal, Opened done)
{
    ++m_handoff;
    settle(m_pending, std::unexpected(HandoffError::Superseded));
    m_mode = TransportMode::None;
    m_output.stop();
    auto shared = std::make_shared<Opened>(std::move(done));
    const std::uint64_t handoff = m_handoff;
    m_indexed.open(path, {}, [this, path, audioOrdinal, shared, handoff](auto timeline) {
        if (handoff != m_handoff)
            return (*shared)(std::unexpected(HandoffError::Superseded));
        if (!timeline)
            return (*shared)(std::unexpected(HandoffError::SourceFailure));
        m_timeline = *timeline;
        const int ordinal = std::clamp(audioOrdinal, 0, std::max(0, int(m_timeline.audioTracks.size()) - 1));
        auto openGeneral = [this, path, ordinal, shared, handoff] {
            m_general.open(path, [this, ordinal, shared, handoff](auto description) {
                if (handoff != m_handoff)
                    return (*shared)(std::unexpected(HandoffError::Superseded));
                if (!description)
                    return (*shared)(std::unexpected(HandoffError::SourceFailure));
                // libass draws subtitles; the player's own stay suppressed.
                m_general.selectSubtitleTrack(-1);
                if (!description->audioTracks.empty())
                    m_general.selectAudioTrack(std::min(ordinal, int(description->audioTracks.size()) - 1));
                m_mode = TransportMode::General;
                (*shared)(m_timeline);
            });
        };
        if (m_timeline.audioTracks.empty())
            return openGeneral();
        m_indexed.openAudio(m_timeline.audioTracks[static_cast<std::size_t>(ordinal)],
                            [openGeneral, shared, handoff, this](auto audio) {
                                if (handoff != m_handoff)
                                    return (*shared)(std::unexpected(HandoffError::Superseded));
                                if (!audio)
                                    return (*shared)(std::unexpected(HandoffError::SourceFailure));
                                openGeneral();
                            });
    });
}

void TransportCoordinator::toIndexed(Acked done)
{
    if (m_mode == TransportMode::None)
        return done(std::unexpected(HandoffError::NotOpen));
    if (m_mode != TransportMode::General)
        return done(std::unexpected(HandoffError::WrongMode));
    const std::uint64_t handoff = ++m_handoff;
    m_pending = std::move(done);
    m_mode = TransportMode::Switching;
    // The carried position is the last frame the player actually delivered.
    const std::int64_t carried = m_general.clock().mediaUs;
    m_general.stop(); // stops its audio and video, flushes, and resolves its seeks Stale
    const int index = std::max(0, frameAt(carried));
    HandoffAck ack;
    ack.to = TransportMode::Indexed;
    ack.handoff = handoff;
    ack.carriedUs = carried;
    ack.indexedFrame = index;
    ack.indexedFrameUs = frameStartUs(index);
    m_indexed.frame(index, [this, handoff, ack](std::expected<IndexedFrame, SourceError> frame) mutable {
        if (handoff != m_handoff)
            return; // a newer switch owns the transport now
        if (!frame) {
            m_mode = TransportMode::General;
            return settle(m_pending, std::unexpected(HandoffError::SourceFailure));
        }
        auto acknowledge = [this, handoff, ack] {
            if (handoff != m_handoff)
                return;
            m_indexedFrame = ack.indexedFrame;
            m_mode = TransportMode::Indexed;
            settle(m_pending, ack);
        };
        if (!m_presenter)
            return acknowledge();
        auto lease = std::make_shared<const IndexedFrame>(std::move(*frame));
        m_presenter->present({lease->generation, lease, nullptr, {}},
                             [this, handoff, acknowledge](PresentResult r) {
                                 if (handoff != m_handoff)
                                     return;
                                 if (r.outcome != PresentOutcome::Accepted) {
                                     m_mode = TransportMode::General;
                                     return settle(m_pending, std::unexpected(HandoffError::SourceFailure));
                                 }
                                 acknowledge();
                             });
    });
}

void TransportCoordinator::toGeneral(Acked done)
{
    if (m_mode == TransportMode::None)
        return done(std::unexpected(HandoffError::NotOpen));
    if (m_mode == TransportMode::General)
        return done(std::unexpected(HandoffError::WrongMode));
    const std::uint64_t handoff = ++m_handoff;
    settle(m_pending, std::unexpected(HandoffError::Superseded)); // a switch to indexed still in flight
    m_pending = std::move(done);
    m_mode = TransportMode::Switching;
    m_output.stop();       // the editor output stops and flushes
    m_indexed.cancelReads(); // pending indexed work resolves Cancelled
    const int index = std::max(0, m_indexedFrame);
    HandoffAck ack;
    ack.to = TransportMode::General;
    ack.handoff = handoff;
    ack.indexedFrame = index;
    ack.indexedFrameUs = frameStartUs(index);
    ack.carriedUs = ack.indexedFrameUs;
    m_general.seek(ack.carriedUs, [this, handoff, ack](auto result) mutable {
        if (handoff != m_handoff)
            return;
        if (!result) {
            m_mode = TransportMode::Indexed;
            return settle(m_pending, std::unexpected(HandoffError::SourceFailure));
        }
        ack.delivered = *result;
        m_mode = TransportMode::General;
        settle(m_pending, ack);
    });
}

} // namespace hikari::backends
