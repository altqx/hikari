#include "video_fullscreen_controller.h"

#include "settings_store.h"
#include "video_controller.h"
#include "video_view_controller.h"

#include "hikari/application/video_fullscreen.h"

#include <QEvent>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>

#include <memory>

namespace hikari::ui {

namespace {

application::MonitorRect rectOf(const QRect &r)
{
    return {r.x(), r.y(), r.width(), r.height()};
}

bool positionsKnown()
{
    // Wayland clients do not know where their windows are.
    return !QGuiApplication::platformName().startsWith(QLatin1String("wayland"));
}

}

VideoFullscreenController::VideoFullscreenController(VideoController &video, VideoViewController &view,
                                                     SettingsStore &settings, QObject *parent)
    : QObject(parent), m_video(video), m_view(view), m_settings(settings)
{
    m_idle.setSingleShot(true);
    m_idle.setInterval(application::kFullscreenBarIdleMs);
    connect(&m_idle, &QTimer::timeout, this, [this] {
        m_awake = false;
        updateBar();
    });
    connect(&m_settings, &SettingsStore::changed, this, [this](const QString &id) {
        if (id == QLatin1String("video.fullscreenBar")) {
            updateBar();
            emit panelChanged();
        }
    });
    connect(&m_video, &VideoController::changed, this, [this] {
        if (m_active && !m_video.hasVideo())
            leave();
        if (!m_pendingPath.isEmpty() && m_video.openRequest() != m_pendingRequest) {
            m_pendingPath.clear();
        } else if (!m_pendingPath.isEmpty()) {
            if (m_video.hasVideo()) {
                const bool same = QFileInfo(QString::fromStdString(m_video.session().path())) == QFileInfo(m_pendingPath);
                if (same || m_pendingOpening)
                    m_pendingPath.clear();
                if (same && !m_active)
                    enter(0);
            } else if (m_video.indexing()) {
                m_pendingOpening = true;
            } else if (m_pendingOpening) {
                m_pendingPath.clear();
            }
        }
        updateBar();
        emit progressChanged();
    });
}

void VideoFullscreenController::setWindows(QQuickWindow *fullscreen, QWindow *main)
{
    if (m_window)
        m_window->removeEventFilter(this);
    m_window = fullscreen;
    m_main = main;
    if (m_window)
        m_window->installEventFilter(this);
}

bool VideoFullscreenController::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_window && event->type() == QEvent::Leave)
        m_pointer.reset();
    return QObject::eventFilter(watched, event);
}

QQuickWindow *VideoFullscreenController::window() const
{
    return m_window;
}

QList<QScreen *> VideoFullscreenController::monitors()
{
    // QGuiApplication::screens() lists the primary screen first.
    return QGuiApplication::screens();
}

bool VideoFullscreenController::ownedByMainWindow()
{
    return application::fullscreenOwnedByMainWindow(QGuiApplication::platformName().toStdString());
}

int VideoFullscreenController::monitorFor(int monitor) const
{
    const auto screens = monitors();
    std::vector<application::MonitorRect> rects;
    for (QScreen *s : screens)
        rects.push_back(rectOf(s->geometry()));
    if (monitor == 0 && screens.size() > 1 && m_main && !positionsKnown()) {
        const int i = static_cast<int>(screens.indexOf(m_main->screen()));
        return i >= 0 ? i : 0;
    }
    const QRect program = m_main ? m_main->frameGeometry() : QRect();
    return application::fullscreenMonitor(monitor, rects, rectOf(program));
}

int VideoFullscreenController::barMode() const
{
    return static_cast<int>(application::fullscreenBarMode(m_settings.integer("video.fullscreenBar")));
}

void VideoFullscreenController::setBarMode(int mode)
{
    const auto m = application::fullscreenBarMode(mode);
    if (static_cast<int>(m) != barMode())
        m_settings.set("video.fullscreenBar", static_cast<int>(m));
}

bool VideoFullscreenController::pinned() const
{
    return application::fullscreenBarMode(barMode()) == application::FullscreenBarMode::Always;
}

void VideoFullscreenController::cycleBarMode()
{
    setBarMode(static_cast<int>(application::nextFullscreenBarMode(application::fullscreenBarMode(barMode()))));
}

