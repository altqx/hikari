pragma ComponentBehavior: Bound
// F2: legacy SelectLines "Select" (GLOBAL_OPEN_SELECT_LINES): what to find
// and where, Dialogue/Comments, the selection operation and the action on
// the selected Lines. PU2 (#304): a modeless window on DG1's frame (it can
// leave the main window), short labelled rows instead of group boxes, the
// styles button PK1's Choose styles popover, and each run's count a toast
// while the window stays open (PU2-select-lines).
pragma Translator: "HikariSub.SelectLines"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: dialog
    objectName: "selectLinesDialog"
    required property var app
    // NT1 (#302): where the count goes ("%1 lines selected.", an info
    // toast titled with the command).
    property ToastStack notices: null
    dialogId: "selectLines"
    title: qsTr("Select lines") // the menu command's name (legacy titled it "Select")
    defaultButton: runButton
    initialFocus: findText
    property var recent: []
    Component.onCompleted: IconTheme.setWindowIcon(dialog, "select-lines")

    readonly property var fieldNames: [qsTr("Text"), qsTr("Styles"), qsTr("Actor"), qsTr("Effect"), qsTr("Start time"),
                                       qsTr("End time")]
    function openDialog() {
        const s = app.openSelectLines()
        withChoice.currentIndex = s.with ? 0 : 1
        matchCase.checked = s.matchCase
        regex.checked = s.regex
        fields.currentIndex = s.field
        dialogues.checked = s.dialogues
        comments.checked = s.comments
        modes.currentIndex = s.mode
        actions.currentIndex = s.action
        recent = s.recent
        findText.editText = recent.length > 0 ? recent[0] : ""
        open()
        findText.forceActiveFocus()
    }
    function settings() {
        return {find: findText.editText, with: withChoice.currentIndex === 0, matchCase: matchCase.checked,
                regex: regex.checked, field: fields.currentIndex, dialogues: dialogues.checked,
                comments: comments.checked, mode: modes.currentIndex, action: actions.currentIndex}
    }
    function run(allTabs) {
        const text = findText.editText
        const message = app.selectLines(settings(), allTabs)
        recent = app.selectLinesSettings().recent
        findText.editText = text
        if (notices && message.length > 0)
            notices.post("info", qsTr("Select lines"), message)
    }
    onVisibleChanged: if (!visible) app.saveSelectLinesSettings(settings())

    component RowLabel: Label {
        Layout.preferredWidth: 72
        Layout.alignment: Qt.AlignVCenter
    }

    GridLayout {
        columns: 2
        columnSpacing: 8
        rowSpacing: 10
        Layout.fillWidth: true

        RowLabel { text: qsTr("Find") }
        RowLayout {
            spacing: 8
            SegmentedButtons {
                id: withChoice
                objectName: "selectWithChoice"
                choices: [{ text: qsTr("With"), name: "selectWith" }, { text: qsTr("Without"), name: "selectWithout" }]
                Accessible.name: qsTr("Lines with or without the text")
                onActivated: index => currentIndex = index
            }
            ComboBox {
                id: findText
                objectName: "selectFindText"
                editable: true
                model: dialog.recent
                Layout.fillWidth: true
                Layout.minimumWidth: 214
                Accessible.name: qsTr("Search text:")
                // The editable ComboBox takes Enter before the window's
                // default button sees it.
                Keys.onReturnPressed: dialog.run(false)
                Keys.onEnterPressed: dialog.run(false)
            }
            IconToolButton {
                id: selectChooseStylesButton
                objectName: "selectChooseStyles"
                iconRole: "styles"
                text: qsTr("Choose styles")
                onClicked: {
                    stylesChooser.before = { text: findText.editText, field: fields.currentIndex, regex: regex.checked }
                    stylesChooser.openFor(selectChooseStylesButton, dialog.app.styleNames(), [], { counts: dialog.app.styleLineCounts() })
                }
            }
        }

        Item { implicitWidth: 1 }
        RowLayout {
            spacing: 18
            CheckBox { id: matchCase; objectName: "selectMatchCase"; text: qsTr("Match case") }
            CheckBox { id: regex; objectName: "selectRegex"; text: qsTr("Regular expressions") }
        }

        RowLabel { text: qsTr("In") }
        SegmentedButtons {
            id: fields
            objectName: "selectField"
            choices: dialog.fieldNames.map((t, i) => ({ text: t, name: "selectField" + i }))
            Accessible.name: qsTr("In field")
            onActivated: index => currentIndex = index
        }

        RowLabel { text: qsTr("Lines") }
        RowLayout {
            spacing: 18
            CheckBox { id: dialogues; objectName: "selectDialogues"; text: qsTr("Dialogue") }
            CheckBox { id: comments; objectName: "selectComments"; text: qsTr("Comments") }
        }

        RowLabel { text: qsTr("Selection") }
        SegmentedButtons {
            id: modes
            objectName: "selectMode"
            choices: [{ text: qsTr("Select"), name: "selectMode0" }, { text: qsTr("Add to selection"), name: "selectMode1" },
                      { text: qsTr("Deselect"), name: "selectMode2" }]
            Accessible.name: qsTr("Selection")
            Layout.fillWidth: true // as wide as the In row: no choice elided
            onActivated: index => currentIndex = index
        }

        RowLabel { text: qsTr("Then") }
        ComboBox {
            id: actions
            objectName: "selectAction"
            model: [qsTr("Do nothing"), qsTr("Copy"), qsTr("Cut"), qsTr("Move to beginning"), qsTr("Move to end"),
                    qsTr("Set as comment"), qsTr("Delete")]
            Accessible.name: qsTr("Action")
            Layout.preferredWidth: 214
        }
    }

    leadingActions: [
        Button {
            objectName: "selectRunAllTabs"
            text: qsTr("Select in all tabs")
            onClicked: dialog.run(true)
        }
    ]
    buttons: [
        PrimaryButton {
            id: runButton
            objectName: "selectRun"
            text: qsTr("Select")
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: dialog.run(false)
        },
        Button {
            objectName: "selectClose"
            text: qsTr("Close")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: dialog.close()
        }
    ]

    // Legacy Stylelistbox "Choose styles", now PK1's Choose styles popover:
    // the ticks make the checked styles a Styles regular expression at once
    // (no ticks: "^$", as legacy's empty choice); Esc puts the search as it
    // was (approved F2-style-cancel, 2026-10-04: cancelling changes
    // nothing; legacy filled in "^$").
    ChooseStyles {
        id: stylesChooser
        objectName: "selectStylesChooser"
        property var before: null
        onChanged: (names, reverted) => {
            if (reverted && before) {
                findText.editText = before.text
                fields.currentIndex = before.field
                regex.checked = before.regex
            } else {
                findText.editText = dialog.app.selectStylesPattern(names)
                fields.currentIndex = 1
                regex.checked = true
            }
        }
    }
}
