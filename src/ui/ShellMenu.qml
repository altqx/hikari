import QtQuick
import QtQuick.Controls
import Hikari.Ui

Menu {
    id: menu
    property string iconRole
    property string help
    property string shortcutText
    property string detailText
    // False hides this submenu's row in its parent menu.
    property bool shown: true
    property Component lazyContent
    property MenuContent lazyItems
    signal built()
    function build() {
        if (!lazyContent || lazyItems)
            return
        lazyItems = lazyContent.createObject(menu)
        for (const item of lazyItems.items) {
            if (item instanceof Menu)
                addMenu(item)
            else if (item instanceof Item)
                addItem(item)
        }
        lazyItems.placed = true
        layOut()
        built()
    }
    function layOut() {
        const list = contentItem as ListView
        list?.forceLayout()
    }
    // Qt misplaces an in-window popup inside a KDDockWidgets floating window (#1186).
    property bool ownWindowWhenFloating: false
    popupType: ownWindowWhenFloating && parent?.Window.window?.transientParent ? Popup.Window : Popup.Item
    function keepOnScreen() {
        if (!parent || !background || (parent as MenuItem)?.subMenu === menu)
            return
        const by = ShellMenus.shiftOnScreen(background, parent)
        x += by.x
        y += by.y
    }
    onOpened: {
        layOut()
        keepOnScreen()
    }
    onAboutToShow: {
        ShellMenus.menuShowing()
        build()
        StatusHelp.menuOpened()
    }
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
