// PK1 (#301): the popover's surface, the colour popover's (SY2) made
// shared. MuseScore 4's popup (docs/research/musescore-shell.md, "Popups"):
// the panel surface, radius 4, a 1-pixel boundary and a shadow.
import QtQuick
import Hikari.Ui

Rectangle {
    color: Theme.panel
    border.color: Theme.line
    radius: 4
    // The shadow: rings fading outwards (DockFloatingWindow's).
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
