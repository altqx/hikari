import QtQuick
import QtQuick.Controls
import Hikari.Ui

Label {
    leftPadding: 12
    rightPadding: 12
    topPadding: 4
    bottomPadding: 2
    color: Theme.muted
    font.bold: true
    font.pixelSize: Math.round(Qt.application.font.pixelSize * 0.85)
}
