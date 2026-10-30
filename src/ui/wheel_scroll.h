#pragma once

#include <QObject>
#include <QPointF>
#include <QtQml/qqmlregistration.h>

class QQuickItem;
class QWheelEvent;

namespace hikari::ui {

// Pixels a wheel event scrolls: the touchpad's own distance, or the system's lines per notch of `lineHeight`.
// With only vertical `room`, Shift+wheel's sideways delta scrolls vertically.
QPointF wheelScrollPixels(const QWheelEvent &event, qreal lineHeight, Qt::Orientations room);

// While one exists, every interactive Flickable scrolls by wheelScrollPixels() of the application font's lines.
class WheelScroll : public QObject {
    Q_OBJECT
    QML_ELEMENT

public:
    explicit WheelScroll(QObject *parent = nullptr);
    ~WheelScroll() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    static bool scroll(QQuickItem *flickable, const QWheelEvent &event);
};

}
