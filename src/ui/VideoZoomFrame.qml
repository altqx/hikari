import QtQuick
import Hikari.Ui

// V4: the zoom mode's frame over the video, RendererVideo::DrawZoom
// (RendererVideo.cpp:831-871): everything from the video window's corner to
// the video's right and bottom edges outside the zoom rectangle dimmed
// (legacy 0x88000000), and the rectangle's one-pixel outline (legacy
// 0xFFBB0000) through its corners x, y and width - 1, height - 1. No
// per-colour settings (the 2026-10-05 appearance decision): the dimming
// over the video is legacy's black at 0x88 in every theme, as the other
// marks drawn on the video keep theirs, and the outline, a selection-type
// mark, is the theme layer's (K2) accent. The visual tools are not drawn
// meanwhile.
Item {
    id: frame
    objectName: "videoZoomFrame"
    required property VideoViewController view
    visible: view.zoomMode
    readonly property rect hole: view.zoomFrame
    readonly property rect bounds: view.zoomBounds
    readonly property color dim: Qt.rgba(0, 0, 0, 0x88 / 255)

    Rectangle { // above the rectangle
        x: 0; y: 0
        width: frame.bounds.width; height: Math.max(0, frame.hole.y)
        color: frame.dim
    }
    Rectangle { // below it
        x: 0; y: frame.hole.y + frame.hole.height
        width: frame.bounds.width; height: Math.max(0, frame.bounds.height - y)
        color: frame.dim
    }
    Rectangle { // left of it
        x: 0; y: frame.hole.y
        width: Math.max(0, frame.hole.x); height: frame.hole.height
        color: frame.dim
    }
    Rectangle { // right of it
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
