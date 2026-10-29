pragma Translator: "HikariSub.Fonts"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

ItemDelegate {
    id: row
    required property string family
    property string note: ""
    // Compared, not just read: a compiled binding drops an unused read and its dependency.
    readonly property var face: FontFaces.revision >= 0 ? FontFaces.face(family) : ({})
    implicitHeight: 28
    clip: true
    Accessible.name: family
    contentItem: Item {
        implicitHeight: names.implicitHeight
        Row {
            id: names
            spacing: 12
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - (noteLabel.visible ? noteLabel.width + 12 : 0)
            Label {
                textFormat: Text.PlainText
                objectName: "fontFamilyName"
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, names.width - (sample.visible ? sample.width + 12 : 0))
                elide: Text.ElideRight
                text: row.family
                font.family: row.face.family.length && !row.face.sample.length ? row.face.family : row.font.family
                font.pixelSize: 15
                color: row.highlighted ? row.palette.highlightedText : row.palette.text
            }
            Label {
                textFormat: Text.PlainText
                id: sample
                objectName: "fontFamilySample"
                anchors.verticalCenter: parent.verticalCenter
                visible: row.face.sample.length > 0
                text: row.face.sample
                font.family: row.face.family
                font.pixelSize: 15
                color: row.highlighted ? row.palette.highlightedText : Theme.muted
                Accessible.ignored: true
            }
        }
        Label {
            textFormat: Text.PlainText
            id: noteLabel
            objectName: "fontFamilyNote"
            visible: row.note.length > 0
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, parent.width / 3)
            text: row.note
            elide: Text.ElideRight
            font.pixelSize: 11
            color: row.highlighted ? row.palette.highlightedText : Theme.muted
        }
    }
}
