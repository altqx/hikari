#pragma once

// KB2: which handler a key's hotkeys run on. The shell's panels keep their
// own key handlers (their Keys.onShortcutOverride / Keys.onPressed run the
// context's bindings, HotkeysController::actionFor); the router runs the
// focused panel's handler before the focused control sees the key, and a
// key that panel does not bind goes to the fallback target when
// `fallbackAccepts(key, modifiers)` says so (Space plays and pauses
// everywhere).
//
// It stands aside while a text field has focus, while a control the
// keyboard moved to has focus (it keeps its own Space and arrows), inside a
// popup and in any window that holds no registered panel.

#include <QJSValue>
#include <QObject>
#include <QPointer>
#include <QtQml/qqmlregistration.h>

#include <optional>
#include <vector>

class QKeyEvent;
class QQuickItem;
class QQuickWindow;

namespace hikari::ui {

class KeyRouter : public QObject {
    Q_OBJECT
    QML_ELEMENT
    // (key, modifiers) => bool: a key no panel takes goes to fallbackTarget.
    Q_PROPERTY(QJSValue fallbackAccepts READ fallbackAccepts WRITE setFallbackAccepts)
    Q_PROPERTY(QQuickItem *fallbackTarget READ fallbackTarget WRITE setFallbackTarget)

public:
    explicit KeyRouter(QObject *parent = nullptr);
    ~KeyRouter() override;

    // A panel and the item holding its key handlers (the panel itself when null).
    Q_INVOKABLE void addPanel(QQuickItem *panel, QQuickItem *keyTarget = nullptr);
    QQuickItem *fallbackTarget() const { return m_fallbackTarget; }
    void setFallbackTarget(QQuickItem *target) { m_fallbackTarget = target; }
    QJSValue fallbackAccepts() const { return m_fallbackAccepts; }
    void setFallbackAccepts(const QJSValue &f) { m_fallbackAccepts = f; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Panel {
        QPointer<QQuickItem> panel;
        QPointer<QQuickItem> keyTarget;
    };
    struct Routed {
        int key = 0;
        int modifiers = 0;
        QPointer<QQuickItem> target;
    };

    bool shellWindow(QQuickWindow *window) const;
    const Panel *panelOf(QQuickItem *item) const;
    bool shortcutOverride(QQuickWindow *window, QKeyEvent *event);
    bool keyPress(QKeyEvent *event);
    bool takes(QQuickItem *target, const QKeyEvent *event);

    std::vector<Panel> m_panels;
    QJSValue m_fallbackAccepts;
    QPointer<QQuickItem> m_fallbackTarget;
    std::optional<Routed> m_routed;
    bool m_dispatching = false;
};

} // namespace hikari::ui
