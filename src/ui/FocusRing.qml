import QtQuick
import QtQuick.Templates as T
import Hikari.Ui

Rectangle {
    id: ring
    required property T.Control control
    readonly property int offset: 0
    objectName: "focusRing"
    anchors.fill: parent
    anchors.margins: -(offset + border.width)
    radius: 0
    color: "transparent"
    border.width: 1
    border.color: Theme.focus
    visible: ring.control.visualFocus
}
