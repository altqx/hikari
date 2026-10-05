#include "video_fullscreen_controller.h"

#include "video_controller.h"
#include "video_view_controller.h"

#include "hikari/application/video_fullscreen.h"

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
    // Wayland clients do not know where their windows are; the compositor
    // says which outputs a surface is on (QWindow::screen).
    return !QGuiApplication::platformName().startsWith(QLatin1String("wayland"));
}

} // namespace

VideoFullscreenController::VideoFullscreenController(VideoController &video, VideoViewController &view,
                                                     QObject *parent)
    : QObject(parent), m_video(video), m_view(view)
{
    connect(&m_video, &VideoController::changed, this, [this] {
        // The video went (Unload video, another editing target): the
        // fullscreen window has nothing left to show.
        if (m_active && !m_video.hasVideo())
            leave();
        if (!m_pendingPath.isEmpty() && m_video.openRequest() != m_pendingRequest) {
            m_pendingPath.clear(); // another open, or none made
        } else if (!m_pendingPath.isEmpty()) {
            if (m_video.hasVideo()) {
                // The video shown before the open started stays until it does.
                const bool same = QFileInfo(QString::fromStdString(m_video.session().path())) == QFileInfo(m_pendingPath);
                if (same || m_pendingOpening)
                    m_pendingPath.clear();
                if (same && !m_active)
                    enter(0);
            } else if (m_video.indexing()) {
                m_pendingOpening = true;
            } else if (m_pendingOpening) {
                m_pendingPath.clear(); // the open failed or was cancelled
            }
        }
        emit progressChanged();
    });
}

void VideoFullscreenController::setWindows(QQuickWindow *fullscreen, QWindow *main)
{
    m_window = fullscreen;
    m_main = main;
}

QQuickWindow *VideoFullscreenController::window() const
{
    return m_window;
}

QList<QScreen *> VideoFullscreenController::monitors()
{
    // GetMonitorRect1's list: on Windows MonitorEnumProc1's, the primary
    // first, then the others in EnumDisplayMonitors' order; on Linux
    // wxDisplay's. Qt's screens are that list: its primary screen is always
    // the first (QGuiApplication::primaryScreen), the others follow in the
    // system's order (on Wayland the outputs' order, as wxDisplay's).
    return QGuiApplication::screens();
}

int VideoFullscreenController::monitorFor(int monitor) const
{
    const auto screens = monitors();
    std::vector<application::MonitorRect> rects;
    for (QScreen *s : screens)
        rects.push_back(rectOf(s->geometry()));
    if (monitor == 0 && screens.size() > 1 && m_main && !positionsKnown()) {
        // The program's centre is unknown here: the monitor the main window is on.
        const int i = static_cast<int>(screens.indexOf(m_main->screen()));
        return i >= 0 ? i : 0;
    }
    const QRect program = m_main ? m_main->frameGeometry() : QRect();
    return application::fullscreenMonitor(monitor, rects, rectOf(program));
}

