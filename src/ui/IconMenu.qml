// K1: a submenu with an icon of the set on its item in the parent menu
// (legacy menus' bitmaps on submenus: recent files, sorting, conversion,
// last session), in the normal or disabled colours of the appearance.
import QtQuick
import QtQuick.Controls
import Hikari.Ui

Menu {
    id: menu
    property string iconRole
    icon.source: iconRole.length === 0 ? ""
        : IconTheme.image(iconRole, enabled ? IconTheme.normal : IconTheme.disabled,
                          enabled ? IconTheme.accent : IconTheme.disabled, mirrored && IconTheme.mirrors(iconRole))
    icon.color: "transparent"
    icon.width: 16
    icon.height: 16
}
