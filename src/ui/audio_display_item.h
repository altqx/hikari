#pragma once

#include "hikari/application/audio_timing.h"

#include <QFont>
#include <QImage>
#include <QPointer>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

class QSinglePointEvent;

namespace hikari::ui {

class AudioController;

class AudioDisplayItem : public QQuickItem {
    Q_OBJECT
    QML_NAMED_ELEMENT(AudioDisplay)
    Q_PROPERTY(QObject *controller READ controller WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(int timelineHeight READ timelineHeight CONSTANT)
public:
    explicit AudioDisplayItem(QQuickItem *parent = nullptr);

    QObject *controller() const;
    void setController(QObject *controller);
    int timelineHeight() const;
    // MUST KILL: font takes an AudioShape::Font as an int
    QFont font(int role) const;

signals:
    void controllerChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void hoverLeaveEvent(QHoverEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseUngrabEvent() override;

private:
    void pushSize();
    // MUST KILL: timingEvent's type is application::AudioMouse::Type as an int
    application::AudioMouseResult timingEvent(const QSinglePointEvent *event, int type);

    QPointer<AudioController> m_controller;
    QFont m_scale, m_cursor, m_label;
    quint64 m_drawnRevision = 0;
    QImage m_sceneImage;
};

}
