#include "key_router.h"

#include <QCoreApplication>
#include <QKeyEvent>
#include <QQuickItem>
#include <QQuickWindow>

namespace hikari::ui {

namespace {

// A field that takes typing (TextInput, TextEdit and the controls made of
// them while editable): Qt Quick sets the flag only while it is not read-only.
bool takesText(const QQuickItem *item)
{
    return item && (item->flags() & QQuickItem::ItemAcceptsInputMethod);
}

// A control the keyboard moved to (Tab, F6, a menu's keys): Qt Quick
// Controls' visualFocus. A click leaves it false.
bool keyboardFocused(const QQuickItem *item)
{
    return item && item->property("visualFocus").toBool();
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

} // namespace

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

const KeyRouter::Panel *KeyRouter::panelOf(QQuickItem *item) const
{
    for (QQuickItem *i = item; i; i = i->parentItem())
        for (const auto &p : m_panels)
            if (p.panel == i)
                return &p;
    return nullptr;
}

// The handler's ShortcutOverride answer: whether it binds the key. The
// shell's handlers only look the key up there; nothing runs.
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

bool KeyRouter::shortcutOverride(QQuickWindow *window, QKeyEvent *event)
{
    m_routed.reset();
    QQuickItem *focus = window->activeFocusItem();
    if (takesText(focus) || keyboardFocused(focus) || inPopup(focus))
        return false;
    const Panel *context = panelOf(focus);
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
    return false;
}

bool KeyRouter::keyPress(QKeyEvent *event)
{
    if (!m_routed)
        return false;
    const Routed routed = *m_routed;
    m_routed.reset();
    if (!sameKey(event, routed.key, routed.modifiers) || !routed.target)
        return false;
    QKeyEvent copy(event->type(), event->key(), event->modifiers(), event->nativeScanCode(), event->nativeVirtualKey(),
                   event->nativeModifiers(), event->text(), event->isAutoRepeat(), event->count());
    m_dispatching = true;
    QCoreApplication::sendEvent(routed.target, &copy);
    m_dispatching = false;
    return true;
}

bool KeyRouter::eventFilter(QObject *watched, QEvent *event)
{
    if (m_dispatching)
        return false;
    switch (event->type()) {
    case QEvent::ShortcutOverride: {
        // Qt Quick's delivery agent offers the override to the focus item,
        // then to its parents until one accepts. The router answers at the
        // focus item.
        auto *item = qobject_cast<QQuickItem *>(watched);
        QQuickWindow *window = item ? item->window() : qobject_cast<QQuickWindow *>(watched);
        if (!window || (item && item != window->activeFocusItem()))
            return false;
        if (!item && window->activeFocusItem())
            return false; // the agent brings it to the focus item next
        if (!shellWindow(window))
            return false;
        return shortcutOverride(window, static_cast<QKeyEvent *>(event));
    }
    case QEvent::KeyPress: {
        auto *window = qobject_cast<QQuickWindow *>(watched);
        if (!window)
            return false;
        return keyPress(static_cast<QKeyEvent *>(event));
    }
    default:
        return false;
    }
}

} // namespace hikari::ui
