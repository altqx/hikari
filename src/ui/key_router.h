#pragma once

// KB1/KB2: which panel a key's hotkeys act on. The shell's panels keep their
// own key handlers (their Keys.onShortcutOverride / Keys.onPressed run the
// context's bindings, HotkeysController::actionFor); the router chooses the
// handler before the focused control sees the key:
//
// - the hovered panel's handler when the pointer is over a panel other than
//   the focused one (the hotkey context follows the pointer; nothing takes
//   keyboard focus on hover), else the focused panel's;
// - a key that panel does not bind goes to the fallback target when
//   `fallbackAccepts(key, modifiers)` says so (Space plays and pauses
//   everywhere);
// - a key the focused panel binds but the hovered one does not is the
//   focused panel's no longer: the Global bindings may still take it.
//
// It stands aside while a text field has focus (typing is never redirected
// by hover), while a control the keyboard moved to has focus (it keeps its
// own Space and arrows), inside a popup and in any window that holds no
// registered panel.

#include <QJSValue>
#include <QObject>
#include <QPointF>
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
    // () => bool, asked at each key: whether hotkeys follow the hovered
    // panel (true when unset).
    Q_PROPERTY(QJSValue followsPointer READ followsPointer WRITE setFollowsPointer)
    // The panel under the pointer, if any (tests and diagnostics).
    Q_PROPERTY(QQuickItem *hoveredPanel READ hoveredPanel NOTIFY hoveredPanelChanged)

public:
    explicit KeyRouter(QObject *parent = nullptr);
    ~KeyRouter() override;

    // A panel and the item holding its key handlers (the panel itself when null).
    Q_INVOKABLE void addPanel(QQuickItem *panel, QQuickItem *keyTarget = nullptr);
    QQuickItem *hoveredPanel() const { return m_hovered; }
    QQuickItem *fallbackTarget() const { return m_fallbackTarget; }
    void setFallbackTarget(QQuickItem *target) { m_fallbackTarget = target; }
    QJSValue fallbackAccepts() const { return m_fallbackAccepts; }
    void setFallbackAccepts(const QJSValue &f) { m_fallbackAccepts = f; }
    QJSValue followsPointer() const { return m_followsPointer; }
    void setFollowsPointer(const QJSValue &f) { m_followsPointer = f; }

signals:
    void hoveredPanelChanged();

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
        QPointer<QQuickItem> target; // null: swallowed
    };

    bool shellWindow(QQuickWindow *window) const;
    const Panel *panelAt(QQuickWindow *window, QPointF scenePos) const;
    const Panel *panelOf(QQuickItem *item) const;
    void setHovered(QQuickItem *panel);
    void hoverAtPointer();
    bool shortcutOverride(QQuickWindow *window, QKeyEvent *event);
    bool keyPress(QKeyEvent *event);
    bool takes(QQuickItem *target, const QKeyEvent *event);

    std::vector<Panel> m_panels;
    QPointer<QQuickItem> m_hovered;
    // Where the pointer last moved: panels moved or resized under a still
    // pointer change the hovered panel without a move.
    QPointer<QQuickWindow> m_pointerWindow;
    QPointF m_pointerPos;
    QJSValue m_fallbackAccepts;
    QJSValue m_followsPointer;
    QPointer<QQuickItem> m_fallbackTarget;
    std::optional<Routed> m_routed;
    const QEvent *m_withheld = nullptr; // an override kept from the focused panel's items
    bool m_dispatching = false;
};

} // namespace hikari::ui
