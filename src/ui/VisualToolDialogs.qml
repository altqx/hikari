pragma Translator: "HikariSub.VisualTools"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

// The visual tools' dialogs, with the inspector bar that opens them (VT1;
// before it the options row). A notice a tool gives (legacy's HikariMessageBox
// titled "Warning", VisualClips.cpp:1013-1016) is a warning toast (NT1,
// VisualToolsController::notice). T5: the drawing's shape list's "Edit"
// opens "Edit shapes"; T6: the all-tags tool's Edit opens "Edit tags" (PU5,
// modeless windows on DG1's frame, kept where they were left).
Item {
    id: dialogs
    objectName: "visualToolDialogs"
    required property VisualToolsController tools
    property SettingsStore settings: null
    width: 0
    height: 0

    // T5: the shape list's "Edit".
    ShapesEditionDialog {
        id: shapesDialog
        editor: dialogs.tools.shapeEditor
        settings: dialogs.settings
    }
    // T6: the all-tags tool's Edit.
    AllTagsEditionDialog {
        id: tagsDialog
        editor: dialogs.tools.tagsEditor
        settings: dialogs.settings
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
