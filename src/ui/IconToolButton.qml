// K1: an icon-only tool button (legacy MappedButton with a bitmap). Its text
// stays its accessible name and is not drawn; the tooltip shows `tip`, by
// default that text. The icon takes the disabled colour with the button;
// hover and press shade the button's panel.
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
    // Always plain (a tip may name a font from a file): the shared tooltip
    // reads text whose "<" opens a tag as rich text; a word joiner after
    // each "<" and "&" of "&lt;" keeps it plain.
    ToolTip.text: tip.replace(/</g, "<\u2060").replace(/&lt;/g, "&\u2060lt;")
}
