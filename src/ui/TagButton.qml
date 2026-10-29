import QtQuick
import QtQuick.Controls
import Hikari.Ui

ToolButton {
    id: button
    property string iconRole
    property string tip: text
    Accessible.name: text.length > 0 ? text : IconTheme.label(iconRole)
    ToolTip.visible: hovered && tip.length > 0
    ToolTip.text: tip
    contentItem: Item {
        implicitWidth: button.iconRole.length > 0 ? 16 : label.implicitWidth
        implicitHeight: Math.max(16, label.implicitHeight)
        Icon {
            anchors.centerIn: parent
            visible: button.iconRole.length > 0
            iconRole: button.iconRole
        }
        Label {
            id: label
            anchors.centerIn: parent
            visible: button.iconRole.length === 0
            text: button.text
            font: button.font
            color: button.enabled ? button.palette.buttonText : Theme.disabled
        }
    }
}
