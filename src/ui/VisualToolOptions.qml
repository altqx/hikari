import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

// T2, T3: the active family's own options, legacy VideoToolbar's second row
// (the VisualItem of the family: PositionItem's "by rectangle", X and Y
// toggles and its alignment choice, MoveItem's two points, ScaleItem,
// RotationZItem, RotationXYItem, ...), in a row above the values below the
// canvas (layout A). Each toggle shows its icon of the K1 set with legacy's
// help text as its tooltip; a greyed icon is a disabled button (X and Y
// without the rectangle, as legacy greyed them), a pushed one a checked
// button. The item's links (an option that switches another on) are the
// tool's (setOption).
RowLayout {
    id: optionsRow
    objectName: "visualToolOptions"
    required property VisualToolsController tools
    spacing: 1
    visible: repeater.count > 0

    Repeater {
        id: repeater
        model: optionsRow.tools.options
        delegate: Loader {
            id: option
            required property var modelData
            sourceComponent: modelData.kind === "choice" ? choiceComponent : toggleComponent
            Component {
                id: toggleComponent
                IconToolButton {
                    objectName: "visualOption_" + option.modelData.name
                    // The roles the families' options show (one line: icon_tests reads the roles from it).
                    iconRole: ["frame-to-scale", "scale-x", "link", "scale-y", "original-frame", "tool-scale-rotation", "resample", "two-points"].includes(option.modelData.iconRole) ? option.modelData.iconRole : ""
                    text: option.modelData.tooltip.split("\n")[0]
                    tip: option.modelData.tooltip
                    checkable: true
                    checked: option.modelData.checked
                    enabled: option.modelData.enabled && optionsRow.tools.railEnabled
                    focusPolicy: Qt.NoFocus
                    // The tool decides (a greyed or linked option); the binding then
                    // shows what it took.
                    onClicked: {
                        optionsRow.tools.setOption(option.modelData.name, checked ? 1 : 0)
                        checked = Qt.binding(() => option.modelData.checked)
                    }
                }
            }
            Component {
                id: choiceComponent
                ComboBox {
                    objectName: "visualOption_" + option.modelData.name
                    model: option.modelData.choices
                    currentIndex: option.modelData.index
                    enabled: option.modelData.enabled && optionsRow.tools.railEnabled
                    focusPolicy: Qt.NoFocus
                    implicitContentWidthPolicy: ComboBox.WidestText
                    Accessible.name: option.modelData.tooltip
                    ToolTip.visible: hovered
                    ToolTip.text: option.modelData.tooltip
                    onActivated: index => optionsRow.tools.setOption(option.modelData.name, index)
                }
            }
        }
    }
}
