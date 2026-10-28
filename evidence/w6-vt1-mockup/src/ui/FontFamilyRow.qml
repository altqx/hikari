// SY3: a font list's row, the family's name drawn in its own face
// (FontFaces): a symbol font, or a face without the letters of its name,
// draws the name in the interface font and a sample after it, and a family the system's font
// database lacks (an external font only the renderer leases) draws its name
// in the interface font. Rows have one height whatever the face's metrics,
// and clip what a tall face draws beyond it.
pragma Translator: "HikariSub.Fonts"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

ItemDelegate {
    id: row
    required property string family
    // Asked again when the fonts change (FontFaces.revision).
    readonly property var face: FontFaces.revision >= 0 ? FontFaces.face(family) : ({})
    implicitHeight: 28
    clip: true
    Accessible.name: family
    contentItem: Row {
        spacing: 12
        Label {
            objectName: "fontFamilyName"
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, row.availableWidth - (sample.visible ? sample.width + 12 : 0))
            elide: Text.ElideRight
            text: row.family
            font.family: row.face.family.length && !row.face.sample.length ? row.face.family : row.font.family
            font.pixelSize: 15
            color: row.highlighted ? row.palette.highlightedText : row.palette.text
        }
        Label {
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
}
