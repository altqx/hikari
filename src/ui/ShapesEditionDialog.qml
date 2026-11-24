pragma Translator: "HikariSub.VisualTools"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: dialog
    objectName: "shapesEditionDialog"
    property ShapeEditor editor
    title: pane.title
    dialogId: "shapesEdition"
    resizable: true
    initialFocus: pane.firstFocus

    // MUST KILL: a close while transientParent is hidden keeps the editor, so the window can be made again
    onClosing: if (editor && !(transientParent && !transientParent.visible)) editor.close()
    Connections {
        target: dialog.editor
        function onClosed() { dialog.close() }
    }

    ShapesEditionPane {
        id: pane
        Layout.fillWidth: true
        Layout.fillHeight: true
        editor: dialog.editor
        settings: dialog.settings
        open: dialog.visible
        shortcutsActive: true
    }

    leadingActions: EditionUndoRedo {
        prefix: "shapes"
        canUndo: dialog.editor ? dialog.editor.canUndo : false
        canRedo: dialog.editor ? dialog.editor.canRedo : false
        undoStep: dialog.editor ? pane.stepText(dialog.editor.undoStep) : ""
        redoStep: dialog.editor ? pane.stepText(dialog.editor.redoStep) : ""
        onUndo: pane.undo()
        onRedo: pane.redo()
    }
    buttons: PrimaryButton {
        id: closeButton
        objectName: "shapesClose"
        text: qsTr("Close")
        DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        onClicked: dialog.close()
    }
    defaultButton: closeButton
}
