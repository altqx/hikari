// K1: an icon-only tool button (legacy MappedButton with a bitmap). Its text
// stays its accessible name and is not drawn; the tooltip shows `tip`, by
// default that text. The icon takes the hover/pressed and disabled colours
// with the button.
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
        hovered: button.hovered
        pressed: button.down
    }
    Accessible.name: text
    ToolTip.visible: hovered && tip.length > 0
    ToolTip.text: tip
}
