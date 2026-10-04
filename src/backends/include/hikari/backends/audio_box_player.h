#pragma once

// The audio box's player (A4; legacy DirectSoundPlayer2 at 20d647c4) through
// the editor output (N6, ADR 0010). It plays the box's cached frames, as
// legacy read them from its provider's cache (ReadCache, then ApplyVolume),
// kept a little ahead of the device: the range, then silence until it is
// stopped, as legacy's looping buffer went on with silence past the end.
// A range shorter than legacy's 100 ms latency was played once and the player
// stopped itself after it. The output is opened at the audio's rate and
// channels; when the device refuses the rate, the frames are resampled
// (linearly) to the rate it takes.
//
// The position is the frame being heard: the frames the device has taken,
// moved on by the time since that callback less the time until its first
// frame is heard (the clock estimate's DAC time), mapped back to the
// audio's rate. Before the output reports it is the start.

#include "hikari/application/audio_display.h"
#include "hikari/application/audio_output.h"
#include "hikari/application/audio_playback.h"

#include <QObject>
#include <QString>
#include <QTimer>

#include <functional>
#include <vector>

namespace hikari::backends {

class AudioBoxPlayer : public QObject, public application::AudioPlayerPort {
    Q_OBJECT
public:
    using Audio = std::function<const application::DisplayAudio *()>;
    using Clock = std::function<double()>; // steady_clock seconds

    static constexpr int kWantedLatencyMs = 100; // legacy WantedLatency
    static constexpr double kAheadSeconds = 0.25; // queued beyond what the device has taken
    static constexpr std::int64_t kChunkFrames = 2048;

    // `audio` is the box's audio at the time of each call (null: none).
    AudioBoxPlayer(application::AudioOutputPort &output, Audio audio, QObject *parent = nullptr);
    // The output made at the first play (PortAudio initializes then, as
    // legacy made its player with the audio).
    AudioBoxPlayer(std::function<application::AudioOutputPort &()> output, Audio audio, QObject *parent = nullptr);
    ~AudioBoxPlayer() override;

    void setClock(Clock now) { m_now = std::move(now); }
    // The output device (empty: the default); used at the next open.
    void setDevice(std::string id) { m_device = std::move(id); }
    // Closes the output; the next play opens it again.
    void close() override;

    void play(std::int64_t start, std::int64_t count) override;
    void stop() override;
    bool playing() const override { return m_playing; }
    std::int64_t position() const override;
    std::int64_t startPosition() const override { return m_start; }
    std::int64_t endPosition() const override { return m_end; }
    void setEndPosition(std::int64_t end) override;
    void setVolume(double volume) override { m_volume = volume; }
    double volume() const { return m_volume; }

    // The opened output's format and the source format it plays.
    application::OutputFormat outputFormat() const { return m_outFormat; }
    application::OutputFormat sourceFormat() const { return m_srcFormat; }
    // Source frames converted so far (legacy next_input_frame).
    std::int64_t nextFrame() const { return m_next; }

    // Fills the output and follows it; the timer calls it every 5 ms.
    void pump();

signals:
    // Legacy HikariLog of the player's errors.
    void failed(const QString &message);

private:
    bool openOutput(const application::DisplayAudio &audio);
    void fill();
    void halt(); // legacy SetStopped
    std::int64_t consumed() const;

    application::AudioOutputPort &output();

    std::function<application::AudioOutputPort &()> m_makeOutput;
    application::AudioOutputPort *m_output = nullptr;
    Audio m_audio;
    Clock m_now;
    std::string m_device;
    QTimer m_timer;
    application::OutputFormat m_srcFormat{0, 0};
    application::OutputFormat m_outFormat{0, 0};
    bool m_playing = false;
    bool m_started = false;
    bool m_oneShot = false; // legacy's single-buffer playback of a short range
    double m_volume = 1.0;
    std::int64_t m_start = 0, m_end = 0;
    std::int64_t m_next = 0;          // the next source frame to convert
    std::int64_t m_written = 0;       // output frames the output accepted
    std::int64_t m_rangeFrames = 0;   // output frames of the range itself
    std::uint64_t m_baseConsumed = 0; // the output's count when this play began
    std::vector<float> m_pending;     // converted frames the output had no room for
    // the linear resampler's state
    double m_phase = 0;
    bool m_primed = false;
    std::vector<float> m_previous;
    std::vector<std::int16_t> m_frames; // a chunk read from the cache
};

} // namespace hikari::backends
