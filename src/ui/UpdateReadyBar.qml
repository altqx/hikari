pragma Translator: "HikariSub.MainWindow"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

InlineBar {
    id: bar
    objectName: "updateReadyBar"
    required property var updates
    readonly property var installer: updates.installer
    readonly property bool manual: installer.state === "manual"
    property bool dismissed: false
    signal restartRequested()
    visible: !dismissed && (installer.state === "ready" || manual)
    iconRole: "check-updates"
    label.objectName: "updateReadyText"
    text: bar.manual ? qsTr("Update downloaded: %1").arg(bar.installer.readyFile)
                     : qsTr("HikariSub %1 is ready to install").arg(bar.installer.readyVersion)
    closable: bar.manual
    closeButton.objectName: "updateDismiss"
    onClosed: bar.dismissed = true
    Connections {
        target: bar.installer
        function onReady() { bar.dismissed = false }
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
}
