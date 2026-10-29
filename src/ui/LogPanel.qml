pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.MainWindow"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Pane {
    id: panel
    required property LogController log
    readonly property LogEntries entries: log.entries
    implicitWidth: 520
    implicitHeight: 200
    padding: 0
    readonly property size minimumSize: Qt.size(Math.ceil(strip.implicitWidth) + 16,
                                                Math.ceil(strip.implicitHeight) + 48)

    function iconColour(severity) {
        return severity === LogController.Error ? Theme.danger
             : severity === LogController.Warning ? Theme.warning : IconTheme.normal
    }
    function iconRole(severity) {
        return severity === LogController.Error ? "notice-error"
             : severity === LogController.Warning ? "notice-warning" : "about"
    }
    function showMarked() {
        const row = entries.rowOf(log.marked)
        if (row >= 0) {
            list.currentIndex = row
            list.positionViewAtIndex(row, ListView.Contain)
        }
    }
    Connections {
        target: panel.log
        function onMarkedChanged() { Qt.callLater(panel.showMarked) }
        function onRevealRequested() { Qt.callLater(panel.showMarked) }
    }

    component SeverityToggle: Button {
        id: toggle
        required property int severity
        required property int count
        property string name
        checkable: true
        text: count
        icon.source: IconTheme.image(panel.iconRole(severity), panel.iconColour(severity),
                                     severity === LogController.Info ? IconTheme.accent : panel.iconColour(severity), false)
        icon.color: "transparent"
        icon.width: 16
        icon.height: 16
        font.features: { "tnum": 1 }
        ToolTip.visible: hovered
        ToolTip.text: name
        Accessible.name: name
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            id: strip
            spacing: 4
            Layout.fillWidth: true
            Layout.margins: 6
            SeverityToggle {
                objectName: "logShowInfo"
                severity: LogController.Info
                count: panel.log.infoCount
                name: qsTr("Information")
                checked: panel.entries.showInfo
                onToggled: panel.entries.showInfo = checked
            }
            SeverityToggle {
                objectName: "logShowWarnings"
                severity: LogController.Warning
                count: panel.log.warningCount
                name: qsTr("Warnings")
                checked: panel.entries.showWarnings
                onToggled: panel.entries.showWarnings = checked
            }
            SeverityToggle {
                objectName: "logShowErrors"
                severity: LogController.Error
                count: panel.log.errorCount
                name: qsTr("Errors")
                checked: panel.entries.showErrors
                onToggled: panel.entries.showErrors = checked
            }
            ToolSeparator {}
            TextField {
                id: filterField
                objectName: "logFilter"
                placeholderText: qsTr("Filter")
                text: panel.entries.filterText
                selectByMouse: true
                Layout.preferredWidth: 220
                Accessible.name: qsTr("Filter the Log")
                onTextEdited: panel.entries.filterText = text
                Keys.onEscapePressed: event => {
                    if (text.length === 0) {
                        event.accepted = false
                        return
                    }
                    panel.entries.filterText = ""
                }
            }
            Item { Layout.fillWidth: true }
            IconToolButton {
                objectName: "logCopy"
                iconRole: "edit-copy"
                text: qsTr("Copy")
                tip: qsTr("Copy the entries shown")
                enabled: list.count > 0
                onClicked: panel.log.copyShown()
            }
        }
        Rectangle {
            color: Theme.field
            border.color: Theme.line
            Layout.fillWidth: true
            Layout.fillHeight: true
            ListView {
                id: list
                objectName: "logEntries"
                anchors.fill: parent
                anchors.margins: 1
                clip: true
                model: panel.entries
                currentIndex: -1
                keyNavigationEnabled: true
                activeFocusOnTab: true
                boundsBehavior: Flickable.StopAtBounds
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Log")
                ScrollBar.vertical: ScrollBar { policy: list.contentHeight > list.height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded }
                Keys.onPressed: event => {
                    if (event.matches(StandardKey.Copy) && currentItem) {
                        panel.log.copyEntry(currentItem.entryId)
                        event.accepted = true
                    }
                }
                delegate: Rectangle {
                    id: row
                    required property int index
                    required property int entryId
                    required property string time
                    required property int severity
                    required property string title
                    required property string text
                    required property string details
                    readonly property bool marked: entryId === panel.log.marked
                    width: ListView.view.width
                    implicitHeight: rowLayout.implicitHeight + 6
                    color: marked || ListView.isCurrentItem && list.activeFocus ? Theme.select : "transparent"
                    objectName: "logEntry_" + entryId
                    Accessible.role: Accessible.ListItem
                    Accessible.name: [title, text, details].filter(t => t.length > 0).join(". ")
                    Rectangle {
                        visible: row.marked
                        width: 2
                        height: parent.height
                        color: Theme.accent
                    }
                    TapHandler {
                        onTapped: {
                            list.currentIndex = row.index
                            list.forceActiveFocus()
                        }
                    }
                    RowLayout {
                        id: rowLayout
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.topMargin: 3
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 8
                        Label {
                            text: row.time
                            color: Theme.muted
                            font.features: { "tnum": 1 }
                            Layout.alignment: Qt.AlignTop
                        }
                        Icon {
                            iconRole: panel.iconRole(row.severity)
                            color: panel.iconColour(row.severity)
                            accentColor: row.severity === LogController.Info ? IconTheme.accent : color
                            Layout.alignment: Qt.AlignTop
                            Layout.topMargin: 1
                        }
                        ColumnLayout {
                            spacing: 0
                            Layout.fillWidth: true
                            TextEdit {
                                objectName: "logEntryText"
                                readOnly: true
                                selectByMouse: true
                                activeFocusOnPress: false
                                textFormat: TextEdit.RichText
                                wrapMode: TextEdit.Wrap
                                color: Theme.text
                                selectionColor: Theme.accent
                                selectedTextColor: Theme.onAccent
                                font: Application.font
                                text: (row.title.length > 0 ? "<b>" + panel.escaped(row.title) + "</b> " : "")
                                      + panel.escaped(row.text).replace(/\n/g, "<br>")
                                Layout.fillWidth: true
                            }
                            TextEdit {
                                visible: row.details.length > 0
                                readOnly: true
                                selectByMouse: true
                                activeFocusOnPress: false
                                wrapMode: TextEdit.Wrap
                                color: Theme.muted
                                selectionColor: Theme.accent
                                selectedTextColor: Theme.onAccent
                                font: Application.font
                                leftPadding: 14
                                textFormat: TextEdit.PlainText
                                text: row.details
                                Layout.fillWidth: true
                            }
                        }
                    }
                }
            }
            Label {
                objectName: "logEmpty"
                anchors.centerIn: parent
                visible: list.count === 0
                color: Theme.muted
                text: panel.log.infoCount + panel.log.warningCount + panel.log.errorCount > 0
                      ? qsTr("No entries match") : qsTr("Nothing logged yet")
            }
        }
    }
    function escaped(s) {
        return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
    }
}
