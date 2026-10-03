// E1: the editor's font dialog (legacy FontDialog "Select a font"). Each
// change is applied to the edited text at once, as legacy's FONT_CHANGED
// does; Cancel takes every change back.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Dialog {
    id: dialog
    objectName: "fontDialog"
    required property LineEditorController editor
    title: qsTr("Select a font")
    modal: true
    standardButtons: Dialog.Ok | Dialog.Cancel
    property bool loading: false
    readonly property var families: Qt.fontFamilies()

    // Opens on the field `role` with its selection; false when the editor refuses.
    function openFor(role, selectionStart, selectionEnd) {
        const f = editor.beginFont(role, selectionStart, selectionEnd)
        if (f.name === undefined)
            return false
        loading = true
        fontName.text = f.name
        fontSize.text = f.size
        bold.checked = f.bold
        italic.checked = f.italic
        underline.checked = f.underline
        strikeOut.checked = f.strikeOut
        fontList.currentIndex = families.indexOf(f.name)
        fontList.positionViewAtIndex(Math.max(0, fontList.currentIndex), ListView.Center)
        loading = false
        open()
        return true
    }
    function current() {
        return { name: fontName.text, size: fontSize.text, bold: bold.checked, italic: italic.checked,
                 underline: underline.checked, strikeOut: strikeOut.checked }
    }
    function changed() {
        if (!loading)
            changeTimer.restart()
    }
    function flush() {
        if (changeTimer.running) {
            changeTimer.stop()
            editor.changeFont(current())
        }
    }
    Timer {
        id: changeTimer
        interval: 100
        onTriggered: dialog.editor.changeFont(dialog.current())
    }
    onAccepted: {
        flush()
        editor.endDialog(true)
    }
    onRejected: {
        changeTimer.stop()
        editor.endDialog(false)
    }

    ColumnLayout {
        anchors.fill: parent
        GroupBox {
            title: qsTr("Font")
            Layout.fillWidth: true
            RowLayout {
                anchors.fill: parent
                ListView {
                    id: fontList
                    objectName: "fontList"
                    Layout.preferredWidth: 250
                    Layout.preferredHeight: 200
                    clip: true
                    model: dialog.families
                    Accessible.name: qsTr("Fonts")
                    ScrollBar.vertical: ScrollBar {}
                    delegate: ItemDelegate {
                        required property string modelData
                        required property int index
                        width: ListView.view.width
                        text: modelData
                        highlighted: ListView.isCurrentItem
                        onClicked: {
                            fontList.currentIndex = index
                            fontName.text = modelData
                        }
                    }
                }
                ColumnLayout {
                    Layout.alignment: Qt.AlignTop
                    TextField {
                        id: fontName
                        objectName: "fontName"
                        Layout.preferredWidth: 150
                        Accessible.name: qsTr("Font name")
                        onTextChanged: {
                            // Legacy selects the first font that starts with the text.
                            if (!dialog.loading) {
                                const lower = text.toLowerCase()
                                const i = dialog.families.findIndex(f => f.toLowerCase().startsWith(lower))
                                if (i >= 0) {
                                    fontList.currentIndex = i
                                    fontList.positionViewAtIndex(i, ListView.Contain)
                                }
                            }
                            dialog.changed()
                        }
                    }
                    TextField {
                        id: fontSize
                        objectName: "fontSize"
                        Layout.preferredWidth: 80
                        Accessible.name: qsTr("Font size")
                        validator: DoubleValidator { bottom: 1; top: 10000; notation: DoubleValidator.StandardNotation }
                        onTextChanged: if (acceptableInput) dialog.changed()
                    }
                    CheckBox { id: bold; objectName: "fontBold"; text: qsTr("Bold"); onToggled: dialog.changed() }
                    CheckBox { id: italic; objectName: "fontItalic"; text: qsTr("Italic"); onToggled: dialog.changed() }
                    CheckBox { id: underline; objectName: "fontUnderline"; text: qsTr("Underline"); onToggled: dialog.changed() }
                    CheckBox { id: strikeOut; objectName: "fontStrikeOut"; text: qsTr("Strikethrough"); onToggled: dialog.changed() }
                }
            }
        }
        GroupBox {
            title: qsTr("Preview")
            Layout.fillWidth: true
            Layout.preferredHeight: 120
            Label {
                anchors.fill: parent
                clip: true
                text: qsTr("AaBbCcDdEeFfGg 0123456789")
                font.family: fontName.text
                font.pixelSize: Math.min(64, Math.max(6, Number(fontSize.text) || 20))
                font.bold: bold.checked
                font.italic: italic.checked
                font.underline: underline.checked
                font.strikeout: strikeOut.checked
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }
}
