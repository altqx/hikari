pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.MainWindow"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

// P8, ADR 0019, DG1 (#222): a newer release, on the dialog frame: its name
// and the version this is, its notes (SH3's rendering), the requirement of a
// package this system cannot run, the download's progress in place of the
// button that started it. Resizable (the notes grow), modeless; closing it
// while the download runs leaves the download running, and the ready bar
// takes over when it is done.
// PU7 (#309): the one update window. An AUR install's variant (the former
// AUR notice) has "Update with your AUR helper" and the command to copy, or
// the release page for a prerelease its package does not carry. Skip this
// version at the leading edge; the offer (Download and install, Open the
// download page or Open the release page) and Remind me later at the trailing
// one, Close where there is no offer. The update options are on Settings >
// Updates (PU7-update-settings).
DialogWindow {
    id: available
    objectName: "updateAvailable"
    required property var updates // UpdateChecker
    property var app: null // copyText
    property var whatsNew: null // SH3: WhatsNew, which renders the notes
    property var release: ({})
    readonly property bool aur: release.offer === "aur"
    // An AUR install whose package carries the release: the command, no offer.
    readonly property bool aurCommand: aur && release.aurCarries !== false
    readonly property bool installs: release.offer === "install" && updates.autoInstall
    readonly property bool downloading: updates.installer.state === "downloading"
    readonly property bool runs: release.offer !== "requirement"
    readonly property bool offers: runs && !aurCommand && !downloading
    dialogId: "updateAvailable"
    resizable: true
    title: qsTr("A new version is available")
    defaultButton: offers ? offer : closeButton

    // `quiet`: the automatic check's, which takes no keyboard (openQuietly).
    function showRelease(r, quiet) {
        release = r
        copied.running = false
        if (quiet)
            openQuietly()
        else
            open()
    }

    RowLayout {
        spacing: 14
        Layout.preferredWidth: 552
        Icon { iconRole: "check-updates"; size: 32; Layout.alignment: Qt.AlignVCenter }
        ColumnLayout {
            spacing: 2
            Layout.fillWidth: true
            Label {
                objectName: "updateName"
                text: available.release.name || ""
                textFormat: Text.PlainText // GitHub's text, never markup
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
        Layout.topMargin: 2
        Layout.preferredHeight: 196
        Layout.minimumHeight: 96
        TextArea {
            objectName: "updateNotes"
            readonly property string notes: available.release.notes || ""
            textFormat: available.whatsNew ? TextEdit.RichText : TextEdit.PlainText
            text: available.whatsNew ? available.whatsNew.render(notes, Theme.muted, Theme.accent, Theme.raised) : notes
            readOnly: true
            wrapMode: TextEdit.Wrap
            onLinkActivated: link => available.whatsNew.openLink(link)
            HoverHandler { cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.IBeamCursor }
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
        Icon { iconRole: "notice-warning"; color: Theme.warning; accentColor: Theme.warning; Layout.alignment: Qt.AlignTop }
        Label {
            objectName: "updateRequirement"
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            text: available.release.requirementIsOs
                  ? qsTr("This version needs Windows %1 or later.").arg(available.release.requirement || "")
                  : qsTr("This version needs glibc %1 or later.").arg(available.release.requirement || "")
        }
    }
    // ADR 0019: an AUR install is updated by the package manager, never by
    // the program. hikarisub-stable-bin carries stable versions only, so a
    // prerelease it does not carry points at the AppImage instead.
    RowLayout {
        visible: available.aur && !available.aurCommand
        spacing: 8
        Layout.fillWidth: true
        Icon { iconRole: "notice-warning"; color: Theme.warning; accentColor: Theme.warning; Layout.alignment: Qt.AlignTop }
        Label {
            objectName: "updateAurPrerelease"
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            text: qsTr("The AUR package carries stable versions only. To try %1, use the AppImage from the release page.")
                  .arg(available.release.version || "")
        }
    }
    ColumnLayout {
        visible: available.aurCommand
        spacing: 4
        Layout.fillWidth: true
        Label {
            text: qsTr("Update with your AUR helper")
            font.weight: Font.DemiBold
        }
        Label {
            objectName: "updateAurText"
            text: qsTr("You installed HikariSub from the AUR, so your package manager updates it.")
            color: Theme.muted
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        RowLayout {
            spacing: 8
            Layout.fillWidth: true
            Layout.topMargin: 4
            TextField {
                id: commandField
                objectName: "updateAurCommand"
                text: available.updates.aurCommand
                readOnly: true
                selectByMouse: true
                font.family: "monospace"
                Accessible.name: qsTr("Command")
                Layout.fillWidth: true
            }
            IconTextButton {
                objectName: "updateAurCopy"
                iconRole: "edit-copy"
                text: copied.running ? qsTr("Copied") : qsTr("Copy")
                onClicked: {
                    if (available.app)
                        available.app.copyText(commandField.text)
                    copied.restart()
                }
            }
            Timer { id: copied; interval: 2000 }
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

    leadingActions: [
        Button {
            objectName: "updateSkip"
            text: qsTr("Skip this version")
            enabled: !available.downloading
            onClicked: {
                available.updates.skipVersion(available.release.version || "")
                available.close()
            }
        }
    ]
    buttons: [
        PrimaryButton {
            id: offer
            objectName: "updateOffer"
            visible: available.offers
            text: available.aur ? qsTr("Open the release page")
                : available.installs ? qsTr("Download and install") : qsTr("Open the download page")
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: {
                if (available.installs)
                    available.updates.downloadOffer()
                else
                    available.updates.openReleasePage(available.release.url || "")
            }
        },
        PrimaryButton {
            id: closeButton
            objectName: "updateClose"
            visible: !available.offers
            text: qsTr("Close")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: available.close()
        },
        Button {
            objectName: "updateRemind"
            visible: !available.downloading
            text: qsTr("Remind me later")
            DialogButtonBox.buttonRole: DialogButtonBox.NoRole
            onClicked: {
                available.updates.remindInAWeek()
                available.close()
            }
        }
    ]
}
