import QtQuick
import QtQuick.Controls
import Hikari.Ui

TabButton {
    id: button
    property string iconRole
    icon.source: iconRole.length === 0 ? ""
        : IconTheme.image(iconRole, enabled ? IconTheme.normal : IconTheme.disabled,
                          enabled ? IconTheme.accent : IconTheme.disabled,
                          mirrored && IconTheme.mirrors(iconRole))
    icon.color: "transparent"
    icon.width: 16
    icon.height: 16
}
