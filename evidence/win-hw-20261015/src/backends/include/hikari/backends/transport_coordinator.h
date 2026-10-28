#pragma once

// General <-> indexed transport handoff (I4; docs/qt/media.md, the accepted
// media handoff closure). One media file is open in both owners; only the
// active one plays and owns the clock. A switch stops and flushes the old
// owner, invalidates its pending work and clock, and acknowledges the
// destination before the new owner may start:
//   to indexed: the general player's last delivered frame time is resolved
//     through the FFMS2 timestamp table (never the player's approximate
//     position), that indexed frame is fetched and presented, then acked;
//   to general: the editor output is stopped and flushed, indexed reads are
//     cancelled, and the general player seeks to the indexed frame's time;
//     the ack reports the frame it actually delivered.
// Results from before a switch are rejected. The general player's embedded
// subtitles stay suppressed (libass draws subtitles) and both owners play the
// same audio track.

#include "hikari/application/audio_output.h"
#include "hikari/application/general_player.h"
#include "hikari/application/indexed_source.h"
#include "hikari/application/presenter.h"

#include <QObject>

#include <expected>
#include <functional>
#include <optional>

namespace hikari::backends {

enum class TransportMode { None, General, Indexed, Switching };

enum class HandoffError {
    NotOpen,
    WrongMode,
    Superseded,    // a newer switch replaced this one
    SourceFailure, // the destination could not produce its frame
};

struct HandoffAck {
    TransportMode to = TransportMode::None;
    std::uint64_t handoff = 0;       // which switch this acknowledges
    std::int64_t carriedUs = 0;      // the position carried over
    int indexedFrame = -1;           // the resolved indexed frame (both directions)
    std::int64_t indexedFrameUs = 0; // its presentation time
    std::optional<application::SeekResult> delivered; // to general: what the player delivered
};

class TransportCoordinator : public QObject {
    Q_OBJECT
public:
    using Opened = std::function<void(std::expected<application::SourceTimeline, HandoffError>)>;
    using Acked = std::function<void(std::expected<HandoffAck, HandoffError>)>;

    // The presenter is optional; without one, a to-indexed switch is acked
    // when the frame arrives.
    TransportCoordinator(application::GeneralPlayerPort &general, application::IndexedSourcePort &indexed,
                         application::AudioOutputPort &output, application::PresenterPort *presenter = nullptr,
                         QObject *parent = nullptr);

    // Opens `path` in both owners, starting in general mode. `audioOrdinal`
    // picks the audio track among the file's audio tracks (0: the first).
    void open(const std::string &path, int audioOrdinal, Opened done);
    void toIndexed(Acked done);
    void toGeneral(Acked done);

    TransportMode mode() const { return m_mode; }
    int indexedFrame() const { return m_indexedFrame; }
    const application::SourceTimeline &timeline() const { return m_timeline; }
    // The frame whose presentation interval contains `us`, by timestamp
    // lookup in the FFMS2 table; -1 before the first frame or with no table.
    int frameAt(std::int64_t us) const;
    std::int64_t frameStartUs(int index) const;

private:
    void settle(Acked &done, std::expected<HandoffAck, HandoffError> result);

    application::GeneralPlayerPort &m_general;
    application::IndexedSourcePort &m_indexed;
    application::AudioOutputPort &m_output;
    application::PresenterPort *m_presenter;
    TransportMode m_mode = TransportMode::None;
    application::SourceTimeline m_timeline;
    std::uint64_t m_handoff = 0;
    int m_indexedFrame = -1;
    Acked m_pending; // the switch in flight
};

} // namespace hikari::backends
