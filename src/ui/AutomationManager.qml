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
    // A size of its own (it lost the heading that gave it one): its window
    // or panel sizes it otherwise.
    implicitWidth: 420
    implicitHeight: 360

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        // While a script runs: Cancel. (No heading: the window or dock
        // title names the tool.)
        RowLayout {
            visible: root.controller.busy
            Label {
                text: qsTr("A script is running")
                color: Theme.muted
                Layout.fillWidth: true
            }
            Button {
                objectName: "cancelRun"
                text: qsTr("Cancel")
                enabled: root.controller.busy
                onClicked: root.controller.cancel()
            }
        }
        // The empty state.
        Label {
            objectName: "scriptsEmpty"
            visible: scripts.count === 0
            Layout.fillWidth: true
            topPadding: 24
            horizontalAlignment: Text.AlignHCenter
            color: Theme.muted
            text: qsTr("No scripts loaded")
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
                    // The state in words, muted; a failure in the danger colour.
                    Label {
                        objectName: "state_" + script.index
                        readonly property string scriptState: script.modelData.state
                        text: scriptState === "loading" ? qsTr("Loading")
                            : scriptState === "ready" ? qsTr("Ready")
                            : scriptState === "running" ? qsTr("Running")
                            : scriptState === "failed" ? qsTr("Failed to load")
                            : scriptState === "unavailable" ? qsTr("Unavailable") : scriptState
                        color: scriptState === "failed" || scriptState === "unavailable" ? Theme.danger : Theme.muted
                    }
                    IconToolButton {
                        iconRole: "refresh"
                        text: qsTr("Reload")
                        enabled: script.modelData.state !== "running" && script.modelData.state !== "loading"
                        onClicked: root.controller.reload(script.modelData.path)
                    }
                    IconToolButton {
                        objectName: "forceStop_" + script.index
                        iconRole: "media-stop"
                        text: qsTr("Force stop")
                        visible: script.modelData.forceStopOffered
                        onClicked: root.controller.forceStop(script.modelData.path)
                    }
                    IconToolButton {
                        iconRole: "unload"
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
                    color: Theme.danger
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
