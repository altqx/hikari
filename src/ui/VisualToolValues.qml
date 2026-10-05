import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

// T1: below the canvas (layout A), the active tool's values, its numeric and
// keyboard alternative, and the batch picker (accepted on #55): the Lines a
// batch tool edits are picked from the Grid's selection and kept, whatever
// the active Line or a later selection; with none picked a tool edits the
// active Line.
RowLayout {
    id: values
    objectName: "visualToolValues"
    required property VisualToolsController tools
    spacing: 6

    // T2: the family's options before its values (legacy's second toolbar row).
    VisualToolOptions {
        tools: values.tools
        visible: values.tools.options.length > 0
    }
    Repeater {
        model: values.tools.values
        delegate: RowLayout {
            required property var modelData
            Label { text: modelData.label + ":" }
            TextField {
                objectName: "visualValue_" + modelData.name
                text: modelData.text
                readOnly: !modelData.editable
                selectByMouse: true
                implicitWidth: 110
                Accessible.name: modelData.label
                onAccepted: values.tools.setValue(modelData.name, text)
            }
        }
    }
    Item { Layout.fillWidth: true }
    Label {
        objectName: "visualBatch"
        text: values.tools.batchCount > 0 ? qsTr("Targets: %n picked line(s)", "", values.tools.batchCount)
                                          : qsTr("Targets: active line")
    }
    Button {
        objectName: "visualPickBatch"
        text: qsTr("Pick selected lines")
        focusPolicy: Qt.NoFocus
        enabled: values.tools.railEnabled
        onClicked: values.tools.pickBatch()
    }
    Button {
        objectName: "visualClearBatch"
        text: qsTr("Clear")
        focusPolicy: Qt.NoFocus
        enabled: values.tools.batchCount > 0
        onClicked: values.tools.clearBatch()
    }
}
