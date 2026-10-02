#pragma once

// GeneralPlayer through Qt Multimedia's FFmpeg-backed QMediaPlayer (N5), in
// the application process (ADR 0016). The player renders into its own
// QVideoSink; every frame that sink receives is a delivered frame, which is
// what acknowledges a seek and anchors the clock. Audio goes to the default
// output device when one exists; without one playback is silent.

#include "hikari/application/general_player.h"

#include <QMediaPlayer>
#include <QObject>
#include <QVideoSink>

#include <memory>
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

    // The player's own sink (presentation reads frames from here).
    QVideoSink *videoSink() { return &m_sink; }
    QString subtitleText() const { return m_sink.subtitleText(); }
    std::uint64_t deliveredFrames() const { return m_delivered; }

signals:
    void frameDelivered(const QVideoFrame &frame);
    void subtitleTextChanged(const QString &text);
    void stateChanged();

private:
    void onStatus(QMediaPlayer::MediaStatus status);
    void onFrame(const QVideoFrame &frame);
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
};

} // namespace hikari::backends
