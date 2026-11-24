pragma Translator: "HikariSub.VisualTools"
import QtQuick
import QtQuick.Layouts
import Hikari.Ui

RowLayout {
    id: history
    required property string prefix
    required property bool canUndo
    required property bool canRedo
    required property string undoStep
    required property string redoStep
    signal undo()
    signal redo()
    spacing: 8
    IconTextButton {
        objectName: history.prefix + "Undo"
        iconRole: "undo"
        text: qsTr("Undo")
        enabled: history.canUndo
        tip: enabled ? qsTr("Undo “%1”").arg(history.undoStep) : ""
        onClicked: history.undo()
    }
    IconTextButton {
        objectName: history.prefix + "Redo"
        iconRole: "redo"
        text: qsTr("Redo")
        enabled: history.canRedo
        tip: enabled ? qsTr("Redo “%1”").arg(history.redoStep) : ""
        onClicked: history.redo()
    }
}
