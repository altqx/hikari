pragma Translator: "HikariSub.VisualTools"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

// The visual tools' dialogs, with the inspector bar that opens them (VT1;
// before it the options row). A notice a tool gives is legacy's
// HikariMessageBox titled "Warning" (VisualClips.cpp:1013-1016): a modal box
// in the window, closed by OK. T5: the drawing's shape list's "Edit" opens
// "Vector shape editing"; T6: the all-tags tool's Edit opens "Tag editing".
Item {
    id: dialogs
    objectName: "visualToolDialogs"
    required property VisualToolsController tools
    width: 0
    height: 0

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
        onClosed: dialogs.tools.dismissNotice()
    }
    // T5: the shape list's "Edit".
    ShapesEditionDialog {
        id: shapesDialog
        editor: dialogs.tools.shapeEditor
    }
    // T6: the all-tags tool's Edit.
    AllTagsEditionDialog {
        id: tagsDialog
        editor: dialogs.tools.tagsEditor
    }
    Connections {
        target: dialogs.tools
        function onChanged() {
            if (dialogs.tools.notice.length > 0 && !noticeBox.visible) {
                noticeText.text = dialogs.tools.notice
                noticeBox.open()
            }
            else if (dialogs.tools.notice.length === 0 && noticeBox.visible)
                noticeBox.close()
        }
        function onShapeEditorChanged() {
            if (dialogs.tools.shapeEditor)
                shapesDialog.open()
            else if (shapesDialog.visible)
                shapesDialog.close()
        }
        function onTagsEditorChanged() {
            if (dialogs.tools.tagsEditor)
                tagsDialog.open()
            else if (tagsDialog.visible)
                tagsDialog.close()
        }
    }
}
