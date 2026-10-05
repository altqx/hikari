import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

// T2: the active family's own options, legacy VideoToolbar's second row
// (VisualItem: PositionItem's "by rectangle", X and Y toggles and its
// alignment choice, MoveItem's two points). A toggle shows its icon of the
// K1 set with legacy's help text; X and Y are greyed without the rectangle,
// as legacy greyed them.
RowLayout {
    id: options
    objectName: "visualToolOptions"
    required property VisualToolsController tools
    spacing: 2
    // The options' names, in the order of the roles below.
    readonly property var optionNames: ["byRectangle", "x", "y", "twoPoints"]

    Repeater {
        model: options.tools.options
        delegate: Loader {
            id: option
            required property var modelData
            sourceComponent: modelData.kind === "choice" ? choiceComponent : toggleComponent
            Component {
                id: toggleComponent
                IconToolButton {
                    objectName: "visualOption_" + option.modelData.name
                    text: option.modelData.tooltip.split("\n")[0]
                    tip: option.modelData.tooltip
                    iconRole: ["frame-to-scale", "scale-x", "scale-y", "two-points"][options.optionNames.indexOf(option.modelData.name)] ?? ""
                    checkable: true
                    checked: option.modelData.checked
                    enabled: option.modelData.enabled
                    focusPolicy: Qt.NoFocus
                    onClicked: options.tools.setOption(option.modelData.name, checked ? 1 : 0)
                }
            }
            Component {
                id: choiceComponent
                ComboBox {
                    objectName: "visualOption_" + option.modelData.name
                    model: option.modelData.choices
                    currentIndex: option.modelData.index
                    enabled: option.modelData.enabled
                    focusPolicy: Qt.NoFocus
                    implicitContentWidthPolicy: ComboBox.WidestText
                    Accessible.name: option.modelData.tooltip
                    ToolTip.visible: hovered
                    ToolTip.text: option.modelData.tooltip
                    onActivated: index => options.tools.setOption(option.modelData.name, index)
                }
            }
        }
    }
}
