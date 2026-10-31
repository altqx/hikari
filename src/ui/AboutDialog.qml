pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.MainWindow"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: about
    objectName: "aboutDialog"
    required property var app
    required property var updates
    signal reportIssueRequested()
    dialogId: "about"
    title: qsTr("About HikariSub")
    defaultButton: ok

    RowLayout {
        spacing: 16
        Layout.preferredWidth: 412
        Image {
            Layout.alignment: Qt.AlignTop
            source: "qrc:/hikari/ui/app-icon.png"
            sourceSize: Qt.size(64, 64)
            Layout.preferredWidth: 64
            Layout.preferredHeight: 64
            Accessible.ignored: true
        }
        ColumnLayout {
            spacing: 2
            Layout.fillWidth: true
            Label {
                objectName: "aboutName"
                text: "HikariSub"
                font: about.fontAt(18, Font.DemiBold)
            }
            RowLayout {
                spacing: 4
                Label {
                    objectName: "aboutVersion"
                    text: qsTr("Version %1").arg(about.updates.versionLabel)
                    color: Theme.muted
                }
                IconToolButton {
                    objectName: "aboutCopyVersion"
                    iconRole: "edit-copy"
                    iconSize: 14
                    padding: 4
                    text: qsTr("Copy version")
                    onClicked: about.app.copyText(about.updates.versionLabel)
                }
            }
            Label {
                objectName: "aboutAuthor"
                text: qsTr("Subtitle editor by altqx.")
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                Layout.topMargin: 4
            }
        }
    }
    Label {
        objectName: "aboutLicence"
        text: qsTr("HikariSub is free software under the GNU General Public License, version 3. It comes with no warranty.")
        color: Theme.muted
        wrapMode: Text.WordWrap
        lineHeight: 1.1
        Layout.preferredWidth: 412
        Layout.topMargin: 4
    }
    RowLayout {
        spacing: 24
        LinkButton {
            objectName: "aboutWebsite"
            iconRole: "open-link"
            text: qsTr("HikariSub website")
            onClicked: about.app.openWebsite()
        }
        LinkButton {
            objectName: "aboutReportIssue"
            iconRole: "report-issue"
            text: qsTr("Report an issue")
            onClicked: about.reportIssueRequested()
        }
    }

    buttons: [
        PrimaryButton {
            id: ok
            objectName: "aboutOk"
            text: qsTr("OK")
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: about.accept()
        }
    ]
}
