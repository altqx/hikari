import QtQuick
import com.kdab.dockwidgets 2.0

// D1: the splitter between docked panels, the engine's Separator.qml with
// the application palette's window colour in place of its fixed light grey,
// so the seams follow the light or dark appearance. Loaded through the
// adapter's view factory (ui/docking.cpp).
Rectangle {
    id: root
    objectName: "dockSeparator"
    anchors.fill: parent
    color: root.palette.window

    readonly property SeparatorView kddwSeparator: parent // qmllint disable incompatible-type

    MouseArea {
        cursorShape: root.kddwSeparator ? (root.kddwSeparator.isVertical ? Qt.SizeVerCursor : Qt.SizeHorCursor) : Qt.SizeHorCursor
        anchors.fill: parent
        onPressed: root.kddwSeparator.onMousePressed()
        onReleased: root.kddwSeparator.onMouseReleased()
        onPositionChanged: mouse => root.kddwSeparator.onMouseMoved(Qt.point(mouse.x, mouse.y))
        onDoubleClicked: root.kddwSeparator.onMouseDoubleClicked()
    }
}
