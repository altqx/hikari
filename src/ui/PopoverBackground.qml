import QtQuick
import Hikari.Ui

Rectangle {
    color: Theme.panel
    border.color: Theme.line
    radius: 4
    Repeater {
        model: 8
        Rectangle {
            required property int index
            z: -1
            anchors.fill: parent
            anchors.margins: -(index + 1)
            radius: 4 + index + 1
            color: "transparent"
            border.color: Qt.rgba(0, 0, 0, 0.12 * Math.pow((8 - index) / 8, 2))
        }
    }
}
