// K1: a dialog's title with its icon of the set before it (legacy dialogs
// show their bitmap as the window's icon, SetIcon; a dialog drawn in the
// window's overlay has no title bar of its own). Shown, sized and painted as
// the style's own title: bold, padding 6, on the window colour, only while
// the dialog sits in the overlay.
import QtQuick
import QtQuick.Controls
import Hikari.Ui

Label {
    id: header
    property string iconRole
    readonly property bool rtl: LayoutMirroring.enabled
    visible: text.length > 0 && parent?.parent === Overlay.overlay
    elide: Label.ElideRight
    font.bold: true
    padding: 6
    leftPadding: rtl ? 6 : 6 + icon.width + 6
    rightPadding: rtl ? 6 + icon.width + 6 : 6
    Icon {
        id: icon
        objectName: "dialogIcon"
        iconRole: header.iconRole
        x: header.rtl ? header.width - 6 - width : 6
        anchors.verticalCenter: parent.verticalCenter
    }
    background: Rectangle {
        color: header.palette.window
    }
}
