pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Automation"
import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import QtQuick.Layouts
import Hikari.Ui

FocusScope {
    id: card
    objectName: "automationCard"
    property var automation: null
    signal openRun(int runId)
    signal focusReturned(Item panel)

    readonly property bool reachable: visible && !notice
    property Item returnTo: null
    function enter(from) {
        returnTo = from
        const first = [logToggle, openInPanel, cancelButton, forceStop, closeButton].find(b => b.visible && b.enabled)
        if (first)
            first.forceActiveFocus(Qt.TabFocusReason)
    }
    function leave() {
        returnTo = null
    }
    function giveBack() {
        const panel = returnTo
        returnTo = null
        focusReturned(panel)
    }
    Keys.onEscapePressed: giveBack()
    onReachableChanged: if (!reachable && activeFocus) giveBack()

    readonly property string phase: automation ? automation.cardState : ""
    readonly property bool notice: phase === "notice"
    readonly property bool running: phase === "notice" || phase === "running"
    readonly property bool ended: phase.length > 0 && !running
    property bool logOpen: false
    property bool logChosen: false
    readonly property bool shown: phase.length > 0
    visible: shown
    implicitWidth: 360
    implicitHeight: notice ? pill.implicitHeight : body.implicitHeight + 18
    activeFocusOnTab: false

    Accessible.role: Accessible.Grouping
    Accessible.name: title.text

    AutomationRunLines {
        id: lines
        runs: card.automation ? card.automation.runs : null
        run: card.automation ? card.automation.runId : 0
        onRunChanged: {
            card.logOpen = false
            card.logChosen = false
        }
        onCountChanged: if (count > 0 && !card.logChosen && card.running) card.logOpen = true
    }
    onPhaseChanged: if (phase === "problems" && !logChosen) logOpen = true

    function iconOf(state) {
        return ({ applied: "run-applied", problems: "notice-warning", cancelled: "run-cancelled",
                  failed: "notice-error" })[state] || "run-busy"
    }
    function colourOf(state) {
        return state === "failed" ? Theme.danger : state === "problems" || state === "cancelled" ? Theme.warning
                                                                                                 : IconTheme.normal
    }
    function titleOf(state) {
        const name = automation ? automation.runTitle : ""
        return state === "problems" ? qsTr("%1 named problems").arg(name)
             : state === "cancelled" ? qsTr("%1 cancelled").arg(name)
             : state === "failed" ? qsTr("%1 failed").arg(name) : name
    }
    function endText(state) {
        if (!automation)
            return ""
        if (state === "applied")
            // With no English catalog, %n would read "1 lines".
            return lines.total === 1 ? qsTr("Applied. The script wrote 1 line to its log.")
                                     : qsTr("Applied. The script wrote %n lines to its log.", "", lines.total)
        if (state === "problems")
            return automation.problems === 1 ? qsTr("Applied. The templates have 1 problem.")
                                             : qsTr("Applied. The templates have %n problems.", "", automation.problems)
        const parts = automation.cardText.split("\n").filter(l => l.trim().length > 0)
        return parts.find(l => !/^File ".*", line -?\d+/.test(l)) || parts[0] || ""
    }

    component RunBar: T.ProgressBar {
        id: bar
        from: 0
        to: 100
        implicitHeight: 6
        implicitWidth: 60
        background: Rectangle {
            color: Theme.raised
            border.color: Theme.line
            radius: 1
        }
        contentItem: Item {
            clip: true
            Rectangle {
                id: fill
                height: parent.height
                color: Theme.accent
                width: bar.indeterminate ? parent.width * 0.3 : parent.width * bar.position
                XAnimator on x {
                    running: bar.indeterminate && bar.visible
                    from: -fill.width
                    to: bar.width
                    duration: 1200
                    loops: Animation.Infinite
                }
                x: 0
            }
        }
    }
    component BusyIcon: Icon {
        id: busyIcon
        RotationAnimator on rotation {
            running: busyIcon.visible && busyIcon.iconRole === "run-busy"
            from: 0
            to: 360
            duration: 1200
            loops: Animation.Infinite
        }
    }

    Item {
        id: pill
        objectName: "automationNotice"
        visible: card.notice
        anchors.right: parent.right
        width: Math.min(card.width, pillRow.implicitWidth + 24)
        height: implicitHeight
        implicitHeight: 36
        PopoverBackground { anchors.fill: parent }
        RowLayout {
            id: pillRow
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 10
            BusyIcon { iconRole: "run-busy" }
            Label {
                objectName: "automationNoticeTitle"
                text: card.automation ? card.automation.runTitle : ""
                textFormat: Text.PlainText
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                Layout.maximumWidth: 200
            }
            RunBar {
                Layout.preferredWidth: 100
                indeterminate: !card.automation || !card.automation.progressKnown
                value: card.automation ? card.automation.progress : 0
            }
        }
    }

    PopoverBackground {
        anchors.fill: parent
        visible: !card.notice
    }
    ColumnLayout {
        id: body
        visible: !card.notice
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: 12
        anchors.rightMargin: 8
        anchors.topMargin: 10
        spacing: 6
        RowLayout {
            spacing: 10
            Layout.fillWidth: true
            BusyIcon {
                iconRole: card.iconOf(card.phase)
                color: card.colourOf(card.phase)
                accentColor: card.phase === "applied" || card.running ? IconTheme.accent : color
            }
            Label {
                id: title
                objectName: "automationCardTitle"
                text: card.titleOf(card.phase)
                textFormat: Text.PlainText
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Label {
                visible: card.running && card.automation !== null && card.automation.progressKnown
                text: card.automation ? Math.round(card.automation.progress) + " %" : ""
                color: Theme.muted
                font.features: { "tnum": 1 }
                Layout.rightMargin: 4
            }
            IconToolButton {
                id: closeButton
                objectName: "automationCardClose"
                visible: card.ended
                iconRole: "tab-close"
                iconSize: 14
                text: qsTr("Close")
                onClicked: card.automation.dismissCard()
            }
        }
        Label {
            objectName: "automationCardTask"
            visible: text.length > 0
            text: !card.automation ? ""
                  : card.ended ? card.endText(card.phase)
                  : card.automation.cancelling ? qsTr("Cancelling…") : card.automation.task
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            maximumLineCount: 3
            elide: Text.ElideRight
            Layout.fillWidth: true
            Layout.leftMargin: 26
        }
        RunBar {
            objectName: "automationCardBar"
            visible: card.running
            indeterminate: !card.automation || !card.automation.progressKnown
            value: card.automation ? card.automation.progress : 0
            Layout.fillWidth: true
            Layout.leftMargin: 26
            Layout.rightMargin: 4
            Layout.topMargin: 2
            Accessible.name: qsTr("Progress")
        }
        Label {
            visible: card.running && text.length > 0
            text: !card.automation ? ""
                  : card.automation.cancelling && card.automation.forceStopOffered
                    ? qsTr("The script has not stopped yet. Force stop ends its helper; nothing is changed.")
                  : card.automation.runs.documentOf(card.automation.runId).length > 0
                    ? qsTr("%1 is read-only until the macro ends").arg(card.automation.runs.documentOf(card.automation.runId))
                    : ""
            color: Theme.muted
            wrapMode: Text.Wrap
            font.pixelSize: Math.max(10, Qt.application.font.pixelSize - 1)
            Layout.fillWidth: true
            Layout.leftMargin: 26
        }
        Rectangle {
            visible: card.logOpen && lines.count > 0
            color: Theme.field
            border.color: Theme.line
            Layout.fillWidth: true
            Layout.leftMargin: 26
            Layout.rightMargin: 4
            Layout.preferredHeight: Math.min(log.contentHeight, 6 * logMetrics.height) + 8
            ListView {
                id: log
                objectName: "automationCardLog"
                anchors.fill: parent
                anchors.margins: 4
                clip: true
                model: lines
                boundsBehavior: Flickable.StopAtBounds
                property bool follow: true
                onMovementEnded: follow = atYEnd
                onCountChanged: if (follow) Qt.callLater(positionViewAtEnd)
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Log")
                ScrollBar.vertical: ScrollBar {}
                delegate: RunLogLine {
                    width: ListView.view.width
                    font: logMetrics.font
                }
            }
            FontMetrics {
                id: logMetrics
                font.family: "monospace"
                font.pixelSize: Math.max(10, Qt.application.font.pixelSize - 1)
            }
        }
        RowLayout {
            spacing: 6
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.topMargin: 2
            Button {
                id: logToggle
                objectName: "automationCardLogToggle"
                visible: lines.total > 0
                flat: true
                icon.source: IconTheme.image(card.logOpen ? "chevron-down" : "chevron-right", IconTheme.normal,
                                             IconTheme.accent, false)
                icon.color: "transparent"
                icon.width: 14
                icon.height: 14
                text: lines.total === 1 ? qsTr("Log · 1 line") : qsTr("Log · %n lines", "", lines.total)
                Accessible.name: card.logOpen ? qsTr("Fold the log") : qsTr("Show the log")
                onClicked: {
                    card.logChosen = true
                    card.logOpen = !card.logOpen
                }
            }
            Item { Layout.fillWidth: true }
            IconTextButton {
                iconRole: "automation-manager"
                id: openInPanel
                objectName: "automationOpenRun"
                visible: card.ended
                flat: true
                text: qsTr("Open in panel")
                onClicked: card.openRun(card.automation.runId)
            }
            DangerButton {
                id: forceStop
                objectName: "automationForceStop"
                visible: card.running && card.automation !== null && card.automation.forceStopOffered
                text: qsTr("Force stop")
                onClicked: card.automation.forceStopRun()
            }
            IconTextButton {
                iconRole: "run-cancelled"
                id: cancelButton
                objectName: "automationCancel"
                visible: card.running
                enabled: card.automation !== null && !card.automation.cancelling
                text: card.automation && card.automation.cancelling ? qsTr("Cancelling…") : qsTr("Cancel")
                onClicked: card.automation.cancelRun()
            }
        }
    }
}
