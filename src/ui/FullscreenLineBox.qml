pragma Translator: "HikariSub.Video"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Rectangle {
    id: box
    objectName: "fullscreenLineBox"
    required property var shell
    readonly property var editor: shell.editor
    readonly property bool hasLine: editor.hasLine
    readonly property int number: shell.shell.activeLineNumber
    readonly property alias field: field
    readonly property alias stylePicker: stylePicker
    implicitHeight: column.implicitHeight + 16
    signal leaveRequested()
    color: Theme.panel

    function focusText(reason) {
        (box.hasLine ? field : box).forceActiveFocus(reason)
    }
    function step(direction) {
        field.report()
        box.editor.nextLine(direction)
    }

    ColumnLayout {
        id: column
        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter
                  leftMargin: 12; rightMargin: 12 }
        spacing: 4
        RowLayout {
            objectName: "fullscreenLineBoxTop"
            Layout.fillWidth: true
            spacing: 4
            visible: box.hasLine
            IconToolButton {
                objectName: "fullscreenLineBoxPrevious"
                Layout.minimumWidth: implicitWidth
                iconRole: "chevron-up"
                text: qsTranslate("HikariSub.Hotkeys", "Previous line")
                implicitHeight: 24
                tip: box.shell.bitmapTip(text, "GLOBAL_PREVIOUS_LINE", 0)
                focusPolicy: Qt.TabFocus
                onClicked: if (!box.shell.hotkeyGesture("GLOBAL_PREVIOUS_LINE", 0, "bitmap")) box.step(-1)
            }
            Label {
                objectName: "fullscreenLineBoxNumber"
                Layout.minimumWidth: implicitWidth
                text: box.number
                leftPadding: 6
                rightPadding: 6
                font.features: { "tnum": 1 }
                background: Rectangle {
                    color: Theme.raised
                    radius: Theme.radius
                }
            }
            IconToolButton {
                objectName: "fullscreenLineBoxNext"
                Layout.minimumWidth: implicitWidth
                iconRole: "chevron-down"
                text: qsTranslate("HikariSub.Hotkeys", "Next line")
                implicitHeight: 24
                tip: box.shell.bitmapTip(text, "GLOBAL_NEXT_LINE", 0)
                focusPolicy: Qt.TabFocus
                onClicked: if (!box.shell.hotkeyGesture("GLOBAL_NEXT_LINE", 0, "bitmap")) box.step(1)
            }
            Label {
                objectName: "fullscreenLineBoxTimes"
                Layout.leftMargin: 8
                Layout.fillWidth: true
                Layout.maximumWidth: implicitWidth
                elide: Text.ElideRight
                text: box.editor.videoTimes
                textFormat: Text.PlainText
                color: Theme.muted
                font.family: "monospace"
            }
            IconTextButton {
                id: styleButton
                objectName: "fullscreenLineBoxStyle"
                Layout.leftMargin: 8
                Layout.maximumWidth: 200
                Layout.minimumWidth: Math.min(implicitWidth, 96)
                flat: true
                iconRole: "styles"
                text: box.editor.style
                enabled: box.editor.editable && box.editor.assFields
                //: The full-screen line box's style button: %1 the Line's style
                Accessible.name: qsTr("Style: %1").arg(text)
                tip: qsTr("Choose the line’s style")
                focusPolicy: Qt.TabFocus
                onClicked: stylePicker.openFor(styleButton)
            }
            Item { Layout.fillWidth: true }
            Label {
                objectName: "fullscreenLineBoxKeys"
                text: qsTr("Enter applies · Esc reverts")
                color: Theme.muted
                elide: Text.ElideRight
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.maximumWidth: implicitWidth
                horizontalAlignment: Text.AlignRight
            }
            IconToolButton {
                objectName: "fullscreenLineBoxUndo"
                Layout.minimumWidth: implicitWidth
                iconRole: "undo"
                text: qsTranslate("HikariSub.Hotkeys", "Undo")
                implicitHeight: 24
                tip: box.shell.bitmapTip(text, "GLOBAL_UNDO", 0)
                focusPolicy: Qt.TabFocus
                onClicked: if (!box.shell.hotkeyGesture("GLOBAL_UNDO", 0, "bitmap")) box.editor.undo()
            }
            IconToolButton {
                objectName: "fullscreenLineBoxRedo"
                Layout.minimumWidth: implicitWidth
                iconRole: "redo"
                text: qsTranslate("HikariSub.Hotkeys", "Redo")
                implicitHeight: 24
                tip: box.shell.bitmapTip(text, "GLOBAL_REDO", 0)
                focusPolicy: Qt.TabFocus
                onClicked: if (!box.shell.hotkeyGesture("GLOBAL_REDO", 0, "bitmap")) box.editor.redo()
            }
        }
        Flickable {
            id: textScroll
            visible: box.hasLine
            clip: true
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(field.implicitHeight,
                                             field.topPadding + field.bottomPadding + 2 * metrics.lineSpacing)
            contentWidth: width
            contentHeight: field.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: textScroll.contentHeight > textScroll.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }
            FontMetrics {
                id: metrics
                font: field.font
            }
            TextArea.flickable: TextArea {
                id: field
                objectName: "fullscreenLineBoxText"
                readonly property int role: box.editor.translationMode ? 1 : 0
                readonly property string shown: role === 0 ? box.editor.text : box.editor.translationText
                property bool syncing: false
                property bool pending: false
                readOnly: !box.editor.editable
                wrapMode: TextEdit.Wrap
                selectByMouse: true
                persistentSelection: true
                //: The full-screen line box's name: %1 the active Line's number
                Accessible.name: qsTr("Line %1 text").arg(box.number)

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
                    if (!pending)
                        return
                    pending = false
                    if (role === 0)
                        box.editor.textEdited(text, cursorPosition)
                    else
                        box.editor.translationEdited(text, cursorPosition)
                }
                Component.onCompleted: sync()
                onShownChanged: if (!pending) sync()
                onTextChanged: {
                    if (syncing)
                        return
                    pending = true
                    Qt.callLater(field.report)
                }
                Connections {
                    target: box.editor
                    function onChanged() {
                        if (!field.pending)
                            field.sync()
                    }
                }
                EditorHighlighter {
                    document: field.textDocument
                    colour: Theme.spellcheck
                    runs: {
                        box.editor.textState
                        box.editor.translationMode
                        return box.editor.colourRuns(field.role, field.text)
                    }
                    colours: Theme.syntax
                }

                readonly property var lineCommands: ["GLOBAL_PREVIOUS_LINE", "GLOBAL_NEXT_LINE"]
                function command(event) {
                    return box.shell.hotkeys.actionFor(0, event.key, event.modifiers)
                }
                function enter(event) {
                    return (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && !inputMethodComposing
                }
                function undoRedoKey(event) {
                    return (event.modifiers & ~Qt.ShiftModifier) === Qt.ControlModifier
                           && (event.key === Qt.Key_Z || (event.key === Qt.Key_Y && !(event.modifiers & Qt.ShiftModifier)))
                }
                Keys.onShortcutOverride: event => event.accepted = field.enter(event) || field.undoRedoKey(event)
                                                                   || event.key === Qt.Key_Escape
                                                                   || field.lineCommands.includes(field.command(event))
                Keys.onPressed: event => {
                    const command = field.command(event)
                    if (field.enter(event)) {
                        field.report()
                        box.editor.applyChanges()
                        event.accepted = true
                    } else if (event.key === Qt.Key_Escape && box.editor.hasDraft) {
                        field.pending = false
                        box.editor.discard()
                        field.sync()
                        event.accepted = true
                    } else if (event.key === Qt.Key_Escape && !inputMethodComposing) {
                        box.leaveRequested()
                        event.accepted = true
                    } else if (field.undoRedoKey(event)) {
                        field.report()
                        if (event.key === Qt.Key_Z && !(event.modifiers & Qt.ShiftModifier))
                            box.editor.undo()
                        else
                            box.editor.redo()
                        event.accepted = true
                    } else if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) {
                        const forward = event.key === Qt.Key_Tab && !(event.modifiers & Qt.ShiftModifier)
                        field.nextItemInFocusChain(forward).forceActiveFocus(forward ? Qt.TabFocusReason : Qt.BacktabFocusReason)
                        event.accepted = true
                    } else if (field.lineCommands.includes(command)) {
                        box.step(command === "GLOBAL_NEXT_LINE" ? 1 : -1)
                        event.accepted = true
                    }
                }
            }
        }
        Label {
            id: noLine
            objectName: "fullscreenLineBoxEmpty"
            visible: !box.hasLine
            Layout.fillWidth: true
            text: qsTr("No line at this time")
            color: Theme.muted
        }
    }
    Rectangle {
        anchors.fill: parent
        visible: box.activeFocus || field.activeFocus
        color: "transparent"
        border.color: Theme.focus
    }

    StylePickerPopover {
        id: stylePicker
        styles: box.shell.styleManager
        shellWindow: box.shell.shellWindow
        app: box.shell.app
        editor: box.editor
        activeNumber: box.number
    }
}
