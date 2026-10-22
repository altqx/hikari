import QtQuick
import QtQuick.Controls
import Hikari.Ui

// A link that opens the browser: an icon and the text in the accent,
// underlined under the pointer, with the focus ring (DG1's About; SH3's
// What's new).
AbstractButton {
    id: link
    required property string iconRole
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    padding: 2
    contentItem: Row {
        spacing: 6
        Icon {
            anchors.verticalCenter: parent.verticalCenter
            iconRole: link.iconRole
            size: 14
            color: IconTheme.accent
        }
        Label {
            anchors.verticalCenter: parent.verticalCenter
            text: link.text
            color: Theme.accent
            font.underline: link.hovered
        }
    }
    background: Item { FocusRing { control: link } }
    Accessible.role: Accessible.Link
    Accessible.name: text
}
