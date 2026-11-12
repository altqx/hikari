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
        onObjectAdded: (index, object) => presetMenu.insertItem(index, object)
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
