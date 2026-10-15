pragma Translator: "HikariSub.MainWindow"
// ADR 0019: a copy installed from the AUR is updated by the package manager,
// never by the program: the notice names the new version and how to get it.
// hikarisub-stable-bin carries stable versions only, so a prerelease that
// the install's package does not carry (aurCarries) points at the AppImage
// instead; hikarisub-beta-bin carries prereleases too.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Dialog {
    id: notice
    objectName: "updateAurNotice"
    required property var updates
    property var release: ({})
    title: qsTr("A new version is available")
    modal: true
    function show(r) {
        release = r
        open()
    }
    ColumnLayout {
        anchors.fill: parent
        Label {
            objectName: "updateAurText"
            Layout.preferredWidth: 460
            wrapMode: Text.WordWrap
            text: notice.release.prerelease && !notice.release.aurCarries
                  ? qsTr("HikariSub %1 is a prerelease. The AUR package carries stable versions only. To try it, use the AppImage from the release page.").arg(notice.release.version || "")
                  : qsTr("HikariSub %1 is available; you have %2. You installed HikariSub from the AUR, so update it with your package manager or AUR helper, for example yay -Syu or paru -Syu.").arg(notice.release.version || "").arg(notice.updates.version)
        }
        WindowFooter {
            Button {
                objectName: "updateAurWhatsNew"
                text: qsTr("What's new")
                DialogButtonBox.buttonRole: DialogButtonBox.HelpRole
                onClicked: notice.updates.openReleasePage(notice.release.url || "")
            }
            Button {
                objectName: "updateAurRemind"
                text: qsTr("Remind me in a week")
                DialogButtonBox.buttonRole: DialogButtonBox.NoRole
                onClicked: { notice.updates.remindInAWeek(); notice.close() }
            }
            Button {
                objectName: "updateAurClose"
                text: qsTr("Close")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                onClicked: notice.close()
            }
        }
    }
}
