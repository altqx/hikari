import QtQuick
import Hikari.Ui

Rectangle {
    objectName: "dockDropHighlight"
    anchors.fill: parent
    color: Color.transparent(Theme.accent, 0.3)
    border.width: 1
    border.color: Theme.accent
}
