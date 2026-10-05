import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

// T1: the active tool's read-only values (the crosshair's position under the
// pointer, the scale tool's X and Y), beside the video times: a readout in
// tabular figures with its name in the muted colour, shown only while it has
// a value, in a row that is always there, so the video does not resize as
// the pointer enters and leaves it. The editable values are fields in the
// tool strip (VisualToolValues).
RowLayout {
    id: readout
    objectName: "visualToolReadout"
    required property VisualToolsController tools
    spacing: 10
    Repeater {
        model: readout.tools.values
        delegate: RowLayout {
            required property var modelData
            visible: !modelData.editable && modelData.text.length > 0
            spacing: 4
            Label {
                text: modelData.label
                color: Theme.muted
            }
            Label {
                objectName: modelData.editable ? "" : "visualValue_" + modelData.name
                text: modelData.text
                font.features: { "tnum": 1 }
                Accessible.name: modelData.label
            }
        }
    }
}
