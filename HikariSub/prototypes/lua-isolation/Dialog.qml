import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    width: 720; height: 610; visible: true
    title: "Throwaway Lua isolation — synthetic dialog"
    property int ticks: 0
    property var controls: []
    property var buttons: []
    property string caseMode: ""
    property string statusText: "Waiting for native helper"
    property bool dialogVisible: false
    Timer { interval: 20; running: true; repeat: true; onTriggered: { root.ticks++; probe.heartbeat(root.ticks) } }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 20; spacing: 12
        Label { text: "Native LuaJIT / typed pipe / QML"; font.pixelSize: 22 }
        Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: "Original synthetic controls; no full automation API or OS input qualification. The GUI continues ticking while Lua waits synchronously." }
        Label { text: "QML event-loop heartbeats: " + root.ticks }
        Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: root.statusText }
        Dialog {
            objectName: "syntheticScriptDialog"
            parent: Overlay.overlay
            anchors.centerIn: parent
            width: root.width - 60
            title: "Original synthetic script options"
            modal: true
            closePolicy: Popup.NoAutoClose
            visible: root.dialogVisible
            contentItem: ColumnLayout {
                spacing: 12
                Repeater {
                    id: controlRows
                    model: root.controls
                    RowLayout {
                        id: row
                        required property var modelData
                        Layout.fillWidth: true
                        Label { text: row.modelData.name || "Info"; Layout.preferredWidth: 95 }
                        Loader {
                            id: loader
                            Layout.fillWidth: true
                            property var spec: row.modelData
                            sourceComponent: spec.class === "edit" ? editControl : spec.class === "intedit" ? intControl : spec.class === "checkbox" ? boolControl : spec.class === "dropdown" ? choiceControl : labelControl
                            onLoaded: item.spec = spec
                        }
                        function control() { return loader.item }
                    }
                }
                RowLayout {
                    Repeater {
                        model: root.buttons
                        Button { required property var modelData; text: modelData.label; onClicked: root.finish(modelData.value) }
                    }
                    Button { text: "Window close (sample)"; onClicked: root.finish(false) }
                }
            }
        }
        Item { Layout.fillHeight: true }
    }
    Component { id: labelControl; Label { property var spec: ({}); text: spec.label || ""; property var result: null; function fixtureValue() {} } }
    Component { id: editControl; TextField { property var spec: ({}); text: spec.text || ""; property var result: text; function fixtureValue() { text = "静かな港 🌙" } } }
    Component { id: intControl; SpinBox { property var spec: ({}); from: spec.min || 0; to: spec.max || 99; value: spec.value || 0; property var result: value; function fixtureValue() { value = 37 } } }
    Component { id: boolControl; CheckBox { property var spec: ({}); text: spec.label || ""; checked: spec.value === true; property var result: checked; function fixtureValue() { checked = true } } }
    Component { id: choiceControl; ComboBox { property var spec: ({}); model: spec.items || []; currentIndex: 0; property var result: currentText; function fixtureValue() { currentIndex = 1 } } }
    function values() {
        let result = {}
        for (let i=0; i<controlRows.count; ++i) {
            let row = controlRows.itemAt(i), control = row.control()
            if (row.modelData.name && row.modelData.class !== "label") result[row.modelData.name] = control.result
        }
        return result
    }
    function finish(button) {
        if (!dialogVisible) return
        let result = values()
        dialogVisible = false
        probe.reply(button, result)
    }
    function autoReply() {
        for (let i=0; i<controlRows.count; ++i) controlRows.itemAt(i).control().fixtureValue()
        if (caseMode === "default_ok") finish("")
        else if (caseMode === "default_close" || caseMode === "cancel_dialog") finish(false)
        else if (caseMode === "custom_cancel") finish("Cancel")
        else finish("Apply")
    }
}