void VideoFullscreenController::setMenuOpen(bool open)
{
    if (m_menuOpen == open)
        return;
    m_menuOpen = open;
    if (!open && m_active) {
        m_awake = true;
        m_idle.start();
    }
    updateBar();
}

void VideoFullscreenController::setPointerOverBar(bool over)
{
    if (m_pointerOverBar == over)
        return;
    m_pointerOverBar = over;
    if (!over && m_active) {
        m_awake = true;
        m_idle.start();
    }
    updateBar();
}

void VideoFullscreenController::pointerMoved(QPointF at)
{
    if (!m_active || m_pointer == at)
        return;
    m_pointer = at;
    m_awake = true;
    m_idle.start();
    updateBar();
}

void VideoFullscreenController::updateBar()
{
    const application::FullscreenBarState state{m_awake, m_video.playing(), m_menuOpen, m_pointerOverBar};
    const auto mode = application::fullscreenBarMode(barMode());
    const bool shown = m_active && application::fullscreenBarShown(mode, state);
    const bool hidden = m_active && application::fullscreenIdle(state);
    if (shown == m_barShown && hidden == m_cursorHidden)
        return;
    m_barShown = shown;
    m_cursorHidden = hidden;
    emit panelChanged();
}

QString VideoFullscreenController::lineLabel(const QString &text) const
{
    return QString::fromStdString(application::fullscreenLineText(text.toStdString()));
}

QString VideoFullscreenController::progressText() const
{
    const auto &session = m_video.session();
    if (!m_video.hasVideo())
        return {};
    return QString::fromStdString(application::fullscreenProgressText(session.tellMs(), session.durationMs()));
}

QString VideoFullscreenController::videoName() const
{
    if (!m_video.hasVideo())
        return {};
    const QString name = QFileInfo(QString::fromStdString(m_video.session().path())).fileName();
    return QString::fromStdString(application::fullscreenOneRow(name.toStdString()));
}

bool VideoFullscreenController::toggle(int monitor)
{
    if (!m_video.hasVideo())
        return false;
    return m_active ? leave() : enter(monitor);
}

bool VideoFullscreenController::showOn(int monitor)
{
    if (!m_video.hasVideo())
        return false;
    return enter(monitor);
}

void VideoFullscreenController::enterWhenShown(const QString &path)
{
    m_pendingPath = path;
    m_pendingRequest = m_video.openRequest() + 1;
    m_pendingOpening = false;
}

void VideoFullscreenController::place(QScreen *screen)
{
    if (m_window->isVisible())
        m_window->hide();
    const std::uint64_t placing = ++m_placing;
    QScreen *mainScreen = m_main ? m_main->screen() : nullptr;
    if (!positionsKnown() && mainScreen && mainScreen != screen) {
        // Wayland compositors (sway) give the keyboard only to windows mapped on the focused output: map there first.
        m_window->setScreen(mainScreen);
        m_window->showNormal();
        m_window->requestActivate();
        auto once = std::make_shared<QMetaObject::Connection>();
        *once = connect(m_window, &QQuickWindow::frameSwapped, this,
                        [this, once, placing, target = QPointer<QScreen>(screen)] {
            disconnect(*once);
            if (!m_active || !m_window || !target || placing != m_placing)
                return;
            m_window->setScreen(target);
            m_window->setGeometry(target->geometry());
            m_window->showFullScreen();
            m_window->requestActivate();
        }, Qt::QueuedConnection);
        return;
    }
    m_window->setScreen(screen);
    m_window->setGeometry(screen->geometry());
    m_window->showFullScreen();
    m_window->raise();
    m_window->requestActivate();
    // On Wayland an activation request made before the surface is mapped can go unanswered.
    auto once = std::make_shared<QMetaObject::Connection>();
    *once = connect(m_window, &QQuickWindow::frameSwapped, this, [this, once] {
        disconnect(*once);
        if (m_active && m_window && !m_window->isActive())
            m_window->requestActivate();
    }, Qt::QueuedConnection);
}

