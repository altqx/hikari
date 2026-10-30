#pragma once

#include <QJSValue>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QtQml/qqmlregistration.h>

#include <map>
#include <optional>
#include <vector>

class QKeyEvent;
class QQuickItem;
class QQuickWindow;

namespace hikari::ui {

class KeyRouter : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QJSValue fallbackAccepts READ fallbackAccepts WRITE setFallbackAccepts)
    Q_PROPERTY(QQuickItem *fallbackTarget READ fallbackTarget WRITE setFallbackTarget)
    Q_PROPERTY(QJSValue followsPointer READ followsPointer WRITE setFollowsPointer)
    Q_PROPERTY(QQuickItem *hoveredPanel READ hoveredPanel NOTIFY hoveredPanelChanged)

public:
    explicit KeyRouter(QObject *parent = nullptr);
    ~KeyRouter() override;

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
    // The pointer entered `panel` while followsPointer() holds and nothing keeps the focus where it is.
    void focusRequested(QQuickItem *panel);

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
        // MUST KILL: a null target means the key is swallowed
        QPointer<QQuickItem> target;
    };

    bool shellWindow(QQuickWindow *window) const;
    const Panel *panelAt(QQuickWindow *window, QPointF scenePos) const;
    const Panel *panelOf(QQuickItem *item) const;
    void setHovered(QQuickItem *panel);
    void hoverAtPointer();
    bool focusStays() const;
    bool followingPointer() const;
    bool shortcutOverride(QQuickWindow *window, QKeyEvent *event);
    bool keyPress(QKeyEvent *event);
    bool keyRelease(QKeyEvent *event);
    bool takes(QQuickItem *target, const QKeyEvent *event);

    std::vector<Panel> m_panels;
    QPointer<QQuickItem> m_hovered;
    QPointer<QQuickWindow> m_pointerWindow;
    QPointF m_pointerPos;
    QJSValue m_fallbackAccepts;
    QJSValue m_followsPointer;
    QPointer<QQuickItem> m_fallbackTarget;
    QPointer<QQuickItem> m_typedIn;
    std::optional<Routed> m_routed;
    std::map<int, QPointer<QQuickItem>> m_held;
    const QEvent *m_withheld = nullptr;
    bool m_dispatching = false;
};

}
