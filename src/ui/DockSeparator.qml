import QtQuick
import Hikari.Ui
import com.kdab.dockwidgets 2.0 as KDDW

// D3: the docking engine's separator between panels, after MuseScore's
// DockSeparator.qml (docs/research/musescore-docking.md §5): 1 pixel (the
// adapter's separator thickness) in the theme's boundary colour, with a hit
// area 5 pixels wider on each side so it is easy to grab. Loaded through the
// adapter's view factory (ui/docking.cpp).
Rectangle {
    id: root
    objectName: "dockSeparator"
    anchors.fill: parent
    color: Theme.line

    readonly property KDDW.SeparatorView kddwSeparator: parent
    readonly property bool vertical: kddwSeparator ? kddwSeparator.isVertical : false
    readonly property int grabMargin: 5

    // Above the groups beside it, so the wider hit area reaches over their edges.
    Component.onCompleted: if (kddwSeparator) kddwSeparator.z = 1

    MouseArea {
        objectName: "dockSeparatorHandle"
        anchors.fill: parent
        anchors.leftMargin: root.vertical ? 0 : -root.grabMargin
        anchors.rightMargin: root.vertical ? 0 : -root.grabMargin
        anchors.topMargin: root.vertical ? -root.grabMargin : 0
        anchors.bottomMargin: root.vertical ? -root.grabMargin : 0
        cursorShape: root.vertical ? Qt.SizeVerCursor : Qt.SizeHorCursor
        onPressed: root.kddwSeparator.onMousePressed()
        onReleased: root.kddwSeparator.onMouseReleased()
        onPositionChanged: mouse => root.kddwSeparator.onMouseMoved(mapToItem(root, mouse.x, mouse.y))
        onDoubleClicked: root.kddwSeparator.onMouseDoubleClicked()
    }
}
