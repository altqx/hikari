import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

// The active family's own options, legacy VideoToolbar's second row
// (VisualItem). T4: the clips' buttons: VectorItem's point modes (one on at
// a time) and Invert clip, ClipRectangleItem's Invert clip, each with its
// icon of the K1 set and legacy's help text. A toggle shows its state; an
// action acts at once. T5: the drawing's point modes (not usable while a
// shape is chosen) and its shape list ("Choose", the presets, "Edit", which
// opens the "Vector shape editing" dialog). A notice a tool gives is
// legacy's HikariMessageBox titled "Warning" (VisualClips.cpp:1013-1016): a
// modal box in the window, closed by OK.
RowLayout {
    id: options
    objectName: "visualToolOptions"
    required property VisualToolsController tools
    spacing: 1

    Repeater {
        model: options.tools.options
        delegate: Loader {
            id: option
            required property var modelData
            sourceComponent: modelData.kind === "choice" ? choiceComponent : buttonComponent
            Component {
                id: buttonComponent
                IconToolButton {
                    objectName: "visualOption_" + option.modelData.name
                    // The K1 roles the options name (on one line: icon_tests reads them from it).
                    iconRole: ["vector-drag", "vector-line", "vector-bezier", "vector-bspline", "vector-point", "vector-delete", "clip-invert"].indexOf(option.modelData.iconRole) >= 0 ? option.modelData.iconRole : ""
                    text: option.modelData.tooltip.split("\n")[0]
                    tip: option.modelData.tooltip
                    checkable: option.modelData.kind === "toggle"
                    checked: option.modelData.checked
                    enabled: option.modelData.enabled && options.tools.railEnabled
                    focusPolicy: Qt.NoFocus
                    onClicked: {
                        options.tools.setOption(option.modelData.name, option.modelData.kind === "toggle" ? (checked ? 1 : 0) : 1)
                        checked = Qt.binding(() => option.modelData.checked) // the tool's state, not the click's
                    }
                }
            }
            Component {
                id: choiceComponent
                ComboBox {
                    objectName: "visualOption_" + option.modelData.name
                    model: option.modelData.choices
                    currentIndex: option.modelData.index
                    enabled: option.modelData.enabled && options.tools.railEnabled
                    focusPolicy: Qt.NoFocus
                    implicitContentWidthPolicy: ComboBox.WidestText
                    Accessible.name: option.modelData.tooltip.split("\n")[0]
                    ToolTip.visible: hovered
                    ToolTip.text: option.modelData.tooltip
                    ToolTip.delay: Qt.styleHints.mousePressAndHoldInterval
                    onActivated: index => {
                        options.tools.setOption(option.modelData.name, index)
                        currentIndex = Qt.binding(() => option.modelData.index) // the tool's choice
                    }
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
        onClosed: options.tools.dismissNotice()
    }
    // T5: the shape list's "Edit".
    ShapesEditionDialog {
        id: shapesDialog
        editor: options.tools.shapeEditor
    }
    Connections {
        target: options.tools
        function onChanged() {
            if (options.tools.notice.length > 0 && !noticeBox.visible) {
                noticeText.text = options.tools.notice
                noticeBox.open()
            }
            else if (options.tools.notice.length === 0 && noticeBox.visible)
                noticeBox.close()
        }
        function onShapeEditorChanged() {
            if (options.tools.shapeEditor)
                shapesDialog.open()
            else if (shapesDialog.visible)
                shapesDialog.close()
        }
    }
}
