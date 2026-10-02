#include "hikari/backends/qt_general_player.h"

#include <QAudioDevice>
#include <QAudioOutput>
#include <QLocale>
#include <QMediaDevices>
#include <QMediaFormat>
#include <QMediaMetaData>
#include <QUrl>
#include <QVideoFrame>

#include <chrono>

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

} // namespace

QtGeneralPlayer::QtGeneralPlayer(bool useAudioOutput, QObject *parent) : QObject(parent)
{
    m_player.setVideoSink(&m_sink);
    if (useAudioOutput && !QMediaDevices::audioOutputs().isEmpty()) {
        m_audio = std::make_unique<QAudioOutput>();
        m_player.setAudioOutput(m_audio.get());
    }
    connect(&m_player, &QMediaPlayer::mediaStatusChanged, this, &QtGeneralPlayer::onStatus);
    connect(&m_player, &QMediaPlayer::playbackStateChanged, this, [this] {
        ++m_epoch; // a stop, pause or start re-anchors the clock
        m_clock.valid = false;
        emit stateChanged();
    });
    connect(&m_player, &QMediaPlayer::playbackRateChanged, this, [this] {
        ++m_epoch;
        m_clock.valid = false;
    });
    connect(&m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error e, const QString &) {
        if (m_pendingOpen)
            failOpen(errorOf(e));
    });
    connect(&m_sink, &QVideoSink::videoFrameChanged, this, &QtGeneralPlayer::onFrame);
    connect(&m_sink, &QVideoSink::subtitleTextChanged, this, &QtGeneralPlayer::subtitleTextChanged);
}

QtGeneralPlayer::~QtGeneralPlayer()
{
    m_player.stop();
}

void QtGeneralPlayer::open(const std::string &path, Opened done)
{
    ++m_generation;
    ++m_epoch;
    m_clock = {};
    if (auto previous = std::exchange(m_pendingOpen, nullptr))
        previous(std::unexpected(PlayerError::Stale));
    if (auto seek = std::exchange(m_pendingSeek, std::nullopt))
        seek->done(std::unexpected(PlayerError::Stale));
    m_pendingOpen = std::move(done);
    m_player.setSource(QUrl::fromLocalFile(QString::fromStdString(path)));
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
        return failOpen(errorOf(m_player.error()));
    if (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia) {
        auto done = std::exchange(m_pendingOpen, nullptr);
        done(describe());
    }
}

MediaDescription QtGeneralPlayer::describe() const
{
    MediaDescription d;
    d.generation = m_generation;
    if (m_player.duration() > 0)
        d.durationUs = m_player.duration() * 1000;
    d.seekable = m_player.isSeekable();
    d.audioOutput = m_audio != nullptr;
    d.videoTracks = tracksOf(m_player.videoTracks(), true);
    d.audioTracks = tracksOf(m_player.audioTracks(), false);
    d.subtitleTracks = tracksOf(m_player.subtitleTracks(), false);
    d.activeVideo = m_player.activeVideoTrack();
    d.activeAudio = m_player.activeAudioTrack();
    d.activeSubtitle = m_player.activeSubtitleTrack();
    return d;
}

MediaDescription QtGeneralPlayer::description() const
{
    return describe();
}

void QtGeneralPlayer::seek(std::int64_t us, Seeked done)
{
    if (m_player.source().isEmpty() || m_pendingOpen)
        return done(std::unexpected(PlayerError::NotOpen));
    if (!m_player.isSeekable())
        return done(std::unexpected(PlayerError::Unsupported));
    if (us < 0)
        return done(std::unexpected(PlayerError::InvalidInput));
    if (auto previous = std::exchange(m_pendingSeek, std::nullopt))
        previous->done(std::unexpected(PlayerError::Stale));
    ++m_epoch;
    m_clock.valid = false;
    m_pendingSeek = PendingSeek{m_generation, us, std::move(done)};
    m_player.setPosition(us / 1000); // QMediaPlayer positions are milliseconds
}

void QtGeneralPlayer::onFrame(const QVideoFrame &frame)
{
    if (!frame.isValid())
        return;
    ++m_delivered;
    const qint64 start = frame.startTime();
    const qint64 end = frame.endTime();
    if (start >= 0) {
        m_clock.epoch = m_epoch;
        m_clock.mediaUs = start;
        m_clock.monotonicSeconds = monotonicSeconds();
        m_clock.rate = m_player.playbackRate();
        m_clock.uncertaintyUs = end > start ? end - start : 0;
        m_clock.valid = m_player.playbackState() == QMediaPlayer::PlayingState;
    }
    if (m_pendingSeek && start >= 0) {
        auto pending = std::move(*m_pendingSeek);
        m_pendingSeek.reset();
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
    m_player.play();
}

void QtGeneralPlayer::pause()
{
    m_player.pause();
}

void QtGeneralPlayer::stop()
{
    m_player.stop();
    if (auto seek = std::exchange(m_pendingSeek, std::nullopt))
        seek->done(std::unexpected(PlayerError::Stale));
}

bool QtGeneralPlayer::selectAudioTrack(int index)
{
    if (index < -1 || index >= m_player.audioTracks().size())
        return false;
    m_player.setActiveAudioTrack(index);
    return true;
}

bool QtGeneralPlayer::selectSubtitleTrack(int index)
{
    if (index < -1 || index >= m_player.subtitleTracks().size())
        return false;
    m_player.setActiveSubtitleTrack(index);
    return true;
}

PlaybackState QtGeneralPlayer::playbackState() const
{
    switch (m_player.playbackState()) {
    case QMediaPlayer::PlayingState: return PlaybackState::Playing;
    case QMediaPlayer::PausedState: return PlaybackState::Paused;
    default: return PlaybackState::Stopped;
    }
}

MediaStatus QtGeneralPlayer::mediaStatus() const
{
    switch (m_player.mediaStatus()) {
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
    return m_player.bufferProgress();
}

PlayerClock QtGeneralPlayer::clock() const
{
    PlayerClock c = m_clock;
    c.valid = c.valid && c.epoch == m_epoch && m_player.playbackState() == QMediaPlayer::PlayingState;
    return c;
}

} // namespace hikari::backends
