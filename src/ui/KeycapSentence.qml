import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

RowLayout {
    id: sentence
    property string text
    property var keys: []
    property color color: Theme.text
    property font font: Qt.application.font
    spacing: 4
    Repeater {
        model: sentence.text.split(/(%[1-9])/).map(piece => piece.trim()).filter(piece => piece.length > 0)
        delegate: Item {
            id: piece
            required property string modelData
            readonly property int key: /^%[1-9]$/.test(modelData) ? Number(modelData.slice(1)) - 1 : -1
            implicitWidth: key >= 0 ? cap.implicitWidth : words.implicitWidth
            implicitHeight: key >= 0 ? cap.implicitHeight : words.implicitHeight
            Keycap {
                id: cap
                visible: piece.key >= 0
                anchors.verticalCenter: parent.verticalCenter
                text: piece.key >= 0 ? sentence.keys[piece.key] ?? "" : ""
            }
            Label {
                id: words
                visible: piece.key < 0
                anchors.verticalCenter: parent.verticalCenter
                text: piece.key < 0 ? piece.modelData : ""
                color: sentence.color
                font: sentence.font
            }
        }
    }
}
