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
