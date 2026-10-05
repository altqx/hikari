import QtQuick
import QtQuick.Controls

// The tab menu: legacy Notebook::ContextMenu (Notebook.cpp, at 20d647c4) on
// tab `index` (-1: none), made anew at every right click (P9): every tab by
// its name, the active one checked (MENU_CHOOSE + g, 886-889; choosing one
// moves it to the first visible place, OnTabSel 1019-1039), "Save" for that
// tab while it is modified, "Save all", "Close all tabs" (890-893), the
// folders of the tab's subtitles, video, audio and keyframes that it has
// (894-911, SelectInFolder), and R1's "Subtitle comparison" (915-956).
// Legacy's "Show two tabs" split is left out: the protected reference stands
// for it (P6). Legacy's tab menu items carry no icons.
ShellMenu {
    id: tabMenu
    objectName: "documentTabMenu"
    required property var app
    // The tab the menu was opened on (-1: none) and the first tab the bar
    // shows (legacy firstVisibleTab).
    property int tabIndex: -1
    property int firstVisible: 0
    property var menuState: ({ tabs: [], save: false, folders: [] })
    signal saveRequested(var id)
    signal saveAllRequested()
    signal closeAllRequested()

    // Before the menu opens: legacy builds it on every right click.
    function openOn(index, item, x, y, first) {
        tabIndex = index
        firstVisible = first ?? 0
        const state = app.tabMenu(index)
        // The tab rows first, so the folder rows find their place after them.
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
            // ITEM_RADIO on the active tab.
            checkable: true
            checked: modelData.current
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
        text: qsTr("Save")
        enabled: tabMenu.tabIndex >= 0 && tabMenu.menuState.save === true
        onTriggered: tabMenu.saveRequested(tabMenu.app.tabDocument(tabMenu.tabIndex))
    }
    ShellMenuItem {
        objectName: "tabMenuSaveAll"
        text: qsTr("Save all")
        onTriggered: tabMenu.saveAllRequested()
    }
    ShellMenuItem {
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
            text: modelData.kind === "subtitles" ? qsTr("Open the folder containing the subtitles")
                : modelData.kind === "video" ? qsTr("Open video containing folder")
                : modelData.kind === "audio" ? qsTr("Open audio containing folder")
                : qsTr("Open keyframes containing folder")
            onTriggered: tabMenu.app.showInFolder(modelData.path)
        }
        // After the tab rows, the separator, Save, Save all and Close all tabs.
        onObjectAdded: (index, object) => tabMenu.insertItem(tabItems.count + 4 + index, object)
        onObjectRemoved: (index, object) => tabMenu.removeItem(object)
    }

    SubtitleComparisonMenu {
        id: comparisonMenu
        app: tabMenu.app
    }
}
