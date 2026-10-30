import QtQuick
import Hikari.Ui

Item {
    id: frame
    objectName: "videoZoomFrame"
    required property VideoViewController view
    visible: view.zoomMode
    readonly property rect hole: view.zoomFrame
    readonly property rect bounds: view.zoomBounds
    readonly property color dim: Color.rgba(0, 0, 0, 0x88 / 255)

    Rectangle {
        x: 0; y: 0
        width: frame.bounds.width; height: Math.max(0, frame.hole.y)
        color: frame.dim
    }
    Rectangle {
        x: 0; y: frame.hole.y + frame.hole.height
        width: frame.bounds.width; height: Math.max(0, frame.bounds.height - y)
        color: frame.dim
    }
    Rectangle {
        x: 0; y: frame.hole.y
        width: Math.max(0, frame.hole.x); height: frame.hole.height
        color: frame.dim
    }
    Rectangle {
        x: frame.hole.x + frame.hole.width; y: frame.hole.y
        width: Math.max(0, frame.bounds.width - x); height: frame.hole.height
        color: frame.dim
    }
    Rectangle {
        objectName: "videoZoomOutline"
        x: frame.hole.x; y: frame.hole.y
        width: frame.hole.width + 1; height: frame.hole.height + 1
        color: "transparent"
        border.width: 1
        border.color: Theme.accent
    }
}
