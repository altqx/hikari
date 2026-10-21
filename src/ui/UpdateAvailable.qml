pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.MainWindow"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

// P8, ADR 0019, DG1 (#222): a newer release, on the dialog frame: its name
// and the version this is, its notes (plain text; SH3 renders them), the
// requirement of a package this system cannot run, the download's progress
// in place of the button that started it, and the update options. Remind me
// in a week at the leading edge; Download and install (or Open the download
// page where HikariSub does not install updates itself) and Close at the
// trailing one. Resizable (the notes grow), modeless; closing it while the
// download runs leaves the download running, and the ready bar takes over
// when it is done.
DialogWindow {
    id: available
    objectName: "updateAvailable"
    required property var updates // UpdateChecker
    property var release: ({})
    readonly property bool installs: release.offer === "install" && updates.autoInstall
    readonly property bool downloading: updates.installer.state === "downloading"
    readonly property bool runs: release.offer !== "requirement"
    dialogId: "updateAvailable"
    resizable: true
    title: qsTr("A new version is available")
    defaultButton: offer.visible ? offer : closeButton

    // `quiet`: the automatic check's, which takes no keyboard (openQuietly).
    function showRelease(r, quiet) {
        release = r
        if (quiet)
            openQuietly()
        else
            open()
    }

    RowLayout {
        spacing: 14
        Layout.preferredWidth: 512
        Icon { iconRole: "check-updates"; size: 32; Layout.alignment: Qt.AlignVCenter }
        ColumnLayout {
            spacing: 2
            Layout.fillWidth: true
            Label {
                objectName: "updateName"
                text: available.release.name || ""
                font: available.fontAt(18, Font.DemiBold)
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Label {
                objectName: "updateVersions"
                text: qsTr("You have version %1.").arg(available.updates.version)
                color: Theme.muted
                Layout.fillWidth: true
            }
        }
    }
    ScrollView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.topMargin: 4
        Layout.preferredHeight: 158
        Layout.minimumHeight: 96
        TextArea {
            objectName: "updateNotes"
            text: available.release.notes || ""
            readOnly: true
            wrapMode: TextEdit.Wrap
            background: Rectangle {
                color: Theme.field
                border.width: 1
                border.color: parent.activeFocus ? Theme.focus : Theme.line
            }
        }
    }
    // A package this system cannot run: the release page only.
    RowLayout {
        visible: !available.runs
        spacing: 8
        Layout.fillWidth: true
        Icon { iconRole: "notice-warning"; color: Theme.warning }
        Label {
            objectName: "updateRequirement"
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            text: available.release.requirementIsOs
                  ? qsTr("This version needs Windows %1 or later.").arg(available.release.requirement || "")
                  : qsTr("This version needs glibc %1 or later.").arg(available.release.requirement || "")
        }
    }
    // The download in place of the button that started it.
    RowLayout {
        visible: available.downloading
        spacing: 12
        Layout.fillWidth: true
        ProgressBar {
            objectName: "updateProgress"
            Layout.fillWidth: true
            value: available.updates.installer.progress
        }
        Label {
            text: qsTr("Downloading… %1 %").arg(Math.round(available.updates.installer.progress * 100))
            color: Theme.muted
        }
    }
    ColumnLayout {
        spacing: 0
        CheckBox {
            objectName: "updateAutoCheck"
            text: qsTr("Check for updates automatically")
            checked: available.updates.autoCheck
            onToggled: available.updates.autoCheck = checked
        }
        CheckBox {
            objectName: "updateStableOnly"
            visible: available.updates.stableOnlyOption
            text: qsTr("Stable versions only")
            checked: available.updates.stableOnly
            onToggled: available.updates.stableOnly = checked
        }
        CheckBox {
            objectName: "updateAutoInstall"
            visible: available.updates.installsUpdates
            text: qsTr("Install updates automatically")
            checked: available.updates.autoInstall
            onToggled: available.updates.autoInstall = checked
        }
    }

    leadingActions: [
        Button {
            objectName: "updateRemind"
            text: qsTr("Remind me in a week")
            onClicked: {
                available.updates.remindInAWeek()
                available.close()
            }
        }
    ]
    buttons: [
        PrimaryButton {
            id: offer
            objectName: "updateOffer"
            visible: available.runs && !available.downloading
            text: available.installs ? qsTr("Download and install") : qsTr("Open the download page")
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: {
                if (available.installs)
                    available.updates.downloadOffer()
                else
                    available.updates.openReleasePage(available.release.url || "")
            }
        },
        Button {
            id: closeButton
            objectName: "updateClose"
            text: qsTr("Close")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: available.close()
        }
    ]
}
