// W1: the optional DirectShow playback adapter in the composition. The
// settings choose it (video.playbackPlayer, Windows only); it then plays the
// video in place of the general player behind the same port, and its graph
// fills legacy's Filters menu. Other platforms have no adapter and ignore the
// setting.
#include "hikari/app/application.h"

#include <QCoreApplication>
#include <QQuickItem>
#include <QQuickWindow>

namespace hikari::app {

#ifdef _WIN32
namespace {

// Legacy's HR messages in DShowPlayer::OpenFile / InitializeGraph, for the
// translation catalogs.
[[maybe_unused]] constexpr const char *kOpenMessages[] = {
    QT_TRANSLATE_NOOP("DirectShowPlayer", "Failed to initialize DirectShow"),
    QT_TRANSLATE_NOOP("DirectShowPlayer", "Cannot create filters interface"),
    QT_TRANSLATE_NOOP("DirectShowPlayer", "Cannot create controller"),
    QT_TRANSLATE_NOOP("DirectShowPlayer", "Cannot create search interface"),
    QT_TRANSLATE_NOOP("DirectShowPlayer", "Cannot create audio interface"),
    QT_TRANSLATE_NOOP("DirectShowPlayer", "Source filter not added"),
    QT_TRANSLATE_NOOP("DirectShowPlayer", "Cannot add video renderer"),
    QT_TRANSLATE_NOOP("DirectShowPlayer", "Cannot create audio renderer instance"),
    QT_TRANSLATE_NOOP("DirectShowPlayer", "Cannot add Direct Sound renderer"),
    QT_TRANSLATE_NOOP("DirectShowPlayer", "Cannot enumerate source pins"),
    QT_TRANSLATE_NOOP("DirectShowPlayer", "Cannot enumerate renderer pins"),
    QT_TRANSLATE_NOOP("DirectShowPlayer", "Cannot enumerate Direct Sound pins"),
    QT_TRANSLATE_NOOP("DirectShowPlayer", "Cannot get renderer pin"),
    QT_TRANSLATE_NOOP("DirectShowPlayer", "Cannot get dsound pin"),
    QT_TRANSLATE_NOOP("DirectShowPlayer", "No IMediaTypes"),
    QT_TRANSLATE_NOOP("DirectShowPlayer", "No track type info"),
    QT_TRANSLATE_NOOP("DirectShowPlayer", "Cannot connect source pin to video renderer"),
    QT_TRANSLATE_NOOP("DirectShowPlayer", "Cannot connect source pin to audio renderer"),
    QT_TRANSLATE_NOOP("DirectShowPlayer", "Cannot connect source pin to audio1 renderer"),
    QT_TRANSLATE_NOOP("DirectShowPlayer", "Cannot find connected source pin"),
    QT_TRANSLATE_NOOP("DirectShowPlayer", "Cannot get splitter pin info"),
    QT_TRANSLATE_NOOP("DirectShowPlayer", "Cannot enumerate splitter pins"),
    QT_TRANSLATE_NOOP("DirectShowPlayer", "Cannot connect source pin to video2 renderer"),
};

} // namespace
#endif

bool Application::directShowPlayback() const
{
#ifdef _WIN32
    return m_directShow && m_activePlayer == m_directShow.get();
#else
    return false;
#endif
}

void Application::choosePlayer()
{
    application::GeneralPlayerPort *player = m_generalPlayer.get();
#ifdef _WIN32
    if (m_settings->integer("video.playbackPlayer") == 1) {
        if (!m_directShow) {
            m_directShow = std::make_unique<backends::DirectShowPlayer>([this](std::function<void()> task) {
                QMetaObject::invokeMethod(this, std::move(task), Qt::QueuedConnection);
            });
            // Its frames are shown as the general player's are.
            m_directShow->setFrameSink([this](application::IndexedFrame frame, std::int64_t startUs) {
                m_video->session().generalFrame(std::move(frame), startUs);
                emit m_video->changed();
            });
            // Legacy: HR's HikariLogSilent for the failed step, then
            // VideoBox::LoadVideo's warning box.
            m_directShow->setOpenFailed([this](const std::string &message) {
                m_log->log(QCoreApplication::translate("DirectShowPlayer", message.c_str()), true);
                emit playerNotice(tr("The file is not a valid video file or is corrupted,\n"
                                     "or codecs or a splitter may be missing"));
            });
        }
        m_directShow->setColorSpace(m_video->session().matrix());
        player = m_directShow.get();
    }
#endif
    if (player == m_activePlayer)
        return;
    m_activePlayer = player;
    m_video->session().setGeneralPlayer(player);
    m_videoView->setPlayer(player);
#ifdef _WIN32
    // Legacy deleted the renderer when the player changed (DeleteRenderer):
    // the graph and its file go with it.
    if (player != m_directShow.get())
        m_directShow.reset();
    else
        m_video->session().preparePlayer();
#endif
    emit playbackPlayerChanged();
}

QVariantList Application::playerFilters() const
{
    QVariantList out;
#ifdef _WIN32
    // Legacy: only while the DirectShow renderer has the video
    // (GetState() != None && m_IsDirectShow).
    if (directShowPlayback() && m_video->loaded() && m_video->session().playerHasVideo())
        for (const auto &f : m_directShow->filters())
            out.push_back(QVariantMap{{QStringLiteral("name"), QString::fromStdString(f.name)},
                                      {QStringLiteral("enabled"), f.hasPropertyPages}});
#endif
    return out;
}

bool Application::showPlayerFilterProperties(const QString &name, QObject *item, qreal x, qreal y)
{
#ifdef _WIN32
    if (!directShowPlayback())
        return false;
    std::uintptr_t owner = 0;
    QPointF at(x, y);
    if (auto *quick = qobject_cast<QQuickItem *>(item); quick && quick->window()) {
        QQuickWindow *window = quick->window();
        owner = static_cast<std::uintptr_t>(window->winId());
        // The owner's client area, in physical pixels.
        at = quick->mapToScene(at) * window->devicePixelRatio();
    }
    return m_directShow->showFilterProperties(name.toStdString(), owner, qRound(at.x()), qRound(at.y()));
#else
    Q_UNUSED(name)
    Q_UNUSED(item)
    Q_UNUSED(x)
    Q_UNUSED(y)
    return false;
#endif
}

} // namespace hikari::app
