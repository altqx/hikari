pragma Translator: "HikariSub.TimeShift"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

ColumnLayout {
    id: preview
    required property string tool
    property var plan: ({})
    property string message
    readonly property bool postprocessor: tool === "postprocessor"
    readonly property int changed: plan.changed || 0
    readonly property string problem: plan.problem || ""
    readonly property string stepName: postprocessor ? qsTr("Timing postprocessor") : qsTr("Shift times")
    readonly property string applyText: changed === 0 ? (postprocessor ? qsTr("Apply") : qsTr("Shift"))
                                      : postprocessor ? qsTr("Apply to %n line(s)", "", changed)
                                                      : qsTr("Shift %n line(s)", "", changed)
    readonly property string status: {
        if (problem !== "")
            return problem
        if (changed === 0)
            return qsTr("No line changes")
        if (!postprocessor)
            return qsTr("Changes %n line(s)", "", changed)
        const counts = plan.ruleCounts || {}
        const parts = [qsTr("Changes %1 of %n line(s)", "", plan.total || 0).arg(changed)]
        if (counts.leadIn)
            parts.push(qsTr("%n lead-in", "", counts.leadIn))
        if (counts.leadOut)
            parts.push(qsTr("%n lead-out", "", counts.leadOut))
        if (counts.gapClosed)
            parts.push(qsTr("%n gap(s) closed", "", counts.gapClosed))
        if (counts.snapped)
            parts.push(qsTr("%n snapped", "", counts.snapped))
        return parts.join(" · ")
    }
    signal apply()
    spacing: 0

    function timeText(ms) {
        const cs = Math.floor(ms / 10) % 100, s = Math.floor(ms / 1000) % 60
        const m = Math.floor(ms / 60000) % 60, h = Math.floor(ms / 3600000)
        return h + ":" + String(m).padStart(2, "0") + ":" + String(s).padStart(2, "0") + "." + String(cs).padStart(2, "0")
    }
    function reasonText(rules) {
        const words = { leadIn: qsTr("lead-in"), leadOut: qsTr("lead-out"), gapClosed: qsTr("gap closed"),
                        snappedStart: qsTr("snapped start"), snappedEnd: qsTr("snapped end") }
        return rules.map(r => words[r]).join(" · ")
    }
    onStatusChanged: statusLabel.Accessible.announce(status)

    Rectangle {
        color: Theme.line
        implicitHeight: 1
        Layout.fillWidth: true
    }
    RowLayout {
        spacing: 8
        Layout.fillWidth: true
        Layout.margins: 8
        Label {
            id: statusLabel
            objectName: preview.tool + "PreviewStatus"
            text: preview.status
            font.bold: true
            color: preview.problem !== "" ? Theme.danger : Theme.text
            wrapMode: Text.WordWrap
            textFormat: Text.PlainText
            Layout.fillWidth: true
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
        Label {
            objectName: "shiftPreviewAmount"
            visible: !preview.postprocessor && preview.changed > 0 && (preview.plan.amount || 0) !== 0
            readonly property int amount: preview.plan.amount || 0
            text: (amount < 0 ? "−" : "+") + (preview.plan.frames ? qsTr("%n frame(s)", "", Math.abs(amount))
                                                                  : preview.timeText(Math.abs(amount)))
            color: Theme.muted
        }
    }
    ListView {
        id: rows
        objectName: preview.tool + "PreviewRows"
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: 48
        Layout.leftMargin: 8
        Layout.rightMargin: 8
        clip: true
        visible: preview.problem === "" && count > 0
        model: preview.problem === "" ? (preview.plan.rows || []) : []
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        Accessible.role: Accessible.List
        Accessible.name: qsTr("Preview")
        delegate: RowLayout {
            id: row
            required property var modelData
            readonly property string reason: preview.postprocessor
                ? (modelData.changed ? preview.reasonText(modelData.rules) : qsTr("no change"))
                : modelData.text
            width: ListView.view.width
            spacing: 8
            Accessible.role: Accessible.ListItem
            Accessible.name: qsTr("Line %1, start %2 to %3, end %4 to %5, %6")
                .arg(modelData.line, preview.timeText(modelData.startBefore), preview.timeText(modelData.start),
                     preview.timeText(modelData.endBefore), preview.timeText(modelData.end), reason)
            Label {
                text: row.modelData.line
                color: Theme.muted
                horizontalAlignment: Text.AlignRight
                Layout.preferredWidth: 36
            }
            Label {
                text: row.modelData.start !== row.modelData.startBefore
                      ? preview.timeText(row.modelData.startBefore) + " → " + preview.timeText(row.modelData.start)
                      : preview.timeText(row.modelData.start)
                font.family: "monospace"
            }
            Label {
                text: row.modelData.end !== row.modelData.endBefore
                      ? preview.timeText(row.modelData.endBefore) + " → " + preview.timeText(row.modelData.end)
                      : preview.timeText(row.modelData.end)
                font.family: "monospace"
            }
            Label {
                text: row.reason
                textFormat: Text.PlainText
                elide: Text.ElideRight
                color: preview.postprocessor ? Theme.muted : Theme.text
                Layout.fillWidth: true
            }
        }
    }
    Label {
        objectName: preview.tool + "PreviewMore"
        visible: preview.problem === "" && (preview.plan.more || 0) > 0
        text: qsTr("%n more line(s) change.", "", preview.plan.more || 0)
        color: Theme.muted
        Layout.margins: 8
    }
    Label {
        objectName: preview.tool + "Message"
        visible: preview.message.length > 0
        text: preview.message
        color: Theme.danger
        wrapMode: Text.WordWrap
        textFormat: Text.PlainText
        Layout.fillWidth: true
        Layout.leftMargin: 8
        Layout.rightMargin: 8
        Accessible.role: Accessible.AlertMessage
    }
    RowLayout {
        spacing: 8
        Layout.fillWidth: true
        Layout.margins: 8
        Label {
            text: qsTr("One undo step: “%1”").arg(preview.stepName)
            color: Theme.muted
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        PrimaryButton {
            objectName: preview.tool + "Apply"
            text: preview.applyText
            enabled: preview.problem === "" && preview.changed > 0
            onClicked: preview.apply()
        }
    }
}
