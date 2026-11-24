pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Export"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

ColumnLayout {
    id: block
    objectName: "exportReport"
    required property var report
    required property string formatName
    required property string fileName
    required property string ranText
    required property DialogWindow host
    signal lineChosen(var row)
    signal copyRequested(string text)

    readonly property var lost: report ? report.lost ?? [] : []
    readonly property int lostLines: report ? report.notes.length : 0
    readonly property font smallFont: host.fontAt(12)
    readonly property font titleFont: host.fontAt(13, Font.DemiBold)
    readonly property font monoFont: {
        const f = host.fontAt(12)
        f.family = "monospace"
        f.features = { "tnum": 1 }
        return f
    }
    readonly property alias list: rows

    function listText() {
        const out = []
        let group = ""
        for (const row of lost) {
            if (row.group !== group) {
                group = row.group
                out.push(group)
            }
            out.push(row.number + "\t" + row.text)
        }
        return out.join("\n")
    }

    spacing: 8

    Rectangle {
        objectName: "exportLostPanel"
        visible: block.lostLines > 0
        Layout.fillWidth: true
        implicitHeight: lostColumn.implicitHeight + 2
        radius: Theme.popupRadius
        color: "transparent"
        border.color: Theme.line
        ColumnLayout {
            id: lostColumn
            anchors.fill: parent
            anchors.margins: 1
            spacing: 0
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: Theme.headerHeight
                color: Theme.raised
                topLeftRadius: Theme.popupRadius - 1
                topRightRadius: Theme.popupRadius - 1
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 4
                    spacing: 8
                    Icon { iconRole: "notice-warning"; size: 16; color: Theme.warning; accentColor: Theme.warning }
                    Label {
                        objectName: "exportSummary"
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        font: block.titleFont
                        text: qsTr("Saved. %n line(s) lost something %1 can’t carry.", "", block.lostLines).arg(block.formatName)
                    }
                    Button {
                        objectName: "exportCopyList"
                        flat: true
                        text: qsTr("Copy list")
                        onClicked: block.copyRequested(block.listText())
                    }
                }
            }
            ListView {
                id: rows
                objectName: "exportNotes"
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, 8 * Theme.gridRowHeight + 2 * Theme.controlHeight)
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: block.lost
                currentIndex: -1
                keyNavigationEnabled: true
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Export report")
                ScrollBar.vertical: ScrollBar {}
                // MUST KILL: clicking stops onCurrentIndexChanged choosing the row a click chooses itself
                property bool clicking: false
                // MUST KILL: ListView makes row 0 current on every model change
                property bool chosen: false
                onModelChanged: chosen = false
                onCurrentIndexChanged: {
                    if (activeFocus)
                        chosen = true
                    if (activeFocus && !clicking && currentIndex >= 0)
                        block.lineChosen(block.lost[currentIndex])
                }
                section.property: "group"
                section.delegate: Label {
                    required property string section
                    width: ListView.view.width
                    height: Theme.controlHeight
                    leftPadding: 12
                    verticalAlignment: Text.AlignBottom
                    bottomPadding: 2
                    font: block.titleFont
                    text: section
                }
                delegate: ItemDelegate {
                    id: row
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    height: Theme.gridRowHeight
                    padding: 0
                    leftPadding: 12
                    rightPadding: 12
                    focusPolicy: Qt.NoFocus
                    Accessible.name: qsTr("Line %1: %2").arg(modelData.number, modelData.text)
                    background: Rectangle {
                        color: row.ListView.isCurrentItem && rows.chosen ? Theme.select : row.hovered ? Theme.hover : "transparent"
                    }
                    contentItem: RowLayout {
                        spacing: 12
                        Label {
                            text: row.modelData.number
                            color: Theme.muted
                            font: block.monoFont
                            horizontalAlignment: Text.AlignRight
                            Layout.minimumWidth: rowNumberWidth.advanceWidth
                        }
                        Label {
                            text: row.modelData.text
                            font: block.smallFont
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                    onClicked: {
                        rows.forceActiveFocus()
                        rows.clicking = true
                        rows.chosen = true
                        rows.currentIndex = index
                        rows.clicking = false
                        block.lineChosen(modelData)
                    }
                }
                Keys.onReturnPressed: if (currentIndex >= 0) block.lineChosen(block.lost[currentIndex])
                Keys.onEnterPressed: if (currentIndex >= 0) block.lineChosen(block.lost[currentIndex])
                TextMetrics { id: rowNumberWidth; font: block.monoFont; text: "00000" }
            }
            Item { Layout.preferredHeight: 4 }
        }
    }

    Rectangle {
        objectName: "exportNothingLost"
        visible: block.report !== null && block.lostLines === 0
        Layout.fillWidth: true
        implicitHeight: Theme.headerHeight
        radius: Theme.radius
        color: Theme.successFill
        border.width: Theme.highContrast ? 1 : 0
        border.color: Theme.success
        Label {
            objectName: "exportNothingLostText"
            anchors.fill: parent
            leftPadding: 12
            rightPadding: 12
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideMiddle
            text: block.report ? qsTr("Saved %1 · %n line(s) · nothing lost", "", block.report.written).arg(block.fileName) : ""
        }
    }

    Label {
        objectName: "exportRanBefore"
        visible: text !== ""
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        color: Theme.muted
        font: block.smallFont
        text: block.ranText
    }
    Label {
        objectName: "exportSkipped"
        visible: text !== ""
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        color: Theme.muted
        font: block.smallFont
        text: !block.report ? "" : [
            block.report.comments > 0 ? qsTr("%n comment line(s) skipped.", "", block.report.comments) : "",
            block.report.empty > 0 ? qsTr("%n line(s) with nothing to show skipped.", "", block.report.empty) : ""
        ].filter(s => s.length > 0).join(" ")
    }
    Label {
        objectName: "exportSrv3Saved"
        visible: text !== ""
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        color: Theme.muted
        font: block.smallFont
        text: block.report && (block.report.srv3Styles ?? 0) > 0
              ? qsTr("Style options for %n style(s) saved in the subtitle file.", "", block.report.srv3Styles) : ""
    }
}
