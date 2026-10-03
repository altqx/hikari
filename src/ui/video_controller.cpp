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

} // namespace hikari::ui
