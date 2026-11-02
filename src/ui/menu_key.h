#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointF>
#include <QtQml/qqmlregistration.h>

class QQuickItem;
class QQuickWindow;

namespace hikari::ui {

// The Menu key and Shift+F10 open the context menu of the focused item: the nearest MenuKey at or above it
// gets a point at the focused item, else Qt Quick gets a keyboard ContextMenu event at that point.
class MenuKey : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("MenuKey is an attached property")
    QML_ATTACHED(MenuKey)
    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY enabledChanged FINAL)
public:
    explicit MenuKey(QObject *parent = nullptr);
    static MenuKey *qmlAttachedProperties(QObject *object);

    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    static void install();
    static bool request(QQuickWindow *window);

signals:
    void enabledChanged();
    void requested(QPointF position);

private:
    bool m_enabled = true;
};

class MenuKeyFilter : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QElapsedTimer m_answered;
};

}
