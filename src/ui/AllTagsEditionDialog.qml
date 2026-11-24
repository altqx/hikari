pragma Translator: "HikariSub.VisualTools"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: dialog
    objectName: "tagsEditionDialog"
    property AllTagsEditor editor
    title: pane.title
    dialogId: "allTagsEdition"
    resizable: true
    initialFocus: pane.firstFocus

    // MUST KILL: a hidden transientParent means the window went with its opener, not a close
    onClosing: if (editor && !(transientParent && !transientParent.visible)) editor.close()
    Connections {
        target: dialog.editor
        function onClosed() { dialog.close() }
    }

    AllTagsEditionPane {
        id: pane
        Layout.fillWidth: true
        Layout.fillHeight: true
        editor: dialog.editor
        settings: dialog.settings
        open: dialog.visible
        shortcutsActive: true
    }

    leadingActions: EditionUndoRedo {
        prefix: "tags"
        canUndo: dialog.editor ? dialog.editor.canUndo : false
        canRedo: dialog.editor ? dialog.editor.canRedo : false
        undoStep: dialog.editor ? pane.stepText(dialog.editor.undoStep) : ""
        redoStep: dialog.editor ? pane.stepText(dialog.editor.redoStep) : ""
        onUndo: pane.undo()
        onRedo: pane.redo()
    }
    buttons: PrimaryButton {
        id: closeButton
        objectName: "tagsClose"
        text: qsTr("Close")
        DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        onClicked: dialog.close()
    }
    defaultButton: closeButton
}
