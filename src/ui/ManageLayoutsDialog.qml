pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Workspace"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

// WS3 (#217): Manage layouts…: the saved layouts, in the order they were
// saved, renamed or deleted at once (every tab can use them; a tab whose
// arrangement started from one keeps the arrangement under the new name, or
// its preset's once the layout is deleted). A layout this version cannot use
// says why and can be deleted.
// PU7 (#309): a compact window on DG1's frame (opened from menus). The
// current row shows Rename and Delete as icon buttons; Rename (or F2) turns
// the row into the name field (Enter renames, Esc puts the name back, a
// refused name says why under the list), and Delete (or the Delete key)
// asks first in PK1's message box (PU7-layout-delete-asks).
DialogWindow {
    id: dialog
    objectName: "manageLayoutsDialog"
    required property var layouts // WorkspaceLayoutController
    title: qsTr("Manage layouts")
    dialogId: "manageLayouts"
    // Modal to the main window, as the Dialog was: a rename or delete can't
    // race a layout chosen meanwhile.
    modal: true
    initialFocus: list
    defaultButton: closeButton
    readonly property var rows: layouts.savedLayouts
    readonly property var current: list.currentIndex >= 0 && list.currentIndex < rows.length ? rows[list.currentIndex] : null
    // The name being typed in the current row, while renaming.
    property bool renaming: false
    property string newName: ""
    readonly property string problem: renaming && current ? layouts.layoutNameProblem(newName, current.name) : ""

    function show() {
        renaming = false
        list.currentIndex = rows.length > 0 ? Math.max(0, rows.findIndex(r => r.name === layouts.layout)) : -1
        open()
        list.forceActiveFocus()
    }
    function startRename() {
        if (!current)
            return
        newName = current.name
        renaming = true
    }
    function endRename() {
        renaming = false
        list.forceActiveFocus()
    }
    function rename() {
        if (!current || problem.length > 0)
            return
        const from = current.name
        const name = newName.trim()
        endRename()
        if (name !== from && layouts.renameSavedLayout(from, name))
            list.currentIndex = rows.findIndex(r => r.name === name)
    }
    function askDelete() {
        if (!current)
            return
        const name = current.name
        messages.ask({
            title: qsTr("Delete layout"),
            text: qsTr("Delete the layout “%1”?").arg(name),
            detail: qsTr("Tabs that use it keep their arrangement, under the preset it started from."),
            severity: "warning",
            choices: [
                { id: "delete", text: qsTr("Delete"), role: "destructive" },
                { id: "cancel", text: qsTr("Cancel"), role: "reject", isDefault: true }
            ]
        }, answer => {
            if (answer === "delete")
                dialog.remove(name)
        })
    }
    function remove(name) {
        const at = rows.findIndex(r => r.name === name)
        if (at < 0)
            return
        layouts.deleteSavedLayout(name)
        list.currentIndex = Math.min(at, rows.length - 1)
        if (rows.length === 0)
            dialog.close()
    }
    onCurrentChanged: renaming = false

    MessageHost {
        id: messages
        objectName: "manageLayoutsMessages"
        settings: dialog.settings
    }

    Frame {
        padding: 1
        Layout.preferredWidth: 332
        Layout.preferredHeight: 200
        background: Rectangle { color: Theme.field; border.color: Theme.line }
        ListView {
            id: list
            objectName: "manageLayoutsList"
            anchors.fill: parent
            clip: true
            model: dialog.rows
            keyNavigationEnabled: !dialog.renaming
            activeFocusOnTab: true
            highlightMoveDuration: 0
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Saved layouts")
            // Both are global hotkeys' keys too (F2: Select line at
            // current video position): the list takes them first.
            Keys.onShortcutOverride: event => event.accepted = event.modifiers === Qt.NoModifier
                                                               && (event.key === Qt.Key_Delete || event.key === Qt.Key_F2)
            Keys.onPressed: event => {
                if (event.key === Qt.Key_Delete) {
                    dialog.askDelete()
                    event.accepted = true
                } else if (event.key === Qt.Key_F2) {
                    dialog.startRename()
                    event.accepted = true
                }
            }
            delegate: ItemDelegate {
                id: row
                required property var modelData
                required property int index
                readonly property bool editing: highlighted && dialog.renaming
                // The list gives its current row the focus: the field keeps
                // none of it once the row is a row again (Esc closes then).
                onEditingChanged: if (!editing) nameField.focus = false
                width: ListView.view.width
                height: modelData.usable ? 32 : 44
                highlighted: ListView.isCurrentItem
                text: modelData.name
                focusPolicy: Qt.NoFocus // the list holds the focus
                onClicked: list.currentIndex = index
                onDoubleClicked: dialog.startRename()
                Accessible.role: Accessible.ListItem
                Accessible.name: modelData.usable ? modelData.name
                                                  : qsTr("%1, cannot be used").arg(modelData.name)
                Accessible.description: modelData.problem
                Accessible.selected: highlighted
                background: Rectangle {
                    color: row.highlighted ? Theme.select : "transparent"
                    Rectangle { visible: row.highlighted; width: 3; height: parent.height; color: Theme.accent }
                }
                contentItem: RowLayout {
                    spacing: 4
                    ColumnLayout {
                        visible: !row.editing
                        spacing: 0
                        Layout.fillWidth: true
                        Label {
                            text: row.modelData.name
                            elide: Text.ElideRight
                            color: row.modelData.usable ? Theme.text : Theme.muted
                            Layout.fillWidth: true
                        }
                        Label {
                            visible: !row.modelData.usable
                            text: qsTr("Cannot be used: %1").arg(row.modelData.problem)
                            elide: Text.ElideRight
                            font: dialog.fontAt(12)
                            color: Theme.muted
                            Layout.fillWidth: true
                        }
                    }
                    TextField {
                        id: nameField
                        objectName: row.editing ? "manageLayoutsName" : ""
                        visible: row.editing
                        text: dialog.newName
                        maximumLength: 64 // WorkspaceLayoutController::kMaximumNameLength
                        Layout.fillWidth: true
                        Accessible.name: qsTr("Name")
                        Accessible.description: dialog.problem
                        onVisibleChanged: if (visible) { forceActiveFocus(); selectAll() }
                        onTextEdited: dialog.newName = text
                        // Enter renames and Esc puts the name back, before the window's keys.
                        Keys.onReturnPressed: event => { event.accepted = true; dialog.rename() }
                        Keys.onEnterPressed: event => { event.accepted = true; dialog.rename() }
                        Keys.onEscapePressed: event => { event.accepted = true; dialog.endRename() }
                    }
                    IconToolButton {
                        objectName: row.highlighted ? "manageLayoutsRename" : ""
                        visible: row.highlighted && !row.editing
                        iconRole: "edit"
                        text: qsTr("Rename (F2)")
                        focusPolicy: Qt.NoFocus
                        onClicked: dialog.startRename()
                    }
                    IconToolButton {
                        objectName: row.highlighted ? "manageLayoutsDelete" : ""
                        visible: row.highlighted && !row.editing
                        iconRole: "delete"
                        text: qsTr("Delete")
                        focusPolicy: Qt.NoFocus
                        onClicked: dialog.askDelete()
                    }
                }
            }
        }
    }
    Label {
        objectName: "manageLayoutsProblem"
        text: dialog.problem
        visible: text.length > 0
        color: Theme.danger
        wrapMode: Text.WordWrap
        Layout.maximumWidth: 332
        Accessible.role: Accessible.AlertMessage
    }
    buttons: [
        PrimaryButton {
            id: closeButton
            objectName: "manageLayoutsClose"
            text: qsTr("Close")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: dialog.close()
        }
    ]
}
