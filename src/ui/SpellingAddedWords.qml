pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Spelling"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: dialog
    objectName: "spellAddedWords"
    title: qsTr("Words added to dictionary")
    dialogId: "spellAddedWords"
    modal: true
    initialFocus: filter
    defaultButton: closeButton
    property var words: []
    property var checked: []
    readonly property var shown: {
        const f = filter.text.toLowerCase()
        return f.length === 0 ? words : words.filter(w => w.toLowerCase().indexOf(f) >= 0)
    }
    signal removeRequested(var words)

    function openWith(list) {
        words = list
        checked = []
        filter.text = ""
        open()
    }
    function toggle(word, on) {
        const list = checked.filter(w => w !== word)
        if (on)
            list.push(word)
        checked = list
    }

    TextField {
        id: filter
        objectName: "spellAddedFilter"
        Layout.fillWidth: true
        placeholderText: qsTr("Filter")
        Accessible.name: qsTr("Filter")
        Keys.onEscapePressed: event => {
            if (text.length > 0)
                text = ""
            else
                event.accepted = false
        }
    }
    Frame {
        Layout.fillWidth: true
        Layout.preferredWidth: 300
        Layout.preferredHeight: 200
        padding: 1
        background: Rectangle { color: Theme.field; border.color: Theme.line }
        Label {
            objectName: "spellAddedEmpty"
            anchors.centerIn: parent
            visible: list.count === 0
            color: Theme.muted
            text: dialog.words.length === 0 ? qsTr("No words added") : qsTr("No matching words")
        }
        ListView {
            id: list
            objectName: "spellAddedList"
            anchors.fill: parent
            clip: true
            model: dialog.shown
            Accessible.role: Accessible.List
            Accessible.name: dialog.title
            ScrollBar.vertical: ScrollBar {}
            delegate: CheckDelegate {
                required property string modelData
                objectName: "spellAdded_" + modelData
                width: ListView.view.width
                text: modelData
                checked: dialog.checked.indexOf(modelData) >= 0
                onToggled: dialog.toggle(modelData, checked)
            }
        }
    }

    buttons: [
        DangerButton {
            objectName: "spellAddedRemove"
            enabled: dialog.checked.length > 0
            text: dialog.checked.length > 0 ? qsTr("Remove %n word(s)", "", dialog.checked.length) : qsTr("Remove")
            DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole
            onClicked: {
                dialog.removeRequested(dialog.checked)
                dialog.accept()
            }
        },
        PrimaryButton {
            id: closeButton
            objectName: "spellAddedClose"
            text: qsTr("Close")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: dialog.close()
        }
    ]
}
