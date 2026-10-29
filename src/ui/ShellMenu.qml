import QtQuick
import QtQuick.Controls
import Hikari.Ui

Menu {
    id: menu
    property string iconRole
    property string help
    onAboutToShow: StatusHelp.menuOpened()
    onAboutToHide: StatusHelp.menuClosed()
    Component.onDestruction: if (visible) StatusHelp.menuClosed()
    icon.source: iconRole.length === 0 ? ""
        : IconTheme.image(iconRole, enabled ? IconTheme.normal : IconTheme.disabled,
                          enabled ? IconTheme.accent : IconTheme.disabled, mirrored && IconTheme.mirrors(iconRole))
    icon.color: "transparent"
    icon.width: 16
    icon.height: 16
    delegate: ShellMenuItem {}
}
