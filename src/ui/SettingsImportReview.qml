pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.SettingsImport"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

ColumnLayout {
    id: review
    required property var importer
    required property var window
    spacing: 6

    property bool conflictsOnly: true
    property string search: ""
    readonly property var conflicts: importer.rows.filter(r => r.selectable)
    readonly property var shownRows: {
        const needle = search.trim().toLowerCase()
        return (conflictsOnly ? conflicts : importer.rows).filter(r => needle.length === 0
            || [r.key, r.destination, r.value, r.current, r.source].some(t => (t || "").toLowerCase().includes(needle)))
    }
    readonly property var currentRow: rowList.currentIndex >= 0 && rowList.currentIndex < shownRows.length
                                      ? shownRows[rowList.currentIndex] : null
    readonly property int chosenCount: importer.chosenIds.length

    function reset() {
        conflictsOnly = true
        search = ""
        rowList.currentIndex = shownRows.length > 0 ? 0 : -1
    }
    function showFirst() {
        rowList.currentIndex = shownRows.length > 0 ? 0 : -1
    }
    function statusOf(row) {
        if (row.selectable)
            return qsTr("Conflict")
        switch (row.disposition) {
        case "unchanged":
        case "missing":
            return qsTr("Same")
        case "unresolved":
            return qsTr("Unresolved")
        case "read":
            return qsTr("File read")
        default:
            return qsTr("Not here")
        }
    }
    function chooseShown(on) {
        for (const row of shownRows)
            if (row.selectable)
                importer.setChosen(row.id, on)
    }

    RowLayout {
        spacing: 10
        Layout.fillWidth: true
        Layout.topMargin: 6
        SegmentedButtons {
            objectName: "settingsImportFilter"
            choices: [{ text: qsTr("Conflicts %1").arg(review.conflicts.length), name: "settingsImportConflicts" },
                      { text: qsTr("All %1").arg(review.importer.rows.length), name: "settingsImportAll" }]
            currentIndex: review.conflictsOnly ? 0 : 1
            onActivated: index => {
                review.conflictsOnly = index === 0
                rowList.currentIndex = review.shownRows.length > 0 ? 0 : -1
            }
        }
        TextField {
            objectName: "settingsImportSearch"
            placeholderText: qsTr("Search settings")
            Accessible.name: qsTr("Search settings")
            text: review.search
            onTextEdited: review.search = text
            Layout.preferredWidth: 240
        }
        Item { Layout.fillWidth: true }
        Label {
            objectName: "settingsImportSummary"
            color: Theme.muted
            text: qsTr("%1 of %2 chosen").arg(review.chosenCount).arg(review.conflicts.length)
        }
    }
    Frame {
        padding: 1
        Layout.fillWidth: true
        Layout.fillHeight: true
        background: Rectangle { color: Theme.field; border.color: Theme.line }
        ColumnLayout {
            anchors.fill: parent
            spacing: 0
            Rectangle {
                color: Theme.raised
                implicitHeight: 28
                Layout.fillWidth: true
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 10
                    Item { implicitWidth: 16 }
                    Label { text: qsTr("Setting"); color: Theme.muted; Layout.fillWidth: true }
                    Label { text: qsTr("Legacy"); color: Theme.muted; Layout.preferredWidth: 160 }
                    Label { text: qsTr("Now"); color: Theme.muted; Layout.preferredWidth: 160 }
                    Label { text: qsTr("Status"); color: Theme.muted; Layout.preferredWidth: 84 }
                }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
            }
            ListView {
                id: rowList
                objectName: "settingsImportRows"
                clip: true
                model: review.shownRows
                keyNavigationEnabled: true
                activeFocusOnTab: true
                highlightMoveDuration: 0
                Layout.fillWidth: true
                Layout.fillHeight: true
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Settings to review")
                ScrollBar.vertical: ScrollBar {}
                Keys.onSpacePressed: {
                    const row = review.currentRow
                    if (row && row.selectable)
                        review.importer.setChosen(row.id, review.importer.chosenIds.indexOf(row.id) < 0)
                }
                delegate: ItemDelegate {
                    id: rowItem
                    required property var modelData
                    required property int index
                    objectName: "settingsImportRow_" + modelData.id
                    width: ListView.view.width
                    height: 28
                    highlighted: ListView.isCurrentItem
                    focusPolicy: Qt.NoFocus
                    onClicked: rowList.currentIndex = index
                    Accessible.role: Accessible.ListItem
                    Accessible.name: (modelData.destination || modelData.key) + ", " + review.statusOf(modelData)
                    background: Rectangle {
                        color: rowItem.highlighted ? Theme.select : "transparent"
                        Rectangle { visible: rowItem.highlighted; width: 3; height: parent.height; color: Theme.accent }
                    }
                    contentItem: RowLayout {
                        spacing: 10
                        CheckBox {
                            objectName: "settingsImportChoose_" + rowItem.modelData.id
                            padding: 0
                            enabled: rowItem.modelData.selectable
                            opacity: enabled ? 1 : 0.4
                            focusPolicy: Qt.NoFocus
                            function isChosen() { return review.importer.chosenIds.indexOf(rowItem.modelData.id) >= 0 }
                            checked: isChosen()
                            onToggled: {
                                review.importer.setChosen(rowItem.modelData.id, checked)
                                // The click broke the checked binding.
                                checked = Qt.binding(isChosen)
                            }
                            Accessible.name: rowItem.modelData.destination || rowItem.modelData.key
                        }
                        Label {
                            text: rowItem.modelData.destination || rowItem.modelData.key
                            color: rowItem.modelData.selectable ? Theme.text : Theme.muted
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Label {
                            text: rowItem.modelData.value
                            color: rowItem.modelData.selectable ? Theme.text : Theme.muted
                            elide: Text.ElideRight
                            Layout.preferredWidth: 160
                        }
                        Label {
                            text: rowItem.modelData.current
                            color: rowItem.modelData.selectable ? Theme.text : Theme.muted
                            elide: Text.ElideRight
                            Layout.preferredWidth: 160
                        }
                        Item {
                            implicitHeight: 20
                            Layout.preferredWidth: 84
                            Rectangle {
                                objectName: "settingsImportStatus_" + rowItem.modelData.id
                                readonly property string text: review.statusOf(rowItem.modelData)
                                width: statusLabel.implicitWidth + 12
                                height: 20
                                radius: 2
                                color: Theme.panel
                                border.color: rowItem.modelData.selectable ? Theme.accent
                                            : rowItem.modelData.disposition === "unresolved" ? Theme.warning : Theme.line
                                Label {
                                    id: statusLabel
                                    anchors.centerIn: parent
                                    text: parent.text
                                    font: review.window.fontAt(11, rowItem.modelData.selectable ? Font.DemiBold : Font.Normal)
                                    color: rowItem.modelData.selectable ? Theme.text
                                         : rowItem.modelData.disposition === "unresolved" ? Theme.warning : Theme.muted
                                }
                            }
                        }
                    }
                }
            }
        }
        Label {
            anchors.centerIn: parent
            visible: rowList.count === 0
            text: review.conflictsOnly && review.search.length === 0
                  ? qsTr("No conflicts: the legacy settings are the same as yours or need no choice.")
                  : qsTr("Nothing matches.")
            color: Theme.muted
        }
    }
    Rectangle {
        color: Theme.raised
        border.color: Theme.line
        implicitHeight: Math.ceil(detailsMetrics.lineSpacing * 3) + 16
        Layout.fillWidth: true
        FontMetrics { id: detailsMetrics; font: details.font }
        Label {
            id: details
            objectName: "settingsImportDetails"
            x: 10
            y: 8
            width: parent.width - 20
            wrapMode: Text.Wrap
            maximumLineCount: 3
            elide: Text.ElideRight
            textFormat: Text.PlainText
            readonly property var row: review.currentRow
            text: !row ? qsTr("Choose a row to see where it comes from.")
                : [row.source + " › " + row.key + (row.raw.length > 0 ? " = " + row.raw : "")
                   + (row.destination.length > 0 ? "  →  " + row.destination : ""),
                   row.reason,
                   row.paths.length > 0 ? qsTr("Paths not found, kept as they are: %1").arg(row.paths.join(", ")) : ""]
                  .filter(t => t.length > 0).join("\n")
        }
    }
    RowLayout {
        spacing: 16
        LinkButton {
            objectName: "settingsImportProposed"
            iconRole: "restore-default"
            text: qsTr("Proposed choice")
            onClicked: review.importer.chooseAll("proposed", true)
        }
        LinkButton {
            objectName: "settingsImportAllConflicts"
            iconRole: "commit"
            text: qsTr("All conflicts")
            onClicked: review.chooseShown(true)
        }
        LinkButton {
            objectName: "settingsImportNone"
            iconRole: "clear"
            text: qsTr("None")
            onClicked: review.chooseShown(false)
        }
    }
}
