// PU3 (#305): a filter chip (the font picker's catalog and characters): a
// rounded button with its icon, its text and a chevron, opening the menu or
// popover that sets the filter. While the filter narrows the list (`on`)
// it is filled in `select` with an accent boundary and bold text; at rest
// `raised` with a `line` boundary, accent under the pointer. Keyboard focus
// is the 1-wide ring (FocusRing).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T
import Hikari.Ui

T.Button {
    id: chip
    property string iconRole
    property bool on: false
    implicitWidth: implicitContentWidth + leftPadding + rightPadding
    implicitHeight: 26
    leftPadding: 8
    rightPadding: 6
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.ButtonMenu
    background: Rectangle {
        radius: height / 2
        color: chip.on ? Theme.select : chip.down ? Theme.select : Theme.raised
        border.width: 1
        border.color: chip.on || chip.hovered ? Theme.accent : Theme.line
        FocusRing { control: chip; radius: parent.radius + 1 }
    }
    contentItem: RowLayout {
        spacing: 5
        Icon {
            iconRole: chip.iconRole
            size: 14
            visible: chip.iconRole.length > 0
        }
        Label {
            text: chip.text
            font.bold: chip.on
            elide: Text.ElideRight
            Layout.maximumWidth: 200
        }
        Icon {
            iconRole: "chevron-down"
            size: 14
        }
    }
}
