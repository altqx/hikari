pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.NodeEffects"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

ShellMenu {
    id: presetMenu
    objectName: "nodePresetMenu"
    required property NodeEffectsController controller
    title: qsTr("From a preset")
    iconRole: "node-editor"
    MenuHeading {
        id: karaokeHeading
        objectName: "nodeKaraokeHeading"
        text: qsTr("For karaoke lines")
    }
    Instantiator {
        model: presetMenu.controller.presets
        delegate: ShellMenuItem {
            required property var modelData
            required property int index
            objectName: "nodePreset_" + index
            iconRole: "node-editor"
            text: modelData.title
            help: modelData.description
            ToolTip.visible: hovered
            ToolTip.text: modelData.description
            onTriggered: presetMenu.controller.addPreset(modelData.index)
        }
        onObjectAdded: (index, object) => {
            let at = 0
            while (at < presetMenu.count && presetMenu.itemAt(at) !== karaokeHeading)
                ++at
            presetMenu.insertItem(at + 1 + index, object)
        }
        onObjectRemoved: (index, object) => presetMenu.removeItem(object)
    }
    MenuSeparator {}
    MenuHeading {
        objectName: "nodeTypesettingHeading"
        text: qsTr("For selected lines")
    }
    Instantiator {
        model: presetMenu.controller.typesettingPresets
        delegate: ShellMenuItem {
            required property var modelData
            required property int index
            objectName: "nodeTypesettingPreset_" + index
            iconRole: ["graph-shake", "graph-typewriter", "graph-fade-by-letter", "graph-wobble", "graph-bounce", "graph-focus-lines", "graph-grain", "graph-chromatic-aberration", "graph-randomove", "graph-karaoke-to-alpha"][modelData.index] || "node-editor"
            text: modelData.title
            help: modelData.description
            ToolTip.visible: hovered
            ToolTip.text: modelData.description
            onTriggered: presetMenu.controller.addTypesettingGraph(modelData.index)
        }
        onObjectAdded: (index, object) => presetMenu.insertItem(presetMenu.count, object)
        onObjectRemoved: (index, object) => presetMenu.removeItem(object)
    }
}
