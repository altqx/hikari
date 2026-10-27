#include "video_controller.h"

#include "video_presenter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QVariantMap>

#include <algorithm>
#include <chrono>

namespace hikari::ui {

using application::SourceError;
using application::VideoSession;

namespace {

QString reasonOf(std::optional<SourceError> e)
{
    switch (e.value_or(SourceError::BackendFailure)) {
    case SourceError::InvalidInput: return QCoreApplication::translate("HikariSub.Video", "the file could not be read");
    case SourceError::Unsupported: return QCoreApplication::translate("HikariSub.Video", "the file has no usable video track");
    case SourceError::HelperLost: return QCoreApplication::translate("HikariSub.Video", "the media helper ended");
    case SourceError::MissingDependency: return QCoreApplication::translate("HikariSub.Video", "a media component is missing");
    default: return QCoreApplication::translate("HikariSub.Video", "the video could not be opened");
    }
}

QString clock(std::int64_t us)
{
    const std::int64_t cs = us / 10'000;
    return QStringLiteral("%1:%2:%3.%4")
        .arg(cs / 360'000)
        .arg(cs / 6'000 % 60, 2, 10, QLatin1Char('0'))
        .arg(cs / 100 % 60, 2, 10, QLatin1Char('0'))
        .arg(cs % 100, 2, 10, QLatin1Char('0'));
}

} // namespace

VideoController::VideoController(application::IndexedSourcePort &source, application::SubtitleRendererPort &renderer,
                                 QObject *parent)
    : QObject(parent), m_session(source, renderer)
{
    m_session.setObserver([this] { sessionChanged(); });
    m_textsTimer = new QTimer(this);
    m_textsTimer->setSingleShot(true);
    m_textsTimer->setInterval(100);
    connect(m_textsTimer, &QTimer::timeout, this, &VideoController::textsChanged);
    m_seekTimer = new QTimer(this);
    m_seekTimer->setSingleShot(true);
    m_seekTimer->setTimerType(Qt::PreciseTimer);
    connect(m_seekTimer, &QTimer::timeout, this, &VideoController::seekBarTimer);
    connect(this, &VideoController::changed, this, &VideoController::textsMayChange); // O5
}

void VideoController::setMediaInfo(application::DisplayAudioPort *tracks, application::ChapterPort *chapters)
{
    m_trackPort = tracks;
    m_chapterPort = chapters;
}

// V3: a video that became ready gets its track names and chapters from the
// media helper (legacy read them while the provider indexed); prevchap
// starts again with each video (VideoBox::LoadVideo, VideoBox.cpp:287).
void VideoController::sessionChanged()
{
    const bool ready = m_session.state() == VideoSession::State::Ready;
    const std::string path = ready ? m_session.path() : std::string();
    if (path != m_mediaInfoPath) {
        m_mediaInfoPath = path;
        m_trackInfo.clear();
        m_chapters.clear();
        m_prevChapter = -1;
        if (ready && !m_session.dummy()) { // ProviderDummy::GetChapters gives none
            const std::weak_ptr<bool> alive = m_alive;
            if (m_trackPort)
                m_trackPort->probe(path, [this, alive, path](std::expected<application::MediaProbe, application::AudioFailure> probe) {
                    if (alive.expired() || path != m_mediaInfoPath || !probe)
                        return;
                    m_trackInfo = std::move(probe->audio);
                    emit changed();
                });
            if (m_chapterPort)
                m_chapterPort->chapters(path, [this, alive, path](std::expected<std::vector<application::Chapter>, application::PlayerError> list) {
                    if (alive.expired() || path != m_mediaInfoPath || !list)
                        return;
                    m_chapters = std::move(*list);
                    emit changed();
                });
        }
    }
    emit changed();
}

// V1: every player frame changes the session; the texts, the frame and the
// seek bar follow at most every 100 ms while playing, at once otherwise.
// Legacy refreshed the time and the slider while playing on a 100 ms timer
// too (VideoBox::Play's m_VideoTimeTimer, HikariSub/VideoBox.cpp:220-222,
// and RefreshTime, VideoBox.cpp:1386-1410).
// V2 (user decision 2026-10-14): a seek while playing (the seek bar, a
// jump, the active Line) shows at once, so the bar never shows where the
// player was; the 100 ms pace then goes on.
void VideoController::textsMayChange()
{
    if (m_session.playing() && m_session.playEpoch() == m_textsEpoch) {
        if (!m_textsTimer->isActive())
            m_textsTimer->start();
        return;
    }
    textsNow();
}

void VideoController::textsNow()
{
    m_textsEpoch = m_session.playEpoch();
    m_textsTimer->stop();
    emit textsChanged();
}

double VideoController::indexingProgress() const
{
    const auto p = m_session.indexingProgress();
    if (!indexing() || !p || p->second <= 0)
        return -1;
    return std::clamp(static_cast<double>(p->first) / static_cast<double>(p->second), 0.0, 1.0);
}

void VideoController::cancelIndexing()
{
    ++m_openRequest; // an open still in the filter is cancelled too
    m_session.cancelOpen();
    emit changed();
}

bool VideoController::unloadVideo()
{
    // legacy OnDeleteVideo returns at once without a video (VideoBox.cpp:1062-1063)
    if (m_session.state() == VideoSession::State::Closed)
        return false;
    ++m_openRequest;
    m_session.close();
    emit changed();
    emit unloaded();
    return true;
}

QVariantList VideoController::streams() const
{
    QVariantList rows;
    const auto &tracks = m_session.audioTracks();
    const int playing = m_session.playbackAudioTrack();
    for (std::size_t i = 0; i < tracks.size(); ++i) {
        application::AudioTrack info;
        info.index = tracks[i];
        for (const auto &t : m_trackInfo)
            if (t.index == tracks[i])
                info = t;
        rows << QVariantMap{{QStringLiteral("label"), QString::fromStdString(application::audioStreamLabel(info))},
                            {QStringLiteral("checked"), static_cast<int>(i) == playing}};
    }
    return rows;
}

bool VideoController::selectStream(int index)
{
    return m_session.selectPlaybackAudioTrack(index);
}

std::vector<int> VideoController::chapterStarts() const
{
    // legacy chapter.time: whole ms
    std::vector<int> starts;
    starts.reserve(m_chapters.size());
    for (const auto &c : m_chapters)
        starts.push_back(static_cast<int>(c.startUs / 1000));
    return starts;
}

QVariantList VideoController::chapters() const
{
    QVariantList rows;
    const auto starts = chapterStarts();
    const int current = application::currentChapter(starts, m_session.tell());
    for (std::size_t j = 0; j < m_chapters.size(); ++j) {
        // name + "\t[" + SubsTime::raw() + "]" (VideoBox.cpp:1012-1014): H:MM:SS.CC
        const int ms = std::max(0, starts[j]);
        const QString time = QStringLiteral("%1:%2:%3.%4")
                                 .arg(ms / 3600000)
                                 .arg(ms / 60000 % 60, 2, 10, QLatin1Char('0'))
                                 .arg(ms / 1000 % 60, 2, 10, QLatin1Char('0'))
                                 .arg(ms / 10 % 100, 2, 10, QLatin1Char('0'));
        rows << QVariantMap{{QStringLiteral("label"), QString::fromStdString(m_chapters[j].title)},
                            {QStringLiteral("time"), QStringLiteral("[%1]").arg(time)},
                            {QStringLiteral("checked"), static_cast<int>(j) == current}};
    }
    return rows;
}

bool VideoController::seekChapter(int index)
{
    if (!hasVideo() || index < 0 || index >= chapterCount())
        return false;
    // VideoBox.cpp:1046-1047: Seek(time) (prevchap unchanged)
    return m_session.seekToMs(chapterStarts()[static_cast<std::size_t>(index)]);
}

bool VideoController::nextChapter()
{
    if (!hasVideo())
        return false;
    const auto starts = chapterStarts();
    const auto jj = application::nextChapter(starts, m_session.tell(), m_prevChapter);
    return jj && m_session.seekToMs(starts[static_cast<std::size_t>(*jj)]);
}

bool VideoController::previousChapter()
{
    if (!hasVideo())
        return false;
    const auto starts = chapterStarts();
    const auto jj = application::previousChapter(starts, m_session.tell(), m_prevChapter);
    return jj && m_session.seekToMs(starts[static_cast<std::size_t>(*jj)]);
}

void VideoController::resetForTarget()
{
    ++m_openRequest; // P6: an open still in the filter belonged to the previous target
    m_session.close();
    m_offer.clear();
    m_offerApplyToAll = false;
    emit changed();
}

void VideoController::setOffer(const QString &text, const QString &associatedLabel, const QString &directoryLabel)
{
    m_offer = text;
    m_offerAssociated = associatedLabel;
    m_offerDirectory = directoryLabel;
    m_offerApplyToAll = false;
    emit changed();
}

void VideoController::withdrawOffer()
{
    m_offer.clear();
    emit changed();
}

void VideoController::setOfferApplyToAll(bool on)
{
    if (m_offerApplyToAll == on)
        return;
    m_offerApplyToAll = on;
    emit changed();
}

QString VideoController::status() const
{
    switch (m_session.state()) {
    case VideoSession::State::Closed: return QCoreApplication::translate("HikariSub.Video", "No video open");
    case VideoSession::State::Opening:
        if (m_session.dummy())
            return QCoreApplication::translate("HikariSub.Video", "Opening dummy video…");
        return QCoreApplication::translate("HikariSub.Video", "Indexing %1…")
                   .arg(QFileInfo(QString::fromStdString(m_session.path())).fileName());
    case VideoSession::State::Failed:
        return QCoreApplication::translate("HikariSub.Video", "Video unavailable: %1").arg(reasonOf(m_session.error()));
    case VideoSession::State::Ready: break;
    }
    const int f = frame();
    const auto start = m_session.frameStart(f);
    return QCoreApplication::translate("HikariSub.Video", "Frame %1 of %2  %3")
               .arg(f).arg(frameCount()).arg(start ? clock(start->microseconds()) : QString());
}

void VideoController::attachPresenter(QObject *presenter)
{
    if (m_presenter)
        disconnect(m_presenter, nullptr, this, nullptr);
    m_presenter = qobject_cast<VideoPresenter *>(presenter);
    if (m_presenter)
        connect(m_presenter, &VideoPresenter::screenSizeChanged, this, &VideoController::followScreenSize);
    m_session.setPresenter(m_presenter);
    followScreenSize();
}

void VideoController::setSubtitlesAtScreenSize(bool on)
{
    m_screenSizeSubtitles = on;
    followScreenSize();
}

void VideoController::followScreenSize()
{
    const QSize size = m_presenter ? m_presenter->screenSize() : QSize();
    if (m_screenSizeSubtitles && !size.isEmpty())
        m_session.setScreenSize(std::pair(size.width(), size.height()));
    else
        m_session.setScreenSize(std::nullopt);
}

void VideoController::answer(int answer)
{
    if (m_offer.isEmpty())
        return;
    const bool all = m_offerApplyToAll;
    m_offer.clear();
    emit changed();
    emit offerAnswered(answer, all);
}

void VideoController::loadAssociated()
{
    answer(0);
}

void VideoController::loadFromDirectory()
{
    answer(1);
}

void VideoController::dismissOffer()
{
    answer(2);
}

void VideoController::openVideo(const QString &path)
{
    if (m_beforeOpen)
        m_beforeOpen();
    const bool withdrawn = !m_offer.isEmpty();
    m_offer.clear();
    open(application::isDummyVideo(path.toStdString()) ? path : QDir::toNativeSeparators(path));
    emit changed();
    if (withdrawn)
        emit offerWithdrawn();
}

void VideoController::open(const QString &path)
{
    const std::uint64_t request = ++m_openRequest;
    m_requestedVideo = path;
    auto asVideo = [self = QPointer<VideoController>(this), request, path](application::IndexRequest index) {
        if (!self || request != self->m_openRequest)
            return;
        self->m_session.open(path.toStdString(), std::move(index));
        emit self->changed();
    };
    if (m_filter && !application::isDummyVideo(path.toStdString())) // a dummy has no file to probe
        m_filter(path, std::move(asVideo));
    else
        asVideo({});
}

bool VideoController::stepFrames(int frames)
{
    return m_session.step(frames);
}

bool VideoController::play()
{
    const bool done = m_session.play();
    emit changed();
    return done;
}

bool VideoController::playActualLine()
{
    if (!m_lineTimes)
        return false;
    const bool done = m_session.playLine(static_cast<int>(m_lineTimes->first.microseconds() / 1000),
                                         static_cast<int>(m_lineTimes->second.microseconds() / 1000));
    emit changed();
    return done;
}

bool VideoController::loopRange(int startMs, int endMs)
{
    const bool done = m_session.playLine(startMs, endMs, /*loop*/ true);
    emit changed();
    return done;
}

bool VideoController::pause()
{
    const bool done = m_session.pause();
    emit changed();
    return done;
}

bool VideoController::stop()
{
    const bool done = m_session.stop();
    emit changed();
    return done;
}

QString VideoController::times() const
{
    const auto shown = m_session.reportedFrame();
    const auto start = shown ? m_session.frameStart(*shown) : std::nullopt;
    if (!start)
        return {};
    const std::int64_t ms = start->microseconds() / 1000;
    // SubsTime::raw(SRT): "%02i:%02i:%02i,%03i".
    QString out = QStringLiteral("%1:%2:%3,%4;  %5;  ")
                      .arg(ms / 3'600'000, 2, 10, QLatin1Char('0'))
                      .arg(ms / 60'000 % 60, 2, 10, QLatin1Char('0'))
                      .arg(ms / 1000 % 60, 2, 10, QLatin1Char('0'))
                      .arg(ms % 1000, 3, 10, QLatin1Char('0'))
                      .arg(*shown);
    if (m_lineTimes) {
        // The Line's start frame is the one at or after its start
        // (Timebase::FrameAt); 0 past the last frame.
        const int lineFrame = m_session.frameAtOrAfter(m_lineTimes->first).value_or(0);
        out += QStringLiteral("%1;  ").arg(*shown - lineFrame);
    }
    if (m_lineTimes) {
        auto zeroIt = [](core::DocumentTime t) { return t.microseconds() / 1000 / 10 * 10; };
        out += QStringLiteral("%1 ms, %2 ms").arg(ms - zeroIt(m_lineTimes->first)).arg(ms - zeroIt(m_lineTimes->second));
    }
    return out;
}

bool VideoController::keyframeShown() const
{
    const auto shown = m_session.reportedFrame();
    return shown && m_session.isKeyframe(*shown);
}

bool VideoController::showFrameAt(int frame)
{
    if (!hasVideo())
        return false;
    return m_session.seekFrame(frame);
}

void VideoController::seekBarMoved(int frame, bool held)
{
    if (!held) {
        seekBarSeek(frame);
        return;
    }
    seekBarSeek(m_seekThrottle.move(frame, application::SeekThrottle::Clock::now()));
    seekBarTimer();
}

void VideoController::seekBarReleased()
{
    m_seekTimer->stop();
    seekBarSeek(m_seekThrottle.release(application::SeekThrottle::Clock::now()));
    // The bar follows the frame again: at once, not with the last texts
    // (the frame let go on may have been sought before the release).
    textsNow();
}

void VideoController::seekBarSeek(std::optional<int> frame)
{
    if (!frame)
        return;
    showFrameAt(*frame);
    emit seekBarSought(*frame);
}

// Seeks the waiting frame once it is due, else waits for it.
void VideoController::seekBarTimer()
{
    const auto now = application::SeekThrottle::Clock::now();
    seekBarSeek(m_seekThrottle.tick(now));
    const auto due = m_seekThrottle.due();
    if (!due) {
        m_seekTimer->stop();
        return;
    }
    const auto wait = std::chrono::ceil<std::chrono::milliseconds>(*due - now);
    m_seekTimer->start(static_cast<int>(std::max<std::chrono::milliseconds::rep>(wait.count(), 1)));
}

bool VideoController::seekBy(int ms)
{
    return m_session.seekBy(ms);
}

bool VideoController::goToLineStart()
{
    if (!hasVideo() || !m_lineTimes)
        return false;
    // Seek(MAX(0, start)): the frame at or after it.
    if (m_lineTimes->first.microseconds() <= 0)
        m_session.showFrame(0);
    else
        m_session.seekTo(m_lineTimes->first);
    return true;
}

bool VideoController::goToLineEnd()
{
    if (!hasVideo() || !m_lineTimes)
        return false;
    m_session.seekToEnd(m_lineTimes->second);
    return true;
}

void VideoController::setActiveLineTimes(std::optional<std::pair<core::DocumentTime, core::DocumentTime>> times)
{
    if (times == m_lineTimes)
        return;
    m_lineTimes = times;
    emit changed();
}

} // namespace hikari::ui
