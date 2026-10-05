import QtQuick
import QtQuick.Controls
import Hikari.Ui

// A shell menu: its submenu entries are ShellMenuItems too.
//
// K1: with an iconRole, a submenu shows that icon of the set on its item in
// the parent menu (legacy menus' bitmaps on submenus: recent files, sorting,
// conversion, last session), in the normal or disabled colours of the
// appearance.
Menu {
    id: menu
    property string iconRole
    icon.source: iconRole.length === 0 ? ""
        : IconTheme.image(iconRole, enabled ? IconTheme.normal : IconTheme.disabled,
                          enabled ? IconTheme.accent : IconTheme.disabled, mirrored && IconTheme.mirrors(iconRole))
    icon.color: "transparent"
    icon.width: 16
    icon.height: 16
    delegate: ShellMenuItem {}
    // As wide as its widest item: the style's menu is only as wide as its
    // background (200), which cut longer names short ("Shift times / run
    // time post proce...", D2).
    contentWidth: {
        let widest = 0
        for (let i = 0; i < count; ++i) {
            const item = itemAt(i)
            if (item)
                widest = Math.max(widest, item.implicitWidth)
        }
        return widest
    }
}
