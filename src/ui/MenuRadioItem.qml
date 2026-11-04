import QtQuick
import QtQuick.Controls
import Hikari.Ui

ShellMenuItem {
    id: radioItem
    checkable: true
    keepsMenuOpen: false
    Accessible.role: Accessible.RadioButton
    indicator: Rectangle {
        readonly property color ink: radioItem.enabled && (radioItem.down || radioItem.highlighted)
                                     ? radioItem.palette.highlightedText : radioItem.palette.text
        x: radioItem.mirrored ? radioItem.width - width - radioItem.rightPadding : radioItem.leftPadding
        y: radioItem.topPadding + (radioItem.availableHeight - height) / 2
        implicitWidth: 14
        implicitHeight: 14
        radius: 7
        color: "transparent"
        border.color: ink
        Rectangle {
            anchors.centerIn: parent
            width: 6
            height: 6
            radius: 3
            color: parent.ink
            visible: radioItem.checked
        }
}
}
