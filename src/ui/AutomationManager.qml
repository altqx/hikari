// The automation manager tool (L1): loaded scripts with their state and
// macros. Loading and reloading run the script's top level visibly; a failed
// or unavailable script says why and offers reload.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Pane {
    id: root
    required property AutomationManagerController controller

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        RowLayout {
            Label {
                text: qsTr("Automation")
                font.bold: true
                Layout.fillWidth: true
            }
            Button {
                objectName: "cancelRun"
                text: qsTr("Cancel")
                enabled: root.controller.busy
                onClicked: root.controller.cancel()
            }
        }

        ListView {
            id: scripts
            objectName: "scripts"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 8
            model: root.controller.scripts
            delegate: ColumnLayout {
                id: script
                required property var modelData
                required property int index
                objectName: "script_" + index
                width: ListView.view.width
                Accessible.role: Accessible.Grouping
                Accessible.name: modelData.name !== "" ? modelData.name : modelData.fileName

                RowLayout {
                    Label {
                        text: script.modelData.name !== "" ? script.modelData.name : script.modelData.fileName
                        font.bold: true
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                    Label {
                        objectName: "state_" + script.index
                        text: script.modelData.state
                    }
                    Button {
                        text: qsTr("Reload")
                        enabled: script.modelData.state !== "running" && script.modelData.state !== "loading"
                        onClicked: root.controller.reload(script.modelData.path)
                    }
                    Button {
                        text: qsTr("Unload")
                        enabled: script.modelData.state !== "running"
                        onClicked: root.controller.unload(script.modelData.path)
                    }
                }
                Label {
                    objectName: "error_" + script.index
                    visible: text !== ""
                    text: script.modelData.error
                    wrapMode: Text.Wrap
                    color: palette.brightText
                    Layout.fillWidth: true
                }
                Repeater {
                    model: script.modelData.macros
                    delegate: RowLayout {
                        required property var modelData
                        Button {
                            objectName: "macro_" + script.index + "_" + modelData.ordinal
                            text: modelData.name
                            enabled: script.modelData.state === "ready" && !root.controller.busy
                            ToolTip.text: modelData.alias + (modelData.description !== "" ? " — " + modelData.description : "")
                            ToolTip.visible: hovered
                            onClicked: root.controller.run(script.modelData.path, modelData.ordinal)
                        }
                    }
                }
            }
        }
    }
}
