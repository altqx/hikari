import QtQuick
import QtQuick.Controls
import Hikari.Ui

ToolButton {
    id: button
    property string iconRole
    property string tip: text
    property int iconSize: 16
    display: AbstractButton.IconOnly
    contentItem: Icon {
        iconRole: button.iconRole
        size: button.iconSize
    }
    Accessible.name: text
    ToolTip.visible: hovered && tip.length > 0
    // Word joiners keep the tooltip's rich-text detection from reading "<" or "&lt;" as markup.
    ToolTip.text: tip.replace(/</g, "<\u2060").replace(/&lt;/g, "&\u2060lt;")
}
