#pragma once

// The Audio panel's display (A1): scene-graph geometry for the shapes the
// AudioController describes (legacy AudioDisplay's Direct3D drawing), with
// the mouse cursor in a node of its own so moving it never rebuilds the
// image. Shapes are in Direct3D 9 coordinates, whose pixel centres are at
// integers; they are drawn half a pixel to the right and down. Fonts follow
// legacy's sizes around the application font: the ruler one point smaller,
// the cursor time three larger, labels one larger.

#include <QFont>
#include <QPointer>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

namespace hikari::ui {

class AudioController;

class AudioDisplayItem : public QQuickItem {
    Q_OBJECT
    QML_NAMED_ELEMENT(AudioDisplay)
    Q_PROPERTY(QObject *controller READ controller WRITE setController NOTIFY controllerChanged)
    // Rows under the waveform for the time ruler (legacy timelineHeight).
    Q_PROPERTY(int timelineHeight READ timelineHeight CONSTANT)
public:
    explicit AudioDisplayItem(QQuickItem *parent = nullptr);

    QObject *controller() const;
    void setController(QObject *controller);
    int timelineHeight() const;
    QFont font(int role) const; // AudioShape::Font

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

private:
    void pushSize();

    QPointer<AudioController> m_controller;
    QFont m_scale, m_cursor, m_label;
    quint64 m_drawnRevision = 0;
};

} // namespace hikari::ui