bool VideoFullscreenController::enter(int monitor)
{
    if (!m_window)
        return false;
    const auto screens = monitors();
    const int index = monitorFor(monitor);
    if (index < 0 || index >= screens.size())
        return false;
    if (!m_active) {
        m_focusWindow = QGuiApplication::focusWindow();
        auto *quick = qobject_cast<QQuickWindow *>(m_focusWindow.data());
        m_focusItem = quick ? quick->activeFocusItem() : nullptr;
        for (QQuickItem *i = m_focusItem; i; i = i->parentItem())
            if (i->inherits("QQuickPopupItem")) {
                m_focusItem = nullptr;
                break;
            }
    }
    m_active = true;
    m_monitor = index;
    m_onAnotherMonitor = monitor != 0;
    m_awake = true;
    m_idle.start();
    m_view.setFullscreen(true);
    place(screens[index]);
    emit activeChanged();
    updateBar();
    emit panelChanged();
    emit progressChanged();
    return true;
}

bool VideoFullscreenController::leave()
{
    if (!m_active)
        return false;
    if (m_video.playing())
        m_video.pause();
    m_active = false;
    m_onAnotherMonitor = false;
    m_idle.stop();
    m_awake = false;
    m_pointer.reset();
    m_menuOpen = false;
    m_pointerOverBar = false;
    m_view.setFullscreen(false);
    if (m_window)
        hideWindow();
    emit activeChanged();
    updateBar();
    emit panelChanged();
    restoreFocus();
    return true;
}

void VideoFullscreenController::hideWindow()
{
    const std::uint64_t placing = ++m_placing;
    QScreen *mainScreen = m_main ? m_main->screen() : nullptr;
    if (positionsKnown() || !mainScreen || !m_window->isVisible() || !m_window->isActive()
        || m_window->screen() == mainScreen) {
        m_window->hide();
        return;
    }
    // Wayland: hidden on another output, the keyboard stays there (sway marks the main window urgent), so carry it back first.
    QPointer<QQuickWindow> w = m_window;
    auto step = std::make_shared<int>(0);
    auto resized = std::make_shared<QMetaObject::Connection>();
    auto finish = [this, w, step, resized, placing] {
        disconnect(*resized);
        if (w && placing == m_placing && !m_active)
            w->hide();
    };
    auto next = [this, w, step, resized, placing, mainScreen = QPointer<QScreen>(mainScreen), finish] {
        if (!w || placing != m_placing || m_active) {
            disconnect(*resized);
            return;
        }
        if (*step == 1 && mainScreen) {
            *step = 2;
            w->setScreen(mainScreen);
            w->showFullScreen();
            QTimer::singleShot(1000, w, finish);
        } else if (*step == 2) {
            finish();
        }
    };
    *resized = connect(w, &QWindow::widthChanged, this, [next] { QTimer::singleShot(0, next); });
    *step = 1;
    w->showNormal();
    QTimer::singleShot(1000, w, [next, step] {
        if (*step == 1)
            next();
    });
}

void VideoFullscreenController::restoreFocus()
{
    QWindow *w = m_focusWindow ? m_focusWindow.data() : m_main.data();
    QQuickItem *item = m_focusItem;
    if (!item || item->window() != w || !item->isVisible())
        item = m_focusFallback;
    if (w && w != m_window) {
        w->requestActivate();
        if (item && item->isVisible())
            item->forceActiveFocus(Qt::OtherFocusReason);
        else if (item)
            QTimer::singleShot(0, item, [item] { item->forceActiveFocus(Qt::OtherFocusReason); });
    }
    m_focusWindow = nullptr;
    m_focusItem = nullptr;
}

QVariantMap VideoFullscreenController::progressBar(qreal clientWidth, qreal textWidth, qreal textHeight) const
{
    const auto &session = m_video.session();
    const auto bar = application::fullscreenProgressBar(static_cast<int>(clientWidth), static_cast<int>(textWidth),
                                                        static_cast<int>(textHeight), session.tellMs(),
                                                        session.durationMs());
    auto rect = [](int l, int t, int r, int b) { return QRectF(l, t, r - l + 1, b - t + 1); };
    return {{QStringLiteral("frame"), rect(bar.frameLeft, bar.frameTop, bar.frameRight, bar.frameBottom)},
            {QStringLiteral("inner"), rect(bar.innerLeft, bar.innerTop, bar.innerRight, bar.innerBottom)},
            {QStringLiteral("bar"), QRectF(bar.barLeft, bar.barY - bar.barWidth / 2.0, bar.barRight - bar.barLeft,
                                           bar.barWidth)},
            {QStringLiteral("text"), QRectF(bar.textLeft, bar.textTop, bar.textRight - bar.textLeft,
                                            bar.textBottom - bar.textTop)}};
}

}
