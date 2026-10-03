// F3: legacy SpellCheckerDialog "Spellchecker" (GLOBAL_OPEN_SPELLCHECKER), a
// modeless window over the editing target: the misspelled word, its
// replacement and suggestions, Replace / Replace all / Ignore / Ignore All /
// Add to dictionary / Remove from dictionary. Enter replaces, Esc closes.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Dialog {
    id: dialog
    objectName: "spellCheckerDialog"
    required property var app
    title: qsTr("Spellchecker")
    modal: false
    property var current: ({})
    property var suggestions: []

    function options() {
        return {ignoreComments: ignoreComments.checked, ignoreUpperCase: ignoreUpper.checked}
    }
    // SetNextMisspell's display: the word, its suggestions, the first one to
    // replace with; nothing found clears them and says so.
    function show(next) {
        current = next
        if (next.problem && next.problem.length > 0) {
            message.text = next.problem
            message.open()
            return
        }
        misspell.text = next.word
        suggestions = next.suggestions
        suggestionList.currentIndex = -1
        replacement.text = next.replacement
        if (!next.found) {
            message.text = qsTr("No spelling errors were found")
            message.open()
        }
    }
    function openDialog() {
        open()
        show(app.openSpellChecker(options()))
        replacement.forceActiveFocus()
    }
    function replace() { show(app.spellCheckerReplace(replacement.text, options())) }
    function replaceAll() { show(app.spellCheckerReplaceAll(misspell.text, replacement.text, options())) }

    // OnActive: back in the window after the editor or the Grid.
    onActiveFocusChanged: if (activeFocus && opened && !message.opened) {
        const next = app.spellCheckerActivated(options())
        if (next.restarted)
            show(next)
    }
    onClosed: app.closeSpellChecker()

    ColumnLayout {
        anchors.fill: parent
        RowLayout {
            Label { text: qsTr("Misspell word:"); Layout.preferredWidth: 110 }
            TextField {
                id: misspell
                objectName: "spellMisspell"
                readOnly: true
                Layout.fillWidth: true
                Layout.minimumWidth: 260
                Accessible.name: qsTr("Misspell word:")
            }
        }
        RowLayout {
            Label { text: qsTr("Replace to:"); Layout.preferredWidth: 110 }
            TextField {
                id: replacement
                objectName: "spellReplacement"
                Layout.fillWidth: true
                Accessible.name: qsTr("Replace to:")
                Keys.onReturnPressed: dialog.replace()
                Keys.onEnterPressed: dialog.replace()
                Keys.onEscapePressed: dialog.close()
            }
        }
        RowLayout {
            ListView {
                id: suggestionList
                objectName: "spellSuggestions"
                model: dialog.suggestions
                clip: true
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 220
                Layout.minimumWidth: 220
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Suggestions")
                delegate: ItemDelegate {
                    required property string modelData
                    required property int index
                    width: ListView.view.width
                    text: modelData
                    highlighted: ListView.isCurrentItem
                    onClicked: {
                        suggestionList.currentIndex = index
                        replacement.text = modelData
                    }
                    onDoubleClicked: {
                        replacement.text = modelData
                        dialog.replace()
                    }
                }
            }
            ColumnLayout {
                Layout.alignment: Qt.AlignTop
                CheckBox { id: ignoreComments; objectName: "spellIgnoreComments"; text: qsTr("Ignore comments") }
                CheckBox {
                    id: ignoreUpper
                    objectName: "spellIgnoreUpper"
                    text: qsTr("Ignore words written entirely\nin uppercase")
                }
                Button { objectName: "spellReplace"; text: qsTr("Replace"); Layout.fillWidth: true; onClicked: dialog.replace() }
                Button { objectName: "spellReplaceAll"; text: qsTr("Replace all"); Layout.fillWidth: true; onClicked: dialog.replaceAll() }
                Button {
                    objectName: "spellIgnore"
                    text: qsTr("Ignore")
                    Layout.fillWidth: true
                    onClicked: dialog.show(dialog.app.spellCheckerIgnore(dialog.options()))
                }
                Button {
                    objectName: "spellIgnoreAll"
                    text: qsTr("Ignore All")
                    Layout.fillWidth: true
                    onClicked: dialog.show(dialog.app.spellCheckerIgnoreAll(misspell.text, dialog.options()))
                }
                Button {
                    objectName: "spellAddWord"
                    text: qsTr("Add to dictionary")
                    Layout.fillWidth: true
                    onClicked: dialog.show(dialog.app.spellCheckerAddWord(misspell.text, dialog.options()))
                }
                Button {
                    objectName: "spellRemoveWord"
                    text: qsTr("Remove from dictionary")
                    Layout.fillWidth: true
                    ToolTip.text: qsTr("Remove from dictionary words added by user.")
                    ToolTip.visible: hovered
                    onClicked: {
                        addedWords.words = dialog.app.addedDictionaryWords()
                        addedWords.checked = []
                        addedWords.open()
                    }
                }
                Button { objectName: "spellClose"; text: qsTr("Close"); Layout.fillWidth: true; onClicked: dialog.close() }
            }
        }
    }

    Dialog {
        id: message
        objectName: "spellMessage"
        title: qsTr("Warning")
        modal: true
        anchors.centerIn: parent
        property alias text: messageLabel.text
        standardButtons: Dialog.Ok
        Label { id: messageLabel; Accessible.role: Accessible.AlertMessage }
    }

    // CustomCheckListBox "Words added to dictionary".
    Dialog {
        id: addedWords
        objectName: "spellAddedWords"
        title: qsTr("Words added to dictionary")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        property var words: []
        property var checked: []
        onAccepted: dialog.app.removeDictionaryWords(checked)
        ListView {
            implicitWidth: 260
            implicitHeight: 240
            clip: true
            model: addedWords.words
            delegate: CheckDelegate {
                required property string modelData
                width: ListView.view.width
                text: modelData
                onToggled: {
                    const list = addedWords.checked.filter(w => w !== modelData)
                    if (checked)
                        list.push(modelData)
                    addedWords.checked = list
                }
            }
        }
    }
}
