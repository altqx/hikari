pragma Translator: "HikariSub.MainWindow"
// ADR 0019: the update notice under the menus. "HikariSub <V> is ready to
// install" with Restart now and Later once a download has been verified (and
// again at each start while it waits); Later, or closing the window, installs
// it at the next quit. A deb or rpm copy without pkexec gets the downloaded
// file instead, with the command that installs it.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Pane {
    id: bar
    objectName: "updateReadyBar"
    required property var updates
    readonly property var installer: updates.installer
    readonly property bool manual: installer.state === "manual"
    // Later: hidden for this session; a new download shows it again.
    property bool dismissed: false
    signal restartRequested()
    visible: !dismissed && (installer.state === "ready" || manual)
    padding: 6
    Connections {
        target: bar.installer
        function onReady() { bar.dismissed = false }
    }
    RowLayout {
        anchors.fill: parent
        spacing: 8
        Icon {
            iconRole: "check-updates"
            size: 16
            Layout.alignment: Qt.AlignVCenter
        }
        Label {
            objectName: "updateReadyText"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            Accessible.role: Accessible.AlertMessage
            text: bar.manual ? qsTr("Update downloaded: %1").arg(bar.installer.readyFile)
                             : qsTr("HikariSub %1 is ready to install").arg(bar.installer.readyVersion)
        }
        Button {
            objectName: "updateWhatsNew"
            visible: !bar.manual
            flat: true
            text: qsTr("What's new")
            onClicked: bar.updates.openReleasePage(bar.updates.releasePageOf(bar.installer.readyVersion))
        }
        Button {
            objectName: "updateRestartNow"
            visible: !bar.manual
            highlighted: true
            text: qsTr("Restart now")
            onClicked: bar.restartRequested()
        }
        Button {
            objectName: "updateLater"
            visible: !bar.manual
            text: qsTr("Later")
            onClicked: bar.dismissed = true
        }
        Button {
            objectName: "updateShowInFolder"
            visible: bar.manual
            text: qsTr("Show in folder")
            onClicked: bar.installer.showInFolder()
        }
        Button {
            objectName: "updateCopyCommand"
            visible: bar.manual
            text: qsTr("Copy install command")
            onClicked: bar.installer.copyInstallCommand()
        }
        IconToolButton {
            objectName: "updateDismiss"
            visible: bar.manual
            iconRole: "tab-close"
            text: qsTr("Close")
            onClicked: bar.dismissed = true
        }
    }
}
