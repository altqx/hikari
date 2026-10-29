pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Workspace"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

ShellMenu {
    id: menu
    required property var layouts
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
    // AT-SPI and UI Automation list a menu's items in item order; the list view puts an item added later last.
    function restack() {
        for (let i = count - 2; i >= 0; --i)
            ShellMenus.stackBefore(itemAt(i), itemAt(i + 1))
    }

    readonly property var presetTexts: [QT_TRANSLATE_NOOP("HikariSub.Workspace", "Editing"),
        QT_TRANSLATE_NOOP("HikariSub.Workspace", "Timing"), QT_TRANSLATE_NOOP("HikariSub.Workspace", "Translation"),
        QT_TRANSLATE_NOOP("HikariSub.Workspace", "Typesetting"), QT_TRANSLATE_NOOP("HikariSub.Workspace", "Karaoke")]
    Instantiator {
        model: menu.presets
        delegate: ShellMenuItem {
            id: presetItem
            required property string modelData
            objectName: menu.prefix + "Preset" + modelData
            text: qsTranslate("HikariSub.Workspace", modelData)
            checkable: true
            checked: menu.layouts.layout.length === 0 && menu.layouts.preset === modelData
            keepsMenuOpen: false
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
