import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

// The active family's own options, legacy VideoToolbar's second row
// (VisualItem). T4: the clips' buttons: VectorItem's point modes (one on at
// a time) and Invert clip, ClipRectangleItem's Invert clip, each with its
// icon of the K1 set and legacy's help text. A toggle shows its state; an
// action acts at once. A notice a tool gives is legacy's HikariMessageBox
// titled "Warning" (VisualClips.cpp:1013-1016): a modal box in the window,
// closed by OK.
RowLayout {
    id: options
    objectName: "visualToolOptions"
    required property VisualToolsController tools
    spacing: 1

    Repeater {
        model: options.tools.options
        delegate: IconToolButton {
            required property var modelData
            objectName: "visualOption_" + modelData.name
            // The K1 roles the options name (on one line: icon_tests reads them from it).
            iconRole: ["vector-drag", "vector-line", "vector-bezier", "vector-bspline", "vector-point", "vector-delete", "clip-invert"].indexOf(modelData.iconRole) >= 0 ? modelData.iconRole : ""
            text: modelData.tooltip.split("\n")[0]
            tip: modelData.tooltip
            checkable: modelData.kind === "toggle"
            checked: modelData.checked
            enabled: modelData.enabled && options.tools.railEnabled
            focusPolicy: Qt.NoFocus
            onClicked: {
                options.tools.setOption(modelData.name, modelData.kind === "toggle" ? (checked ? 1 : 0) : 1)
                checked = Qt.binding(() => modelData.checked) // the tool's state, not the click's
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
    }
}
