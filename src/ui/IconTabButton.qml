// K1: a tab button with an icon of the set beside its text, in the
// appearance's colours (hover/pressed while hovered or pressed, disabled while
// disabled). Directional icons mirror in right-to-left layouts.
import QtQuick
import QtQuick.Controls
import Hikari.Ui

TabButton {
    id: button
    property string iconRole
    readonly property bool iconActive: enabled && (hovered || down)
    icon.source: iconRole.length === 0 ? ""
        : IconTheme.image(iconRole, !enabled ? IconTheme.disabled : iconActive ? IconTheme.active : IconTheme.normal,
                          !enabled ? IconTheme.disabled : iconActive ? IconTheme.active : IconTheme.accent,
                          mirrored && IconTheme.mirrors(iconRole))
    icon.color: "transparent"
    icon.width: 16
    icon.height: 16
}
