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
    // AT-SPI and UI Automation list a menu's items in stacking order; the list view stacks an item it adopts later last.
    function restack() {
        for (let i = count - 2; i >= 0; --i)
            ShellMenus.stackBefore(itemAt(i), itemAt(i + 1))
    }
    Connections {
        target: (menu.contentItem as Flickable)?.contentItem ?? null
        function onChildrenChanged() { menu.restack() }
    }
}
