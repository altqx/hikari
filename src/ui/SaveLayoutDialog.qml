pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Workspace"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

// WS3 (#217): Save layout as…: the current Document tab's arrangement under
// a name, a saved layout every tab can use. The name starts as the tab's
// saved layout's, so saving again replaces it; a name in use (in any case)
// says so and the button reads Replace. A preset's name, an empty name or one
// too long is refused with the reason under the field.
// PU7 (#309): opened from menus (the layout switcher's, View > Layout), so a
// compact window on DG1's frame by the small forms rule, titled after the
// command (PU7-layout-windows).
DialogWindow {
    id: dialog
    objectName: "saveLayoutDialog"
    required property var layouts // WorkspaceLayoutController
    title: qsTr("Save layout as")
    dialogId: "saveLayout"
    initialFocus: nameField
    defaultButton: saveButton
    // Both read the saved layouts too: re-evaluated when they change, not
    // only when the name does (the dialog opens again on the same name).
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
            maximumLength: 64 // WorkspaceLayoutController::kMaximumNameLength
            Accessible.name: qsTr("Name")
            Accessible.description: dialog.problem
        }
    }
    RowLayout {
        // Nothing to say while the field is empty: the button is off.
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
