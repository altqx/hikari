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
// The fill runs on a thread of its own, as legacy's DirectSoundPlayer2Thread
// filled its buffer: every 5 ms while playing it reads the next frames from
// the cache and queues them on the output, so a busy GUI thread does not
// starve the device. The owner thread's calls and the fill thread's work
// are serialized by one lock; the output's real-time callback takes neither
// (it reads the output's lock-free queue and allocates nothing). The audio
// being played is held until the player stops, so the box may let it go.
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

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace hikari::backends {

class AudioBoxPlayer : public QObject, public application::AudioPlayerPort {
    Q_OBJECT
public:
    using Audio = std::function<const application::DisplayAudio *()>;
    // The box's audio, held while it plays.
    using SharedAudio = std::function<std::shared_ptr<const application::DisplayAudio>()>;
    using Clock = std::function<double()>; // steady_clock seconds

    static constexpr int kWantedLatencyMs = 100; // legacy WantedLatency
    static constexpr double kAheadSeconds = 0.25; // queued beyond what the device has taken
    static constexpr std::int64_t kChunkFrames = 2048;

    // `audio` is the box's audio at the time of each call (null: none).
    AudioBoxPlayer(application::AudioOutputPort &output, Audio audio, QObject *parent = nullptr);
    // The output made at the first play (PortAudio initializes then, as
    // legacy made its player with the audio).
    AudioBoxPlayer(std::function<application::AudioOutputPort &()> output, Audio audio, QObject *parent = nullptr);
    AudioBoxPlayer(std::function<application::AudioOutputPort &()> output, SharedAudio audio,
                   QObject *parent = nullptr);
    ~AudioBoxPlayer() override;

    // Who fills the output: the player's own thread (the default), or the
    // owner calling pump() (tests that drive the device buffer by buffer).
    enum class Pumping { Thread, Manual };
    void setPumping(Pumping pumping);

    void setClock(Clock now) { m_now = std::move(now); }
    // The output device (empty: the default); used at the next open.
    void setDevice(std::string id) { m_device = std::move(id); }
    // Closes the output; the next play opens it again.
    void close() override;
    // The output is made again (the factory asked anew) at the next play:
    // let go now while idle, else once playback stops (a changed
    // audio.outputHostApi takes effect when the output next opens).
    void reopenOutput();

    void play(std::int64_t start, std::int64_t count) override;
    void stop() override;
    bool playing() const override { return m_playing.load(); }
    std::int64_t position() const override;
    std::int64_t startPosition() const override;
    std::int64_t endPosition() const override;
    void setEndPosition(std::int64_t end) override;
    void setVolume(double volume) override;
    double volume() const;

    // The opened output's format and the source format it plays.
    application::OutputFormat outputFormat() const;
    application::OutputFormat sourceFormat() const;
    // Source frames converted so far (legacy next_input_frame).
    std::int64_t nextFrame() const;

    // Fills the output and follows it: the fill thread every 5 ms, or the
    // owner with Pumping::Manual.
    void pump();

signals:
    // Legacy HikariLog of the player's errors.
    void failed(const QString &message);

private:
    // The calls below run with m_lock held.
    bool openOutput(const application::DisplayAudio &audio);
    void pumpLocked();
    void fill();
    void stopLocked();
    void halt(); // legacy SetStopped
    void releaseOutput();
    std::int64_t consumed() const;
    void fail(const QString &message);
    void fillThread();

    application::AudioOutputPort &output();

    std::function<application::AudioOutputPort &()> m_makeOutput;
    application::AudioOutputPort *m_output = nullptr;
    SharedAudio m_audio;
    std::shared_ptr<const application::DisplayAudio> m_playingAudio; // held while playing
    Clock m_now;
    std::string m_device;
    application::OutputFormat m_srcFormat{0, 0};
    application::OutputFormat m_outFormat{0, 0};
    std::atomic<bool> m_playing = false;
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
    std::vector<float> m_converted;      // the chunk as output samples
    // the fill thread
    mutable std::mutex m_lock;
    std::condition_variable m_wake;
    std::thread m_thread;
    std::thread::id m_threadId;
    bool m_quit = false;
    bool m_reopen = false; // reopenOutput while playing
    Pumping m_pumping = Pumping::Thread;
};

} // namespace hikari::backends
