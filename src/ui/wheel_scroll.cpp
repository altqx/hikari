#include "wheel_scroll.h"

#include <QCoreApplication>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QQuickItem>
#include <QStyleHints>
#include <QWheelEvent>

#include <algorithm>

namespace hikari::ui {

namespace {

enum FlickableDirection { HorizontalFlick = 0x1, VerticalFlick = 0x2 };

struct Axis {
    const char *position;
    qreal at;
    qreal first;
    qreal last;

    qreal after(qreal pixels) const { return std::clamp(at - pixels, first, last); }
};

Axis axis(const QQuickItem &flickable, Qt::Orientation orientation)
{
    const bool vertical = orientation == Qt::Vertical;
    const auto real = [&flickable](const char *name) { return flickable.property(name).toReal(); };
    const char *position = vertical ? "contentY" : "contentX";
    const qreal origin = real(vertical ? "originY" : "originX");
    const qreal first = origin - real(vertical ? "topMargin" : "leftMargin");
    const qreal last = origin + real(vertical ? "contentHeight" : "contentWidth")
                       + real(vertical ? "bottomMargin" : "rightMargin")
                       - (vertical ? flickable.height() : flickable.width());
    return {position, real(position), first, std::max(first, last)};
}

}

QPointF wheelScrollPixels(const QWheelEvent &event, qreal lineHeight, Qt::Orientations room)
{
    // Wayland gives mouse wheels a pixel delta too; only the source tells a touchpad apart.
    const bool touchpad = !event.pixelDelta().isNull() && event.source() != Qt::MouseEventNotSynthesized;
    QPointF pixels = touchpad ? QPointF(event.pixelDelta())
                              : QPointF(event.angleDelta()) / QWheelEvent::DefaultDeltasPerStep
                                    * QGuiApplication::styleHints()->wheelScrollLines() * lineHeight;
    if (room == Qt::Vertical && pixels.y() == 0 && event.modifiers() & Qt::ShiftModifier)
        pixels = QPointF(0, pixels.x());
    return pixels;
}

WheelScroll::WheelScroll(QObject *parent) : QObject(parent)
{
    QCoreApplication::instance()->installEventFilter(this);
}

WheelScroll::~WheelScroll()
{
    if (auto *app = QCoreApplication::instance())
        app->removeEventFilter(this);
}

bool WheelScroll::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() != QEvent::Wheel || !watched->inherits("QQuickFlickable"))
        return false;
    auto *flickable = static_cast<QQuickItem *>(watched);
    auto *wheel = static_cast<QWheelEvent *>(event);
    if (!flickable->property("interactive").toBool() || (wheel->angleDelta().isNull() && wheel->pixelDelta().isNull()))
        return false;
    wheel->setAccepted(scroll(flickable, *wheel));
    return true;
}

bool WheelScroll::scroll(QQuickItem *flickable, const QWheelEvent &event)
{
    const int direction = flickable->property("flickableDirection").toInt();
    const Axis vertical = axis(*flickable, Qt::Vertical);
    const Axis horizontal = axis(*flickable, Qt::Horizontal);
    Qt::Orientations room;
    if (direction != HorizontalFlick && vertical.last > vertical.first)
        room |= Qt::Vertical;
    if (direction != VerticalFlick && horizontal.last > horizontal.first)
        room |= Qt::Horizontal;
    const QPointF pixels = wheelScrollPixels(event, QFontMetricsF(QGuiApplication::font()).lineSpacing(), room);
    const qreal y = room.testFlag(Qt::Vertical) ? vertical.after(pixels.y()) : vertical.at;
    const qreal x = room.testFlag(Qt::Horizontal) ? horizontal.after(pixels.x()) : horizontal.at;
    if (y == vertical.at && x == horizontal.at)
        return false;
    QMetaObject::invokeMethod(flickable, "movementStarted");
    if (y != vertical.at)
        flickable->setProperty(vertical.position, y);
    if (x != horizontal.at)
        flickable->setProperty(horizontal.position, x);
    QMetaObject::invokeMethod(flickable, "movementEnded");
    return true;
}

}
