// K1: a tab button with an icon of the set beside its text, in the
// appearance's colours (disabled while disabled; hover and press leave the
// icon as it is). Directional icons mirror in right-to-left layouts.
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
