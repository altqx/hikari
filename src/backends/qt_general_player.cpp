#include "hikari/backends/qt_general_player.h"

#include <QAudioDevice>
#include <QAudioOutput>
#include <QLocale>
#include <QMediaDevices>
#include <QMediaFormat>
#include <QMediaMetaData>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>

#include <chrono>
#include <cstdlib>

namespace hikari::backends {

using application::MediaDescription;
using application::MediaStatus;
using application::PlaybackState;
using application::PlayerClock;
using application::PlayerError;
using application::PlayerTrack;
using application::SeekResult;

namespace {

double monotonicSeconds()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

PlayerError errorOf(QMediaPlayer::Error e)
{
    switch (e) {
    case QMediaPlayer::ResourceError: return PlayerError::ResourceError;
    case QMediaPlayer::FormatError: return PlayerError::FormatError;
    case QMediaPlayer::AccessDeniedError: return PlayerError::AccessDenied;
    default: return PlayerError::BackendFailure;
    }
}

std::vector<PlayerTrack> tracksOf(const QList<QMediaMetaData> &list, bool video)
{
    std::vector<PlayerTrack> out;
    for (const QMediaMetaData &m : list) {
        PlayerTrack t;
        const QVariant language = m.value(QMediaMetaData::Language);
        if (language.isValid() && language.value<QLocale::Language>() != QLocale::AnyLanguage)
            t.language = QLocale::languageToCode(language.value<QLocale::Language>(), QLocale::ISO639Part2).toStdString();
        t.title = m.stringValue(QMediaMetaData::Title).toStdString();
        const QVariant codec = m.value(video ? QMediaMetaData::VideoCodec : QMediaMetaData::AudioCodec);
        if (codec.isValid())
            t.codec = (video ? QMediaFormat::videoCodecName(codec.value<QMediaFormat::VideoCodec>())
                             : QMediaFormat::audioCodecName(codec.value<QMediaFormat::AudioCodec>()))
                          .toStdString();
        out.push_back(std::move(t));
    }
    return out;
}

constexpr std::chrono::milliseconds kSeekFallback{3000};

bool nearSeek(const QVideoFrame &frame, std::int64_t requestedUs)
{
    const qint64 duration = frame.endTime() > frame.startTime() ? frame.endTime() - frame.startTime() : 100'000;
    return std::abs(frame.startTime() - requestedUs) <= duration + 1000;
}

}

struct QtGeneralPlayer::Engine {
    QMediaPlayer player;
    QVideoSink sink;
    std::unique_ptr<QAudioOutput> audio;
};

QtGeneralPlayer::QtGeneralPlayer(bool useAudioOutput, QObject *parent) : QObject(parent), m_useAudioOutput(useAudioOutput)
{
    // Qt's FFmpeg reports no error for a video stream it cannot decode (AV1 without a hardware decoder).
    m_frameDeadline.setSingleShot(true);
    m_frameDeadline.setInterval(std::chrono::seconds(5));
    connect(&m_frameDeadline, &QTimer::timeout, this, &QtGeneralPlayer::missFrameDeadline);
    m_seekFallback.setSingleShot(true);
    m_seekFallback.setInterval(kSeekFallback);
    connect(&m_seekFallback, &QTimer::timeout, this, [this] {
        if (m_pendingSeek && m_heldBack.isValid())
            deliver(std::exchange(m_heldBack, QVideoFrame()));
    });
}

QtGeneralPlayer::Engine &QtGeneralPlayer::engine()
{
    if (m_engine)
        return *m_engine;
    m_engine = std::make_unique<Engine>();
    QMediaPlayer &player = m_engine->player;
    QVideoSink &sink = m_engine->sink;
    player.setVideoSink(&sink);
    applyOutputDevice();
    connect(&player, &QMediaPlayer::mediaStatusChanged, this, &QtGeneralPlayer::onStatus);
    connect(&player, &QMediaPlayer::playbackStateChanged, this, [this] {
        ++m_epoch;
        m_clock.valid = false;
        emit stateChanged();
    });
    connect(&player, &QMediaPlayer::playbackRateChanged, this, [this] {
        ++m_epoch;
        m_clock.valid = false;
    });
    connect(&player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error e, const QString &) {
        if (m_pendingOpen)
            failOpen(errorOf(e));
    });
    connect(&sink, &QVideoSink::videoFrameChanged, this, [this, slot = m_slot](const QVideoFrame &frame) {
        if (!frame.isValid())
            return;
        std::lock_guard lock(slot->mutex);
        if (!slot->alive)
            return;
        ++slot->received;
        if (slot->frame.isValid())
            ++slot->superseded;
        slot->frame = frame;
        if (!std::exchange(slot->queued, true))
            QMetaObject::invokeMethod(this, &QtGeneralPlayer::takeFrame, Qt::QueuedConnection);
    }, Qt::DirectConnection);
    connect(&sink, &QVideoSink::subtitleTextChanged, this, &QtGeneralPlayer::subtitleTextChanged);
    return *m_engine;
}

