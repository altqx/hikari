import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

// T3: the active family's options, legacy VideoToolbar's second row (the
// VisualItem of the family: ScaleItem, RotationZItem, RotationXYItem, ...),
// in a row above the values below the canvas (layout A). Each toggle shows its icon
// of the K1 set with legacy's help text as its tooltip; a greyed icon is a
// disabled button, a pushed one a checked button. The item's links (an
// option that switches another on) are the tool's (setOption).
RowLayout {
    id: optionsRow
    objectName: "visualToolOptions"
    required property VisualToolsController tools
    spacing: 1
    visible: repeater.count > 0

    Repeater {
        id: repeater
        model: optionsRow.tools.options
        delegate: IconToolButton {
            required property var modelData
            objectName: "visualOption_" + modelData.name
            // The roles the families' options show (one line: icon_tests reads the roles from it).
            iconRole: ["frame-to-scale", "scale-x", "link", "scale-y", "original-frame", "tool-scale-rotation", "resample", "two-points"].includes(modelData.iconRole) ? modelData.iconRole : ""
            text: modelData.tooltip.split("\n")[0]
            tip: modelData.tooltip
            checkable: true
            checked: modelData.checked
            enabled: modelData.enabled && optionsRow.tools.railEnabled
            focusPolicy: Qt.NoFocus
            // The tool decides (a greyed or linked option); the binding then
            // shows what it took.
            onClicked: {
                optionsRow.tools.setOption(modelData.name, checked ? 1 : 0)
                checked = Qt.binding(() => modelData.checked)
            }
        }
    }
}
