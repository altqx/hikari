import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Hikari.Ui

// Classic shell (V1-S): menus across the top, video left, audio above the
// Line editor on the right, the Grid across the bottom and, when a protected
// comparison reference is open, its tray below. Panels are focus scopes, so
// F6/Shift+F6 traversal returns to whatever had focus inside a panel.
// Movable/floating panels and layout persistence arrive with the docking cards.
ApplicationWindow {
    id: root
    objectName: "mainWindow"
    width: 1280
    height: 800
    visible: true
    title: shell.hasEditingTarget ? qsTr("%1 - HikariSub").arg(shell.editingTitle) : "HikariSub"

    required property ShellController shell
    required property LineEditorController editor
    required property var app

    // Major panels in F6 order; a hidden panel is skipped.
    readonly property list<Item> panels: [videoPanel, audioPanel, editorPanel, gridPanel, referencePanel]

    function panelOf(item) {
        for (let p = item; p; p = p.parent)
            for (const panel of panels)
                if (p === panel)
                    return panel
        return null
    }

    function cyclePanels(step) {
        const shown = panels.filter(p => p.visible)
        const current = shown.indexOf(panelOf(root.activeFocusItem))
        const next = current < 0 ? (step > 0 ? 0 : shown.length - 1)
                                 : (current + step + shown.length) % shown.length
        shown[next].forceActiveFocus(Qt.TabFocusReason)
    }

    Shortcut {
        sequences: ["F6"]
        context: Qt.WindowShortcut
        onActivated: root.cyclePanels(1)
    }
    Shortcut {
        sequences: ["Shift+F6"]
        context: Qt.WindowShortcut
        onActivated: root.cyclePanels(-1)
    }

    // Classic menus. Commands join through the shared action system as their
    // cards land.
    menuBar: MenuBar {
        MenuBarItem {
            objectName: "fileMenuBarItem"
            menu: Menu {
                title: qsTr("&File")
                MenuItem {
                    action: Action {
                        text: qsTr("&Open…")
                        shortcut: StandardKey.Open
                        onTriggered: openDialog.open()
                    }
                }
                MenuItem {
                    objectName: "saveMenuItem"
                    action: Action {
                        text: qsTr("&Save")
                        shortcut: StandardKey.Save
                        enabled: root.editor.editable
                        onTriggered: root.editor.save()
                    }
                }
            }
        }
        Menu {
            title: qsTr("&Edit")
            Action {
                text: qsTr("&Undo")
                enabled: root.editor.hasLine
                onTriggered: root.editor.undo()
            }
            Action {
                text: qsTr("&Redo")
                enabled: root.editor.hasLine
                onTriggered: root.editor.redo()
            }
        }
        Menu { title: qsTr("&View") }
        Menu { title: qsTr("&Help") }
    }

    // One text role of the Line editor (Original or Translated). The field
    // mirrors the editor's projection; user changes go to the controller,
    // which maps them onto the raw source. Programmatic updates are not edits.
    component RoleField: TextArea {
        id: field
        required property int role
        readonly property string shown: role === 0 ? root.editor.text : root.editor.translationText
        readOnly: !root.editor.editable
        wrapMode: TextEdit.Wrap
        selectByMouse: true
        Layout.fillWidth: true
        Layout.fillHeight: true

        property bool syncing: false
        function sync() {
            if (text === shown)
                return
            syncing = true
            const caret = cursorPosition
            text = shown
            cursorPosition = Math.min(caret, length)
            syncing = false
        }
        function report() {
            if (syncing)
                return
            if (role === 0)
                root.editor.textEdited(text, cursorPosition)
            else
                root.editor.translationEdited(text, cursorPosition)
        }
        Component.onCompleted: sync()
        onTextChanged: if (!syncing) Qt.callLater(field.report)
        Connections {
            target: root.editor
            function onChanged() { field.sync() }
            function onSelectionRequested() {
                if (field.activeFocus)
                    field.select(root.editor.selectionStart, root.editor.selectionEnd)
            }
        }
        Keys.onPressed: event => {
            const ctrl = event.modifiers & Qt.ControlModifier
            if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                    && !(event.modifiers & Qt.ShiftModifier) && !field.inputMethodComposing) {
                root.editor.commitAndAdvance()
                event.accepted = true
            } else if (ctrl && (event.key === Qt.Key_B || event.key === Qt.Key_I)) {
                // Legacy defaults: Ctrl+B Bold, Ctrl+I Italic.
                root.editor.toggleTagIn(field.role, event.key === Qt.Key_B ? "b" : "i",
                                        field.selectionStart, field.selectionEnd)
                event.accepted = true
            } else if (event.key === Qt.Key_Escape) {
                root.editor.discard()
                event.accepted = true
            } else if (ctrl && event.key === Qt.Key_Z && !(event.modifiers & Qt.ShiftModifier)) {
                root.editor.undo()
                event.accepted = true
            } else if (ctrl && (event.key === Qt.Key_Y
                                || (event.key === Qt.Key_Z && (event.modifiers & Qt.ShiftModifier)))) {
                root.editor.redo()
                event.accepted = true
            }
        }
    }

    component Panel: FocusScope {
        id: panel
        property string title
        default property alias content: body.data
        activeFocusOnTab: false
        Accessible.role: Accessible.Pane
        Accessible.name: title

        Rectangle {
            anchors.fill: parent
            color: panel.palette.base
            border.width: panel.activeFocus ? 2 : 1
            border.color: panel.activeFocus ? panel.palette.highlight : panel.palette.mid
        }
        Label {
            id: heading
            objectName: panel.objectName + "Title"
            text: panel.title
            font.bold: true
            x: 8
            y: 4
        }
        Item {
            id: body
            anchors {
                fill: parent
                topMargin: heading.height + 8
                margins: 4
            }
        }
    }

    SplitView {
        anchors.fill: parent
        orientation: Qt.Vertical

        SplitView {
            orientation: Qt.Horizontal
            SplitView.fillHeight: true
            SplitView.preferredHeight: 440

            Panel {
                id: videoPanel
                objectName: "videoPanel"
                title: qsTr("Video")
                SplitView.preferredWidth: 640
                Label {
                    anchors.centerIn: parent
                    text: qsTr("No video open")
                }
            }

            SplitView {
                orientation: Qt.Vertical
                SplitView.fillWidth: true

                Panel {
                    id: audioPanel
                    objectName: "audioPanel"
                    title: qsTr("Audio")
                    SplitView.preferredHeight: 160
                    Label {
                        anchors.centerIn: parent
                        text: qsTr("No audio open")
                    }
                }

                Panel {
                    id: editorPanel
                    objectName: "editorPanel"
                    title: shell.hasEditingTarget ? qsTr("Line editor: %1").arg(shell.editingTitle)
                                                  : qsTr("Line editor")
                    SplitView.fillHeight: true
                    ColumnLayout {
                        anchors.fill: parent

                        // Local inspector: timing and margins of the active Line.
                        RowLayout {
                            Layout.fillWidth: true
                            component Field: TextField {
                                property string value
                                text: value
                                enabled: root.editor.editable
                                selectByMouse: true
                                Layout.preferredWidth: 90
                                onValueChanged: text = value
                            }
                            Field {
                                objectName: "startField"
                                value: root.editor.startText
                                Accessible.name: qsTr("Start")
                                onEditingFinished: root.editor.setStartText(text)
                            }
                            Field {
                                objectName: "endField"
                                value: root.editor.endText
                                Accessible.name: qsTr("End")
                                onEditingFinished: root.editor.setEndText(text)
                            }
                            Field {
                                objectName: "marginLeftField"
                                value: root.editor.marginLeftText
                                Layout.preferredWidth: 50
                                Accessible.name: qsTr("Left margin")
                                onEditingFinished: root.editor.setMarginText(0, text)
                            }
                            Field {
                                objectName: "marginRightField"
                                value: root.editor.marginRightText
                                Layout.preferredWidth: 50
                                Accessible.name: qsTr("Right margin")
                                onEditingFinished: root.editor.setMarginText(1, text)
                            }
                            Field {
                                objectName: "marginVerticalField"
                                value: root.editor.marginVerticalText
                                Layout.preferredWidth: 50
                                Accessible.name: qsTr("Vertical margin")
                                onEditingFinished: root.editor.setMarginText(2, text)
                            }
                            // Ordinary ASS controls; they keep focus (and the
                            // selection) in the text field.
                            Repeater {
                                model: [
                                    { tag: "b", label: qsTr("B"), name: qsTr("Bold") },
                                    { tag: "i", label: qsTr("I"), name: qsTr("Italic") },
                                    { tag: "u", label: qsTr("U"), name: qsTr("Underline") },
                                    { tag: "s", label: qsTr("S"), name: qsTr("Strikeout") }
                                ]
                                ToolButton {
                                    required property var modelData
                                    objectName: "tag_" + modelData.tag
                                    text: modelData.label
                                    focusPolicy: Qt.NoFocus
                                    enabled: root.editor.editable
                                    Accessible.name: modelData.name
                                    onClicked: {
                                        const field = translationText.activeFocus ? translationText : lineText
                                        root.editor.toggleTagIn(field.role, modelData.tag, field.selectionStart, field.selectionEnd)
                                    }
                                }
                            }
                            CheckBox {
                                objectName: "showTags"
                                text: qsTr("Show tags")
                                checked: root.editor.showTags
                                onToggled: root.editor.showTags = checked
                            }
                        }

                        RoleField {
                            id: lineText
                            objectName: "lineText"
                            role: 0
                            focus: true
                            Accessible.name: root.editor.translationMode ? qsTr("Original text") : qsTr("Line text")
                        }
                        RoleField {
                            id: translationText
                            objectName: "translationText"
                            role: 1
                            visible: root.editor.translationMode
                            Accessible.name: qsTr("Translated text")
                        }

                        Label {
                            objectName: "editorProblem"
                            visible: text.length > 0
                            text: root.editor.problem
                            color: "firebrick"
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                            Accessible.role: Accessible.AlertMessage
                        }
                        Label {
                            objectName: "editorAttempted"
                            visible: root.editor.attempted.length > 0
                            text: qsTr("Not applied: %1").arg(root.editor.attempted)
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                }
            }
        }

        Panel {
            id: gridPanel
            objectName: "gridPanel"
            title: shell.hasEditingTarget ? qsTr("Editing: %1").arg(shell.editingTitle) : qsTr("No document open")
            SplitView.preferredHeight: 260
            focus: true
            HikariGrid {
                id: grid
                objectName: "editingGrid"
                anchors.fill: parent
                focus: true
                model: shell.lines
                onActiveLineRequested: id => {
                    if (root.editor.showLine(id))
                        root.shell.selectLine(id)
                }
                Connections {
                    target: root.editor
                    function onLineChanged(id) { root.shell.selectLine(id) }
                }
            }
        }

        Panel {
            id: referencePanel
            objectName: "referencePanel"
            visible: shell.hasReference
            title: qsTr("Reference (protected, read-only): %1").arg(shell.referenceTitle)
            SplitView.preferredHeight: 160
            HikariGrid {
                objectName: "referenceGrid"
                anchors.fill: parent
                focus: true
                model: shell.referenceLines
            }
        }
    }

    footer: RowLayout {
        Label {
            objectName: "statusTargets"
            padding: 4
            text: (shell.hasEditingTarget ? qsTr("Editing: %1").arg(shell.editingTitle) : qsTr("No editing target"))
                  + (shell.hasReference ? qsTr("  |  Reference (protected): %1").arg(shell.referenceTitle) : "")
            Layout.fillWidth: true
        }
        Label {
            objectName: "saveStatus"
            padding: 4
            text: (root.editor.dirty ? qsTr("Modified") : "") + (root.editor.saveStatus.length
                  ? (root.editor.dirty ? "  |  " : "") + root.editor.saveStatus : "")
        }
    }

    FileDialog {
        id: openDialog
        nameFilters: [qsTr("Subtitles (*.ass *.ssa *.srt *.sub *.txt *.mpl)"), qsTr("All files (*)")]
        onAccepted: root.app.openFile(selectedFile.toString().replace(/^file:\/\//, ""))
    }
}
