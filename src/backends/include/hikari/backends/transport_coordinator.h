#pragma once

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
    std::int64_t carriedUs = 0;
    int indexedFrame = -1;           // the resolved indexed frame (both directions)
    std::int64_t indexedFrameUs = 0;
    std::optional<application::SeekResult> delivered; // to general: what the player delivered
};

class TransportCoordinator : public QObject {
    Q_OBJECT
public:
    using Opened = std::function<void(std::expected<application::SourceTimeline, HandoffError>)>;
    using Acked = std::function<void(std::expected<HandoffAck, HandoffError>)>;

    // presenter may be null: a to-indexed switch is then acked when the frame arrives.
    TransportCoordinator(application::GeneralPlayerPort &general, application::IndexedSourcePort &indexed,
                         application::AudioOutputPort &output, application::PresenterPort *presenter = nullptr,
                         QObject *parent = nullptr);

    // Starts in general mode; audioOrdinal counts among the file's audio tracks (0: the first).
    void open(const std::string &path, int audioOrdinal, Opened done);
    void toIndexed(Acked done);
    void toGeneral(Acked done);

    TransportMode mode() const { return m_mode; }
    int indexedFrame() const { return m_indexedFrame; }
    const application::SourceTimeline &timeline() const { return m_timeline; }
    // The frame whose presentation interval holds us; -1 before the first frame or with no table.
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
    Acked m_pending;
};

}
