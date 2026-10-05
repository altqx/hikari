import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

// The active family's own options, legacy VideoToolbar's second row
// (VisualItem). T4: the clips' buttons: VectorItem's point modes (one on at
// a time) and Invert clip, ClipRectangleItem's Invert clip, each with its
// icon of the K1 set and legacy's help text. A toggle shows its state; an
// action acts at once. A notice a tool gives (legacy showed a message box)
// sits after them until the next click on the video, or until dismissed.
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
    Label {
        objectName: "visualNotice"
        visible: text.length > 0
        text: options.tools.notice
        font.bold: true
        Accessible.role: Accessible.AlertMessage
        Accessible.name: text
    }
    ToolButton {
        objectName: "visualNoticeDismiss"
        visible: options.tools.notice.length > 0
        text: qsTr("OK")
        focusPolicy: Qt.NoFocus
        onClicked: options.tools.dismissNotice()
    }
}