void QtGeneralPlayer::setOutputDevice(const std::string &id)
{
    m_outputRequest = id;
    if (m_engine)
        applyOutputDevice();
}

void QtGeneralPlayer::applyOutputDevice()
{
    if (!m_useAudioOutput)
        return;
    const QList<QAudioDevice> outputs = QMediaDevices::audioOutputs();
    if (outputs.isEmpty())
        return;
    QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (!m_outputRequest.empty()) {
        const QByteArray wanted = QByteArray::fromStdString(m_outputRequest);
        for (const QAudioDevice &candidate : outputs)
            if (candidate.id() == wanted)
                device = candidate;
    }
    if (device.isNull())
        device = outputs.front();
    auto &audio = m_engine->audio;
    if (!audio) {
        audio = std::make_unique<QAudioOutput>(device);
        audio->setVolume(static_cast<float>(m_volume));
        m_engine->player.setAudioOutput(audio.get());
        return;
    }
    audio->setDevice(device);
}

std::string QtGeneralPlayer::outputDeviceId() const
{
    return m_engine && m_engine->audio ? m_engine->audio->device().id().toStdString() : std::string();
}

QtGeneralPlayer::~QtGeneralPlayer()
{
    {
        std::lock_guard lock(m_slot->mutex);
        m_slot->alive = false;
        m_slot->frame = {};
    }
    if (m_engine)
        m_engine->player.stop();
}

void QtGeneralPlayer::takeFrame()
{
    QVideoFrame frame;
    {
        std::lock_guard lock(m_slot->mutex);
        m_slot->queued = false;
        frame = std::exchange(m_slot->frame, QVideoFrame());
    }
    if (frame.isValid())
        onFrame(frame);
}

void QtGeneralPlayer::dropWaitingFrame()
{
    std::lock_guard lock(m_slot->mutex);
    if (m_slot->frame.isValid()) {
        m_slot->frame = {};
        ++m_slot->superseded;
    }
}

std::uint64_t QtGeneralPlayer::receivedFrames() const
{
    std::lock_guard lock(m_slot->mutex);
    return m_slot->received;
}

std::uint64_t QtGeneralPlayer::supersededFrames() const
{
    std::lock_guard lock(m_slot->mutex);
    return m_slot->superseded;
}

void QtGeneralPlayer::open(const std::string &path, Opened done)
{
    ++m_generation;
    ++m_epoch;
    m_clock = {};
    m_mediaFrames = 0;
    if (auto previous = std::exchange(m_pendingOpen, nullptr))
        previous(std::unexpected(PlayerError::Stale));
    forgetSeek();
    m_pendingOpen = std::move(done);
    dropWaitingFrame();
    engine().player.setSource(QUrl::fromLocalFile(QString::fromStdString(path)));
}

void QtGeneralPlayer::failOpen(PlayerError error)
{
    if (auto done = std::exchange(m_pendingOpen, nullptr))
        done(std::unexpected(error));
}

void QtGeneralPlayer::onStatus(QMediaPlayer::MediaStatus status)
{
    emit stateChanged();
    if (!m_pendingOpen)
        return;
    if (status == QMediaPlayer::InvalidMedia)
        return failOpen(errorOf(m_engine->player.error()));
    if (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia) {
        auto done = std::exchange(m_pendingOpen, nullptr);
        done(describe());
    }
}

MediaDescription QtGeneralPlayer::describe() const
{
    MediaDescription d;
    d.generation = m_generation;
    if (!m_engine)
        return d;
    const QMediaPlayer &player = m_engine->player;
    if (player.duration() > 0)
        d.durationUs = player.duration() * 1000;
    d.seekable = player.isSeekable();
    d.audioOutput = m_engine->audio != nullptr;
    d.videoTracks = tracksOf(player.videoTracks(), true);
    d.audioTracks = tracksOf(player.audioTracks(), false);
    d.subtitleTracks = tracksOf(player.subtitleTracks(), false);
    d.activeVideo = player.activeVideoTrack();
    d.activeAudio = player.activeAudioTrack();
    d.activeSubtitle = player.activeSubtitleTrack();
    return d;
}

MediaDescription QtGeneralPlayer::description() const
{
    return describe();
}

void QtGeneralPlayer::seek(std::int64_t us, Seeked done)
{
    if (!m_engine || m_engine->player.source().isEmpty() || m_pendingOpen)
        return done(std::unexpected(PlayerError::NotOpen));
    QMediaPlayer &player = m_engine->player;
    if (!player.isSeekable())
        return done(std::unexpected(PlayerError::Unsupported));
    if (us < 0)
        return done(std::unexpected(PlayerError::InvalidInput));
    forgetSeek();
    ++m_epoch;
    m_clock.valid = false;
    m_pendingSeek = PendingSeek{m_generation, us, std::move(done)};
    m_seekFallback.start();
    if (m_mediaFrames == 0)
        m_frameDeadline.start();
    dropWaitingFrame();
    const bool stopped = player.playbackState() == QMediaPlayer::StoppedState;
    player.setPosition(us / 1000); // QMediaPlayer positions are milliseconds
    // A stopped QMediaPlayer delivers no frame for a seek.
    if (stopped)
        player.pause();
}

