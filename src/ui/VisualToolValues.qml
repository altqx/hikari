import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

// T1: below the canvas (layout A), the active family's tool strip: its
// options (VisualToolOptions, legacy VideoToolbar's second row), its
// editable values (the numeric and keyboard alternative) and the batch
// picker (accepted on #55): the Lines a batch tool edits are picked from the
// Grid's selection and kept, whatever the active Line or a later selection;
// with none picked a tool edits the active Line. The strip shows only what
// the active family has: none for the crosshair (it edits no batch) or for a
// family whose tool has not landed, so then there is no row at all. The
// read-only values (the crosshair's position, the scale) are a readout
// beside the video times (VisualToolReadout), so the row does not come and
// go with the pointer. The options and values wrap onto a second line
// rather than run out of the panel; the batch picker keeps its trailing
// slot. Icon-only controls name themselves in their tooltip and accessible
// name (visual-language.md, "Tool strips").
RowLayout {
    id: values
    objectName: "visualToolValues"
    required property VisualToolsController tools
    readonly property bool batchTool: tools.activeFamily > 0
        && (tools.families[tools.activeFamily]?.available ?? false)
    readonly property int editableCount: {
        let n = 0
        for (const v of tools.values)
            if (v.editable)
                ++n
        return n
    }
    // D2: false in the player layout (legacy's video toolbar hidden).
    property bool shownInLayout: true
    visible: shownInLayout && (batchTool || options.visible || editableCount > 0)
    spacing: 4

    // The value fields fit a coordinate ("-00000.00"), not the panel.
    TextMetrics {
        id: numberWidth
        font: Qt.application.font
        text: "-00000.00"
    }

    Flow {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.preferredWidth: 0
        Layout.alignment: Qt.AlignVCenter
        spacing: 8
        VisualToolOptions {
            id: options
            tools: values.tools
        }
        Repeater {
            model: values.tools.values
            delegate: RowLayout {
                required property var modelData
                visible: modelData.editable
                spacing: 3
                Label {
                    text: modelData.label
                    color: Theme.muted
                }
                TextField {
                    objectName: modelData.editable ? "visualValue_" + modelData.name : ""
                    text: modelData.text
                    selectByMouse: true
                    implicitWidth: Math.ceil(numberWidth.width) + leftPadding + rightPadding
                    horizontalAlignment: TextInput.AlignRight
                    font.features: { "tnum": 1 }
                    Accessible.name: modelData.label
                    onAccepted: values.tools.setValue(modelData.name, text)
                }
            }
        }
    }
    // The batch picker: its state is the button's (checked while Lines are
    // picked, their number in the tooltip); Clear only while there is a batch.
    IconToolButton {
        objectName: "visualPickBatch"
        Layout.alignment: Qt.AlignTop
        visible: values.batchTool
        iconRole: "pick-lines"
        text: qsTr("Pick selected lines")
        tip: values.tools.batchCount > 0
             ? qsTr("Pick selected lines (%n line(s) picked: the tools edit them, not the active line)", "",
                    values.tools.batchCount)
             : qsTr("Pick selected lines (the tools edit them, not the active line)")
        checked: values.tools.batchCount > 0
        Accessible.description: values.tools.batchCount > 0
                                ? qsTr("%n line(s) picked", "", values.tools.batchCount) : ""
        focusPolicy: Qt.TabFocus
        enabled: values.tools.railEnabled
        onClicked: values.tools.pickBatch()
    }
    IconToolButton {
        objectName: "visualClearBatch"
        Layout.alignment: Qt.AlignTop
        visible: values.batchTool && values.tools.batchCount > 0
        iconRole: "clear"
        text: qsTr("Clear picked lines")
        focusPolicy: Qt.TabFocus
        onClicked: values.tools.clearBatch()
    }
}
