import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import Hikari.Ui

T.Button {
    id: button
    implicitWidth: Math.max(80, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(Theme.controlHeight, implicitContentHeight + topPadding + bottomPadding)
    topPadding: 3
    bottomPadding: 3
    leftPadding: 12
    rightPadding: 12
    focusPolicy: Qt.StrongFocus
    background: Rectangle {
        color: !button.enabled ? "transparent"
             : button.down ? Color.transparent(Theme.danger, 0.24)
             : button.hovered ? Color.transparent(Theme.danger, 0.12)
             : Theme.raised
        border.width: 1
        border.color: button.enabled ? Theme.danger : Theme.line
        radius: Theme.radius
        FocusRing { control: button }
    }
    contentItem: Label {
        text: button.text
        color: button.enabled ? Theme.danger : Theme.disabled
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
