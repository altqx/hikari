pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.MainWindow"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: dialog
    objectName: "whatsNewDialog"
    required property var whatsNew
    readonly property string state: whatsNew.state
    readonly property bool failed: state === "failed"
    dialogId: "whatsNew"
    resizable: true
    title: qsTr("What's new")
    defaultButton: closeButton

    onVisibleChanged: if (visible) dialog.whatsNew.fetchIfNeeded()

    Label {
        objectName: "whatsNewIntro"
        visible: dialog.whatsNew.releases.length > 0 && dialog.whatsNew.since.length > 0
        Layout.fillWidth: true
        Layout.bottomMargin: 8
        wrapMode: Text.WordWrap
        color: Theme.muted
        text: qsTr("Changes since version %1, the one you used before.").arg(dialog.whatsNew.since)
    }
    Rectangle {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredWidth: 572
        Layout.preferredHeight: 400
        Layout.minimumHeight: 200
        color: Theme.field
        border.color: Theme.line

        ReleaseNotesView {
            id: notes
            objectName: "whatsNewDialogNotes"
            anchors.fill: parent
            anchors.margins: 1
            visible: dialog.whatsNew.releases.length > 0
            whatsNew: dialog.whatsNew
            smallFont: dialog.fontAt(11)
            Accessible.name: dialog.title
        }
        ColumnLayout {
            objectName: "whatsNewEmpty"
            visible: !notes.visible
            anchors.centerIn: parent
            width: parent.width - 48
            spacing: 10
            Icon {
                Layout.alignment: Qt.AlignHCenter
                visible: dialog.state !== "fetching"
                iconRole: dialog.failed ? "notice-warning" : "whats-new"
                color: dialog.failed ? Theme.warning : IconTheme.normal
                accentColor: dialog.failed ? Theme.warning : IconTheme.accent
                size: 32
            }
            Label {
                objectName: "whatsNewEmptyTitle"
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                font.weight: Font.DemiBold
                text: dialog.state === "fetching" ? qsTr("Fetching the release notes…")
                    : dialog.state === "failed" ? qsTr("Cannot fetch the release notes")
                    : dialog.state === "none" ? qsTr("No release notes for this version")
                    : qsTr("No release notes yet")
            }
            ProgressBar {
                visible: dialog.state === "fetching"
                indeterminate: true
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 200
            }
            Label {
                objectName: "whatsNewEmptyText"
                visible: text.length > 0
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: Theme.muted
                text: dialog.state === "failed" ? qsTr("HikariSub tries again at the next start.")
                    : dialog.state === "off" ? qsTr("HikariSub fetches the release notes only while “Check for updates automatically” is on.")
                    : ""
            }
        }
    }

    leadingActions: [
        IconTextButton {
            objectName: "whatsNewAllReleaseNotes"
            iconRole: "open-link"
            text: qsTr("All release notes")
            onClicked: dialog.whatsNew.openAllReleaseNotes()
        }
    ]
    buttons: [
        PrimaryButton {
            id: closeButton
            objectName: "whatsNewClose"
            text: qsTr("Close")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: dialog.close()
        }
    ]
}
