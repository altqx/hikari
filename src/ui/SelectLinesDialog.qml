// F2: legacy SelectLines "Select" (GLOBAL_OPEN_SELECT_LINES), a modeless
// dialog: what to find and where, Dialogue/Comments, the selection operation
// and the action on the selected Lines. Each run reports its count in a
// message whose Close also closes the dialog.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Dialog {
    id: dialog
    objectName: "selectLinesDialog"
    required property var app
    title: qsTr("Select")
    modal: false
    property var recent: []

    function openDialog() {
        const s = app.openSelectLines()
        withButton.checked = s.with
        withoutButton.checked = !s.with
        matchCase.checked = s.matchCase
        regex.checked = s.regex
        fields.itemAt(s.field).checked = true
        dialogues.checked = s.dialogues
        comments.checked = s.comments
        modes.itemAt(s.mode).checked = true
        actions.itemAt(s.action).checked = true
        recent = s.recent
        findText.editText = recent.length > 0 ? recent[0] : ""
        open()
        findText.forceActiveFocus()
    }
    function settings() {
        let field = 0, mode = 0, action = 0
        for (let i = 0; i < fields.count; ++i) if (fields.itemAt(i).checked) field = i
        for (let i = 0; i < modes.count; ++i) if (modes.itemAt(i).checked) mode = i
        for (let i = 0; i < actions.count; ++i) if (actions.itemAt(i).checked) action = i
        return {find: findText.editText, with: withButton.checked, matchCase: matchCase.checked,
                regex: regex.checked, field: field, dialogues: dialogues.checked,
                comments: comments.checked, mode: mode, action: action}
    }
    function run(allTabs) {
        const text = findText.editText
        result.text = app.selectLines(settings(), allTabs)
        recent = app.selectLinesSettings().recent
        findText.editText = text
        result.open()
    }
    onClosed: app.saveSelectLinesSettings(settings())

    ColumnLayout {
        anchors.fill: parent
        GroupBox {
            title: qsTr("Find")
            Layout.fillWidth: true
            ColumnLayout {
                anchors.fill: parent
                RowLayout {
                    ButtonGroup { id: withGroup }
                    RadioButton { id: withButton; objectName: "selectWith"; text: qsTr("With"); ButtonGroup.group: withGroup; Layout.fillWidth: true }
                    RadioButton { id: withoutButton; objectName: "selectWithout"; text: qsTr("Without"); ButtonGroup.group: withGroup; Layout.fillWidth: true }
                }
                RowLayout {
                    ComboBox {
                        id: findText
                        objectName: "selectFindText"
                        editable: true
                        model: dialog.recent
                        Layout.fillWidth: true
                        Layout.minimumWidth: 260
                        ToolTip.text: qsTr("Search text:")
                        ToolTip.visible: hovered
                        Accessible.name: qsTr("Search text:")
                        Keys.onReturnPressed: dialog.run(false)
                        Keys.onEnterPressed: dialog.run(false)
                    }
                    Button {
                        objectName: "selectChooseStyles"
                        text: "+"
                        Accessible.name: qsTr("Choose styles")
                        onClicked: stylesDialog.openWith(dialog.app.styleNames())
                    }
                }
                CheckBox { id: matchCase; objectName: "selectMatchCase"; text: qsTr("Match case") }
                CheckBox { id: regex; objectName: "selectRegex"; text: qsTr("Regular expressions") }
            }
        }
        GroupBox {
            title: qsTr("In field")
            Layout.fillWidth: true
            GridLayout {
                anchors.fill: parent
                columns: 4
                ButtonGroup { id: fieldGroup }
                Repeater {
                    id: fields
                    model: [qsTr("Text"), qsTr("Styles"), qsTr("Actor"), qsTr("Effect"), qsTr("Start time"), qsTr("End time")]
                    RadioButton { text: modelData; ButtonGroup.group: fieldGroup; Layout.fillWidth: true }
                }
            }
        }
        GroupBox {
            title: qsTr("Dialogue / comments")
            Layout.fillWidth: true
            RowLayout {
                CheckBox { id: dialogues; objectName: "selectDialogues"; text: qsTr("Dialogue") }
                CheckBox { id: comments; objectName: "selectComments"; text: qsTr("Comments") }
            }
        }
        GroupBox {
            title: qsTr("Selection")
            Layout.fillWidth: true
            GridLayout {
                anchors.fill: parent
                columns: 2
                ButtonGroup { id: modeGroup }
                Repeater {
                    id: modes
                    model: [qsTr("Select"), qsTr("Add to selection"), qsTr("Deselect")]
                    RadioButton { text: modelData; ButtonGroup.group: modeGroup; Layout.fillWidth: true }
                }
            }
        }
        GroupBox {
            title: qsTr("Action")
            Layout.fillWidth: true
            GridLayout {
                anchors.fill: parent
                columns: 2
                ButtonGroup { id: actionGroup }
                Repeater {
                    id: actions
                    model: [qsTr("Do nothing"), qsTr("Copy"), qsTr("Cut"), qsTr("Move to beginning"),
                            qsTr("Move to end"), qsTr("Set as comment"), qsTr("Delete")]
                    RadioButton { text: modelData; ButtonGroup.group: actionGroup; Layout.fillWidth: true }
                }
            }
        }
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Button { objectName: "selectRun"; text: qsTr("Select"); onClicked: dialog.run(false) }
            Button { objectName: "selectRunAllTabs"; text: qsTr("Select in all tabs"); onClicked: dialog.run(true) }
            Button { text: qsTr("Close"); onClicked: dialog.close() }
        }
    }

    // The count after a run: "Close" also closes the Select dialog, "Ok" keeps it.
    Dialog {
        id: result
        objectName: "selectResult"
        property alias text: resultLabel.text
        title: qsTr("Select")
        modal: true
        anchors.centerIn: parent
        Label { id: resultLabel; objectName: "selectResultText" }
        footer: DialogButtonBox {
            Button { text: qsTr("Close"); DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
            Button { text: "Ok"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        }
        onAccepted: dialog.close()
    }

    // Legacy Stylelistbox "Choose styles": OK makes the checked styles a
    // Styles regular expression. Approved F2-style-cancel (2026-10-04):
    // Cancel changes nothing (legacy filled in "^$").
    Dialog {
        id: stylesDialog
        objectName: "selectStylesDialog"
        title: qsTr("Choose styles")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        property var names: []
        property var checkedNames: []
        function openWith(list) {
            names = list
            checkedNames = []
            open()
        }
        function apply(chosen) {
            findText.editText = dialog.app.selectStylesPattern(chosen)
            fields.itemAt(1).checked = true
            regex.checked = true
        }
        ColumnLayout {
            Repeater {
                model: stylesDialog.names
                CheckBox {
                    text: modelData
                    onToggled: {
                        const list = stylesDialog.checkedNames.filter(n => n !== modelData)
                        if (checked)
                            list.push(modelData)
                        stylesDialog.checkedNames = list
                    }
                }
            }
        }
        onAccepted: apply(names.filter(n => checkedNames.indexOf(n) >= 0))
    }
}
