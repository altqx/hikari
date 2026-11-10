pragma Translator: "HikariSub.Tabs"
import QtQuick
import QtQuick.Controls

ShellMenu {
    id: moveMenu
    objectName: "moveTabToWindowMenu"
    iconRole: "tab-to-window"
    title: qsTr("Move tab to window")
    shortcutText: MenuKeys.text("GLOBAL_MOVE_TAB_TO_WINDOW", 0)
    required property var app
    property int shellWindow: 0
    property real document: 0
    enabled: document > 0 && windowItems.count > 0

    function prepare(id) {
        document = id
        windowItems.model = []
        windowItems.model = app.otherWindows(shellWindow)
    }

    Instantiator {
        id: windowItems
        model: []
        delegate: ShellMenuItem {
            required property var modelData
            required property int index
            objectName: "moveTabToWindow" + index
            iconRole: "window-restore"
            text: modelData.title
            onTriggered: moveMenu.app.moveTab(moveMenu.document, modelData.window, -1)
        }
        onObjectAdded: (index, object) => moveMenu.insertItem(index, object)
        onObjectRemoved: (index, object) => moveMenu.removeItem(object)
    }
}
