import QtQuick
import Hikari.Ui

Item {
    id: root
    objectName: "mainWindowEdges"
    required property MainWindowFrame frame
    readonly property Window window: frame.window
    readonly property bool shown: frame.custom && !WindowFrame.nativeResize && window !== null
        && window.visibility !== Window.Maximized && window.visibility !== Window.FullScreen
    readonly property int band: WindowFrame.resizeBand

    visible: shown

    Rectangle {
        objectName: "mainWindowBoundary"
        anchors.fill: parent
        color: "transparent"
        border.width: 1
        border.color: Theme.line
    }

    component ResizeEdge: MouseArea {
        required property int edges
        visible: root.frame.resizable
        cursorShape: {
            const horizontal = (edges & (Qt.LeftEdge | Qt.RightEdge)) !== 0
            const vertical = (edges & (Qt.TopEdge | Qt.BottomEdge)) !== 0
            if (horizontal && vertical)
                return (edges === (Qt.LeftEdge | Qt.TopEdge) || edges === (Qt.RightEdge | Qt.BottomEdge))
                       ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor
            return horizontal ? Qt.SizeHorCursor : Qt.SizeVerCursor
        }
        onPressed: root.window.startSystemResize(edges)
    }
    readonly property int corner: band + 6
    ResizeEdge { objectName: "mainResizeLeft"; edges: Qt.LeftEdge; x: 0; y: root.corner; width: root.band; height: root.height - 2 * root.corner }
    ResizeEdge { objectName: "mainResizeRight"; edges: Qt.RightEdge; x: root.width - root.band; y: root.corner; width: root.band; height: root.height - 2 * root.corner }
    ResizeEdge { objectName: "mainResizeTop"; edges: Qt.TopEdge; x: root.corner; y: 0; width: root.width - 2 * root.corner; height: root.band }
    ResizeEdge { objectName: "mainResizeBottom"; edges: Qt.BottomEdge; x: root.corner; y: root.height - root.band; width: root.width - 2 * root.corner; height: root.band }
    ResizeEdge { edges: Qt.LeftEdge | Qt.TopEdge; x: 0; y: 0; width: root.corner; height: root.band }
    ResizeEdge { edges: Qt.LeftEdge | Qt.TopEdge; x: 0; y: 0; width: root.band; height: root.corner }
    ResizeEdge { edges: Qt.RightEdge | Qt.TopEdge; x: root.width - root.corner; y: 0; width: root.corner; height: root.band }
    ResizeEdge { edges: Qt.RightEdge | Qt.TopEdge; x: root.width - root.band; y: 0; width: root.band; height: root.corner }
    ResizeEdge { edges: Qt.LeftEdge | Qt.BottomEdge; x: 0; y: root.height - root.band; width: root.corner; height: root.band }
    ResizeEdge { edges: Qt.LeftEdge | Qt.BottomEdge; x: 0; y: root.height - root.corner; width: root.band; height: root.corner }
    ResizeEdge { edges: Qt.RightEdge | Qt.BottomEdge; x: root.width - root.corner; y: root.height - root.band; width: root.corner; height: root.band }
    ResizeEdge { edges: Qt.RightEdge | Qt.BottomEdge; x: root.width - root.band; y: root.height - root.corner; width: root.band; height: root.corner }
}
