import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import Hikari.Ui

T.Button {
    id: button
    implicitWidth: Math.max(80, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(30, implicitContentHeight + topPadding + bottomPadding)
    padding: 5
    leftPadding: 10
    rightPadding: 10
    focusPolicy: Qt.StrongFocus
    background: Rectangle {
        color: !button.enabled ? "transparent"
             : button.down ? Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.24)
             : button.hovered ? Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.12)
             : Theme.raised
        border.width: 1
        border.color: button.enabled ? Theme.danger : Theme.line
        radius: 1
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
