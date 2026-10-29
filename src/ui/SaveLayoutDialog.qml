pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Workspace"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: dialog
    objectName: "saveLayoutDialog"
    required property var layouts
    title: qsTr("Save layout as")
    dialogId: "saveLayout"
    modal: true
    initialFocus: nameField
    defaultButton: saveButton
    // Invokable results are not tracked: reading savedLayouts re-evaluates these when the layouts change.
    readonly property string problem: layouts.savedLayouts && layouts.layoutNameProblem(nameField.text)
    readonly property string existing: layouts.savedLayouts && layouts.savedLayoutNamed(nameField.text)

    function show() {
        nameField.text = layouts.layout
        open()
        nameField.selectAll()
        nameField.forceActiveFocus()
    }
    onAccepted: layouts.saveLayoutAs(nameField.text)

    RowLayout {
        spacing: 8
        Label {
            text: qsTr("Name")
            Layout.preferredWidth: 56
        }
        TextField {
            id: nameField
            objectName: "saveLayoutName"
            Layout.preferredWidth: 260
            // MUST KILL: maximumLength 64 duplicates WorkspaceLayoutController::kMaximumNameLength
            maximumLength: 64
            Accessible.name: qsTr("Name")
            Accessible.description: dialog.problem
        }
    }
    RowLayout {
        visible: note.text.length > 0
        spacing: 6
        Layout.leftMargin: 64
        Layout.maximumWidth: 260
        Icon {
            iconRole: dialog.problem.length > 0 ? "notice-error" : "about"
            size: 14
            color: dialog.problem.length > 0 ? Theme.danger : Theme.text
            accentColor: dialog.problem.length > 0 ? Theme.danger : IconTheme.accent
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 2
        }
        Label {
            id: note
            objectName: "saveLayoutNote"
            text: nameField.text.trim().length === 0 ? ""
                : dialog.problem.length > 0 ? dialog.problem
                : dialog.existing.length > 0 ? qsTr("Saving replaces the layout %1.").arg(dialog.existing) : ""
            color: dialog.problem.length > 0 ? Theme.danger : Theme.text
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Accessible.role: Accessible.AlertMessage
        }
    }
    buttons: [
        PrimaryButton {
            id: saveButton
            objectName: "saveLayoutSave"
            text: dialog.existing.length > 0 ? qsTr("Replace") : qsTr("Save")
            enabled: dialog.problem.length === 0
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: dialog.accept()
        },
        Button {
            objectName: "saveLayoutCancel"
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: dialog.reject()
        }
    ]
}
