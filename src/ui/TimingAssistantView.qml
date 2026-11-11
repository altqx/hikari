pragma Translator: "HikariSub.TimeShift"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

FocusScope {
    id: view
    required property var app
    required property int shellWindow
    property bool narrow: false
    property var suggestion: ({ ok: false })
    property bool changing: false
    property int accepted: 0
    property int skipped: 0
    property bool finished: false

    readonly property bool ok: suggestion.ok === true && !finished
    readonly property int windowStart: suggestion.windowStart || 0
    readonly property int windowEnd: Math.max(suggestion.windowEnd || 1, windowStart + 1)

    function refresh() {
        suggestion = app.timingSuggestion(shellWindow)
    }
    function restart() {
        accepted = 0
        skipped = 0
        finished = false
        changing = false
        refresh()
    }
    function timeText(ms) {
        const cs = Math.floor(ms / 10) % 100, s = Math.floor(ms / 1000) % 60
        const m = Math.floor(ms / 60000) % 60, h = Math.floor(ms / 3600000)
        return h + ":" + String(m).padStart(2, "0") + ":" + String(s).padStart(2, "0") + "." + String(cs).padStart(2, "0")
    }
    function parseTime(text) {
        const m = /^\s*(\d+):(\d{1,2}):(\d{1,2})(?:[.,](\d{1,2}))?\s*$/.exec(text)
        if (!m)
            return -1
        const cs = m[4] ? parseInt(m[4].padEnd(2, "0")) : 0
        return ((parseInt(m[1]) * 60 + parseInt(m[2])) * 60 + parseInt(m[3])) * 1000 + cs * 10
    }
    function reasonText(reason, frames, lead) {
        switch (reason) {
        case "onKeyframe": return qsTr("Already on a keyframe.")
        case "keyframeBefore": return qsTr("Snapped to the keyframe %n frame(s) earlier.", "", frames)
        case "keyframeAfter": return qsTr("Snapped to the keyframe %n frame(s) later.", "", frames)
        case "linked": return qsTr("Lead-in of %1 ms, linked to the previous line.").arg(lead)
        case "leadIn": return qsTr("Lead-in of %1 ms.").arg(lead)
        case "leadOut": return qsTr("Lead-out of %1 ms.").arg(lead)
        }
        return qsTr("No rule applies.")
    }
    function linkText() {
        const gap = suggestion.startBefore - suggestion.linkedEndBefore
        return gap >= 0 ? qsTr("Linked: it ended %1 ms before this start.").arg(gap)
                        : qsTr("Linked: it overlapped this line by %1 ms.").arg(-gap)
    }
    function step(acceptIt) {
        if (!ok)
            return
        const last = suggestion.index >= suggestion.count
        let done
        if (acceptIt) {
            const start = changing ? parseTime(startEdit.text) : -1
            const end = changing ? parseTime(endEdit.text) : -1
            if (changing && (start < 0 || end < 0))
                return
            done = app.acceptTimingSuggestion(shellWindow, start, end)
            if (done)
                ++accepted
        } else {
            done = app.skipTimingSuggestion(shellWindow) || last
            ++skipped
        }
        changing = false
        finished = last
        refresh()
    }
    function change() {
        if (!ok)
            return
        changing = !changing
        if (changing) {
            startEdit.text = timeText(suggestion.start)
            endEdit.text = timeText(suggestion.end)
            startEdit.forceActiveFocus()
            startEdit.selectAll()
        }
    }
    function ownsKey(event) {
        const enter = event.key === Qt.Key_Return || event.key === Qt.Key_Enter
        const mods = event.modifiers & (Qt.ShiftModifier | Qt.ControlModifier | Qt.AltModifier)
        return (enter && mods === 0) || (mods === Qt.AltModifier && (event.key === Qt.Key_C || event.key === Qt.Key_S))
    }
    function handleKey(event) {
        if (!ownsKey(event))
            return false
        event.accepted = true
        if (event.key === Qt.Key_C)
            change()
        else if (event.key === Qt.Key_S)
            step(false)
        else
            step(true)
        return true
    }
    Keys.onShortcutOverride: event => event.accepted = ownsKey(event)
    Keys.onPressed: event => handleKey(event)

    function xOf(ms, width) {
        return Math.max(0, Math.min(width, (ms - windowStart) / (windowEnd - windowStart) * width))
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        Label {
            objectName: "assistantEmpty"
            visible: !view.ok
            text: view.finished ? qsTr("No more lines. Accepted %1, skipped %2.").arg(view.accepted).arg(view.skipped)
                                : qsTr("Select a line to time.")
            color: Theme.muted
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        RowLayout {
            visible: view.ok
            spacing: 8
            Layout.fillWidth: true
            Label {
                objectName: "assistantPosition"
                text: qsTr("Line %1 of %2").arg(view.suggestion.index || 0).arg(view.suggestion.count || 0)
                font.bold: true
            }
            Label {
                text: view.suggestion.style || ""
                color: Theme.muted
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }
        Label {
            visible: view.ok
            text: view.suggestion.text || ""
            textFormat: Text.PlainText
            elide: Text.ElideRight
            padding: 6
            Layout.fillWidth: true
            background: Rectangle { color: Theme.field; border.color: Theme.line }
        }
        Item {
            id: strip
            objectName: "assistantStrip"
            visible: view.ok
            Layout.fillWidth: true
            Layout.preferredHeight: 34
            Accessible.ignored: true
            Rectangle { anchors.fill: parent; color: Theme.raised; border.color: Theme.line }
            Rectangle {
                x: view.xOf(view.suggestion.startBefore || 0, strip.width)
                width: Math.max(2, view.xOf(view.suggestion.endBefore || 0, strip.width) - x)
                y: 6; height: 8
                color: "transparent"
                border.color: Theme.muted
                border.width: 1.5
            }
            Rectangle {
                x: view.xOf(view.suggestion.start || 0, strip.width)
                width: Math.max(2, view.xOf(view.suggestion.end || 0, strip.width) - x)
                y: 18; height: 8
                color: Theme.accent
            }
            Repeater {
                model: view.suggestion.keyframes || []
                Rectangle {
                    required property int modelData
                    x: view.xOf(modelData, strip.width) - 1
                    width: 2; height: strip.height
                    color: Theme.warning
                }
            }
        }
        GridLayout {
            visible: view.ok
            columns: view.narrow ? 4 : 5
            columnSpacing: 10
            rowSpacing: 6
            Layout.fillWidth: true

            Item { implicitWidth: 1; implicitHeight: 1 }
            Label { text: qsTr("Now"); color: Theme.muted }
            Item { implicitWidth: 1; implicitHeight: 1 }
            Label { text: qsTr("Suggested"); color: Theme.muted }
            Label { visible: !view.narrow; text: qsTr("Why"); color: Theme.muted; Layout.fillWidth: true }

            Label { text: qsTr("Start"); color: Theme.muted }
            Label { text: view.timeText(view.suggestion.startBefore || 0); font.family: "monospace" }
            Label { text: "→"; color: Theme.muted; Accessible.ignored: true }
            TextField {
                id: startEdit
                objectName: "assistantStartEdit"
                visible: view.changing
                font.family: "monospace"
                Accessible.name: qsTr("Suggested start")
                Layout.preferredWidth: 110
            }
            Label {
                objectName: "assistantStart"
                visible: !view.changing
                text: view.timeText(view.suggestion.start || 0)
                font.family: "monospace"
            }
            Label {
                objectName: "assistantStartWhy"
                text: view.reasonText(view.suggestion.startReason, view.suggestion.startFrames, view.suggestion.leadIn)
                wrapMode: Text.WordWrap
                color: view.narrow ? Theme.muted : Theme.text
                Layout.fillWidth: true
                Layout.columnSpan: view.narrow ? 4 : 1
            }

            Label { text: qsTr("End"); color: Theme.muted }
            Label { text: view.timeText(view.suggestion.endBefore || 0); font.family: "monospace" }
            Label { text: "→"; color: Theme.muted; Accessible.ignored: true }
            TextField {
                id: endEdit
                objectName: "assistantEndEdit"
                visible: view.changing
                font.family: "monospace"
                Accessible.name: qsTr("Suggested end")
                Layout.preferredWidth: 110
            }
            Label {
                objectName: "assistantEnd"
                visible: !view.changing
                text: view.timeText(view.suggestion.end || 0)
                font.family: "monospace"
            }
            Label {
                objectName: "assistantEndWhy"
                text: view.reasonText(view.suggestion.endReason, view.suggestion.endFrames, view.suggestion.leadOut)
                wrapMode: Text.WordWrap
                color: view.narrow ? Theme.muted : Theme.text
                Layout.fillWidth: true
                Layout.columnSpan: view.narrow ? 4 : 1
            }

            Label { visible: view.suggestion.linked === true; text: qsTr("Previous end"); color: Theme.muted }
            Label {
                visible: view.suggestion.linked === true
                text: view.timeText(view.suggestion.linkedEndBefore || 0)
                font.family: "monospace"
            }
            Label { visible: view.suggestion.linked === true; text: "→"; color: Theme.muted; Accessible.ignored: true }
            Label {
                visible: view.suggestion.linked === true
                text: view.timeText(view.suggestion.start || 0)
                font.family: "monospace"
            }
            Label {
                objectName: "assistantLinkWhy"
                visible: view.suggestion.linked === true
                text: view.suggestion.linked === true ? view.linkText() : ""
                wrapMode: Text.WordWrap
                color: view.narrow ? Theme.muted : Theme.text
                Layout.fillWidth: true
                Layout.columnSpan: view.narrow ? 4 : 1
            }
        }
        Flow {
            visible: view.ok
            spacing: 8
            Layout.fillWidth: true
            Button {
                objectName: "assistantAccept"
                text: qsTr("Accept")
                highlighted: true
                icon.source: IconTheme.image("commit", IconTheme.normal, IconTheme.accent, false)
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Accept (Enter)")
                onClicked: view.step(true)
            }
            Button {
                objectName: "assistantChange"
                text: view.changing ? qsTr("Done") : qsTr("Change")
                icon.source: IconTheme.image("edit", IconTheme.normal, IconTheme.accent, false)
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Change the suggested times (Alt+C)")
                onClicked: view.change()
            }
            Button {
                objectName: "assistantSkip"
                text: qsTr("Skip")
                icon.source: IconTheme.image("chevron-right", IconTheme.normal, IconTheme.accent, false)
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Skip this line (Alt+S)")
                onClicked: view.step(false)
            }
        }
        Label {
            objectName: "assistantTally"
            visible: view.ok
            text: qsTr("Accepted %1, skipped %2. Each accepted line is one undo step.").arg(view.accepted).arg(view.skipped)
            color: Theme.muted
            font.pixelSize: Math.max(10, Qt.application.font.pixelSize - 1)
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Item { Layout.fillHeight: true }
    }
}
