pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Workspace"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

// WS3 (#217): the current Document tab's layouts, as the layout switcher and
// View > Layout list them (MS1's research, musescore-shell.md section 7, the
// SH1 row: MuseScore's Workspaces menu): the built-in presets, the user's
// saved layouts, then Save layout as…, Reset this layout and Manage
// layouts…. The tab's layout is checked: its saved layout when it started
// from one, else its preset. A saved layout this version cannot use is listed
// disabled (Manage layouts… says why). The menu runs nothing itself: the
// shell answers its signals.
ShellMenu {
    id: menu
    required property var layouts // WorkspaceLayoutController
    // The items' objectNames start with it (the switcher's and View's menus).
    property string prefix: "layout"
    readonly property var presets: ["Editing", "Timing", "Translation", "Typesetting", "Karaoke"]
    signal presetChosen(string name)
    signal savedLayoutChosen(string name)
    signal saveRequested()
    signal resetRequested()
    signal manageRequested()
    function indexOf(item) {
        for (let i = 0; i < count; ++i)
            if (itemAt(i) === item)
                return i
        return -1
    }
    // The items in their parent's item order as in the menu's, which
    // accessibility follows (AT-SPI and UI Automation list a menu's items in
    // that order): the list view takes an item added later into its content
    // last, when it lays it out, so every move into it puts them back in order.
    function restack() {
        for (let i = count - 2; i >= 0; --i)
            ShellMenus.stackBefore(itemAt(i), itemAt(i + 1))
    }

    // The presets' names as the Instantiator below translates them (also
    // presetLabel's literals), marked here for the context lint.
    readonly property var presetTexts: [QT_TRANSLATE_NOOP("HikariSub.Workspace", "Editing"),
        QT_TRANSLATE_NOOP("HikariSub.Workspace", "Timing"), QT_TRANSLATE_NOOP("HikariSub.Workspace", "Translation"),
        QT_TRANSLATE_NOOP("HikariSub.Workspace", "Typesetting"), QT_TRANSLATE_NOOP("HikariSub.Workspace", "Karaoke")]
    Instantiator {
        model: menu.presets
        delegate: ShellMenuItem {
            id: presetItem
            required property string modelData
            objectName: menu.prefix + "Preset" + modelData
            // presetLabel's names (its literals put them in the catalogs),
            // translated here so a language switch retranslates them.
            text: qsTranslate("HikariSub.Workspace", modelData)
            checkable: true
            checked: menu.layouts.layout.length === 0 && menu.layouts.preset === modelData
            keepsMenuOpen: false // one choice of several
            onParentChanged: menu.restack()
            onTriggered: {
                checked = Qt.binding(() => menu.layouts.layout.length === 0 && menu.layouts.preset === modelData)
                menu.presetChosen(modelData)
            }
        }
        onObjectAdded: (index, object) => menu.insertItem(index, object)
        onObjectRemoved: (index, object) => menu.removeItem(object)
    }
    MenuSeparator {
        id: savedSeparator
        objectName: menu.prefix + "SavedSeparator"
        visible: menu.layouts.savedLayouts.length > 0
        height: visible ? implicitHeight : 0
    }
    Instantiator {
        model: menu.layouts.savedLayouts
        delegate: ShellMenuItem {
            required property var modelData
            required property int index
            objectName: menu.prefix + "Saved" + index
            text: modelData.name
            enabled: modelData.usable
            checkable: true
            checked: menu.layouts.layout === modelData.name
            keepsMenuOpen: false
            onParentChanged: menu.restack()
            onTriggered: {
                checked = Qt.binding(() => menu.layouts.layout === modelData.name)
                menu.savedLayoutChosen(modelData.name)
            }
        }
        // After the separator, wherever the presets are by then.
        onObjectAdded: (index, object) => menu.insertItem(menu.indexOf(savedSeparator) + 1 + index, object)
        onObjectRemoved: (index, object) => menu.removeItem(object)
    }
    MenuSeparator {}
    ShellMenuItem {
        objectName: menu.prefix + "SaveAs"
        iconRole: "layout-save"
        text: qsTr("Save layout as…")
        onTriggered: menu.saveRequested()
    }
    ShellMenuItem {
        objectName: menu.prefix + "Reset"
        iconRole: "layout-reset"
        text: qsTr("Reset this layout")
        onTriggered: menu.resetRequested()
    }
    ShellMenuItem {
        objectName: menu.prefix + "Manage"
        iconRole: "layout-manage"
        text: qsTr("Manage layouts…")
        enabled: menu.layouts.savedLayouts.length > 0
        onTriggered: menu.manageRequested()
    }
}
