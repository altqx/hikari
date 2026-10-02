#pragma once

// Plays a Line's half-open source range through the editor output (I3,
// M50-audio): a resampled PCM stream from the media helper at the output's
// format, kept a little ahead of the device. The report keeps three moments
// apart: the logical stop (the last frame handed to the output queue), the
// callback that took the last frame, and the estimated audible end (that
// frame's DAC time). The callback itself keeps running on silence; its
// cessation is not the end of the sound.

#include "hikari/application/audio_output.h"
#include "hikari/application/indexed_source.h"

#include <QObject>
#include <QString>
#include <QTimer>

#include <optional>
#include <vector>

namespace hikari::backends {

struct AuditionReport {
    std::int64_t sourceStart = 0, sourceCount = 0;
    int outputRate = 0, outputChannels = 0;
    std::int64_t outputFrames = 0;    // frames the stream yielded and the device took
    double startedAt = 0;             // monotonic seconds: the first frames were queued
    double logicalStopAt = 0;         // the last frame was handed to the output queue
    double lastFrameConsumedAt = 0;   // the callback that took the last frame
    double audibleEndAt = 0;          // estimated: when the last frame finishes sounding
    double outputLatency = 0;         // audibleEnd minus that callback's time
    std::uint64_t underruns = 0;      // during the audition
    bool drained = false;             // false when stopped before the last frame was taken
};

class LineAudition : public QObject {
    Q_OBJECT
public:
    // The output must be open; the audition starts it when audio is ready.
    LineAudition(application::IndexedSourcePort &source, application::AudioOutputPort &output,
                 QObject *parent = nullptr);

    // Source frames [start, start + count) of the open audio track.
    bool play(std::int64_t start, std::int64_t count);
    // A logical stop now: queued audio is discarded; the report says not drained.
    void stop();
    bool active() const { return m_active; }

signals:
    void finished(const hikari::backends::AuditionReport &report);
    void failed(const QString &reason);

private:
    void pump();
    void finish(bool drained);

    application::IndexedSourcePort &m_source;
    application::AudioOutputPort &m_output;
    QTimer m_timer;
    bool m_active = false;
    bool m_requestInFlight = false;
    bool m_streamEnded = false;
    bool m_started = false;
    std::uint64_t m_generation = 0; // ignores answers from an earlier audition
    std::uint64_t m_baseConsumed = 0;
    std::uint64_t m_baseUnderruns = 0;
    std::int64_t m_written = 0;      // frames accepted by the output
    std::vector<float> m_pending;    // a chunk the output had no room for yet
    AuditionReport m_report;
};

} // namespace hikari::backends
