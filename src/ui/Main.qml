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
    required property VideoController video
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
                    objectName: "openVideoMenuItem"
                    action: Action {
                        text: qsTr("Open &Video…")
                        onTriggered: videoDialog.open()
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
            MenuItem {
                objectName: "undoToLastSaveMenuItem"
                action: Action {
                    text: qsTr("Undo to last save")
                    enabled: root.editor.canUndoToLastSave
                    onTriggered: root.editor.undoToLastSave()
                }
            }
            MenuItem {
                objectName: "historyMenuItem"
                action: Action {
                    text: qsTr("&History")
                    shortcut: "Ctrl+Shift+H" // legacy GLOBAL_HISTORY default
                    enabled: root.editor.hasLine
                    onTriggered: historyWindow.show()
                }
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
        persistentSelection: true // the Original's selection survives for "Paste the selected"
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
                if (root.editor.selectionRole === field.role)
                    field.select(root.editor.selectionStart, root.editor.selectionEnd)
            }
        }
        Keys.onPressed: event => {
            const ctrl = event.modifiers & Qt.ControlModifier
            const enter = event.key === Qt.Key_Return || event.key === Qt.Key_Enter
            // Legacy EDITBOX defaults; composition keeps its own Enter.
            if (enter && field.inputMethodComposing) {
                return
            } else if (enter && (event.modifiers & Qt.ShiftModifier)) {
                root.editor.splitLine(field.role, field.selectionStart, field.selectionEnd)
                event.accepted = true
            } else if (enter && ctrl) {
                root.editor.commit()
                event.accepted = true
            } else if (enter) {
                root.editor.commitAndAdvance()
                event.accepted = true
            } else if ((event.modifiers & Qt.AltModifier) && event.key === Qt.Key_Down) {
                root.editor.toggleUnconfirmedAndAdvance()
                event.accepted = true
            } else if (ctrl && (event.key === Qt.Key_Comma || event.key === Qt.Key_Period)) {
                // Legacy defaults: Ctrl+, Start difference, Ctrl+. End difference.
                // Legacy writes into the edited field (the Translated one in
                // translation mode) whichever field has focus.
                const target = root.editor.translationMode ? translationText : lineText
                root.editor.insertTimeDifference(event.key === Qt.Key_Period, target.selectionStart, target.selectionEnd)
                event.accepted = true
            } else if (ctrl && event.key === Qt.Key_D) {
                root.editor.findNextUnconfirmed()
                event.accepted = true
            } else if (ctrl && event.key === Qt.Key_R) {
                root.editor.findNextUntranslated()
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
                // Frame stepping while the panel has focus (legacy video arrows).
                Keys.onLeftPressed: root.video.stepFrames(-1)
                Keys.onRightPressed: root.video.stepFrames(1)

                // The legacy "Associated files" confirmation, inline: the
                // Document stays editable whatever is chosen.
                Frame {
                    id: associationOffer
                    objectName: "associationOffer"
                    visible: root.video.offering
                    anchors { left: parent.left; right: parent.right; top: parent.top }
                    z: 1
                    RowLayout {
                        anchors.fill: parent
                        Label {
                            objectName: "associationText"
                            text: root.video.offer
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                        }
                        Button {
                            objectName: "loadAssociated"
                            text: qsTr("Load associated")
                            onClicked: root.video.loadAssociated()
                        }
                        Button {
                            objectName: "dismissAssociation"
                            text: qsTr("No")
                            onClicked: root.video.dismissOffer()
                        }
                    }
                }
                VideoPresenter {
                    id: presenter
                    objectName: "videoPresenter"
                    visible: root.video.hasVideo
                    anchors { left: parent.left; right: parent.right; top: parent.top; bottom: videoControls.top }
                    Component.onCompleted: root.video.attachPresenter(presenter)
                }
                Label {
                    anchors.centerIn: presenter
                    visible: !root.video.hasVideo
                    text: root.video.status
                }
                RowLayout {
                    id: videoControls
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                    Button {
                        objectName: "previousFrame"
                        text: qsTr("Previous frame")
                        enabled: root.video.hasVideo && root.video.frame > 0
                        onClicked: root.video.stepFrames(-1)
                    }
                    Label {
                        objectName: "videoStatus"
                        text: root.video.status
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                    }
                    Button {
                        objectName: "nextFrame"
                        text: qsTr("Next frame")
                        enabled: root.video.hasVideo && root.video.frame + 1 < root.video.frameCount
                        onClicked: root.video.stepFrames(1)
                    }
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
                        // Legacy translation-mode buttons (EDITBOX_PASTE_*,
                        // EDITBOX_HIDE_ORIGINAL renamed Comment out original).
                        RowLayout {
                            visible: root.editor.translationMode
                            Button {
                                objectName: "pasteAllToTranslation"
                                text: qsTr("Paste all")
                                focusPolicy: Qt.NoFocus
                                enabled: root.editor.editable
                                onClicked: root.editor.pasteAllToTranslation()
                            }
                            Button {
                                objectName: "pasteSelectionToTranslation"
                                text: qsTr("Paste the selected")
                                focusPolicy: Qt.NoFocus
                                enabled: root.editor.editable
                                onClicked: root.editor.pasteSelectionToTranslation(lineText.selectionStart,
                                                                                    lineText.selectionEnd,
                                                                                    translationText.cursorPosition)
                            }
                            Button {
                                objectName: "commentOutOriginal"
                                text: qsTr("Comment out original")
                                focusPolicy: Qt.NoFocus
                                enabled: root.editor.editable
                                onClicked: root.editor.commentOutOriginal()
                            }
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
                // Every gesture is a request; the application owns the selection (G1).
                onActiveLineRequested: id => root.app.selectLine(id)
                onExtendRequested: rows => root.app.extendSelection(rows)
                onLineClicked: (id, modifiers) => root.app.clickLine(id, modifiers)
                onLineDragged: id => root.app.dragSelection(id)
                onSelectAllRequested: root.app.selectAllLines()
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
            objectName: "selectionStatus"
            padding: 4
            text: shell.selectionStatus
        }
        Label {
            objectName: "saveStatus"
            padding: 4
            text: (root.editor.dirty ? qsTr("Modified") : "") + (root.editor.saveStatus.length
                  ? (root.editor.dirty ? "  |  " : "") + root.editor.saveStatus : "")
        }
    }

    // Legacy HistoryDialog: every step, the current one selected; Set and a
    // double-click jump there and stay open, OK jumps and closes.
    Window {
        id: historyWindow
        objectName: "historyWindow"
        title: root.editor.history.length === 1 ? qsTr("History (1 element)")
                                                : qsTr("History (%1 elements)").arg(root.editor.history.length)
        width: 360
        height: 420
        modality: Qt.ApplicationModal
        flags: Qt.Dialog
        onVisibleChanged: if (visible) historyList.currentIndex = root.editor.historyCursor
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 6
            ListView {
                id: historyList
                objectName: "historyList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                focus: true
                model: root.editor.history
                keyNavigationEnabled: true
                Accessible.role: Accessible.List
                Accessible.name: historyWindow.title
                delegate: ItemDelegate {
                    required property int index
                    required property string modelData
                    width: ListView.view.width
                    text: modelData
                    highlighted: ListView.isCurrentItem
                    font.bold: index === root.editor.historyCursor
                    onClicked: historyList.currentIndex = index
                    onDoubleClicked: root.editor.goToHistory(index)
                }
            }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                Button {
                    objectName: "historySet"
                    text: qsTr("Set")
                    onClicked: root.editor.goToHistory(historyList.currentIndex)
                }
                Button {
                    objectName: "historyOk"
                    text: qsTr("OK")
                    onClicked: { root.editor.goToHistory(historyList.currentIndex); historyWindow.close() }
                }
                Button {
                    objectName: "historyCancel"
                    text: qsTr("Cancel")
                    onClicked: historyWindow.close()
                }
            }
        }
    }

    FileDialog {
        id: videoDialog
        nameFilters: [qsTr("Video (*.mkv *.mp4 *.avi *.mov *.webm *.ts *.m2ts *.wmv)"), qsTr("All files (*)")]
        onAccepted: root.video.openVideoUrl(selectedFile)
    }

    FileDialog {
        id: openDialog
        nameFilters: [qsTr("Subtitles (*.ass *.ssa *.srt *.sub *.txt *.mpl)"), qsTr("All files (*)")]
        onAccepted: root.app.openFile(selectedFile.toString().replace(/^file:\/\//, ""))
    }
}
