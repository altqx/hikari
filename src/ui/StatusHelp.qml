pragma Singleton
import QtQuick

// P10: the help text a menu item shows in the status bar's first field
// while it is highlighted, as legacy's menus did (MenuDialog::OnMouseEvent,
// Menu.cpp:600-646: the item's help, the field emptied when the pointer
// leaves the menu, and by HideMenus, Menu.cpp:818-822, when it closes). The
// shell writes what `shown` carries to that field and empties it on
// `cleared`, whatever it holds then, as legacy's SetStatusText(emptyString,
// 0) did: text a script or a timer wrote while a menu was open goes too. One
// per engine, so each window set has its own. (The Linux build also echoed
// every tooltip there, hikarisubApp.cpp:636-666, because it turned wxGTK's
// tooltips off; the rewrite shows tooltips on every platform, as the Windows
// build did.)
//
// Legacy's SendEvent queued the chosen command and called HideMenus at once
// (Menu.cpp:673-682), so the field emptied before the command ran and the
// command's own status text stayed. Qt runs an item's command before its menu
// closes (released, the command, triggered, then aboutToHide), so the field
// empties when the item is released, and the close that follows the command
// leaves it alone.
QtObject {
    // The item whose help is shown.
    property var owner: null
    // How many shell menus are open.
    property int openMenus: 0
    // An item's command ran: the menus close after it.
    property bool commandRan: false
    signal shown(string text)
    signal cleared()

    function show(item, text) {
        owner = item
        commandRan = false
        shown(text)
    }
    function hide(item) {
        if (owner !== item)
            return
        owner = null
        cleared()
    }
    // An item is released: its command runs next (HideMenus before the
    // queued command).
    function activating() {
        owner = null
        cleared()
    }
    // Its command ran: the menus close next.
    function activated() {
        commandRan = true
    }
    function menuOpened() {
        ++openMenus
        commandRan = false
    }
    function menuClosed() {
        openMenus = Math.max(openMenus - 1, 0)
        if (openMenus > 0)
            return
        owner = null
        if (commandRan)
            commandRan = false
        else
            cleared()
    }
}
