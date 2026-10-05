import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

// T5: legacy ShapesEdition, "Vector shape editing" (VisualDrawingShapes.cpp:
// 43-118): the drawing tool's shape presets, opened from the shape list's
// "Edit". The list with a new name, Add shape and Delete shape; the edited
// preset's name, "Scaling relative to cursor", the scaling mode (disabled,
// as legacy), "Get shape from active line" and the shape's drawing; Apply
// keeps an edit here, OK keeps it and saves the presets, Cancel drops what
// the dialog changed, Restore default puts legacy's five presets back.
// Legacy's message boxes are modal boxes over the dialog. Saving under a
// name another preset has asks to replace it or rename (accepted on #55).
Dialog {
    id: dialog
    objectName: "shapesEditionDialog"
    property ShapeEditor editor
    title: qsTr("Vector shape editing")
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    closePolicy: Popup.CloseOnEscape
    padding: 8

    // A list change or OK waiting on a question.
    property int pendingIndex: -1
    property bool pendingOk: false
    property bool fromList: false

    function show(message) {
        if (!message || !message.text)
            return false
        messageBox.title = message.title
        messageText.text = message.text
        messageBox.open()
        return true
    }
    function saved(result, ok) {
        // {} kept, {text, title} an error, {clash} a name another preset has.
        if (result.clash !== undefined) {
            pendingOk = ok
            clashText.text = qsTr("A shape named \"%1\" already exists.").arg(result.clash)
            clashBox.open()
            return false
        }
        return !show(result)
    }
    function listChanged(index) {
        // OnListChanged: the edited preset's changes are asked about first.
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

    ColumnLayout {
        anchors.fill: parent
        spacing: 6
        GroupBox {
            title: qsTr("Edited shape")
            Layout.fillWidth: true
            RowLayout {
                anchors.fill: parent
                ComboBox {
                    id: shapeList
                    objectName: "shapeList"
                    Layout.fillWidth: true
                    Layout.preferredWidth: 160
                    model: dialog.editor ? dialog.editor.list : []
                    currentIndex: dialog.editor ? dialog.editor.selection : -1
                    Accessible.name: qsTr("Shapes")
                    onActivated: index => {
                        currentIndex = Qt.binding(() => dialog.editor ? dialog.editor.selection : -1)
                        dialog.listChanged(index)
                    }
                }
                TextField {
                    id: newShapeName
                    objectName: "newShapeName"
                    Layout.fillWidth: true
                    Layout.preferredWidth: 160
                    placeholderText: qsTr("New shape name")
                    Accessible.name: qsTr("New shape name")
                    onAccepted: okButton.clicked()
                }
                Button {
                    objectName: "addShape"
                    text: qsTr("Add shape")
                    onClicked: dialog.show(dialog.editor.addShape(newShapeName.text))
                }
                Button {
                    objectName: "deleteShape"
                    text: qsTr("Delete shape")
                    onClicked: dialog.show(dialog.editor.removeShape())
                }
            }
        }
        GroupBox {
            title: qsTr("Editing")
            Layout.fillWidth: true
            ColumnLayout {
                anchors.fill: parent
                GridLayout {
                    columns: 2
                    Layout.fillWidth: true
                    Label { text: qsTr("Name:") }
                    TextField {
                        id: shapeName
                        objectName: "shapeName"
                        Layout.fillWidth: true
                        text: dialog.editor ? dialog.editor.name : ""
                        Accessible.name: qsTr("Name")
                        // shapeName->SetMaxLength(20): typing stops there; a longer
                        // name read from the file is shown whole.
                        validator: RegularExpressionValidator { regularExpression: /^.{0,20}$/ }
                        onTextEdited: dialog.editor.name = text
                        onAccepted: okButton.clicked()
                    }
                    Label { text: qsTr("Scaling relative to cursor:") }
                    ComboBox {
                        objectName: "shapeMode"
                        Layout.fillWidth: true
                        model: [qsTr("Changed width and height"), qsTr("Preserve aspect ratio"), qsTr("Changed is only width")]
                        currentIndex: dialog.editor ? dialog.editor.mode : 0
                        implicitContentWidthPolicy: ComboBox.WidestText
                        Accessible.name: qsTr("Scaling relative to cursor")
                        onActivated: index => {
                            currentIndex = Qt.binding(() => dialog.editor ? dialog.editor.mode : 0)
                            dialog.editor.mode = index
                        }
                    }
                    Label { text: qsTr("Scaling mode:") }
                    ComboBox {
                        objectName: "shapeScalingMode"
                        Layout.fillWidth: true
                        enabled: false // legacy: scalingMode->Enable(false)
                        model: [qsTr("Changing only drawing coordinates"), qsTr("Changing scale")]
                        currentIndex: dialog.editor ? dialog.editor.scalingMode : 0
                        implicitContentWidthPolicy: ComboBox.WidestText
                        Accessible.name: qsTr("Scaling mode")
                    }
                    Label { text: qsTr("Shape:") }
                    Button {
                        objectName: "shapeFromLine"
                        Layout.fillWidth: true
                        text: qsTr("Get shape from active line")
                        onClicked: dialog.editor.getShapeFromLine()
                    }
                }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 300
                    Layout.preferredWidth: 520
                    TextArea {
                        id: shapeText
                        objectName: "shapeText"
                        text: dialog.editor ? dialog.editor.shape : ""
                        wrapMode: TextEdit.Wrap
                        selectByMouse: true
                        Accessible.name: qsTr("Shape")
                        onTextChanged: if (dialog.editor && text !== dialog.editor.shape) dialog.editor.shape = text
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
                objectName: "shapesApply"
                Layout.fillWidth: true
                text: qsTr("Apply")
                onClicked: { dialog.fromList = false; dialog.saved(dialog.editor.save(false), false) }
            }
            Button {
                id: okButton
                objectName: "shapesOk"
                Layout.fillWidth: true
                text: qsTr("OK")
                onClicked: { dialog.fromList = false; dialog.saved(dialog.editor.save(true), true) }
            }
            Button {
                objectName: "shapesCancel"
                Layout.fillWidth: true
                text: qsTr("Cancel")
                onClicked: dialog.reject()
            }
            Button {
                objectName: "shapesRestoreDefault"
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
        objectName: "shapesMessage"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok
        Label {
            id: messageText
            objectName: "shapesMessageText"
            Accessible.role: Accessible.AlertMessage
            Accessible.name: text
        }
    }
    // HikariMessageBox with Yes and No: a list change's "Save changes to
    // shape ...?" and Restore default's "Are you sure ...?".
    Dialog {
        id: questionBox
        objectName: "shapesQuestion"
        property string kind
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        Label {
            id: questionText
            objectName: "shapesQuestionText"
            Accessible.role: Accessible.AlertMessage
            Accessible.name: text
        }
        onAccepted: {
            if (kind === "restore") {
                dialog.editor.restoreDefaults()
                return
            }
            // Save(ID_BUTTON_COMMIT), then the list's new preset (after an
            // error too, as legacy); a name clash waits for its answer.
            dialog.fromList = true
            const result = dialog.editor.save(false)
            if (result.clash !== undefined) {
                dialog.saved(result, false)
                return
            }
            dialog.show(result)
            dialog.editor.select(dialog.pendingIndex)
        }
        onRejected: if (kind === "list") dialog.editor.select(dialog.pendingIndex)
    }
    // Accepted on #55: a name another preset has.
    Dialog {
        id: clashBox
        objectName: "shapesClash"
        title: qsTr("Confirmation")
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        Label {
            id: clashText
            objectName: "shapesClashText"
            Accessible.role: Accessible.AlertMessage
            Accessible.name: text
        }
        footer: DialogButtonBox {
            Button {
                objectName: "shapesClashReplace"
                text: qsTr("Replace")
                onClicked: {
                    clashBox.close()
                    const index = dialog.editor.replaceClash(dialog.fromList ? dialog.pendingIndex : -1, dialog.pendingOk)
                    if (dialog.fromList)
                        dialog.editor.select(index)
                }
            }
            Button {
                objectName: "shapesClashRename"
                text: qsTr("Rename")
                onClicked: {
                    clashBox.close()
                    shapeName.forceActiveFocus()
                    shapeName.selectAll()
                }
            }
        }
    }
}
