pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Home"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Rectangle {
    id: card
    objectName: "whatsNewCard"
    required property var whatsNew
    property font smallFont: Qt.font({})
    color: Theme.panel
    border.color: Theme.line
    Accessible.role: Accessible.Grouping
    Accessible.name: title.text

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 1
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.minimumHeight: 40
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            spacing: 12
            Label {
                id: title
                text: qsTranslate("HikariSub.MainWindow", "What's new")
                font.bold: true
                Layout.fillWidth: true
            }
            Label {
                objectName: "whatsNewSince"
                visible: card.whatsNew.since.length > 0
                text: qsTr("Since %1").arg(card.whatsNew.since)
                color: Theme.muted
                font: card.smallFont
            }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.line }
        ReleaseNotesView {
            objectName: "whatsNewNotes"
            whatsNew: card.whatsNew
            smallFont: card.smallFont
            Layout.fillWidth: true
            Layout.fillHeight: true
            Accessible.name: title.text
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.line }
        LinkButton {
            objectName: "whatsNewAll"
            Layout.margins: 8
            Layout.leftMargin: 14
            iconRole: "link"
            text: qsTranslate("HikariSub.MainWindow", "All release notes")
            onClicked: card.whatsNew.openAllReleaseNotes()
        }
    }
}
