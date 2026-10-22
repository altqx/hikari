pragma Translator: "HikariSub.VisualTools"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

// The visual tools' dialogs, with the inspector bar that opens them (VT1;
// before it the options row). A notice a tool gives (legacy's HikariMessageBox
// titled "Warning", VisualClips.cpp:1013-1016) is a warning toast (NT1,
// VisualToolsController::notice). T5: the drawing's shape list's "Edit"
// opens "Vector shape editing"; T6: the all-tags tool's Edit opens "Tag
// editing".
Item {
    id: dialogs
    objectName: "visualToolDialogs"
    required property VisualToolsController tools
    width: 0
    height: 0

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
    function syncShapes() {
        if (dialogs.tools.shapeEditor)
            shapesDialog.open()
        else if (shapesDialog.visible)
            shapesDialog.close()
    }
    function syncTags() {
        if (dialogs.tools.tagsEditor)
            tagsDialog.open()
        else if (tagsDialog.visible)
            tagsDialog.close()
    }
    Connections {
        target: dialogs.tools
        function onShapeEditorChanged() { dialogs.syncShapes() }
        function onTagsEditorChanged() { dialogs.syncTags() }
    }
    // Made as fullscreen opens or closes: what is open moves with it.
    Component.onCompleted: {
        syncShapes()
        syncTags()
    }
}
