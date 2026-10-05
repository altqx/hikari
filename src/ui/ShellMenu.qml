import QtQuick
import QtQuick.Controls
import Hikari.Ui

// A shell menu: its submenu entries are ShellMenuItems too.
//
// K1: with an iconRole, a submenu shows that icon of the set on its item in
// the parent menu (legacy menus' bitmaps on submenus: recent files, sorting,
// conversion, last session), in the normal or disabled colours of the
// appearance.
//
// P10: `help` is the help its item in the parent menu shows (ShellMenuItem).
// StatusHelp counts the open shell menus: the status bar's first field
// empties when the last one closes (legacy HideMenus).
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
