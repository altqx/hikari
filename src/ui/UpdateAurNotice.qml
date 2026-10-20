pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.MainWindow"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

// ADR 0019: a copy installed from the AUR is updated by the package manager,
// never by the program: the notice names the new version and how to get it.
// hikarisub-stable-bin carries stable versions only, so a prerelease that
// the install's package does not carry (aurCarries) points at the AppImage
// instead; hikarisub-beta-bin carries prereleases too. DG1 (#222): on the
// dialog frame, fixed size and modeless, What's new at the leading edge.
DialogWindow {
    id: notice
    objectName: "updateAurNotice"
    required property var updates
    property var release: ({})
    dialogId: "updateAurNotice"
    title: qsTr("A new version is available")
    defaultButton: closeButton

    function showRelease(r) {
        release = r
        open()
    }

    RowLayout {
        spacing: 16
        Layout.topMargin: 8
        Icon { iconRole: "check-updates"; size: 32; Layout.alignment: Qt.AlignTop }
        Label {
            objectName: "updateAurText"
            Layout.preferredWidth: 400
            wrapMode: Text.WordWrap
            topPadding: 6
            lineHeight: 1.15
            text: notice.release.prerelease && !notice.release.aurCarries
                  ? qsTr("HikariSub %1 is a prerelease. The AUR package carries stable versions only. To try it, use the AppImage from the release page.").arg(notice.release.version || "")
                  : qsTr("HikariSub %1 is available; you have %2. You installed HikariSub from the AUR, so update it with your package manager or AUR helper, for example yay -Syu or paru -Syu.").arg(notice.release.version || "").arg(notice.updates.version)
        }
    }

    leadingActions: [
        Button {
            objectName: "updateAurWhatsNew"
            text: qsTr("What's new")
            onClicked: notice.updates.openReleasePage(notice.release.url || "")
        }
    ]
    buttons: [
        Button {
            objectName: "updateAurRemind"
            text: qsTr("Remind me in a week")
            DialogButtonBox.buttonRole: DialogButtonBox.NoRole
            onClicked: {
                notice.updates.remindInAWeek()
                notice.close()
            }
        },
        PrimaryButton {
            id: closeButton
            objectName: "updateAurClose"
            text: qsTr("Close")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: notice.close()
        }
    ]
}
