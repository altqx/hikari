// E5: the translation-mode toggle buttons (legacy EditBox DoubtfulTL "Not
// confirmed" and AutoMoveTags "Moving tags", EditBox.cpp:248-258, shown with
// translation mode by SetTlMode, EditBox.cpp:1142-1144).
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
        // SetLine: DoubtfulTL->SetValue(line->IsDoubtful()).
        checked: toggles.editor.unconfirmed
        onClicked: {
            toggles.editor.toggleUnconfirmed() // OnDoubtfulTl: the selected Lines
            checked = Qt.binding(() => toggles.editor.unconfirmed)
        }
    }
    Button {
        id: movingTags
        objectName: "movingTags"
        text: qsTr("Moving tags")
        checkable: true
        focusPolicy: Qt.NoFocus
        checked: toggles.editor.moveTags
        onClicked: {
            toggles.app.setMoveTags(checked) // OnAutoMoveTags: the option, saved at once
            checked = Qt.binding(() => toggles.editor.moveTags)
        }
    }
}
