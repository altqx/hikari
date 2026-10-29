pragma Translator: "HikariSub.Tabs"
import QtQuick
import QtQuick.Controls

ShellMenu {
    id: tabMenu
    objectName: "documentTabMenu"
    required property var app
    property int tabIndex: -1
    property int firstVisible: 0
    property var menuState: ({ tabs: [], save: false, folders: [] })
    signal saveRequested(var id)
    signal saveAllRequested()
    signal closeAllRequested()

    function openOn(index, item, x, y, first) {
        tabIndex = index
        firstVisible = first ?? 0
        const state = app.tabMenu(index)
        tabItems.model = []
        folderItems.model = []
        menuState = state
        tabItems.model = state.tabs
        folderItems.model = state.folders
        comparisonMenu.prepare(index)
        tabMenu.popup(item, x, y)
    }

    Instantiator {
        id: tabItems
        model: []
        delegate: ShellMenuItem {
            required property var modelData
            required property int index
            objectName: "tabMenuTab" + index
            text: modelData.title
            checkable: true
            checked: modelData.current
            keepsMenuOpen: false
            Accessible.checkable: true
            Accessible.checked: modelData.current
            onTriggered: tabMenu.app.chooseTab(index, tabMenu.firstVisible)
        }
        onObjectAdded: (index, object) => tabMenu.insertItem(index, object)
        onObjectRemoved: (index, object) => tabMenu.removeItem(object)
    }
    MenuSeparator {}
    ShellMenuItem {
        objectName: "tabMenuSave"
        iconRole: "save"
        text: qsTr("Save")
        enabled: tabMenu.tabIndex >= 0 && tabMenu.menuState.save === true
        onTriggered: tabMenu.saveRequested(tabMenu.app.tabDocument(tabMenu.tabIndex))
    }
    ShellMenuItem {
        objectName: "tabMenuSaveAll"
        iconRole: "save-all"
        text: qsTr("Save all")
        onTriggered: tabMenu.saveAllRequested()
    }
    ShellMenuItem {
        iconRole: "tab-close-all"
        objectName: "tabMenuCloseAll"
        text: qsTr("Close all tabs")
        onTriggered: tabMenu.closeAllRequested()
    }
    Instantiator {
        id: folderItems
        model: []
        delegate: ShellMenuItem {
            required property var modelData
            required property int index
            objectName: "tabMenuFolder_" + modelData.kind
            iconRole: "show-in-folder"
            text: modelData.kind === "subtitles" ? qsTr("Open the folder containing the subtitles")
                : modelData.kind === "video" ? qsTr("Open video containing folder")
                : modelData.kind === "audio" ? qsTr("Open audio containing folder")
                : qsTr("Open keyframes containing folder")
            onTriggered: tabMenu.app.showInFolder(modelData.path)
        }
        // MUST KILL: 4 counts the separator and the three fixed rows before the folder rows
        onObjectAdded: (index, object) => tabMenu.insertItem(tabItems.count + 4 + index, object)
        onObjectRemoved: (index, object) => tabMenu.removeItem(object)
    }

    SubtitleComparisonMenu {
        id: comparisonMenu
        app: tabMenu.app
    }
}
