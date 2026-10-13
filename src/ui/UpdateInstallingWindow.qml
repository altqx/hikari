pragma Translator: "HikariSub.MainWindow"
// ADR 0019: shown after the main window has closed, while the system's
// password prompt and the package manager install a deb or rpm update.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Window {
    objectName: "updateInstallingWindow"
    title: "HikariSub"
    color: Theme.panel
    flags: Qt.Dialog
    width: 320
    height: 96
    visible: true
    // The wait ends with the package manager, not with this window.
    onClosing: close => close.accepted = false
    RowLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12
        BusyIndicator { running: true }
        Label {
            Layout.fillWidth: true
            text: qsTr("Installing update...")
            Accessible.role: Accessible.AlertMessage
        }
    }
}
