import QtQuick
import Hikari.Ui

Item {
    id: port
    property string type
    property bool hollow: false
    readonly property color colour: Theme.ports[type] ?? Theme.muted
    implicitWidth: 10
    implicitHeight: 10

    Rectangle {
        anchors.centerIn: parent
        width: port.type === "Units" ? 8 : 10
        height: width
        rotation: port.type === "Units" ? 45 : 0
        radius: port.type === "Units" || port.type === "Tags" ? 1 : width / 2
        color: port.hollow ? Theme.field : port.colour
        border.width: port.hollow ? 2 : 1
        border.color: port.hollow ? port.colour : Qt.darker(port.colour, 1.25)
        antialiasing: true
    }
}
