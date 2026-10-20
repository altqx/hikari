// TM1 (#232): choosing the Translation preset for a Document that can take
// Translation mode and has it off asks once (App.translationQuestionDue).
// Turn on, the default answer, is legacy's one "Turning on translator mode"
// step (EditBox::OnTlMode); Not now keeps the arrangement without the mode
// and is remembered for the Document this session
// (App.declineTranslationMode). The message dialog's form
// (docs/research/musescore-shell.md §4): title, bold question, the
// explanation, then the buttons with the accent one focused.
pragma Translator: "HikariSub.Translation"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Dialog {
    id: dialog
    objectName: "translationModeQuestion"
    required property var app
    parent: Overlay.overlay
    anchors.centerIn: parent
    title: qsTr("Translation mode")
    modal: true
    closePolicy: Popup.CloseOnEscape
    ColumnLayout {
        spacing: 10
        Label {
            objectName: "translationModeQuestionText"
            text: qsTr("Turn on Translation mode for this file?")
            font.bold: true
            Layout.maximumWidth: 420
            wrapMode: Text.Wrap
        }
        Label {
            text: qsTr("Each Line keeps its text as the Original text and gets a Translated text beside it. The file saves both until you turn the mode off.")
            Layout.maximumWidth: 420
            Layout.preferredWidth: 420
            wrapMode: Text.Wrap
        }
    }
    footer: DialogButtonBox {
        Button {
            objectName: "translationModeNotNow"
            text: qsTr("Not now")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
        PrimaryButton {
            id: turnOn
            objectName: "translationModeTurnOn"
            text: qsTr("Turn on")
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }
    }
    onOpened: turnOn.forceActiveFocus()
    onAccepted: dialog.app.turnOnTranslationMode()
    onRejected: dialog.app.declineTranslationMode()
}
