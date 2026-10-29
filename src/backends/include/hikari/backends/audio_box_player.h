#pragma once

#include "hikari/application/audio_display.h"
#include "hikari/application/audio_output.h"
#include "hikari/application/audio_playback.h"

#include <QObject>
#include <QString>

#include <atomic>
#include <condition_variable>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace soundtouch {
class SoundTouch;
}

namespace hikari::backends {

class AudioBoxPlayer : public QObject, public application::AudioPlayerPort {
    Q_OBJECT
public:
    using Audio = std::function<const application::DisplayAudio *()>;
    using SharedAudio = std::function<std::shared_ptr<const application::DisplayAudio>()>;
    using Clock = std::function<double()>; // steady_clock seconds

    static constexpr int kWantedLatencyMs = 100;
    static constexpr double kAheadSeconds = 0.25;
    static constexpr std::int64_t kChunkFrames = 2048;

    // `audio` is the box's audio at the time of each call (null: none).
    AudioBoxPlayer(application::AudioOutputPort &output, Audio audio, QObject *parent = nullptr);
    // `output` is called at the first play.
    AudioBoxPlayer(std::function<application::AudioOutputPort &()> output, Audio audio, QObject *parent = nullptr);
    AudioBoxPlayer(std::function<application::AudioOutputPort &()> output, SharedAudio audio,
                   QObject *parent = nullptr);
    ~AudioBoxPlayer() override;

    // Thread: the player's own fill thread fills the output; Manual: the owner calls pump().
    enum class Pumping { Thread, Manual };
    void setPumping(Pumping pumping);

    void setClock(Clock now) { m_now = std::move(now); }
    // By the system's name, empty for the default; the default stands in while it is absent or
    // refuses. Takes effect at the next open.
    void setOutputDevice(std::string name);
    // Owner thread. Reopens after a device rescan: at once from the heard frame while playing,
    // else at the next play.
    void switchOutput();
    // A lost device is retried every `intervalMs`; after `attempts` tries playback stops with
    // failed() (default 12 tries, 250 ms apart).
    void setRecovery(int attempts, int intervalMs);
    void close() override;
    // The factory is asked anew at the next play; the output is released now if idle, else at stop.
    void reopenOutput();

    void play(std::int64_t start, std::int64_t count) override;
    bool playClip(std::shared_ptr<const application::DisplayAudio> clip) override;
    void stop() override;
    bool playing() const override { return m_playing.load(); }
    bool playingClip(const application::DisplayAudio *clip) const;
    std::int64_t position() const override;
    std::int64_t startPosition() const override;
    std::int64_t endPosition() const override;
    void setEndPosition(std::int64_t end) override;
    void setVolume(double volume) override;
    double volume() const;
    void setSpeed(double speed) override;
    double speed() const;
    // How far SoundTouch at `speed` brings an onset forward, in ms of the audio; 0 at speed 1.
    static double stretchLeadMs(double speed);
    // SoundTouch's README speech tuning: short sequences keep onsets within a few ms.
    static constexpr int kStretchSequenceMs = 40;
    static constexpr int kStretchSeekWindowMs = 15;
    static constexpr int kStretchOverlapMs = 8;

    application::OutputFormat outputFormat() const;
    application::OutputFormat sourceFormat() const;
    // Source frames converted so far.
    std::int64_t nextFrame() const;

    // Called by the fill thread every 5 ms, or by the owner with Pumping::Manual.
    void pump();

signals:
    void failed(const QString &message);

private:
    // MUST KILL: most private helpers expect m_lock held, though only some are named *Locked.
    bool openOutput(const application::DisplayAudio &audio);
    std::expected<application::OutputFormat, application::OutputError> openDevice(application::OutputFormat want);
    std::int64_t clockFrames(const application::ClockEstimate &clock) const;
    std::int64_t heardLocked() const;
    std::int64_t sourceFrames(std::int64_t outputFrames) const;
    void restartLocked(std::int64_t frame);
    void resetStretch();
    void playAudio(std::shared_ptr<const application::DisplayAudio> clip, std::int64_t start, std::int64_t count);
    double playSpeed() const { return m_clipping ? 1.0 : m_speed; }
    void stretch(std::vector<float> &frames, int channels);
    void resumeLocked(std::int64_t frame);
    void lostLocked();
    void retry();
    void pumpLocked();
    void fill();
    void stopLocked();
    void halt();
    void releaseOutput();
    std::int64_t consumed() const;
    void fail(const QString &message);
    void fillThread();

    application::AudioOutputPort &output();

    std::function<application::AudioOutputPort &()> m_makeOutput;
    application::AudioOutputPort *m_output = nullptr;
    SharedAudio m_audio;
    std::shared_ptr<const application::DisplayAudio> m_playingAudio;
    Clock m_now;
    std::string m_deviceName;
    bool m_rescan = false;
    std::int64_t m_base = 0;
    bool m_waiting = false;
    bool m_retryQueued = false;
    int m_attempts = 0;
    int m_maxAttempts = 12;
    int m_retryMs = 250;
    application::OutputFormat m_srcFormat{0, 0};
    application::OutputFormat m_outFormat{0, 0};
    std::atomic<bool> m_playing = false;
    bool m_started = false;
    bool m_oneShot = false;
    double m_volume = 1.0;
    std::int64_t m_start = 0, m_end = 0;
    std::int64_t m_next = 0;
    std::int64_t m_written = 0;
    std::int64_t m_rangeFrames = 0;
    std::uint64_t m_baseConsumed = 0;
    std::vector<float> m_pending;
    double m_phase = 0;
    bool m_primed = false;
    std::vector<float> m_previous;
    std::vector<std::int16_t> m_frames;
    std::vector<float> m_source;
    std::vector<float> m_converted;
    double m_speed = 1.0;
    std::unique_ptr<soundtouch::SoundTouch> m_stretch;
    bool m_clipping = false;
    mutable std::mutex m_lock;
    std::condition_variable m_wake;
    std::thread m_thread;
    std::thread::id m_threadId;
    bool m_quit = false;
    bool m_reopen = false;
    Pumping m_pumping = Pumping::Thread;
};

} // namespace hikari::backends
