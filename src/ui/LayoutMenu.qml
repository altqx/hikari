pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Workspace"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

ShellMenu {
    id: layoutMenu
    required property var layouts
    property string prefix: "layout"
    property Action lockAction
    property string lockReason
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

    readonly property var presetTexts: [QT_TRANSLATE_NOOP("HikariSub.Workspace", "Editing"),
        QT_TRANSLATE_NOOP("HikariSub.Workspace", "Timing"), QT_TRANSLATE_NOOP("HikariSub.Workspace", "Translation"),
        QT_TRANSLATE_NOOP("HikariSub.Workspace", "Typesetting"), QT_TRANSLATE_NOOP("HikariSub.Workspace", "Karaoke")]
    Instantiator {
        model: layoutMenu.presets
        delegate: ShellMenuItem {
            id: presetItem
            required property string modelData
            objectName: layoutMenu.prefix + "Preset" + modelData
            text: qsTranslate("HikariSub.Workspace", modelData)
            checkable: true
            checked: layoutMenu.layouts.layout.length === 0 && layoutMenu.layouts.preset === modelData
            enabled: !layoutMenu.layouts.locked
            keepsMenuOpen: false
            onTriggered: {
                checked = Qt.binding(() => layoutMenu.layouts.layout.length === 0 && layoutMenu.layouts.preset === modelData)
                layoutMenu.presetChosen(modelData)
            }
        }
        onObjectAdded: (index, object) => layoutMenu.insertItem(index, object)
        onObjectRemoved: (index, object) => layoutMenu.removeItem(object)
    }
    MenuSeparator {
        id: savedSeparator
        objectName: layoutMenu.prefix + "SavedSeparator"
        visible: layoutMenu.layouts.savedLayouts.length > 0
        height: visible ? implicitHeight : 0
    }
    Instantiator {
        model: layoutMenu.layouts.savedLayouts
        delegate: ShellMenuItem {
            required property var modelData
            required property int index
            objectName: layoutMenu.prefix + "Saved" + index
            text: modelData.name
            iconRole: layoutMenu.layouts.hasProject ? (modelData.tier === "project" ? "project" : "mine") : ""
            reason: modelData.hidden ? qsTr("Hidden by the project’s copy") : ""
            enabled: modelData.usable && !modelData.hidden && !layoutMenu.layouts.locked
            checkable: true
            checked: !modelData.hidden && layoutMenu.layouts.layout === modelData.name
            keepsMenuOpen: false
            onTriggered: {
                checked = Qt.binding(() => !modelData.hidden && layoutMenu.layouts.layout === modelData.name)
                layoutMenu.savedLayoutChosen(modelData.name)
            }
        }
        onObjectAdded: (index, object) => layoutMenu.insertItem(layoutMenu.indexOf(savedSeparator) + 1 + index, object)
        onObjectRemoved: (index, object) => layoutMenu.removeItem(object)
    }
    MenuSeparator {}
    ShellMenuItem {
        objectName: layoutMenu.prefix + "SaveAs"
        iconRole: "layout-save"
        text: qsTr("Save layout as…")
        onTriggered: layoutMenu.saveRequested()
    }
    ShellMenuItem {
        objectName: layoutMenu.prefix + "Reset"
        iconRole: "layout-reset"
        text: qsTr("Reset this layout")
        enabled: !layoutMenu.layouts.locked
        onTriggered: layoutMenu.resetRequested()
    }
    ShellMenuItem {
        objectName: layoutMenu.prefix + "Manage"
        iconRole: "layout-manage"
        text: qsTr("Manage layouts…")
        enabled: layoutMenu.layouts.savedLayouts.length > 0
        onTriggered: layoutMenu.manageRequested()
    }
    MenuSeparator {
        visible: layoutMenu.lockAction !== null
        height: visible ? implicitHeight : 0
    }
    ShellMenuItem {
        objectName: layoutMenu.prefix + "Lock"
        command: "GLOBAL_TOGGLE_LOCK_LAYOUT"
        iconRole: "layout-lock"
        action: layoutMenu.lockAction
        visible: layoutMenu.lockAction !== null
        height: visible ? implicitHeight : 0
        keepsMenuOpen: false
        reason: layoutMenu.lockReason
    }
}
