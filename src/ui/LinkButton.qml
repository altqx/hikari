import QtQuick
import QtQuick.Controls
import Hikari.Ui

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
