pragma Singleton
import QtQuick

QtObject {
    property var owner: null
    property int openMenus: 0
    // Qt runs an item's command before its menu closes (released, triggered, then aboutToHide).
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
    function activating() {
        owner = null
        cleared()
    }
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