void QtGeneralPlayer::missFrameDeadline()
{
    if (m_mediaFrames > 0 || !m_engine || !m_engine->player.hasVideo())
        return;
    m_seekFallback.stop();
    if (auto seek = std::exchange(m_pendingSeek, std::nullopt))
        seek->done(std::unexpected(PlayerError::NoVideoFrame));
}

void QtGeneralPlayer::onFrame(const QVideoFrame &frame)
{
    if (!frame.isValid())
        return;
    ++m_mediaFrames;
    m_frameDeadline.stop();
    // Qt still hands over frames decoded before a setPosition.
    if (m_pendingSeek && frame.startTime() >= 0 && !nearSeek(frame, m_pendingSeek->requestedUs)) {
        m_heldBack = frame;
        return;
    }
    deliver(frame);
}

void QtGeneralPlayer::deliver(const QVideoFrame &frame)
{
    ++m_delivered;
    const qint64 start = frame.startTime();
    const qint64 end = frame.endTime();
    if (start >= 0) {
        m_clock.epoch = m_epoch;
        m_clock.mediaUs = start;
        m_clock.monotonicSeconds = monotonicSeconds();
        m_clock.rate = m_engine->player.playbackRate();
        m_clock.uncertaintyUs = end > start ? end - start : 0;
        m_clock.valid = m_engine->player.playbackState() == QMediaPlayer::PlayingState;
    }
    if (m_pendingSeek && start >= 0) {
        auto pending = std::move(*m_pendingSeek);
        m_pendingSeek.reset();
        m_heldBack = {};
        m_seekFallback.stop();
        SeekResult r;
        r.generation = pending.generation;
        r.requestedUs = pending.requestedUs;
        r.deliveredStartUs = start;
        if (end > start)
            r.deliveredEndUs = end;
        pending.done(r);
    }
    emit frameDelivered(frame);
}

void QtGeneralPlayer::play()
{
    engine().player.play();
}

void QtGeneralPlayer::pause()
{
    if (m_engine)
        m_engine->player.pause();
}

void QtGeneralPlayer::stop()
{
    if (m_engine)
        m_engine->player.stop();
    forgetSeek();
}

void QtGeneralPlayer::forgetSeek()
{
    m_heldBack = {};
    m_seekFallback.stop();
    m_frameDeadline.stop();
    if (auto seek = std::exchange(m_pendingSeek, std::nullopt))
        seek->done(std::unexpected(PlayerError::Stale));
}

bool QtGeneralPlayer::selectAudioTrack(int index)
{
    if (!m_engine)
        return index == -1;
    if (index < -1 || index >= m_engine->player.audioTracks().size())
        return false;
    m_engine->player.setActiveAudioTrack(index);
    return true;
}

bool QtGeneralPlayer::selectSubtitleTrack(int index)
{
    if (!m_engine)
        return index == -1;
    if (index < -1 || index >= m_engine->player.subtitleTracks().size())
        return false;
    m_engine->player.setActiveSubtitleTrack(index);
    return true;
}

PlaybackState QtGeneralPlayer::playbackState() const
{
    if (!m_engine)
        return PlaybackState::Stopped;
    switch (m_engine->player.playbackState()) {
    case QMediaPlayer::PlayingState: return PlaybackState::Playing;
    case QMediaPlayer::PausedState: return PlaybackState::Paused;
    default: return PlaybackState::Stopped;
    }
}

MediaStatus QtGeneralPlayer::mediaStatus() const
{
    if (!m_engine)
        return MediaStatus::NoMedia;
    switch (m_engine->player.mediaStatus()) {
    case QMediaPlayer::NoMedia: return MediaStatus::NoMedia;
    case QMediaPlayer::LoadingMedia: return MediaStatus::Loading;
    case QMediaPlayer::LoadedMedia: return MediaStatus::Loaded;
    case QMediaPlayer::StalledMedia:
    case QMediaPlayer::BufferingMedia: return MediaStatus::Buffering;
    case QMediaPlayer::BufferedMedia: return MediaStatus::Buffered;
    case QMediaPlayer::EndOfMedia: return MediaStatus::EndOfMedia;
    case QMediaPlayer::InvalidMedia: return MediaStatus::Invalid;
    }
    return MediaStatus::NoMedia;
}

double QtGeneralPlayer::bufferProgress() const
{
    return m_engine ? m_engine->player.bufferProgress() : 0.0;
}

PlayerClock QtGeneralPlayer::clock() const
{
    PlayerClock c = m_clock;
    c.valid = c.valid && c.epoch == m_epoch && m_engine && m_engine->player.playbackState() == QMediaPlayer::PlayingState;
    return c;
}

void QtGeneralPlayer::setVolume(double linear)
{
    m_volume = linear;
    if (m_engine && m_engine->audio)
        m_engine->audio->setVolume(static_cast<float>(linear)); // QAudioOutput's volume is linear
}

}
