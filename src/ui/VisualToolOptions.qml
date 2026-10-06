import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

// T2-T4: the active family's own options, legacy VideoToolbar's second row
// (the VisualItem of the family: PositionItem's "by rectangle", X and Y
// toggles and its alignment choice, MoveItem's two points, ScaleItem,
// RotationZItem, RotationXYItem, VectorItem's point modes (one on at a time)
// and Invert clip, ClipRectangleItem's Invert clip, ...), first in the tool
// strip below the canvas (VisualToolValues, layout A); absent with no option. Each button shows its icon of the K1
// set with legacy's help text as its tooltip; a greyed icon is a disabled
// button (X and Y without the rectangle, as legacy greyed them), a pushed
// one a checked toggle; an action acts at once. The item's links (an option
// that switches another on) are the tool's (setOption). A notice a tool
// gives is legacy's HikariMessageBox titled "Warning" (VisualClips.cpp:
// 1013-1016): a modal box in the window, closed by OK.
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
            sourceComponent: modelData.kind === "choice" ? choiceComponent : buttonComponent
            Component {
                id: buttonComponent
                IconToolButton {
                    objectName: "visualOption_" + option.modelData.name
                    // The roles the families' options show (one line: icon_tests reads the roles from it).
                    iconRole: ["frame-to-scale", "scale-x", "link", "scale-y", "original-frame", "tool-scale-rotation", "resample", "two-points", "vector-drag", "vector-line", "vector-bezier", "vector-bspline", "vector-point", "vector-delete", "clip-invert"].includes(option.modelData.iconRole) ? option.modelData.iconRole : ""
                    text: option.modelData.tooltip.split("\n")[0]
                    tip: option.modelData.tooltip
                    checkable: option.modelData.kind === "toggle"
                    checked: option.modelData.checked
                    enabled: option.modelData.enabled && optionsRow.tools.railEnabled
                    focusPolicy: Qt.NoFocus
                    // The tool decides (a greyed or linked option); the binding then
                    // shows what it took, not the click's state.
                    onClicked: {
                        optionsRow.tools.setOption(option.modelData.name,
                                                   option.modelData.kind === "toggle" ? (checked ? 1 : 0) : 1)
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
    Dialog {
        id: noticeBox
        objectName: "visualNotice"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true // legacy HikariMessageBox is modal
        title: qsTr("Warning")
        standardButtons: Dialog.Ok
        // The title without the style's eliding header, whose width loops
        // through the box's implicit width.
        header: Label {
            text: noticeBox.title
            font.bold: true
            padding: 12
            bottomPadding: 0
        }
        Label {
            id: noticeText
            objectName: "visualNoticeText"
            Accessible.role: Accessible.AlertMessage
            Accessible.name: text
        }
        onClosed: optionsRow.tools.dismissNotice()
    }
    Connections {
        target: optionsRow.tools
        function onChanged() {
            if (optionsRow.tools.notice.length > 0 && !noticeBox.visible) {
                noticeText.text = optionsRow.tools.notice
                noticeBox.open()
            }
            else if (optionsRow.tools.notice.length === 0 && noticeBox.visible)
                noticeBox.close()
        }
    }
}
