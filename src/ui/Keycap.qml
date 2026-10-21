import QtQuick
import QtQuick.Controls
import Hikari.Ui

// KA2 (#277): a key as the text names it ("Space", "Esc", "["): its name on
// a raised cap with a thicker bottom edge, for hints that show which key to
// press. A shape, so it reads without its colours too.
Rectangle {
    id: cap
    property alias text: label.text
    implicitWidth: Math.max(implicitHeight, label.implicitWidth + 8)
    implicitHeight: label.implicitHeight + 3
    radius: 3
    color: Theme.raised
    border.color: Theme.control
    Accessible.role: Accessible.StaticText
    Accessible.name: text
    // the cap's bottom edge
    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 2
        radius: 1
        color: Theme.control
    }
    Label {
        id: label
        anchors.horizontalCenter: parent.horizontalCenter
        y: 1
        color: Theme.text
        font.bold: true
        font.pixelSize: Math.round(Qt.application.font.pixelSize * 0.8)
    }
}
