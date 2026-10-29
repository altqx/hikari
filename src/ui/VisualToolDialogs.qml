pragma Translator: "HikariSub.VisualTools"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

Item {
    id: dialogs
    objectName: "visualToolDialogs"
    required property VisualToolsController tools
    property SettingsStore settings: null
    width: 0
    height: 0

    // A Window declared inside an Item gets no QObject parent and would outlive it, so each is made with createObject(dialogs).
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
    Component.onCompleted: {
        syncShapes(true)
        syncTags(true)
    }
}