void VideoFullscreenController::setShowToolbar(bool on)
{
    // The "Show toolbar" check (VideoFullscreen.cpp:82-95): the toolbar row
    // and m_PanelOnFullscreen follow it; the panel stays shown.
    if (m_showToolbar == on)
        return;
    m_showToolbar = on;
    // The check is on the panel, so it is shown when the check is clicked;
    // pinned, it stays shown.
    if (on && m_active)
        m_panelShown = true;
    emit panelChanged();
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
    return m_video.hasVideo() ? QFileInfo(QString::fromStdString(m_video.session().path())).fileName() : QString();
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
    // Fullscreen on the chosen output: X11 and Windows place the window on
    // the screen's rectangle, Wayland asks the compositor for that output
    // (xdg_toplevel.set_fullscreen with the window's screen).
    if (m_window->isVisible())
        m_window->hide();
    const std::uint64_t placing = ++m_placing;
    QScreen *mainScreen = m_main ? m_main->screen() : nullptr;
    if (!positionsKnown() && mainScreen && mainScreen != screen) {
        // A Wayland compositor need not give the keyboard to a window mapped
        // on another output than the focused one (sway gives it only to new
        // windows of the focused workspace). Mapped first on the main
        // window's output, where it takes the keyboard, then made fullscreen
        // on the chosen output, which moves it there with the keyboard.
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
    // On Wayland the request made before the surface is mapped can go
    // unanswered when the window opens on another output than the focused
    // one; asked again once its first frame is shown.
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
        // What had the keyboard, to give it back on leaving.
        m_focusWindow = QGuiApplication::focusWindow();
        auto *quick = qobject_cast<QQuickWindow *>(m_focusWindow.data());
        m_focusItem = quick ? quick->activeFocusItem() : nullptr;
        // A menu's item (the context menu's Full screen and monitors) goes
        // with its menu: the fallback takes the keyboard back.
        for (QQuickItem *i = m_focusItem; i; i = i->parentItem())
            if (i->inherits("QQuickPopupItem")) {
                m_focusItem = nullptr;
                break;
            }
    }
    m_active = true;
    m_monitor = index;
    // `if (monitor && tab->editor)`: the argument, not where it lands.
    m_onAnotherMonitor = monitor != 0;
    // `if (!m_PanelOnFullscreen) panel->Hide()`; pinned, it is shown.
    m_panelShown = m_showToolbar;
    m_view.setFullscreen(true);
    place(screens[index]);
    emit activeChanged();
    emit panelChanged();
    emit progressChanged();
    return true;
}

bool VideoFullscreenController::leave()
{
    if (!m_active)
        return false;
    // `if (GetState() == Playing){ if (tab->editor){ Pause(); } ...}`: the
    // editor is always on in the rewrite.
    if (m_video.playing())
        m_video.pause();
    m_active = false;
    m_onAnotherMonitor = false;
    m_view.setFullscreen(false);
    if (m_window)
        hideWindow();
    emit activeChanged();
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
    // Wayland, on another output than the main window's: hidden there, the
    // keyboard stays on that output, and a compositor need not let the main
    // window take it back (sway marks it urgent). Carried back first, made
    // fullscreen on the main window's output (out of fullscreen, then in,
    // each once the compositor's answer resized the window), then hidden,
    // the keyboard falls to the main window. At most a second for each.
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
        else if (item) // a dock opened again shows its panel a moment later
            QTimer::singleShot(0, item, [item] { item->forceActiveFocus(Qt::OtherFocusReason); });
    }
    m_focusWindow = nullptr;
    m_focusItem = nullptr;
}

bool VideoFullscreenController::pointerMoved(qreal y, qreal height, qreal panelHeight)
{
    if (!m_active)
        return false;
    using C = application::FullscreenPanelChange;
    const C change = application::fullscreenPanelOnPointer(m_showToolbar, m_panelShown, static_cast<int>(y),
                                                           static_cast<int>(height), static_cast<int>(panelHeight));
    if (change == C::None)
        return false;
    m_panelShown = change == C::Show;
    emit panelChanged();
    return change == C::Hide; // ... panel->Show(false); SetFocus();
}

QVariantMap VideoFullscreenController::progressBar(qreal clientWidth, qreal textWidth, qreal textHeight) const
{
    const auto &session = m_video.session();
    const auto bar = application::fullscreenProgressBar(static_cast<int>(clientWidth), static_cast<int>(textWidth),
                                                        static_cast<int>(textHeight), session.tellMs(),
                                                        session.durationMs());
    // Each line's pixels from its first to its last point.
    auto rect = [](int l, int t, int r, int b) { return QRectF(l, t, r - l + 1, b - t + 1); };
    return {{QStringLiteral("frame"), rect(bar.frameLeft, bar.frameTop, bar.frameRight, bar.frameBottom)},
            {QStringLiteral("inner"), rect(bar.innerLeft, bar.innerTop, bar.innerRight, bar.innerBottom)},
            {QStringLiteral("bar"), QRectF(bar.barLeft, bar.barY - bar.barWidth / 2.0, bar.barRight - bar.barLeft,
                                           bar.barWidth)},
            {QStringLiteral("text"), QRectF(bar.textLeft, bar.textTop, bar.textRight - bar.textLeft,
                                            bar.textBottom - bar.textTop)}};
}

} // namespace hikari::ui
