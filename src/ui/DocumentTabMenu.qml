pragma Translator: "HikariSub.Tabs"
import QtQuick
import QtQuick.Controls

ShellMenu {
    id: tabMenu
    objectName: "documentTabMenu"
    required property var app
    property int tabIndex: -1
    property int shellWindow: 0
    property real document: 0
    property int firstVisible: 0
    property var menuState: ({ tabs: [], save: false, folders: [] })
    readonly property bool onTab: tabIndex >= 0
    readonly property var files: onTab ? menuState.folders : []
    readonly property var singleFile: files.length === 1 ? files[0] : null
    signal saveRequested(var id)
    signal saveAllRequested()
    signal closeRequested(int index)
    signal closeOthersRequested(int index, bool toTheRight)
    signal closeAllRequested()

    function openOn(index, item, x, y, first) {
        tabIndex = index
        firstVisible = first ?? 0
        const state = app.tabMenu(index)
        tabItems.model = []
        folderItems.model = []
        pathItems.model = []
        menuState = state
        tabItems.model = state.tabs
        folderItems.model = state.folders.length > 1 ? state.folders : []
        pathItems.model = state.folders.length > 1 ? state.folders : []
        comparisonMenu.prepare(index)
        document = index >= 0 ? app.tabDocument(index) : app.editingDocument()
        moveToWindowMenu.prepare(document)
        tabMenu.popup(item, x, y)
    }
    function kindName(kind) {
        return kind === "subtitles" ? qsTr("Subtitles") : kind === "video" ? qsTr("Video")
            : kind === "audio" ? qsTr("Audio") : qsTr("Keyframes")
    }
    function kindIcon(kind) {
        return kind === "subtitles" ? "open-subtitles" : kind === "video" ? "open-video"
            : kind === "audio" ? "open-audio" : "open-keyframes"
    }

    component TabRow: ShellMenuItem {
        visible: tabMenu.onTab
        height: tabMenu.onTab ? implicitHeight : 0
    }

    ShellMenuItem {
        objectName: "tabMenuNewTab"
        iconRole: "tab-new"
        text: qsTr("New tab")
        shortcutText: MenuKeys.text("GLOBAL_ADD_PAGE", 0)
        visible: !tabMenu.onTab
        height: tabMenu.onTab ? 0 : implicitHeight
        onTriggered: tabMenu.app.addPage(true)
    }
    TabRow {
        objectName: "tabMenuSave"
        iconRole: "save"
        text: qsTr("Save")
        shortcutText: MenuKeys.text("GLOBAL_SAVE_SUBS", 0)
        enabled: tabMenu.menuState.save === true
        onTriggered: tabMenu.saveRequested(tabMenu.app.tabDocument(tabMenu.tabIndex))
    }
    ShellMenuItem {
        objectName: "tabMenuSaveAll"
        iconRole: "save-all"
        text: qsTr("Save all")
        onTriggered: tabMenu.saveAllRequested()
    }
    MenuSeparator {
        visible: tabMenu.onTab
        height: tabMenu.onTab ? implicitHeight : 0
    }
    ShellMenuItem {
        objectName: tabMenu.singleFile ? "tabMenuFolder_" + tabMenu.singleFile.kind : "tabMenuFolder"
        iconRole: "show-in-folder"
        text: qsTr("Show in folder")
        visible: tabMenu.singleFile !== null
        height: tabMenu.singleFile !== null ? implicitHeight : 0
        onTriggered: tabMenu.app.showInFolder(tabMenu.singleFile.path)
    }
    ShellMenu {
        id: folderMenu
        objectName: "tabMenuFolders"
        iconRole: "show-in-folder"
        title: qsTr("Show in folder")
        shown: tabMenu.files.length > 1
        Instantiator {
            id: folderItems
            model: []
            delegate: ShellMenuItem {
                required property var modelData
                objectName: "tabMenuFolder_" + modelData.kind
                iconRole: tabMenu.kindIcon(modelData.kind)
                text: tabMenu.kindName(modelData.kind)
                onTriggered: tabMenu.app.showInFolder(modelData.path)
            }
            onObjectAdded: (index, object) => folderMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => folderMenu.removeItem(object)
        }
    }
    ShellMenuItem {
        objectName: tabMenu.singleFile ? "tabMenuCopyPath_" + tabMenu.singleFile.kind : "tabMenuCopyPath"
        iconRole: "copy-path"
        text: qsTr("Copy path")
        visible: tabMenu.singleFile !== null
        height: tabMenu.singleFile !== null ? implicitHeight : 0
        onTriggered: tabMenu.app.copyText(tabMenu.singleFile.path)
    }
    ShellMenu {
        id: pathMenu
        objectName: "tabMenuCopyPaths"
        iconRole: "copy-path"
        title: qsTr("Copy path")
        shown: tabMenu.files.length > 1
        Instantiator {
            id: pathItems
            model: []
            delegate: ShellMenuItem {
                required property var modelData
                objectName: "tabMenuCopyPath_" + modelData.kind
                iconRole: tabMenu.kindIcon(modelData.kind)
                text: tabMenu.kindName(modelData.kind)
                onTriggered: tabMenu.app.copyText(modelData.path)
            }
            onObjectAdded: (index, object) => pathMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => pathMenu.removeItem(object)
        }
    }
    SubtitleComparisonMenu {
        id: comparisonMenu
        app: tabMenu.app
        shown: tabMenu.onTab
    }
    ShellMenu {
        id: switchMenu
        objectName: "tabMenuSwitchTo"
        iconRole: "tab-switch"
        title: qsTr("Switch to")
        Instantiator {
            id: tabItems
            model: []
            delegate: MenuRadioItem {
                required property var modelData
                required property int index
                objectName: "tabMenuTab" + index
                text: modelData.title
                checked: modelData.current
                Accessible.checked: modelData.current
                onTriggered: tabMenu.app.chooseTab(index, tabMenu.firstVisible)
            }
            onObjectAdded: (index, object) => switchMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => switchMenu.removeItem(object)
        }
    }
    MenuSeparator {}
    ShellMenuItem {
        objectName: "tabMenuMoveToNewWindow"
        iconRole: "tab-to-new-window"
        text: qsTr("Move tab to new window")
        command: "GLOBAL_MOVE_TAB_TO_NEW_WINDOW"
        enabled: tabMenu.document > 0
        onTriggered: tabMenu.app.moveTabToNewWindow(tabMenu.document, Qt.point(0, 0), false)
    }
    MoveTabMenu {
        id: moveToWindowMenu
        app: tabMenu.app
        shellWindow: tabMenu.shellWindow
    }
    MenuSeparator {}
    TabRow {
        objectName: "tabMenuClose"
        iconRole: "tab-close"
        text: qsTr("Close")
        shortcutText: MenuKeys.text("GLOBAL_CLOSE_PAGE", 0)
        onTriggered: tabMenu.closeRequested(tabMenu.tabIndex)
    }
    TabRow {
        objectName: "tabMenuCloseOthers"
        iconRole: "tab-close-others"
        text: qsTr("Close other tabs")
        enabled: tabMenu.menuState.tabs.length > 1
        onTriggered: tabMenu.closeOthersRequested(tabMenu.tabIndex, false)
    }
    TabRow {
        objectName: "tabMenuCloseRight"
        iconRole: "tab-close-right"
        text: qsTr("Close tabs to the right")
        enabled: tabMenu.tabIndex < tabMenu.menuState.tabs.length - 1
        onTriggered: tabMenu.closeOthersRequested(tabMenu.tabIndex, true)
    }
    ShellMenuItem {
        iconRole: "tab-close-all"
        objectName: "tabMenuCloseAll"
        text: qsTr("Close all tabs")
        onTriggered: tabMenu.closeAllRequested()
    }
}
