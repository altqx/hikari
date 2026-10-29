pragma Translator: "HikariSub.Translation"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: toggles
    required property var app
    required property var editor
    Button {
        id: notConfirmed
        objectName: "notConfirmed"
        text: qsTr("Not confirmed")
        checkable: true
        focusPolicy: Qt.NoFocus
        enabled: toggles.editor.editable
        checked: toggles.editor.unconfirmed
        onClicked: {
            toggles.editor.toggleUnconfirmed()
            checked = Qt.binding(() => toggles.editor.unconfirmed)
        }
    }
    Button {
        id: movingTags
        objectName: "movingTags"
        text: qsTr("Tags from original")
        checkable: true
        focusPolicy: Qt.NoFocus
        checked: toggles.editor.moveTags
        onClicked: {
            toggles.app.setMoveTags(checked)
            checked = Qt.binding(() => toggles.editor.moveTags)
        }
    }
}
