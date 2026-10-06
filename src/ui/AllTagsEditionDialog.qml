import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

// T6: legacy AllTagsEdition, "Tag editing" (VisualAllTagsEdition.cpp:
// 155-271): the all-tags tool's tag definitions, opened from the tool row's
// Edit. The list with a new name, Add tag and Delete tag; the edited tag's
// name, tag, minimal and maximal value, value and step, where Insert puts
// it, its decimal places, how many more values it takes and its change
// option, and the more values; Apply keeps an edit here, OK keeps it and
// saves the definitions, Cancel drops what the dialog changed, Restore
// default puts legacy's 22 definitions back. Legacy's message boxes are
// modal boxes over the dialog. Legacy titled the fields' box "Tag editing"
// too; the dialog's title alone names it here (the user's UI rule: no label
// repeats its container's).
Dialog {
    id: dialog
    objectName: "tagsEditionDialog"
    property AllTagsEditor editor
    title: qsTr("Tag editing")
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    closePolicy: Popup.CloseOnEscape
    padding: 8

    // A list change waiting on its question.
    property int pendingIndex: -1

    function show(message) {
        if (!message || !message.text)
            return false
        messageBox.title = message.title
        messageText.text = message.text
        messageBox.open()
        return true
    }
    function listChanged(index) {
        // OnListChanged: the edited tag's changes are asked about first.
        const question = editor.listChangeQuestion()
        if (question.text) {
            pendingIndex = index
            questionBox.kind = "list"
            questionBox.title = question.title
            questionText.text = question.text
            questionBox.open()
        } else {
            editor.select(index)
        }
    }

    onRejected: if (editor) editor.cancel()
    Connections {
        target: dialog.editor
        function onClosed() { dialog.close() }
    }

    // Legacy NumCtrl: digits, a minus where the range takes one, a point or
    // a comma for a decimal; 20 characters at most. The model keeps the
    // last valid text to fall back to.
    component NumberField: TextField {
        id: number
        required property string field
        property bool decimals: true
        property bool negative: true
        objectName: "tagsField_" + field
        Layout.fillWidth: true
        Layout.preferredWidth: 90
        maximumLength: 20
        text: dialog.editor ? dialog.editor.numbers[field] : ""
        validator: RegularExpressionValidator {
            regularExpression: number.decimals ? (number.negative ? /[-0-9.,]*/ : /[0-9.,]*/)
                                               : (number.negative ? /[-0-9]*/ : /[0-9]*/)
        }
        onTextEdited: dialog.editor.setNumber(field, text)
        onAccepted: okButton.clicked()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 6
        GroupBox {
            title: qsTr("Edited tag")
            Layout.fillWidth: true
            RowLayout {
                anchors.fill: parent
                ComboBox {
                    id: tagList
                    objectName: "tagsList"
                    Layout.fillWidth: true
                    Layout.preferredWidth: 150
                    model: dialog.editor ? dialog.editor.list : []
                    currentIndex: dialog.editor ? dialog.editor.selection : -1
                    Accessible.name: qsTr("Tags")
                    onActivated: index => {
                        currentIndex = Qt.binding(() => dialog.editor ? dialog.editor.selection : -1)
                        dialog.listChanged(index)
                    }
                }
                TextField {
                    id: newTagName
                    objectName: "newTagName"
                    Layout.fillWidth: true
                    Layout.preferredWidth: 150
                    placeholderText: qsTr("New tag name")
                    Accessible.name: qsTr("New tag name")
                    onAccepted: addTagButton.clicked()
                }
                Button {
                    id: addTagButton
                    objectName: "addTag"
                    text: qsTr("Add tag")
                    onClicked: dialog.show(dialog.editor.addTag(newTagName.text))
                }
                Button {
                    objectName: "deleteTag"
                    text: qsTr("Delete tag")
                    onClicked: dialog.show(dialog.editor.removeTag())
                }
            }
        }
        Frame {
            Layout.fillWidth: true
            GridLayout {
                anchors.fill: parent
                columns: 4
                columnSpacing: 8
                Label { text: qsTr("Name:") }
                TextField {
                    objectName: "tagsName"
                    Layout.fillWidth: true
                    Layout.preferredWidth: 140
                    text: dialog.editor ? dialog.editor.name : ""
                    Accessible.name: qsTr("Name")
                    onTextEdited: dialog.editor.name = text
                    onAccepted: okButton.clicked()
                }
                Label { text: qsTr("Tag:") }
                TextField {
                    objectName: "tagsTag"
                    Layout.fillWidth: true
                    Layout.preferredWidth: 140
                    text: dialog.editor ? dialog.editor.tag : ""
                    Accessible.name: qsTr("Tag")
                    onTextEdited: dialog.editor.tag = text
                    onAccepted: okButton.clicked()
                }
                Label { text: qsTr("Minimal value:") }
                NumberField { field: "min"; Accessible.name: qsTr("Minimal value") }
                Label { text: qsTr("Maximal value:") }
                NumberField { field: "max"; Accessible.name: qsTr("Maximal value") }
                Label { text: qsTr("Value:") }
                NumberField { field: "value"; Accessible.name: qsTr("Value") }
                Label { text: qsTr("Step:") }
                NumberField { field: "step"; Accessible.name: qsTr("Step") }
                ComboBox {
                    objectName: "tagsPlacing"
                    Layout.columnSpan: 2
                    Layout.fillWidth: true
                    model: dialog.editor ? dialog.editor.placings : []
                    currentIndex: dialog.editor ? dialog.editor.placing : 0
                    implicitContentWidthPolicy: ComboBox.WidestText
                    Accessible.name: qsTr("Where Insert puts the tag")
                    onActivated: index => {
                        currentIndex = Qt.binding(() => dialog.editor ? dialog.editor.placing : 0)
                        dialog.editor.placing = index
                    }
                }
                Label { text: qsTr("Decimal places:") }
                NumberField {
                    field: "digits"
                    decimals: false
                    negative: false
                    Accessible.name: qsTr("Decimal places")
                }
                ComboBox {
                    objectName: "tagsValueCount"
                    Layout.columnSpan: 2
                    Layout.fillWidth: true
                    model: dialog.editor ? dialog.editor.valueCounts : []
                    currentIndex: dialog.editor ? dialog.editor.additionalValues : 0
                    implicitContentWidthPolicy: ComboBox.WidestText
                    Accessible.name: qsTr("Used only when tag have 2 values or more")
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Used only when tag have 2 values or more")
                    onActivated: index => {
                        currentIndex = Qt.binding(() => dialog.editor ? dialog.editor.additionalValues : 0)
                        dialog.editor.additionalValues = index
                    }
                }
                ComboBox {
                    objectName: "tagsChangeOption"
                    Layout.columnSpan: 2
                    Layout.fillWidth: true
                    model: dialog.editor ? dialog.editor.changeOptions : []
                    currentIndex: dialog.editor ? dialog.editor.changeOption : 0
                    implicitContentWidthPolicy: ComboBox.WidestText
                    Accessible.name: qsTr("Tag change options")
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Tag change options")
                    onActivated: index => {
                        currentIndex = Qt.binding(() => dialog.editor ? dialog.editor.changeOption : 0)
                        dialog.editor.changeOption = index
                    }
                }
                Label { text: qsTr("Additional values:") }
                RowLayout {
                    Layout.columnSpan: 3
                    Layout.fillWidth: true
                    // values[i] takes input while the count takes it
                    // (VisualAllTagsEdition.cpp:230-241); legacy's tooltips
                    // number them from 3.
                    NumberField {
                        field: "value2"
                        enabled: dialog.editor ? dialog.editor.additionalValues >= 1 : false
                        Accessible.name: qsTr("Value %1").arg(3)
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Value %1").arg(3)
                    }
                    NumberField {
                        field: "value3"
                        enabled: dialog.editor ? dialog.editor.additionalValues >= 2 : false
                        Accessible.name: qsTr("Value %1").arg(4)
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Value %1").arg(4)
                    }
                    NumberField {
                        field: "value4"
                        enabled: dialog.editor ? dialog.editor.additionalValues >= 3 : false
                        Accessible.name: qsTr("Value %1").arg(5)
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Value %1").arg(5)
                    }
                }
            }
        }
    }

    // Legacy's button row, in its order: Apply, OK, Cancel, Restore default.
    footer: Pane {
        padding: 8
        RowLayout {
            anchors.fill: parent
            uniformCellSizes: true
            Button {
                objectName: "tagsApply"
                Layout.fillWidth: true
                text: qsTr("Apply")
                onClicked: dialog.show(dialog.editor.save(false))
            }
            Button {
                id: okButton
                objectName: "tagsOk"
                Layout.fillWidth: true
                text: qsTr("OK")
                onClicked: dialog.show(dialog.editor.save(true))
            }
            Button {
                objectName: "tagsCancel"
                Layout.fillWidth: true
                text: qsTr("Cancel")
                onClicked: dialog.reject()
            }
            Button {
                objectName: "tagsRestoreDefault"
                Layout.fillWidth: true
                text: qsTr("Restore default")
                onClicked: {
                    const q = dialog.editor.restoreQuestion()
                    questionBox.kind = "restore"
                    questionBox.title = q.title
                    questionText.text = q.text
                    questionBox.open()
                }
            }
        }
    }

    // HikariMessageBox with OK (the errors).
    Dialog {
        id: messageBox
        objectName: "tagsMessage"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok
        Label {
            id: messageText
            objectName: "tagsMessageText"
            Accessible.role: Accessible.AlertMessage
            Accessible.name: text
        }
    }
    // HikariMessageBox with Yes and No: a list change's "Save changes to
    // tag ...?" and Restore default's "Are you sure ...?".
    Dialog {
        id: questionBox
        objectName: "tagsQuestion"
        property string kind
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        Label {
            id: questionText
            objectName: "tagsQuestionText"
            Accessible.role: Accessible.AlertMessage
            Accessible.name: text
        }
        onAccepted: {
            if (kind === "restore") {
                dialog.editor.restoreDefaults()
                return
            }
            // Save(ID_BUTTON_COMMIT), then the list's new tag (after an
            // error too, as legacy).
            dialog.show(dialog.editor.save(false))
            dialog.editor.select(dialog.pendingIndex)
        }
        onRejected: if (kind === "list") dialog.editor.select(dialog.pendingIndex)
    }
}
