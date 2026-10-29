#include "key_router.h"

#include <QCoreApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickWindow>

namespace hikari::ui {

namespace {

// Qt Quick sets ItemAcceptsInputMethod only while a field is editable.
bool takesText(const QQuickItem *item)
{
    return item && (item->flags() & QQuickItem::ItemAcceptsInputMethod);
}

// visualFocus is Qt Quick Controls' keyboard-only focus; a click leaves it false.
bool keyboardFocused(const QQuickItem *item)
{
    return item && item->property("visualFocus").toBool();
}

bool recordsKeys(const QQuickItem *item)
{
    return item && item->property("recordsKeys").toBool();
}

bool inPopup(const QQuickItem *item)
{
    for (const QQuickItem *p = item; p; p = p->parentItem())
        if (p->inherits("QQuickPopupItem"))
            return true;
    return false;
}

bool sameKey(const QKeyEvent *event, int key, int modifiers)
{
    return event->key() == key && int(event->modifiers()) == modifiers;
}

}

KeyRouter::KeyRouter(QObject *parent) : QObject(parent)
{
    QCoreApplication::instance()->installEventFilter(this);
}

KeyRouter::~KeyRouter()
{
    if (auto *app = QCoreApplication::instance())
        app->removeEventFilter(this);
}

void KeyRouter::addPanel(QQuickItem *panel, QQuickItem *keyTarget)
{
    if (!panel)
        return;
    m_panels.push_back({panel, keyTarget ? keyTarget : panel});
}

bool KeyRouter::shellWindow(QQuickWindow *window) const
{
    for (const auto &p : m_panels)
        if (p.panel && p.panel->window() == window)
            return true;
    return false;
}

const KeyRouter::Panel *KeyRouter::panelAt(QQuickWindow *window, QPointF scenePos) const
{
    for (const auto &p : m_panels) {
        if (!p.panel || p.panel->window() != window || !p.panel->isVisible())
            continue;
        if (p.panel->contains(p.panel->mapFromScene(scenePos)))
            return &p;
    }
    return nullptr;
}

const KeyRouter::Panel *KeyRouter::panelOf(QQuickItem *item) const
{
    for (QQuickItem *i = item; i; i = i->parentItem())
        for (const auto &p : m_panels)
            if (p.panel == i)
                return &p;
    return nullptr;
}

void KeyRouter::setHovered(QQuickItem *panel)
{
    if (m_hovered == panel)
        return;
    m_hovered = panel;
    emit hoveredPanelChanged();
}

bool KeyRouter::takes(QQuickItem *target, const QKeyEvent *event)
{
    if (!target)
        return false;
    QKeyEvent probe(QEvent::ShortcutOverride, event->key(), event->modifiers(), event->nativeScanCode(),
                    event->nativeVirtualKey(), event->nativeModifiers(), event->text(), event->isAutoRepeat(),
                    event->count());
    probe.setAccepted(false);
    m_dispatching = true;
    QCoreApplication::sendEvent(target, &probe);
    m_dispatching = false;
    return probe.isAccepted();
}

void KeyRouter::hoverAtPointer()
{
    const Panel *p = m_pointerWindow ? panelAt(m_pointerWindow, m_pointerPos) : nullptr;
    setHovered(p ? p->panel.data() : nullptr);
}

bool KeyRouter::shortcutOverride(QQuickWindow *window, QKeyEvent *event)
{
    m_routed.reset();
    if (!event->isAutoRepeat())
        m_held.erase(event->key());
    hoverAtPointer();
    QQuickItem *focus = window->activeFocusItem();
    if (takesText(focus) || keyboardFocused(focus) || recordsKeys(focus) || inPopup(focus))
        return false;
    const Panel *focused = panelOf(focus);
    const Panel *hovered = nullptr;
    if (!m_followsPointer.isCallable() || m_followsPointer.call().toBool())
        for (const auto &p : m_panels)
            if (p.panel && p.panel == m_hovered && p.panel->isVisible())
                hovered = &p;
    if (hovered && focused && hovered->panel == focused->panel)
        hovered = nullptr;
    const Panel *context = hovered ? hovered : focused;
    const auto route = [&](QQuickItem *target) {
        m_routed = Routed{event->key(), int(event->modifiers()), target};
        event->accept();
        return true;
    };
    if (context && takes(context->keyTarget, event))
        return route(context->keyTarget);
    if (m_fallbackTarget && m_fallbackAccepts.isCallable()) {
        const QJSValue answer = m_fallbackAccepts.call({event->key(), int(event->modifiers())});
        if (answer.toBool() && takes(m_fallbackTarget, event))
            return route(m_fallbackTarget);
    }
    if (hovered && focused && takes(focused->keyTarget, event)) {
        // Unaccepted, the override leaves the key to Qt's shortcut map (the Global bindings).
        m_routed = Routed{event->key(), int(event->modifiers()), nullptr};
        event->ignore();
        return true;
    }
    return false;
}

bool KeyRouter::keyPress(QKeyEvent *event)
{
    if (!m_routed)
        return false;
    const Routed routed = *m_routed;
    m_routed.reset();
    if (!sameKey(event, routed.key, routed.modifiers))
        return false;
    if (routed.target) {
        QKeyEvent copy(event->type(), event->key(), event->modifiers(), event->nativeScanCode(),
                       event->nativeVirtualKey(), event->nativeModifiers(), event->text(), event->isAutoRepeat(),
                       event->count());
        QQuickWindow *window = routed.target->window();
        const QPointer<QQuickItem> focus = window ? window->activeFocusItem() : nullptr;
        const Panel *from = panelOf(focus);
        const Panel *to = panelOf(routed.target);
        m_dispatching = true;
        QCoreApplication::sendEvent(routed.target, &copy);
        m_dispatching = false;
        m_held[event->key()] = routed.target;
        if (focus && window && to != from && window->activeFocusItem() != focus
            && panelOf(window->activeFocusItem()) == to)
            focus->forceActiveFocus(Qt::OtherFocusReason);
    }
    return true;
}

bool KeyRouter::keyRelease(QKeyEvent *event)
{
    const auto it = m_held.find(event->key());
    if (it == m_held.end())
        return false;
    const QPointer<QQuickItem> target = it->second;
    if (!event->isAutoRepeat()) // Qt's auto-repeat: a release and a press while held
        m_held.erase(it);
    if (!target)
        return false;
    QKeyEvent copy(event->type(), event->key(), event->modifiers(), event->nativeScanCode(),
                   event->nativeVirtualKey(), event->nativeModifiers(), event->text(), event->isAutoRepeat(),
                   event->count());
    m_dispatching = true;
    QCoreApplication::sendEvent(target, &copy);
    m_dispatching = false;
    return true;
}

bool KeyRouter::eventFilter(QObject *watched, QEvent *event)
{
    if (m_dispatching)
        return false;
    switch (event->type()) {
    case QEvent::MouseMove: {
        auto *window = qobject_cast<QQuickWindow *>(watched);
        if (!window || !shellWindow(window))
            return false;
        m_pointerWindow = window;
        m_pointerPos = static_cast<QMouseEvent *>(event)->scenePosition();
        hoverAtPointer();
        return false;
    }
    case QEvent::Leave: {
        auto *window = qobject_cast<QQuickWindow *>(watched);
        if (window && window == m_pointerWindow) {
            m_pointerWindow = nullptr;
            setHovered(nullptr);
        }
        return false;
    }
    case QEvent::ShortcutOverride: {
        // Qt Quick offers the override to the focus item, then to its parents until one accepts.
        auto *item = qobject_cast<QQuickItem *>(watched);
        QQuickWindow *window = item ? item->window() : qobject_cast<QQuickWindow *>(watched);
        if (!window)
            return false;
        if (item && item != window->activeFocusItem()) {
            if (event != m_withheld)
                return false;
            event->ignore();
            return true;
        }
        if (!item && window->activeFocusItem())
            return false; // Qt Quick brings it to the focus item next
        m_withheld = nullptr;
        if (!shellWindow(window))
            return false;
        const bool filtered = shortcutOverride(window, static_cast<QKeyEvent *>(event));
        if (filtered && !event->isAccepted())
            m_withheld = event;
        return filtered;
    }
    case QEvent::KeyPress: {
        // A posted key reaches the focus item without passing the window, so the route waits there.
        if (!qobject_cast<QQuickWindow *>(watched) && !(m_routed && qobject_cast<QQuickItem *>(watched)))
            return false;
        return keyPress(static_cast<QKeyEvent *>(event));
    }
    case QEvent::KeyRelease: {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (!qobject_cast<QQuickWindow *>(watched)
            && !(m_held.contains(key->key()) && qobject_cast<QQuickItem *>(watched)))
            return false;
        return keyRelease(static_cast<QKeyEvent *>(event));
    }
    default:
        return false;
    }
}

}
