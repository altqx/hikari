#pragma once

// GeneralPlayer through Qt Multimedia's FFmpeg-backed QMediaPlayer (N5), in
// the application process (ADR 0016). The player renders into its own
// QVideoSink; a frame that sink receives is a delivered frame, which is
// what acknowledges a seek and anchors the clock. V1: the sink's frames are
// taken on the thread that renders them and handed to this object's thread
// latest first: while one waits a newer one replaces it, so a busy GUI
// thread never works through a backlog of old frames (a seek drops the one
// waiting). Audio goes to the default output device when one exists;
// without one playback is silent.

#include "hikari/application/general_player.h"

#include <QMediaPlayer>
#include <QObject>
#include <QVideoFrame>
#include <QVideoSink>

#include <memory>
#include <mutex>
#include <optional>

class QAudioOutput;

namespace hikari::backends {

class QtGeneralPlayer : public QObject, public application::GeneralPlayerPort {
    Q_OBJECT
public:
    // useAudioOutput false: never open an audio device (silent playback).
    explicit QtGeneralPlayer(bool useAudioOutput = true, QObject *parent = nullptr);
    ~QtGeneralPlayer() override;

    void open(const std::string &path, Opened done) override;
    void seek(std::int64_t us, Seeked done) override;
    void play() override;
    void pause() override;
    void stop() override;
    bool selectAudioTrack(int index) override;
    bool selectSubtitleTrack(int index) override;
    application::PlaybackState playbackState() const override;
    application::MediaStatus mediaStatus() const override;
    double bufferProgress() const override;
    application::PlayerClock clock() const override;
    application::MediaDescription description() const override;
    std::uint64_t generation() const override { return m_generation; }
    void setVolume(double linear) override;
    double volume() const { return m_volume; }

    // The player's own sink (presentation reads frames from here).
    QVideoSink *videoSink() { return &m_sink; }
    QString subtitleText() const { return m_sink.subtitleText(); }
    std::uint64_t deliveredFrames() const { return m_delivered; }
    // Frames the sink received, and those a newer one replaced before this
    // thread took them.
    std::uint64_t receivedFrames() const;
    std::uint64_t supersededFrames() const;

signals:
    void frameDelivered(const QVideoFrame &frame);
    void subtitleTextChanged(const QString &text);
    void stateChanged();

private:
    void onStatus(QMediaPlayer::MediaStatus status);
    void onFrame(const QVideoFrame &frame);
    void takeFrame();
    void dropWaitingFrame();
    void failOpen(application::PlayerError error);
    application::MediaDescription describe() const;

    QMediaPlayer m_player;
    QVideoSink m_sink;
    std::unique_ptr<QAudioOutput> m_audio;
    std::uint64_t m_generation = 0;
    std::uint64_t m_epoch = 0;
    std::uint64_t m_delivered = 0;
    Opened m_pendingOpen;
    struct PendingSeek {
        std::uint64_t generation;
        std::int64_t requestedUs;
        Seeked done;
    };
    std::optional<PendingSeek> m_pendingSeek;
    application::PlayerClock m_clock;
    double m_volume = 1.0;
    // The newest frame the sink received, until this thread takes it.
    struct Slot {
        std::mutex mutex;
        QVideoFrame frame;
        bool queued = false; // a takeFrame call is on this thread's queue
        bool alive = true;
        std::uint64_t received = 0;
        std::uint64_t superseded = 0;
    };
    std::shared_ptr<Slot> m_slot = std::make_shared<Slot>();
};

} // namespace hikari::backends
