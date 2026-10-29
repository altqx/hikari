import QtQuick
import Hikari.Ui
import com.kdab.dockwidgets 2.0 as KDDW

Rectangle {
    id: root
    objectName: "dockSeparator"
    anchors.fill: parent
    color: Theme.line

    readonly property KDDW.SeparatorView kddwSeparator: parent
    readonly property bool vertical: kddwSeparator ? kddwSeparator.isVertical : false
    readonly property int grabMargin: 5

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
