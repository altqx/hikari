pragma Translator: "HikariSub.MainWindow"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: installing
    objectName: "updateInstallingWindow"
    property string version: ""
    title: qsTr("Install update")
    closable: false
    Component.onCompleted: open()

    RowLayout {
        spacing: 16
        Layout.preferredWidth: 392
        BusyIndicator {
            running: true
            Layout.alignment: Qt.AlignTop
        }
        ColumnLayout {
            spacing: 4
            Layout.fillWidth: true
            Label {
                objectName: "updateInstallingText"
                text: installing.version.length > 0 ? qsTr("Installing HikariSub %1…").arg(installing.version)
                                                    : qsTr("Installing update…")
                font: installing.fontAt(14, Font.DemiBold)
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                Accessible.role: Accessible.AlertMessage
            }
            Label {
                text: qsTr("Enter your password when the system asks for it. HikariSub starts again when the install is done.")
                color: Theme.muted
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
    }
}
