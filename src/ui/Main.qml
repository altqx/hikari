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
    required property var automation
    required property AutomationManagerController automationManager
    required property AutomationDialogController automationDialogs
    required property AutomationFilePickerController automationPicker
    required property LogController log
    required property TagButtonsController tagButtons
    required property GridFilterController gridFilter

    // Every registered macro, in load and registration order (the dynamic
    // part of the legacy Automation menu).
    readonly property var macroItems: {
        const out = []
        for (const script of root.automationManager.scripts)
            for (const macro of script.macros)
                out.push({ path: script.path, ordinal: macro.ordinal, name: macro.name })
        return out
    }

    readonly property var sortKeys: [
        { key: "start", label: qsTr("The starting time") },
        { key: "end", label: qsTr("End time") },
        { key: "style", label: qsTr("Styles") },
        { key: "actor", label: qsTr("Actor") },
        { key: "effect", label: qsTr("Effect") },
        { key: "layer", label: qsTr("Layer") }
    ]

    // E2: a custom tag button. Types 0 and 1 work in the focused field like
    // Bold; plain text (type 2) goes, as in legacy, into the main field (the
    // translation in translation mode, unless it is empty).
    function applyTagButton(index) {
        const b = root.tagButtons.buttons[index]
        if (!b || b.tag.length === 0)
            return false
        let field = translationText.activeFocus ? translationText : lineText
        if (b.type === 2)
            field = root.editor.translationMode && translationText.text.length > 0 ? translationText : lineText
        return root.editor.applyTagButton(field.role, b.tag, b.type, field.selectionStart, field.selectionEnd)
    }

    // Close review (P1): rows of Documents with unsaved work, then `then`.
    function beginClose(then) {
        const rows = root.app.reviewClose(then)
        if (rows.length === 0)
            root.app.finishClose()
        else
            closeReview.review(rows)
    }
    // Opening subtitles into the editing target (P2): staged first, then the
    // target's unsaved work is reviewed as for Close.
    function openSubtitles(path) {
        const result = root.app.reviewOpen(path)
        if (!result.ok)
            return // the log window shows the problem
        else if (result.rows.length === 0)
            root.app.finishClose()
        else
            closeReview.review(result.rows)
    }
    // Legacy ReloadSubsIfModified when the window becomes active.
    function checkExternalChange() {
        if (root.app.externalChange() === "modified")
            reloadPrompt.show()
    }
    onActiveChanged: {
        if (active)
            checkExternalChange()
    }
    onClosing: close => {
        if (root.app.quitApproved)
            return
        const rows = root.app.reviewClose("quit")
        if (rows.length === 0)
            return
        close.accepted = false
        closeReview.review(rows)
    }
    Connections {
        target: root.app
        function onCloseFinished(done, problem) {
            if (done) {
                closeReview.close()
                if (root.app.quitApproved)
                    root.close()
            } else {
                closeReview.problem = problem
            }
        }
    }

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

    // Legacy GLOBAL_JOIN_WITH_PREVIOUS / _NEXT ("Merge with previous/next line").
    Shortcut {
        sequences: ["F4"]
        context: Qt.WindowShortcut
        enabled: root.shell.hasEditingTarget
        onActivated: root.app.joinLines("previous")
    }
    Shortcut {
        sequences: ["F5"]
        context: Qt.WindowShortcut
        enabled: root.shell.hasEditingTarget
        onActivated: root.app.joinLines("next")
    }
    // Legacy GLOBAL_REMOVE_LINES.
    Shortcut {
        sequences: ["Shift+Del"]
        context: Qt.WindowShortcut
        enabled: root.shell.hasEditingTarget
        onActivated: root.app.deleteLines()
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
                Menu {
                    id: recentMenu
                    objectName: "recentSubtitlesMenu"
                    title: qsTr("Recently opened &subtitles")
                    property var rows: []
                    onAboutToShow: rows = root.app.recentSubtitles()
                    Instantiator {
                        model: recentMenu.rows
                        delegate: MenuItem {
                            required property var modelData
                            required property int index
                            objectName: "recentSubtitles" + index
                            text: modelData.label
                            onTriggered: root.openSubtitles(modelData.path)
                        }
                        onObjectAdded: (index, object) => recentMenu.insertItem(index, object)
                        onObjectRemoved: (index, object) => recentMenu.removeItem(object)
                    }
                    MenuItem {
                        text: qsTr("None")
                        enabled: false
                        visible: recentMenu.rows.length === 0
                        height: visible ? implicitHeight : 0
                    }
                }
                MenuItem {
                    objectName: "newMenuItem"
                    // Legacy GLOBAL_REMOVE_SUBS: the tab gets an Untitled default Document.
                    action: Action {
                        text: qsTr("Remove subtitles from the &editor")
                        onTriggered: root.beginClose("new")
                    }
                }
                MenuItem {
                    objectName: "closeMenuItem"
                    action: Action {
                        text: qsTr("&Close")
                        shortcut: "Ctrl+W" // legacy GLOBAL_CLOSE_PAGE
                        enabled: root.shell.hasEditingTarget
                        onTriggered: root.beginClose("close")
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
                        onTriggered: {
                            if (root.app.targetUntitled())
                                saveAsDialog.open()
                            else
                                root.editor.save()
                        }
                    }
                }
                MenuItem {
                    objectName: "saveAsMenuItem"
                    action: Action {
                        text: qsTr("Save &as…")
                        shortcut: "Ctrl+Shift+S" // legacy GLOBAL_SAVE_SUBS_AS
                        enabled: root.shell.hasEditingTarget
                        onTriggered: saveAsDialog.open()
                    }
                }
                MenuItem {
                    objectName: "logMenuItem"
                    action: Action {
                        text: qsTr("Show / Hide log window")
                        onTriggered: root.log.toggleWindow()
                    }
                }
                MenuItem {
                    objectName: "exitMenuItem"
                    action: Action {
                        text: qsTr("E&xit")
                        onTriggered: root.close()
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
            // Legacy GLOBAL_SORT_LINES / GLOBAL_SORT_SELECTED_LINES submenus.
            Menu {
                objectName: "sortAllMenu"
                title: qsTr("So&rt all lines")
                enabled: root.editor.editable
                id: sortAllMenu
                Instantiator {
                    model: root.sortKeys
                    delegate: MenuItem {
                        required property var modelData
                        objectName: "sortAll_" + modelData.key
                        text: modelData.label
                        onTriggered: root.app.sortLines(modelData.key, false)
                    }
                    onObjectAdded: (index, object) => sortAllMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => sortAllMenu.removeItem(object)
                }
            }
            Menu {
                objectName: "sortSelectedMenu"
                title: qsTr("So&rt selected lines")
                enabled: root.editor.editable
                id: sortSelectedMenu
                Instantiator {
                    model: root.sortKeys
                    delegate: MenuItem {
                        required property var modelData
                        objectName: "sortSelected_" + modelData.key
                        text: modelData.label
                        onTriggered: root.app.sortLines(modelData.key, true)
                    }
                    onObjectAdded: (index, object) => sortSelectedMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => sortSelectedMenu.removeItem(object)
                }
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
        Menu {
            id: automationMenu
            objectName: "automationMenu"
            title: qsTr("&Automation")
            MenuItem {
                objectName: "loadScriptMenuItem"
                action: Action {
                    text: qsTr("&Load script…")
                    onTriggered: scriptDialog.open()
                }
            }
            MenuItem {
                objectName: "reloadAutoloadMenuItem"
                action: Action {
                    text: qsTr("Refresh autoload scripts")
                    onTriggered: root.automation.reloadAutoload()
                }
            }
            MenuItem {
                objectName: "rerunMenuItem"
                action: Action {
                    text: qsTr("Rerun last macro")
                    enabled: root.automation.canRerun
                    onTriggered: root.automation.rerunLast()
                }
            }
            MenuItem {
                objectName: "automationManagerMenuItem"
                action: Action {
                    text: qsTr("Automation &manager")
                    onTriggered: automationManagerWindow.show()
                }
            }
            MenuSeparator {}
            Instantiator {
                model: root.macroItems
                delegate: MenuItem {
                    required property var modelData
                    objectName: "macro_" + modelData.name
                    text: modelData.name
                    enabled: !root.automation.running
                    onTriggered: root.automationManager.run(modelData.path, modelData.ordinal)
                }
                onObjectAdded: (index, object) => automationMenu.insertItem(5 + index, object)
                onObjectRemoved: (index, object) => automationMenu.removeItem(object)
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
        onSelectionStartChanged: root.editor.reportFieldSelection(role, selectionStart, selectionEnd)
        onSelectionEndChanged: root.editor.reportFieldSelection(role, selectionStart, selectionEnd)
        onCursorPositionChanged: root.editor.reportFieldSelection(role, selectionStart, selectionEnd)
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
                            // E2: custom tag buttons; right click (or a button
                            // without a tag) edits it.
                            Repeater {
                                model: root.tagButtons.buttons
                                ToolButton {
                                    required property var modelData
                                    required property int index
                                    objectName: "tagButton" + index
                                    text: modelData.name
                                    focusPolicy: Qt.NoFocus
                                    enabled: root.editor.editable
                                    Accessible.name: modelData.name
                                    Accessible.description: modelData.tag
                                    ToolTip.visible: hovered && modelData.tag.length > 0
                                    ToolTip.text: modelData.tag
                                    onClicked: {
                                        if (modelData.tag.length === 0)
                                            tagButtonDialog.editButton(index)
                                        else
                                            root.applyTagButton(index)
                                    }
                                    TapHandler {
                                        acceptedButtons: Qt.RightButton
                                        onTapped: tagButtonDialog.editButton(index)
                                    }
                                }
                            }
                            ToolButton {
                                objectName: "manageTagButtons"
                                text: qsTr("Manage tag buttons")
                                focusPolicy: Qt.NoFocus
                                onClicked: tagButtonsMenu.popup()
                                Menu {
                                    id: tagButtonsMenu
                                    Instantiator {
                                        model: root.tagButtons.buttons
                                        delegate: MenuItem {
                                            required property var modelData
                                            required property int index
                                            text: modelData.name
                                            onTriggered: root.applyTagButton(index)
                                        }
                                        onObjectAdded: (index, object) => tagButtonsMenu.insertItem(index, object)
                                        onObjectRemoved: (index, object) => tagButtonsMenu.removeItem(object)
                                    }
                                    MenuItem {
                                        objectName: "changeTagButtonCount"
                                        text: qsTr("Change number of buttons")
                                        onTriggered: tagButtonCountDialog.open()
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
                onActiveLineFallbackRequested: id => root.app.moveActiveLine(id)
                onExtendRequested: rows => root.app.extendSelection(rows)
                onLineClicked: (id, modifiers) => root.app.clickLine(id, modifiers)
                onLineDragged: id => root.app.dragSelection(id)
                onSelectAllRequested: root.app.selectAllLines()
                onContextMenuRequested: (x, y) => gridMenu.popup(grid, x, y)
                onHiddenBlockToggleRequested: row => root.app.toggleHiddenBlock(row)
                onGroupToggleRequested: id => root.app.toggleGroup(id)
                onGroupMenuRequested: (id, x, y) => {
                    groupMenu.description = id
                    groupMenu.popup(grid, x, y)
                }
                // Legacy tree description menu (ContextMenuTree).
                Menu {
                    id: groupMenu
                    objectName: "groupMenu"
                    property var description: 0
                    MenuItem { objectName: "groupAddLines"; text: qsTr("Add lines"); onTriggered: root.app.addLinesToGroup(groupMenu.description) }
                    MenuItem { objectName: "groupCopy"; text: qsTr("Copy tree"); onTriggered: root.app.copyGroup(groupMenu.description) }
                    MenuItem {
                        objectName: "groupRename"; text: qsTr("Change description")
                        onTriggered: groupDescriptionDialog.edit(groupMenu.description)
                    }
                    MenuItem { objectName: "groupSelect"; text: qsTr("Select tree lines"); onTriggered: root.app.selectGroup(groupMenu.description) }
                    MenuItem { objectName: "groupDelete"; text: qsTr("Delete"); onTriggered: root.app.removeGroup(groupMenu.description) }
                }
                // Legacy GRID_DUPLICATE_LINES (Ctrl+D) and the clipboard
                // (GRID_COPY Ctrl+C, GRID_CUT Ctrl+X, GRID_PASTE Ctrl+V) in the Grid.
                Keys.onPressed: event => {
                    if (!(event.modifiers & Qt.ControlModifier))
                        return
                    if (event.key === Qt.Key_D)
                        root.app.duplicateLines()
                    else if (event.key === Qt.Key_C)
                        root.app.copyLines()
                    else if (event.key === Qt.Key_X)
                        root.app.cutLines()
                    else if (event.key === Qt.Key_V)
                        root.app.pasteLines()
                    else
                        return
                    event.accepted = true
                }
                Menu {
                    id: gridMenu
                    objectName: "gridMenu"
                    Menu {
                        title: qsTr("&Insert")
                        MenuItem { objectName: "insertBefore"; text: qsTr("Insert &before"); onTriggered: root.app.insertLine(true) }
                        MenuItem { objectName: "insertAfter"; text: qsTr("Insert &after"); onTriggered: root.app.insertLine(false) }
                        MenuItem {
                            objectName: "insertBeforeVideo"; text: qsTr("Insert before with &video time")
                            enabled: root.video.hasVideo; onTriggered: root.app.insertLine(true, "video")
                        }
                        MenuItem {
                            objectName: "insertAfterVideo"; text: qsTr("Insert after with video time")
                            enabled: root.video.hasVideo; onTriggered: root.app.insertLine(false, "video")
                        }
                        MenuItem {
                            objectName: "insertBeforeFrame"; text: qsTr("Insert before with video frame time")
                            enabled: root.video.hasVideo; onTriggered: root.app.insertLine(true, "frame")
                        }
                        MenuItem {
                            objectName: "insertAfterFrame"; text: qsTr("Insert after with video frame time")
                            enabled: root.video.hasVideo; onTriggered: root.app.insertLine(false, "frame")
                        }
                    }
                    MenuItem { objectName: "duplicateLines"; text: qsTr("&Duplicate lines\tCtrl+D"); onTriggered: root.app.duplicateLines() }
                    MenuItem { objectName: "swapLines"; text: qsTr("&Swap"); onTriggered: root.app.swapLines() }
                    MenuItem { objectName: "joinLines"; text: qsTr("Join &lines"); onTriggered: root.app.joinLines("join") }
                    MenuItem { objectName: "joinFirst"; text: qsTr("Join lines and keep first"); onTriggered: root.app.joinLines("first") }
                    MenuItem { objectName: "joinLast"; text: qsTr("Join lines and keep last"); onTriggered: root.app.joinLines("last") }
                    MenuItem {
                        objectName: "continuousPrevious"; text: qsTr("Set times as a continuous (previous line)")
                        onTriggered: root.app.makeContinuous(true)
                    }
                    MenuItem {
                        objectName: "continuousNext"; text: qsTr("Set times as a continuous (next line)")
                        onTriggered: root.app.makeContinuous(false)
                    }
                    MenuItem { objectName: "makeTree"; text: qsTr("Make tree"); onTriggered: root.app.makeGroups() }
                    MenuItem { objectName: "hideSelectedLines"; text: qsTr("Hide selected lines"); onTriggered: root.app.hideSelectedLines() }
                    // Legacy Filtering submenu (GRID_FILTER_*).
                    Menu {
                        id: filteringMenu
                        objectName: "filteringMenu"
                        title: qsTr("Filtering")
                        property var styleNames: []
                        onAboutToShow: styleNames = root.app.styleNames()
                        MenuItem {
                            objectName: "filterAfterLoad"
                            text: qsTr("Filter after loading subtitles")
                            checkable: true
                            enabled: root.shell.assColumns
                            checked: root.gridFilter.afterLoad
                            onTriggered: root.gridFilter.afterLoad = checked
                        }
                        MenuItem {
                            objectName: "filterInvert"
                            text: qsTr("Reverse filtering")
                            checkable: true
                            checked: root.gridFilter.inverted
                            onTriggered: root.gridFilter.inverted = checked
                        }
                        MenuItem {
                            objectName: "filterDoNotReset"
                            text: qsTr("Do not reset previous filtering")
                            checkable: true
                            checked: root.gridFilter.addToFilter
                            onTriggered: root.gridFilter.addToFilter = checked
                        }
                        Menu {
                            id: filterStylesMenu
                            title: qsTr("Hide lines with styles")
                            enabled: root.shell.assColumns
                            Instantiator {
                                model: filteringMenu.styleNames
                                delegate: MenuItem {
                                    required property string modelData
                                    text: modelData
                                    checkable: true
                                    checked: root.gridFilter.styles.indexOf(modelData) >= 0
                                    onTriggered: root.gridFilter.setStyle(modelData, checked)
                                }
                                onObjectAdded: (index, object) => filterStylesMenu.insertItem(index, object)
                                onObjectRemoved: (index, object) => filterStylesMenu.removeItem(object)
                            }
                        }
                        Instantiator {
                            model: [
                                { bit: 2, label: qsTr("Hide selected lines"), name: "filterBySelection" },
                                { bit: 4, label: qsTr("Hide comments"), name: "filterByComments", ass: true },
                                { bit: 8, label: qsTr("Show unconfirmed"), name: "filterByUnconfirmed", tl: true },
                                { bit: 16, label: qsTr("Show untranslated"), name: "filterByUntranslated", tl: true }
                            ]
                            delegate: MenuItem {
                                required property var modelData
                                objectName: modelData.name
                                text: modelData.label
                                checkable: true
                                enabled: (!modelData.ass || root.shell.assColumns) && (!modelData.tl || root.editor.translationMode)
                                checked: (root.gridFilter.filterBy & modelData.bit) !== 0
                                onTriggered: root.gridFilter.setFilterBy(modelData.bit, checked)
                            }
                            onObjectAdded: (index, object) => filteringMenu.insertItem(4 + index, object)
                            onObjectRemoved: (index, object) => filteringMenu.removeItem(object)
                        }
                        MenuItem { objectName: "filter"; text: qsTr("Filter"); onTriggered: root.app.filterLines() }
                        MenuItem {
                            objectName: "turnOffFiltering"
                            text: qsTr("Turn off filtering")
                            enabled: root.shell.filtered
                            onTriggered: root.app.turnOffFiltering()
                        }
                    }
                    MenuItem {
                        objectName: "ignoreFilteringInActions"
                        text: qsTr("Ignore filtering in some actions")
                        checkable: true
                        checked: root.gridFilter.ignoreInActions
                        onTriggered: root.gridFilter.ignoreInActions = checked
                    }
                    // Legacy "Hide columns" (GRID_HIDE_LAYER ... GRID_HIDE_WRAPS).
                    Menu {
                        id: hideColumnsMenu
                        objectName: "hideColumnsMenu"
                        title: qsTr("Hide columns")
                        Instantiator {
                            model: [
                                { bit: 1, label: qsTr("Hide layer"), ass: true },
                                { bit: 2, label: qsTr("Hide start time"), ass: false },
                                { bit: 4, label: qsTr("Hide end time"), ass: false, end: true },
                                { bit: 16, label: qsTr("Hide actor"), ass: true },
                                { bit: 8, label: qsTr("Hide style"), ass: true },
                                { bit: 32, label: qsTr("Hide left margin"), ass: true },
                                { bit: 64, label: qsTr("Hide right margin"), ass: true },
                                { bit: 128, label: qsTr("Hide vertical margin"), ass: true },
                                { bit: 256, label: qsTr("Hide effect"), ass: true },
                                { bit: 512, label: qsTr("Hide characters per second"), ass: false },
                                { bit: 8192, label: qsTr("Hide line wraps"), ass: false }
                            ]
                            delegate: MenuItem {
                                required property var modelData
                                objectName: "hideColumn" + modelData.bit
                                text: modelData.label
                                checkable: true
                                checked: (root.shell.hiddenColumns & modelData.bit) !== 0
                                enabled: (!modelData.ass || root.shell.assColumns) && (!modelData.end || root.shell.endColumn)
                                onTriggered: root.shell.toggleColumn(modelData.bit)
                            }
                            onObjectAdded: (index, object) => hideColumnsMenu.insertItem(index, object)
                            onObjectRemoved: (index, object) => hideColumnsMenu.removeItem(object)
                        }
                    }
                    MenuItem { objectName: "setNewFps"; text: qsTr("Set new FPS"); onTriggered: fpsWindow.show() }
                    MenuItem {
                        objectName: "setFpsFromVideo"; text: qsTr("Set FPS from video")
                        enabled: root.video.hasVideo; onTriggered: root.app.setFpsFromVideo()
                    }
                    MenuItem { objectName: "copyLines"; text: qsTr("Copy\tCtrl+C"); onTriggered: root.app.copyLines() }
                    MenuItem { objectName: "cutLines"; text: qsTr("Cut\tCtrl+X"); onTriggered: root.app.cutLines() }
                    MenuItem { objectName: "pasteLines"; text: qsTr("Paste\tCtrl+V"); onTriggered: root.app.pasteLines() }
                    MenuItem { objectName: "copyColumns"; text: qsTr("Copy columns"); onTriggered: columnsWindow.choose(false) }
                    MenuItem { objectName: "pasteColumns"; text: qsTr("Paste columns"); onTriggered: columnsWindow.choose(true) }
                    MenuItem { objectName: "deleteLines"; text: qsTr("Delete lines\tShift+Del"); onTriggered: root.app.deleteLines() }
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
            objectName: "statusText"
            padding: 4
            text: shell.statusText
            visible: text.length > 0
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
        id: saveAsDialog
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("ASS subtitles (*.ass)"), qsTr("All files (*)")]
        onAccepted: root.app.saveAsUrl(selectedFile)
    }

    // The accepted close review: every affected Document with Save or Discard,
    // Save all, Discard all and Cancel. Nothing closes until every save is
    // acknowledged as written.
    Window {
        id: closeReview
        objectName: "closeReview"
        title: qsTr("Unsaved changes")
        width: 480
        height: 320
        modality: Qt.ApplicationModal
        flags: Qt.Dialog
        property var rows: []
        property var choices: []
        property string problem: ""
        function review(list) {
            choices = list.map(r => ({ id: r.id, save: true, path: "" }))
            rows = list
            problem = ""
            show()
        }
        function proceed() {
            for (let i = 0; i < rows.length; ++i)
                if (choices[i].save && rows[i].untitled && choices[i].path.length === 0) {
                    problem = qsTr("Choose where to save %1.").arg(rows[i].title)
                    return
                }
            root.app.resolveClose(choices)
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            Label {
                text: qsTr("These Documents have unsaved changes:")
            }
            Repeater {
                model: closeReview.rows
                delegate: RowLayout {
                    id: rowItem
                    required property int index
                    required property var modelData
                    Label {
                        text: rowItem.modelData.title
                        Layout.fillWidth: true
                    }
                    RadioButton {
                        objectName: "closeSave" + rowItem.index
                        text: qsTr("Save")
                        checked: closeReview.choices[rowItem.index].save
                        onToggled: closeReview.choices[rowItem.index].save = checked
                    }
                    RadioButton {
                        objectName: "closeDiscard" + rowItem.index
                        text: qsTr("Discard")
                        checked: !closeReview.choices[rowItem.index].save
                        onToggled: closeReview.choices[rowItem.index].save = !checked
                    }
                    Button {
                        visible: rowItem.modelData.untitled
                        text: qsTr("Save as…")
                        onClicked: {
                            closeSaveAs.row = rowItem.index
                            closeSaveAs.open()
                        }
                    }
                }
            }
            Label {
                objectName: "closeReviewProblem"
                text: closeReview.problem
                visible: text.length > 0
                color: "firebrick"
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Button {
                    objectName: "closeSaveAll"
                    text: qsTr("Save all")
                    onClicked: { closeReview.choices.forEach(c => c.save = true); closeReview.proceed() }
                }
                Button {
                    objectName: "closeDiscardAll"
                    text: qsTr("Discard all")
                    onClicked: { closeReview.choices.forEach(c => c.save = false); closeReview.proceed() }
                }
                Button {
                    objectName: "closeContinue"
                    text: qsTr("Continue")
                    onClicked: closeReview.proceed()
                }
                Button {
                    objectName: "closeCancel"
                    text: qsTr("Cancel")
                    onClicked: { root.app.cancelClose(); closeReview.close() }
                }
            }
        }
        FileDialog {
            id: closeSaveAs
            property int row: -1
            fileMode: FileDialog.SaveFile
            nameFilters: [qsTr("ASS subtitles (*.ass)"), qsTr("All files (*)")]
            onAccepted: closeReview.choices[row].path = root.app.localPath(selectedFile)
        }
    }

    // Automation windows (S1): the fixed script dialog and picker, the
    // manager tool and the progress window (legacy LuaProgressDialog).
    AutomationDialog {
        objectName: "automationDialog"
        controller: root.automationDialogs
    }
    AutomationFilePicker {
        picker: root.automationPicker
    }
    Window {
        id: automationManagerWindow
        objectName: "automationManagerWindow"
        title: qsTr("Automation manager")
        width: 560
        height: 420
        AutomationManager {
            anchors.fill: parent
            controller: root.automationManager
        }
    }
    Window {
        id: automationProgress
        objectName: "automationProgress"
        title: root.automation.runTitle
        width: 520
        height: 340
        flags: Qt.Dialog
        // Shown while a macro runs; it stays after a failure so its log can be read.
        property bool keep: false
        visible: root.automation.running || keep
        Connections {
            target: root.automation
            function onRunCompleted(ok, message) { automationProgress.keep = !ok && message.length > 0 }
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            Label {
                objectName: "automationTask"
                text: root.automation.task
                Layout.fillWidth: true
            }
            ProgressBar {
                from: 0
                to: 100
                value: root.automation.progress
                Layout.fillWidth: true
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                TextArea {
                    objectName: "automationLog"
                    readOnly: true
                    wrapMode: TextEdit.Wrap
                    text: root.automation.log
                }
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Button {
                    objectName: "automationForceStop"
                    text: qsTr("Force stop")
                    visible: root.automation.forceStopOffered
                    onClicked: root.automation.forceStopRun()
                }
                Button {
                    objectName: "automationCancel"
                    text: root.automation.running ? qsTr("Cancel") : qsTr("Close")
                    onClicked: {
                        if (root.automation.running)
                            root.automation.cancelRun()
                        else
                            automationProgress.keep = false
                    }
                }
            }
        }
    }
    FileDialog {
        id: scriptDialog
        nameFilters: [qsTr("Automation scripts (*.lua *.moon)"), qsTr("All files (*)")]
        onAccepted: root.automation.loadScript(selectedFile)
    }

    FileDialog {
        id: videoDialog
        nameFilters: [qsTr("Video (*.mkv *.mp4 *.avi *.mov *.webm *.ts *.m2ts *.wmv)"), qsTr("All files (*)")]
        onAccepted: root.video.openVideoUrl(selectedFile)
    }

    FileDialog {
        id: openDialog
        nameFilters: [qsTr("Subtitles (*.ass *.ssa *.srt *.sub *.txt *.mpl)"), qsTr("All files (*)")]
        onAccepted: root.openSubtitles(root.app.localPath(selectedFile))
    }

    // Dropped files open by the legacy rules (subtitles, scripts, video).
    DropArea {
        objectName: "dropArea"
        anchors.fill: parent
        onDropped: drop => {
            if (!drop.hasUrls)
                return
            drop.accept(Qt.CopyAction)
            const subtitles = root.app.openDropped(drop.urls)
            if (subtitles.length > 0)
                root.openSubtitles(subtitles)
        }
    }

    // GRID_SET_NEW_FPS (legacy FPSDialog): the subtitles' FPS and the new one.
    Window {
        id: fpsWindow
        objectName: "fpsWindow"
        title: qsTr("Choose new FPS")
        width: 360
        height: 140
        modality: Qt.ApplicationModal
        flags: Qt.Dialog
        readonly property var presets: ["23.976", "24", "25", "29.97", "30", "60"]
        GridLayout {
            anchors.fill: parent
            anchors.margins: 8
            columns: 2
            Label { text: qsTr("Subtitles FPS") }
            ComboBox {
                id: oldFps
                objectName: "oldFps"
                editable: true
                model: fpsWindow.presets
                validator: RegularExpressionValidator { regularExpression: /[0-9.]*/ }
                Accessible.name: qsTr("Subtitles FPS")
            }
            Label { text: qsTr("New FPS") }
            ComboBox {
                id: newFps
                objectName: "newFps"
                editable: true
                model: fpsWindow.presets
                validator: RegularExpressionValidator { regularExpression: /[0-9.]*/ }
                Accessible.name: qsTr("New FPS")
            }
            RowLayout {
                Layout.columnSpan: 2
                Layout.alignment: Qt.AlignRight
                Button {
                    objectName: "fpsOk"
                    text: qsTr("OK")
                    onClicked: {
                        if (root.app.setNewFps(oldFps.editText, newFps.editText))
                            fpsWindow.close()
                    }
                }
                Button {
                    objectName: "fpsCancel"
                    text: qsTr("Cancel")
                    onClicked: fpsWindow.close()
                }
            }
        }
    }

    // The column choice for Copy columns / Paste columns (legacy Stylelistbox),
    // checked as last chosen.
    Window {
        id: columnsWindow
        objectName: "columnsWindow"
        title: paste ? qsTr("Paste columns") : qsTr("Copy columns")
        width: 320
        height: 420
        modality: Qt.ApplicationModal
        flags: Qt.Dialog
        property bool paste: false
        property var rows: []
        function choose(forPaste) {
            paste = forPaste
            rows = root.app.columnChoices(forPaste)
            show()
        }
        function chosen() {
            let bits = 0
            for (let i = 0; i < columnRepeater.count; ++i)
                if (columnRepeater.itemAt(i).checked)
                    bits |= rows[i].bit
            return bits
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            Repeater {
                id: columnRepeater
                model: columnsWindow.rows
                delegate: CheckBox {
                    required property var modelData
                    required property int index
                    objectName: "column" + index
                    text: modelData.label
                    checked: modelData.checked
                }
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Button {
                    objectName: "columnsOk"
                    text: qsTr("OK")
                    onClicked: {
                        const bits = columnsWindow.chosen()
                        columnsWindow.close()
                        if (columnsWindow.paste)
                            root.app.pasteColumns(bits)
                        else
                            root.app.copyColumns(bits)
                    }
                }
                Button {
                    objectName: "columnsCancel"
                    text: qsTr("Cancel")
                    onClicked: columnsWindow.close()
                }
            }
        }
    }

    // Legacy LogWindow: a message pops it up with just that message; the File
    // menu entry shows the whole log.
    Window {
        id: logWindow
        objectName: "logWindow"
        title: qsTr("Log window")
        width: 520
        height: root.log.full ? 380 : 140
        flags: Qt.Dialog
        visible: root.log.shown
        onClosing: root.log.close()
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            Label {
                objectName: "logLastMessage"
                visible: !root.log.full
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                text: root.log.lastMessage
            }
            ScrollView {
                visible: root.log.full
                Layout.fillWidth: true
                Layout.fillHeight: true
                TextArea {
                    objectName: "logHistory"
                    readOnly: true
                    text: root.log.history
                    Accessible.name: qsTr("Log")
                }
            }
            Item { Layout.fillHeight: !root.log.full }
            Button {
                objectName: "logClose"
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("Close")
                onClicked: root.log.close()
            }
        }
    }

    // Legacy TagButtonDialog ("Enter ASS tag").
    Window {
        id: tagButtonDialog
        objectName: "tagButtonDialog"
        title: qsTr("Enter ASS tag")
        width: 320
        height: 240
        modality: Qt.ApplicationModal
        flags: Qt.Dialog
        property int index: -1
        function editButton(i) {
            const b = root.tagButtons.buttons[i]
            index = i
            tagType.currentIndex = b.type
            tagName.text = b.name
            tagText.text = b.tag
            show()
            tagText.selectAll()
            tagText.forceActiveFocus()
        }
        function saveTag() {
            root.tagButtons.edit(index, tagName.text, tagText.text, tagType.currentIndex)
            close()
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            ComboBox {
                id: tagType
                objectName: "tagType"
                Layout.fillWidth: true
                model: [qsTr("Tag inserted in place of cursor"), qsTr("Insert Tag at text beginning"), qsTr("Plain text")]
                Accessible.name: qsTr("Insertion")
            }
            Label { text: qsTr("Button name") }
            TextField {
                id: tagName
                objectName: "tagName"
                Layout.fillWidth: true
                Accessible.name: qsTr("Button name")
                onAccepted: tagButtonDialog.saveTag()
            }
            Label { text: qsTr("Button tag") }
            TextField {
                id: tagText
                objectName: "tagText"
                Layout.fillWidth: true
                Accessible.name: qsTr("Button tag")
                onAccepted: tagButtonDialog.saveTag()
            }
            RowLayout {
                Button {
                    objectName: "saveTag"
                    text: qsTr("Save tag")
                    onClicked: tagButtonDialog.saveTag()
                }
                Button {
                    objectName: "cancelTag"
                    text: qsTr("Cancel")
                    onClicked: tagButtonDialog.close()
                }
            }
        }
    }

    // Legacy NumTagButtons ("Change number of buttons", 0 to 20).
    Dialog {
        id: tagButtonCountDialog
        objectName: "tagButtonCountDialog"
        title: qsTr("Change number of buttons")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAboutToShow: tagButtonCount.value = root.tagButtons.count
        onAccepted: root.tagButtons.setCount(tagButtonCount.value)
        SpinBox {
            id: tagButtonCount
            objectName: "tagButtonCount"
            from: 0
            to: 20
            editable: true
            Accessible.name: qsTr("Number of buttons")
        }
    }

    // Legacy TreeDialog ("Tree description").
    Window {
        id: groupDescriptionDialog
        objectName: "groupDescriptionDialog"
        title: qsTr("Tree description")
        width: 440
        height: 120
        modality: Qt.ApplicationModal
        flags: Qt.Dialog
        property var description: 0
        function edit(id) {
            description = id
            groupDescription.text = root.app.groupTitle(id)
            show()
            groupDescription.selectAll()
            groupDescription.forceActiveFocus()
        }
        function accept() {
            if (groupDescription.text.length > 0 && root.app.renameGroup(description, groupDescription.text))
                close()
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            TextField {
                id: groupDescription
                objectName: "groupDescription"
                Layout.fillWidth: true
                maximumLength: 500
                Accessible.name: qsTr("Tree description")
                onAccepted: groupDescriptionDialog.accept()
            }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                Button {
                    objectName: "groupDescriptionOk"
                    text: root.app.groupTitle(groupDescriptionDialog.description).length === 0 ? qsTr("Set tree name") : qsTr("Change tree name")
                    onClicked: groupDescriptionDialog.accept()
                }
                Button {
                    objectName: "groupDescriptionCancel"
                    text: qsTr("Cancel")
                    onClicked: groupDescriptionDialog.close()
                }
            }
        }
    }

    // G56-contiguity: a command that would break a Line group is refused;
    // Cancel, or remove the group (its description Line is deleted) and retry.
    Connections {
        target: root.app
        function onGroupBreakRefused(description, title) {
            groupBreakDialog.description = description
            groupBreakDialog.title2 = title
            groupBreakDialog.show()
        }
    }
    Window {
        id: groupBreakDialog
        objectName: "groupBreakDialog"
        title: qsTr("Line group")
        width: 460
        height: 150
        modality: Qt.ApplicationModal
        flags: Qt.Dialog
        property var description: 0
        property string title2: ""
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: groupBreakDialog.description
                      ? qsTr("This would break the Line group “%1”. Nothing was changed. Remove the group (its description Line is deleted) and try again, or cancel.").arg(groupBreakDialog.title2)
                      : qsTr("This would leave group members outside a Line group. Nothing was changed.")
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Button {
                    objectName: "groupBreakRemove"
                    visible: groupBreakDialog.description
                    text: qsTr("Remove group")
                    onClicked: {
                        root.app.removeGroup(groupBreakDialog.description)
                        groupBreakDialog.close()
                    }
                }
                Button {
                    objectName: "groupBreakCancel"
                    text: qsTr("Cancel")
                    onClicked: groupBreakDialog.close()
                }
            }
        }
    }

    Window {
        id: reloadPrompt
        objectName: "reloadPrompt"
        title: qsTr("Reloading")
        width: 420
        height: 120
        modality: Qt.ApplicationModal
        flags: Qt.Dialog
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Subtitles were modified by another program. Reload?")
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Button {
                    objectName: "reloadYes"
                    text: qsTr("Yes")
                    onClicked: {
                        reloadPrompt.close()
                        root.app.reloadTarget()
                    }
                }
                Button {
                    objectName: "reloadNo"
                    text: qsTr("No")
                    onClicked: reloadPrompt.close()
                }
            }
        }
    }
}
