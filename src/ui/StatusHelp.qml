pragma Singleton
import QtQuick

// P10: the help text a menu item shows in the status bar's first field
// while it is highlighted, as legacy's menus did (MenuDialog::OnMouseEvent,
// Menu.cpp:600-646: the item's help, the field emptied when the pointer
// leaves the menu, and by HideMenus, Menu.cpp:818-822, when it closes). The
// shell writes what `shown` carries to that field. One per engine, so each
// window set has its own. (The Linux build also echoed every tooltip there,
// hikarisubApp.cpp:636-666, because it turned wxGTK's tooltips off; the
// rewrite shows tooltips on every platform, as the Windows build did.)
QtObject {
    // The item whose help is shown, and that help.
    property var owner: null
    property string lastText
    // How many shell menus are open.
    property int openMenus: 0
    signal shown(string text)
    // The help is no longer shown: the field empties if it still holds it.
    // (Legacy's HideMenus emptied the field before the chosen command ran,
    // so a command's own status text stays.)
    signal withdrawn(string text)

    function show(item, text) {
        owner = item
        lastText = text
        shown(text)
    }
    function hide(item) {
        if (owner !== item)
            return
        owner = null
        withdrawn(lastText)
    }
    function menuOpened() {
        ++openMenus
    }
    function menuClosed() {
        openMenus = Math.max(openMenus - 1, 0)
        if (openMenus === 0) {
            owner = null
            withdrawn(lastText)
        }
    }
}
