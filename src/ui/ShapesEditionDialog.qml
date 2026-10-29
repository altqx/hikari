pragma Translator: "HikariSub.VisualTools"
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: dialog
    objectName: "shapesEditionDialog"
    property ShapeEditor editor
    title: qsTr("Edit shapes")
    dialogId: "shapesEdition"
    resizable: true
    initialFocus: shapeList

    // MUST KILL: stepText's list must follow the order of ShapesEdition::Step
    function stepText(step) {
        return [qsTr("Rename shape"), qsTr("Edit drawing"), qsTr("Change scaling"), qsTr("New shape"),
                qsTr("Duplicate shape"), qsTr("Delete shape"), qsTr("Take drawing from line"),
                qsTr("Replace shape"), qsTr("Restore default shapes"), qsTr("Undo all changes")][step] ?? ""
    }
    function showFields() {
        if (!editor)
            return
        shapeName.text = editor.name
        shapeName.error = ""
        shapeText.text = editor.shape
        shapeText.error = ""
    }
    function nameTyped() {
        const code = editor.setName(shapeName.text)
        shapeName.error = code === ShapeEditor.NameEmpty ? qsTr("Type a name.")
                        : code === ShapeEditor.NameSeparator ? qsTr("A name can't contain “;”.")
                        : code === ShapeEditor.NameTaken
                          ? qsTr("Another shape is named “%1”. Press Enter to replace it.").arg(editor.takenName())
                        : ""
    }
    property bool asking: false
    function nameLeft() {
        if (!editor || asking || shapeName.error.length === 0) {
            if (editor)
                editor.endTyping()
            return
        }
        if (!visible) {
            showFields()
            return
        }
        if (editor.setName(shapeName.text) !== ShapeEditor.NameTaken) {
            showFields()
            return
        }
        const name = shapeName.text
        asking = true
        messages.ask({
            title: dialog.title,
            text: qsTr("Replace the shape “%1”?").arg(editor.takenName()),
            detail: qsTr("“%1” takes its name and its place in the list; the other is removed. Undo brings it back while this window is open.").arg(editor.name),
            severity: "warning",
            choices: [{ id: "replace", text: qsTr("Replace"), role: "destructive" },
                      { id: "keep", text: qsTr("Keep editing"), role: "reject", isDefault: true }]
        }, id => {
            asking = false
            if (id === "replace") {
                editor.replaceNamed(name)
                showFields()
            } else {
                shapeName.forceActiveFocus()
                shapeName.selectAll()
            }
        })
    }
    function askRestore() {
        const own = editor.ownCount
        messages.ask({
            title: dialog.title,
            text: qsTr("Restore the default shapes?"),
            detail: (own > 0 ? qsTr("Your %n shape(s) are removed and the five default shapes come back.", "", own)
                             : qsTr("The five default shapes come back as they were."))
                    + " " + qsTr("Undo brings yours back while this window is open."),
            severity: "warning",
            choices: [{ id: "restore", text: qsTr("Restore defaults"), role: "destructive" },
                      { id: "cancel", text: qsTr("Cancel"), role: "reject", isDefault: true }]
        }, id => {
            if (id === "restore") {
                editor.restoreDefaults()
                showFields()
            }
        })
    }
    function undo() { if (editor) { editor.undo(); showFields() } }
    function redo() { if (editor) { editor.redo(); showFields() } }

    onEditorChanged: showFields()
    // MUST KILL: a close while transientParent is hidden keeps the editor, so the window can be made again
    onClosing: if (editor && !(transientParent && !transientParent.visible)) editor.close()
    Connections {
        target: dialog.editor
        function onClosed() { dialog.close() }
    }
    Shortcut {
        sequences: [StandardKey.Undo]
        onActivated: dialog.undo()
    }
    Shortcut {
        sequences: [StandardKey.Redo]
        onActivated: dialog.redo()
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredHeight: 420
        spacing: 0

        Rectangle {
            Layout.preferredWidth: 220
            Layout.fillHeight: true
            color: Theme.field
            border.color: Theme.line
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 1
                spacing: 0
                ListView {
                    id: shapeList
                    objectName: "shapeList"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: dialog.editor ? dialog.editor.items : []
                    // Bound only: a JS-array model resets ListView's own currentIndex on each change.
                    currentIndex: dialog.editor && count > 0 ? dialog.editor.selection : -1
                    keyNavigationEnabled: false
                    activeFocusOnTab: true
                    Accessible.role: Accessible.List
                    Accessible.name: qsTr("Shapes")
                    function selectRow(index) {
                        if (!dialog.editor || index < 0 || index >= count || index === dialog.editor.selection)
                            return
                        dialog.editor.select(index)
                        dialog.showFields()
                    }
                    Keys.onUpPressed: if (dialog.editor) selectRow(dialog.editor.selection - 1)
                    Keys.onDownPressed: if (dialog.editor) selectRow(dialog.editor.selection + 1)
                    ScrollBar.vertical: ScrollBar {}
                    delegate: ItemDelegate {
                        id: row
                        required property var modelData
                        required property int index
                        objectName: "shapeRow" + index
                        width: ListView.view.width
                        height: 30
                        focusPolicy: Qt.NoFocus
                        highlighted: ListView.isCurrentItem
                        Accessible.role: Accessible.ListItem
                        Accessible.name: modelData.name
                        Accessible.selected: highlighted
                        onClicked: shapeList.selectRow(index)
                        background: Rectangle {
                            color: row.highlighted ? Theme.select : row.hovered ? Theme.raised : "transparent"
                            Rectangle {
                                visible: row.highlighted
                                width: 3
                                height: parent.height
                                color: Theme.accent
                            }
                        }
                        contentItem: RowLayout {
                            spacing: 10
                            ShapeDrawing {
                                Layout.preferredWidth: 20
                                Layout.preferredHeight: 20
                                path: row.modelData.path
                                view: Qt.rect(row.modelData.box.x - row.modelData.box.width * 0.08,
                                              row.modelData.box.y - row.modelData.box.height * 0.08,
                                              row.modelData.box.width * 1.16, row.modelData.box.height * 1.16)
                            }
                            Label {
                                text: row.modelData.name
                                font.weight: row.highlighted ? Font.DemiBold : Font.Normal
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Label {
                                visible: row.modelData.own
                                text: qsTr("yours")
                                color: Theme.muted
                                font.pixelSize: Math.round(Qt.application.font.pixelSize * 0.85)
                            }
                        }
                    }
                }
                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.line }
                RowLayout {
                    Layout.margins: 4
                    spacing: 2
                    IconToolButton {
                        objectName: "addShape"
                        iconRole: "add"
                        text: qsTr("New shape")
                        onClicked: { dialog.editor.addShape(); dialog.showFields(); shapeName.forceActiveFocus(); shapeName.selectAll() }
                    }
                    IconToolButton {
                        objectName: "duplicateShape"
                        iconRole: "duplicate"
                        text: qsTr("Duplicate")
                        onClicked: { dialog.editor.duplicate(); dialog.showFields() }
                    }
                    IconToolButton {
                        objectName: "deleteShape"
                        iconRole: "delete"
                        text: qsTr("Delete")
                        enabled: dialog.editor ? dialog.editor.canRemove : false
                        tip: enabled ? text : qsTr("The list keeps at least one shape")
                        onClicked: { dialog.editor.remove(); dialog.showFields() }
                    }
                    Item { Layout.fillWidth: true }
                    IconToolButton {
                        id: moreButton
                        objectName: "shapesMore"
                        iconRole: "panel-menu"
                        text: qsTr("More")
                        down: moreMenu.visible
                        onClicked: moreMenu.popup(moreButton, 0, moreButton.height)
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.preferredWidth: 360
            spacing: 8
            Label { text: qsTr("Name"); font.weight: Font.DemiBold }
            TextField {
                id: shapeName
                objectName: "shapeName"
                property string error
                Layout.preferredWidth: 220
                Accessible.name: qsTr("Name")
                Accessible.description: error
                // TextField truncates text past maximumLength, so a longer stored name raises it.
                maximumLength: Math.max(dialog.editor ? dialog.editor.nameMaxLength : 20, text.length)
                onTextEdited: dialog.nameTyped()
                Keys.onReturnPressed: event => { event.accepted = true; dialog.nameLeft() }
                Keys.onEnterPressed: event => { event.accepted = true; dialog.nameLeft() }
                onActiveFocusChanged: if (!activeFocus) dialog.nameLeft()
            }
            FieldError { objectName: "shapeNameError"; text: shapeName.error; Layout.fillWidth: true }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.line; Layout.topMargin: 6 }
            Label { text: qsTr("Scale from the cursor"); font.weight: Font.DemiBold; Layout.topMargin: 6 }
            SegmentedButtons {
                objectName: "shapeMode"
                Layout.fillWidth: true
                Layout.minimumWidth: implicitWidth
                // MUST KILL: the choices' order must follow ShapePreset::Mode
                choices: [{ text: qsTr("Width and height"), name: "shapeMode0" },
                          { text: qsTr("Keep aspect ratio"), name: "shapeMode1" },
                          { text: qsTr("Width only"), name: "shapeMode2" }]
                currentIndex: dialog.editor ? dialog.editor.mode : 0
                Accessible.name: qsTr("Scale from the cursor")
                onActivated: index => dialog.editor.mode = index
            }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.line; Layout.topMargin: 6 }
            Label { text: qsTr("Drawing"); font.weight: Font.DemiBold; Layout.topMargin: 6 }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 90
                TextArea {
                    id: shapeText
                    objectName: "shapeText"
                    property string error
                    wrapMode: TextEdit.Wrap
                    selectByMouse: true
                    font.family: "monospace"
                    Accessible.name: qsTr("Drawing")
                    Accessible.description: error
                    background: Rectangle {
                        color: Theme.field
                        border.width: shapeText.error.length > 0 || shapeText.activeFocus ? 2 : 1
                        border.color: shapeText.error.length > 0 ? Theme.danger
                                    : shapeText.activeFocus ? Theme.focus : Theme.line
                    }
                    onTextChanged: {
                        if (!dialog.editor || text === dialog.editor.shape)
                            return
                        error = dialog.editor.setShape(text) ? ""
                              : text.indexOf(";") >= 0 ? qsTr("A drawing can't contain “;”.")
                              : qsTr("A shape needs a drawing.")
                    }
                    onActiveFocusChanged: {
                        if (activeFocus || !dialog.editor)
                            return
                        dialog.editor.endTyping()
                        if (error.length > 0)
                            dialog.showFields()
                    }
                }
            }
            FieldError { objectName: "shapeTextError"; text: shapeText.error; Layout.fillWidth: true }
            IconTextButton {
                objectName: "shapeFromLine"
                iconRole: "tool-drawing"
                text: qsTr("Take from the active line")
                tip: qsTr("Put the active line's drawing in this shape")
                onClicked: {
                    if (dialog.editor.takeFromLine())
                        dialog.showFields()
                    else
                        shapeText.error = qsTr("The active line has no drawing.")
                }
            }
        }

        ColumnLayout {
            Layout.preferredWidth: 280
            Layout.fillHeight: true
            spacing: 8
            Label { text: qsTr("Preview"); font.weight: Font.DemiBold }
            Rectangle {
                id: previewFrame
                objectName: "shapePreview"
                Layout.fillWidth: true
                Layout.preferredHeight: width
                color: Theme.field
                border.color: Theme.line
                clip: true
                readonly property rect box: dialog.editor ? dialog.editor.box : Qt.rect(0, 0, 0, 0)
                readonly property rect view: {
                    const span = Math.max(box.width, box.height, 1) * 1.5
                    const cx = box.x + box.width / 2, cy = box.y + box.height / 2
                    const x0 = Math.min(cx - span / 2, -span * 0.12), x1 = Math.max(cx + span / 2, span * 0.12)
                    const y0 = Math.min(cy - span / 2, -span * 0.12), y1 = Math.max(cy + span / 2, span * 0.12)
                    const s = Math.max(x1 - x0, y1 - y0)
                    return Qt.rect(x0, y0, s, s)
                }
                Repeater {
                    model: {
                        const v = previewFrame.view
                        const lines = []
                        if (v.width <= 0 || v.width / 50 > 80)
                            return lines
                        for (let g = Math.ceil(v.x / 50) * 50; g <= v.x + v.width; g += 50)
                            lines.push({ vertical: true, at: g })
                        for (let g = Math.ceil(v.y / 50) * 50; g <= v.y + v.height; g += 50)
                            lines.push({ vertical: false, at: g })
                        return lines
                    }
                    Rectangle {
                        required property var modelData
                        color: Theme.line
                        opacity: 0.6
                        x: modelData.vertical ? preview.mapX(modelData.at) : 0
                        y: modelData.vertical ? 0 : preview.mapY(modelData.at)
                        width: modelData.vertical ? 1 : previewFrame.width
                        height: modelData.vertical ? previewFrame.height : 1
                    }
                }
                ShapeDrawing {
                    id: preview
                    anchors.fill: parent
                    visible: shapeText.error.length === 0
                    path: dialog.editor ? dialog.editor.path : ""
                    view: previewFrame.view
                    stroke: Theme.accent
                    strokeWidth: 2
                }
                Item {
                    x: preview.mapX(0)
                    y: preview.mapY(0)
                    Rectangle { x: -8; y: -0.5; width: 16; height: 1; color: Theme.danger }
                    Rectangle { x: -0.5; y: -8; width: 1; height: 16; color: Theme.danger }
                    Rectangle { x: -3; y: -3; width: 6; height: 6; radius: 3; color: "transparent"; border.color: Theme.danger }
                }
            }
            Label {
                objectName: "shapePreviewSize"
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                color: Theme.muted
                text: shapeText.error.length > 0 ? qsTr("The shape keeps its last drawing until this one can be used")
                    : previewFrame.box.width > 0 || previewFrame.box.height > 0
                      ? qsTr("%1 × %2").arg(Math.round(previewFrame.box.width)).arg(Math.round(previewFrame.box.height))
                      : ""
            }
            Item { Layout.fillHeight: true }
        }
    }

    leadingActions: [
        IconTextButton {
            objectName: "shapesUndo"
            iconRole: "undo"
            text: qsTr("Undo")
            enabled: dialog.editor ? dialog.editor.canUndo : false
            tip: enabled ? qsTr("Undo “%1”").arg(dialog.stepText(dialog.editor.undoStep)) : ""
            onClicked: dialog.undo()
        },
        IconTextButton {
            objectName: "shapesRedo"
            iconRole: "redo"
            text: qsTr("Redo")
            enabled: dialog.editor ? dialog.editor.canRedo : false
            tip: enabled ? qsTr("Redo “%1”").arg(dialog.stepText(dialog.editor.redoStep)) : ""
            onClicked: dialog.redo()
        }
    ]
    buttons: PrimaryButton {
        id: closeButton
        objectName: "shapesClose"
        text: qsTr("Close")
        DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        onClicked: dialog.close()
    }
    defaultButton: closeButton

    ShellMenu {
        id: moreMenu
        objectName: "shapesMoreMenu"
        ShellMenuItem {
            objectName: "shapesRestoreDefault"
            iconRole: "restore-default"
            text: qsTr("Restore default shapes…")
            onTriggered: dialog.askRestore()
        }
        MenuSeparator {}
        ShellMenuItem {
            objectName: "shapesUndoAll"
            iconRole: "undo"
            text: qsTr("Undo all changes")
            onTriggered: { dialog.editor.undoAll(); dialog.showFields() }
        }
    }

    MessageHost {
        id: messages
        objectName: "shapesMessages"
        settings: dialog.settings
    }
}
