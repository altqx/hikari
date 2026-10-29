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
    }
    Accessible.name: text
    ToolTip.visible: hovered && tip.length > 0
    ToolTip.text: tip
}
