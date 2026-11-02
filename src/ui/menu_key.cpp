#include "menu_key.h"

#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtQml/qqml.h>

#include <algorithm>

namespace hikari::ui {

namespace {

constexpr qint64 kPlatformEchoMs = 500;
bool g_forwarding = false;

bool opensMenu(const QKeyEvent *event)
{
    if (event->isAutoRepeat())
        return false;
    const auto mods = event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    return (event->key() == Qt::Key_Menu && mods == Qt::NoModifier)
        || (event->key() == Qt::Key_F10 && mods == Qt::ShiftModifier);
}

QPointF anchorIn(const QQuickItem *focus)
{
    const QVariant cursor = focus->property("cursorRectangle");
    if (cursor.isValid()) {
        const QRectF r = cursor.toRectF();
        return {std::clamp(r.left(), 0.0, focus->width()), std::clamp(r.bottom(), 0.0, focus->height())};
    }
    return {0, focus->height()};
}

MenuKey *menuKeyOf(QQuickItem *item)
{
    auto *key = qobject_cast<MenuKey *>(qmlAttachedPropertiesObject<MenuKey>(item, false));
    return key && key->isEnabled() && item->isVisible() ? key : nullptr;
}

bool forwardToQtQuick(QQuickWindow *window, QQuickItem *focus)
{
    const QPointF at = focus->mapToScene(anchorIn(focus));
    QContextMenuEvent request(QContextMenuEvent::Keyboard, at.toPoint(), window->mapToGlobal(at).toPoint());
    request.setAccepted(false);
    g_forwarding = true;
    QCoreApplication::sendEvent(window, &request);
    g_forwarding = false;
    return request.isAccepted();
}

}

MenuKey::MenuKey(QObject *parent) : QObject(parent) {}

MenuKey *MenuKey::qmlAttachedProperties(QObject *object)
{
    return new MenuKey(object);
}

void MenuKey::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    emit enabledChanged();
}

void MenuKey::install()
{
    auto *app = QCoreApplication::instance();
    if (app && !app->findChild<MenuKeyFilter *>(QString(), Qt::FindDirectChildrenOnly))
        app->installEventFilter(new MenuKeyFilter(app));
}

bool MenuKey::request(QQuickWindow *window)
{
    QQuickItem *focus = window ? window->activeFocusItem() : nullptr;
    if (!focus)
        return false;
    for (QQuickItem *item = focus; item; item = item->parentItem())
        if (MenuKey *key = menuKeyOf(item)) {
            emit key->requested(item->mapFromItem(focus, anchorIn(focus)));
            return true;
        }
    return forwardToQtQuick(window, focus);
}

// Windows also reports the Menu key and Shift+F10 as a keyboard context menu event (WM_CONTEXTMENU).
bool MenuKeyFilter::eventFilter(QObject *watched, QEvent *event)
{
    auto *window = qobject_cast<QQuickWindow *>(watched);
    if (!window || g_forwarding)
        return false;
    if (event->type() == QEvent::KeyPress && opensMenu(static_cast<QKeyEvent *>(event))) {
        if (!MenuKey::request(window))
            return false;
        m_answered.start();
        return true;
    }
    if (event->type() == QEvent::ContextMenu
        && static_cast<QContextMenuEvent *>(event)->reason() == QContextMenuEvent::Keyboard) {
        if (!(m_answered.isValid() && m_answered.elapsed() < kPlatformEchoMs))
            MenuKey::request(window);
        return true;
    }
    return false;
}

}
