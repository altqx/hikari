// K1: an icon-only button. Its text stays its accessible name (and is not
// drawn); the tooltip shows `tip`, by default that text. The icon takes the
// hover/pressed and disabled colours with the button.
import QtQuick
import QtQuick.Controls
import Hikari.Ui

Button {
    id: button
    required property string iconRole
    property string tip: text
    property int iconSize: 16
    display: AbstractButton.IconOnly
    padding: 6
    implicitWidth: implicitContentWidth + leftPadding + rightPadding
    implicitHeight: implicitContentHeight + topPadding + bottomPadding
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
