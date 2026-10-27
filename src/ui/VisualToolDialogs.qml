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

    // T5: the shape list's "Edit"; T6: the all-tags tool's Edit. Each window
    // is made at its first opening, as a child of this item: a window
    // declared in an item has no QObject parent and would outlive it (and
    // its engine) when fullscreen makes the bar's dialogs again.
    property ShapesEditionDialog shapesDialog: null
    property AllTagsEditionDialog tagsDialog: null
    Component {
        id: shapesComponent
        ShapesEditionDialog {
            editor: dialogs.tools.shapeEditor
            settings: dialogs.settings
        }
    }
    Component {
        id: tagsComponent
        AllTagsEditionDialog {
            editor: dialogs.tools.tagsEditor
            settings: dialogs.settings
        }
    }
    function syncShapes(quiet) {
        if (dialogs.tools.shapeEditor) {
            if (!shapesDialog)
                shapesDialog = shapesComponent.createObject(dialogs)
            if (quiet)
                shapesDialog.openQuietly()
            else
                shapesDialog.open()
        } else if (shapesDialog && shapesDialog.visible) {
            shapesDialog.close()
        }
    }
    function syncTags(quiet) {
        if (dialogs.tools.tagsEditor) {
            if (!tagsDialog)
                tagsDialog = tagsComponent.createObject(dialogs)
            if (quiet)
                tagsDialog.openQuietly()
            else
                tagsDialog.open()
        } else if (tagsDialog && tagsDialog.visible) {
            tagsDialog.close()
        }
    }
    Connections {
        target: dialogs.tools
        function onShapeEditorChanged() { dialogs.syncShapes() }
        function onTagsEditorChanged() { dialogs.syncTags() }
    }
    // Made as fullscreen opens or closes: what is open moves with it,
    // leaving the keyboard where fullscreen put it (after Escape, the
    // main window's).
    Component.onCompleted: {
        syncShapes(true)
        syncTags(true)
    }
}
