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
             : button.down ? Color.darker(Theme.accent, Theme.dark ? 1.25 : 1.35)
             : button.hovered ? Color.darker(Theme.accent, Theme.dark ? 1.1 : 1.15)
             : Theme.accent
        border.width: button.enabled ? 0 : 1
        border.color: Theme.line
        radius: 1
        FocusRing { control: button }
    }
    contentItem: Label {
        text: button.text
        font.bold: true
        color: button.enabled ? Theme.onAccent : Theme.disabled
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
