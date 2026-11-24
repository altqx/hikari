import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T
import Hikari.Ui

T.Button {
    id: chip
    property string iconRole
    property bool on: false
    implicitWidth: implicitContentWidth + leftPadding + rightPadding
    implicitHeight: Theme.controlHeight
    leftPadding: 8
    rightPadding: 6
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.ButtonMenu
    background: Rectangle {
        radius: height / 2
        color: chip.on ? Theme.select : Theme.raised
        border.width: 1
        border.color: chip.on ? Theme.accent : Theme.control
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            visible: chip.enabled && (chip.hovered || chip.down)
            color: chip.down ? Theme.pressed : Theme.hover
        }
        FocusRing { control: chip; radius: parent.radius + 1 }
    }
    contentItem: RowLayout {
        spacing: 5
        Icon {
            iconRole: chip.iconRole
            size: 14
            visible: chip.iconRole.length > 0
        }
        Label {
            textFormat: Text.PlainText
            text: chip.text
            font.weight: chip.on ? Font.DemiBold : Font.Normal
            elide: Text.ElideRight
            Layout.maximumWidth: 200
        }
        Icon {
            iconRole: "chevron-down"
            size: 14
        }
    }
}
