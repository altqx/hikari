#include "dialog_placement.h"

#include "settings_store.h"

#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QtMath>

#include <algorithm>

namespace hikari::ui {

namespace dialogplacement {

namespace {

std::optional<bool> g_wayland;

bool isWayland()
{
    if (g_wayland)
        return *g_wayland;
    return QGuiApplication::platformName().startsWith(QLatin1String("wayland"));
}

QPoint inside(const QRect &outer, const QRect &area)
{
    const int x = std::clamp(outer.x(), area.left(), std::max(area.left(), area.right() + 1 - outer.width()));
    const int y = std::clamp(outer.y(), area.top(), std::max(area.top(), area.bottom() + 1 - outer.height()));
    return {x, y};
}

}

QRect centred(const QRect &opener, QSize size, const QRect &available, const QMargins &margins)
{
    const QRect area = available.marginsRemoved(QMargins(kMargin, kMargin, kMargin, kMargin));
    size = size.boundedTo(area.size().shrunkBy(margins)).expandedTo(QSize(1, 1));
    const QSize outerSize = size.grownBy(margins);
    QRect outer(QPoint(0, 0), outerSize);
    outer.moveCenter(opener.center() + QPoint(0, kDrop));
    outer.moveTopLeft(inside(outer, area));
    return {outer.topLeft() + QPoint(margins.left(), margins.top()), size};
}

std::optional<QRect> restored(const QRect &saved, QSize size, QSize minimum, bool resizable,
                              const QList<QRect> &screens, const QMargins &margins)
{
    if (!saved.isValid())
        return std::nullopt;
    const QRect edge(saved.x() - margins.left(), saved.y() - margins.top(), saved.width() + margins.left() + margins.right(), 1);
    const QRect *screen = nullptr;
    int best = 0;
    for (const QRect &available : screens) {
        const int on = edge.intersected(available).width();
        if (on > best) {
            best = on;
            screen = &available;
        }
    }
    if (!screen || best < std::min(kGrip, edge.width()))
        return std::nullopt;
    const int room = resizable ? 0 : kMargin;
    if (resizable)
        size = saved.size().expandedTo(minimum);
    size = size.boundedTo(screen->size().shrunkBy(QMargins(room, room, room, room)).shrunkBy(margins)).expandedTo(QSize(1, 1));
    const QRect outer(saved.topLeft() - QPoint(margins.left(), margins.top()), size.grownBy(margins));
    return QRect(inside(outer, *screen) + QPoint(margins.left(), margins.top()), size);
}

std::optional<QRect> find(const QStringList &rows, const QString &id)
{
    for (const QString &row : rows) {
        const qsizetype tab = row.indexOf(QLatin1Char('\t'));
        if (tab < 0 || QStringView(row).left(tab) != id)
            continue;
        const QStringList parts = row.mid(tab + 1).split(QLatin1Char(','));
        if (parts.size() != 4)
            return std::nullopt;
        int values[4];
        for (int i = 0; i < 4; ++i) {
            bool ok = false;
            values[i] = parts[i].toInt(&ok);
            if (!ok)
                return std::nullopt;
        }
        const QRect rect(values[0], values[1], values[2], values[3]);
        return rect.isValid() ? std::optional(rect) : std::nullopt;
    }
    return std::nullopt;
}

QStringList replaced(QStringList rows, const QString &id, const QRect &geometry)
{
    const QString row = id + QLatin1Char('\t')
                        + QStringLiteral("%1,%2,%3,%4").arg(geometry.x()).arg(geometry.y()).arg(geometry.width()).arg(geometry.height());
    const QString prefix = id + QLatin1Char('\t');
    for (QString &existing : rows) {
        if (existing.startsWith(prefix)) {
            existing = row;
            return rows;
        }
    }
    rows << row;
    return rows;
}

}

DialogPlacement::DialogPlacement(QObject *parent)
    : QObject(parent)
{
}

void DialogPlacement::setWindow(QQuickWindow *window)
{
    if (m_window == window)
        return;
    if (m_window)
        disconnect(m_window, nullptr, this, nullptr);
    m_window = window;
    m_shown = QWindow::Windowed;
    if (m_window) {
        connect(m_window, &QWindow::visibilityChanged, this, [this](QWindow::Visibility visibility) {
            if (visibility != QWindow::Hidden)
                m_shown = visibility;
        });
    }
    emit windowChanged();
}

void DialogPlacement::setDialogId(const QString &id)
{
    if (m_dialogId == id)
        return;
    m_dialogId = id;
    emit dialogIdChanged();
}

void DialogPlacement::setSettings(SettingsStore *settings)
{
    if (m_settings == settings)
        return;
    m_settings = settings;
    emit settingsChanged();
}

void DialogPlacement::setResizable(bool resizable)
{
    if (m_resizable == resizable)
        return;
    m_resizable = resizable;
    emit resizableChanged();
}

QWindow *DialogPlacement::opener() const
{
    QWindow *focus = QGuiApplication::focusWindow();
    // Menus and tool tips are windows of their own on some platforms.
    while (focus && focus->transientParent()
           && (focus->type() == Qt::Popup || focus->type() == Qt::ToolTip))
        focus = focus->transientParent();
    if (focus && focus != m_window && focus->isVisible())
        return focus;
    return m_window ? m_window->transientParent() : nullptr;
}

QSize DialogPlacement::natural() const
{
    return m_content ? QSize(qCeil(m_content->implicitWidth()), qCeil(m_content->implicitHeight())) : QSize();
}

void DialogPlacement::fitRoom(QScreen *on, const QMargins &margins)
{
    namespace dp = dialogplacement;
    if (on) {
        const QMargins room(dp::kMargin, dp::kMargin, dp::kMargin, dp::kMargin);
        const QSize size = on->availableGeometry().size().shrunkBy(room).shrunkBy(margins).expandedTo(QSize(1, 1));
        if (size != m_room) {
            m_room = size;
            emit roomChanged();
        }
    }
    QQuickItem *root = m_window->contentItem();
    root->setWidth(std::min(natural().width(), m_room.width()));
    settle(root);
}

namespace {
std::function<void(const QString &)> &openedHook()
{
    static std::function<void(const QString &)> hook;
    return hook;
}
}

void DialogPlacement::setOpenedHook(std::function<void(const QString &dialogId)> hook)
{
    openedHook() = std::move(hook);
}

void DialogPlacement::place(QWindow *opener, QQuickItem *content)
{
    if (!m_window || !content)
        return;
    if (openedHook() && !m_dialogId.isEmpty())
        openedHook()(m_dialogId);
    namespace dp = dialogplacement;
    m_content = content;
    if (m_window->windowStates() & (Qt::WindowMaximized | Qt::WindowFullScreen))
        m_window->setWindowStates(Qt::WindowNoState);
    std::optional<QRect> saved;
    if (m_settings && !m_dialogId.isEmpty())
        saved = dp::find(m_settings->list(dp::kSetting), m_dialogId);
    QScreen *screen = opener && opener->isVisible() ? opener->screen() : nullptr;
    if (!screen)
        screen = m_window->transientParent() ? m_window->transientParent()->screen() : QGuiApplication::primaryScreen();
    settle(m_window->contentItem());
    if (dp::isWayland()) {
        // The Wayland compositor places windows; only the size is ours.
        fitRoom(screen, {});
        const QSize smallest = natural().boundedTo(m_room);
        QSize size = smallest;
        if (saved && m_resizable) {
            size = saved->size().expandedTo(smallest);
            if (screen)
                size = size.boundedTo(screen->availableGeometry().size());
        }
        m_window->resize(size);
        return;
    }
    // Frame margins exist only after create(), and on X11 only once mapped; until then the opener's.
    m_window->create();
    QMargins margins = m_window->frameMargins();
    if (margins.isNull() && !m_window->flags().testFlag(Qt::FramelessWindowHint) && opener
        && !opener->flags().testFlag(Qt::FramelessWindowHint))
        margins = opener->frameMargins();
    QList<QRect> screens;
    for (QScreen *each : QGuiApplication::screens())
        screens << each->availableGeometry();
    fitRoom(screen, margins);
    if (saved) {
        if (auto rect = dp::restored(*saved, natural(), natural(), m_resizable, screens, margins)) {
            QScreen *on = QGuiApplication::screenAt(rect->center());
            if (on && on != screen) {
                fitRoom(on, margins);
                rect = dp::restored(*saved, natural(), natural(), m_resizable, screens, margins);
            }
            if (rect) {
                m_window->setGeometry(*rect);
                return;
            }
        }
    }
    if (!screen) {
        m_window->resize(natural());
        return;
    }
    const QRect available = screen->availableGeometry();
    const QRect around = opener && opener->isVisible() ? opener->frameGeometry() : available;
    m_window->setGeometry(dp::centred(around, natural(), available, margins));
}

void DialogPlacement::refit()
{
    if (!m_window || !m_content || !m_window->isVisible()
        || (m_window->windowStates() & (Qt::WindowMaximized | Qt::WindowFullScreen | Qt::WindowMinimized)))
        return;
    namespace dp = dialogplacement;
    QScreen *screen = m_window->screen();
    if (dp::isWayland()) {
        fitRoom(screen, {});
        if (screen) {
            const QSize size = m_window->size().boundedTo(screen->availableGeometry().size()).expandedTo(m_window->minimumSize());
            if (size != m_window->size())
                m_window->resize(size);
        }
        return;
    }
    const QMargins margins = m_window->frameMargins();
    fitRoom(screen, margins);
    QList<QRect> screens;
    for (QScreen *each : QGuiApplication::screens())
        screens << each->availableGeometry();
    const QSize size = m_resizable ? m_window->size() : natural();
    if (const auto rect = dp::restored(m_window->geometry(), size, natural(), m_resizable, screens, margins)) {
        if (*rect != m_window->geometry())
            m_window->setGeometry(*rect);
        }
}

void DialogPlacement::showWithoutActivating(bool quiet)
{
    // Qt's own: xcb gives the window no user time, Windows shows it without activating.
    if (m_window)
        m_window->setProperty("_q_showWithoutActivating", quiet);
}

void DialogPlacement::unminimize()
{
    if (m_window && (m_window->windowStates() & Qt::WindowMinimized))
        m_window->setWindowStates(m_window->windowStates() & ~Qt::WindowMinimized);
}

void DialogPlacement::remember()
{
    if (!m_window || !m_settings || m_dialogId.isEmpty())
        return;
    if (m_shown == QWindow::Maximized || m_shown == QWindow::FullScreen)
        return;
    namespace dp = dialogplacement;
    const QStringList rows = m_settings->list(dp::kSetting);
    const QStringList next = dp::replaced(rows, m_dialogId, m_window->geometry());
    if (next != rows)
        m_settings->setValue(QString::fromLatin1(dp::kSetting), next);
}

bool DialogPlacement::inPopup(QQuickItem *item)
{
    for (QQuickItem *at = item; at; at = at->parentItem()) {
        if (at->inherits("QQuickOverlay") || at->inherits("QQuickPopupItem"))
            return true;
    }
    return false;
}

namespace {

void polishTree(QQuickItem *item)
{
    const QList<QQuickItem *> children = item->childItems();
    for (QQuickItem *child : children)
        polishTree(child);
    item->ensurePolished();
}

}

void DialogPlacement::settle(QQuickItem *item)
{
    if (!item)
        return;
    // A hidden window never polishes; a second pass lets outer layout widths reflow wrapped text.
    polishTree(item);
    polishTree(item);
}

void DialogPlacement::overrideWayland(std::optional<bool> wayland)
{
    dialogplacement::g_wayland = wayland;
}

}
