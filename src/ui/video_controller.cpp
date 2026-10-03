#include "video_controller.h"

#include "video_presenter.h"

#include <QDir>
#include <QFileInfo>

namespace hikari::ui {

using application::SourceError;
using application::VideoSession;

namespace {

QString reasonOf(std::optional<SourceError> e)
{
    switch (e.value_or(SourceError::BackendFailure)) {
    case SourceError::InvalidInput: return QObject::tr("the file could not be read");
    case SourceError::Unsupported: return QObject::tr("the file has no usable video track");
    case SourceError::HelperLost: return QObject::tr("the media helper ended");
    case SourceError::MissingDependency: return QObject::tr("a media component is missing");
    default: return QObject::tr("the video could not be opened");
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
    m_session.setObserver([this] { emit changed(); });
}

void VideoController::offer(const application::MediaAssociations &associations)
{
    m_session.close();
    m_offeredVideo.clear();
    if (associations.video && associations.video->resolved)
        m_offeredVideo = QString::fromStdString(*associations.video->resolved);
    emit changed();
}

void VideoController::withdrawOffer()
{
    m_offeredVideo.clear();
    emit changed();
}

QString VideoController::offer() const
{
    return m_offeredVideo.isEmpty() ? QString()
                                    : tr("Associated files:\nVideo: %1").arg(QDir::toNativeSeparators(m_offeredVideo));
}

QString VideoController::status() const
{
    switch (m_session.state()) {
    case VideoSession::State::Closed: return tr("No video open");
    case VideoSession::State::Opening: return tr("Indexing %1…").arg(QFileInfo(QString::fromStdString(m_session.path())).fileName());
    case VideoSession::State::Failed:
        return tr("Video unavailable: %1").arg(reasonOf(m_session.error()));
    case VideoSession::State::Ready: break;
    }
    const int f = frame();
    const auto start = m_session.frameStart(f);
    return tr("Frame %1 of %2  %3").arg(f).arg(frameCount()).arg(start ? clock(start->microseconds()) : QString());
}

void VideoController::attachPresenter(QObject *presenter)
{
    m_session.setPresenter(qobject_cast<VideoPresenter *>(presenter));
}

void VideoController::loadAssociated()
{
    const QString path = m_offeredVideo;
    m_offeredVideo.clear();
    if (!path.isEmpty())
        m_session.open(path.toStdString());
    emit changed();
}

void VideoController::dismissOffer()
{
    withdrawOffer();
}

void VideoController::openVideo(const QString &path)
{
    m_offeredVideo.clear();
    m_session.open(QDir::toNativeSeparators(path).toStdString());
    emit changed();
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
    const auto shown = m_session.shownFrame();
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
        // The Line's start frame is the one at or after its start (Timebase::FrameAt).
        int lineFrame = 0;
        for (int i = 0; i < m_session.frameCount(); ++i)
            if (*m_session.frameStart(i) >= m_lineTimes->first) {
                lineFrame = i;
                break;
            }
        out += QStringLiteral("%1;  ").arg(*shown - lineFrame);
        auto zeroIt = [](core::DocumentTime t) { return t.microseconds() / 1000 / 10 * 10; };
        out += QStringLiteral("%1 ms, %2 ms").arg(ms - zeroIt(m_lineTimes->first)).arg(ms - zeroIt(m_lineTimes->second));
    }
    return out;
}

bool VideoController::keyframeShown() const
{
    const auto shown = m_session.shownFrame();
    return shown && m_session.isKeyframe(*shown);
}

bool VideoController::showFrameAt(int frame)
{
    if (!hasVideo() || frame < 0 || frame >= frameCount())
        return false;
    m_session.showFrame(frame);
    return true;
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
