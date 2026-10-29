import QtQuick
import QtQuick.Controls
import Hikari.Ui

Button {
    id: button
    property string iconRole
    property string tip
    icon.source: iconRole.length === 0 ? ""
        : IconTheme.image(iconRole, enabled ? IconTheme.normal : IconTheme.disabled,
                          enabled ? IconTheme.accent : IconTheme.disabled,
                          mirrored && IconTheme.mirrors(iconRole))
    icon.color: "transparent"
    icon.width: 16
    icon.height: 16
    ToolTip.visible: hovered && tip.length > 0
    ToolTip.text: tip
}
