pragma Translator: "HikariSub.InlineAi"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

ColumnLayout {
    id: bar
    objectName: "inlineAiBar"
    required property var controller
    signal settingsRequested()
    spacing: 0
    readonly property var progress: controller ? controller.progress : ({})
    readonly property var notice: controller ? controller.notice : ({})
    readonly property var results: controller ? controller.results : []
    readonly property bool running: !!progress.running
    readonly property bool noticed: !running && !!notice.end
    visible: !!controller && controller.on && (running || noticed)

    function money(dollars) {
        return "$" + Number(dollars).toLocaleString(Qt.locale(), "f", dollars < 0.1 ? 3 : 2)
    }
    function doing(p) {
        const n = p.total || 0
        switch (p.action) {
        case "rewrite": return qsTr("Rewriting %n line(s)", "", n)
        case "shorten": return qsTr("Shortening %n line(s)", "", n)
        case "spelling": return qsTr("Fixing spelling in %n line(s)", "", n)
        case "translate": return qsTr("Translating %n line(s)", "", n)
        default: return qsTr("%1: %n line(s)", "", n).arg(p.label || "")
        }
    }
    function rows(list) {
        return list.join(", ")
    }
    function noticeText(n) {
        const parts = []
        if (n.end === "noModel")
            parts.push(qsTr("No model for this action. Add one in Settings › AI."))
        else if (n.end === "dailyCap")
            parts.push(qsTr("Stopped at today’s cost cap."))
        else if (n.end === "stopped")
            parts.push(qsTr("Stopped."))
        const failed = n.failed || []
        const tags = failed.filter(f => f.why === "tag").map(f => f.row)
        const empty = failed.filter(f => f.why === "empty").map(f => f.row)
        const errors = failed.filter(f => f.why === "failed")
        if (tags.length > 0)
            parts.push(qsTr("Not changed, because the answer dropped a tag: line %1.", "", tags.length).arg(rows(tags)))
        if (empty.length > 0)
            parts.push(qsTr("Not changed, because the answer was empty: line %1.", "", empty.length).arg(rows(empty)))
        if (errors.length > 0)
            parts.push(qsTr("Line %1 failed: %2", "", errors.length)
                           .arg(rows(errors.map(f => f.row))).arg(errors[0].detail || ""))
        if ((n.same || []).length > 0)
            parts.push(qsTr("Came back the same: line %1.", "", n.same.length).arg(rows(n.same)))
        if ((n.edited || []).length > 0)
            parts.push(qsTr("You edited line %1 first, so it was left as it is.", "", n.edited.length).arg(rows(n.edited)))
        if ((n.review || []).length > 0)
            parts.push(n.unconfirmed
                       ? qsTr("The reviewer didn’t approve line %1, so it’s marked Unconfirmed.", "", n.review.length).arg(rows(n.review))
                       : qsTr("The reviewer didn’t approve line %1. Check it before you use it.", "", n.review.length).arg(rows(n.review)))
        if (n.modelsWaitForTrust)
            parts.push(qsTr("This project’s translator and reviewer models are used once you trust its folder; your own ran instead."))
        if ((n.skipped || 0) > 0)
            parts.push(qsTr("%n line(s) weren’t sent.", "", n.skipped))
        return parts.join(" ")
    }

    Pane {
        objectName: "inlineAiProgress"
        visible: bar.running
        Layout.fillWidth: true
        padding: 4
        leftPadding: 8
        background: Rectangle {
            color: Theme.select
            border.color: Theme.accent
            radius: 2
        }
        contentItem: ColumnLayout {
            spacing: 3
            RowLayout {
                spacing: 8
                Icon { iconRole: "ai-assistant"; size: 16 }
                Label {
                    objectName: "inlineAiProgressText"
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                    text: bar.doing(bar.progress) + " · " + qsTr("%1 of %2 done").arg(bar.progress.done || 0).arg(bar.progress.total || 0)
                          + (bar.progress.costKnown ? " · " + bar.money(bar.progress.cost || 0) : "")
                }
                Label {
                    color: Theme.muted
                    textFormat: Text.PlainText
                    text: bar.progress.model || ""
                }
                IconTextButton {
                    id: stop
                    objectName: "inlineAiStop"
                    iconRole: "media-stop"
                    text: qsTr("Stop")
                    onClicked: bar.controller.stop()
                }
            }
            ProgressBar {
                Layout.fillWidth: true
                from: 0
                to: Math.max(1, bar.progress.total || 0)
                value: bar.progress.done || 0
                Accessible.name: qsTr("Progress")
            }
        }
        Keys.onEscapePressed: bar.controller.stop()
    }

    InlineBar {
        objectName: "inlineAiNotice"
        visible: bar.noticed
        Layout.fillWidth: true
        severity: (bar.notice.failed || []).length > 0 || (bar.notice.review || []).length > 0
                  || bar.notice.end === "noModel" ? "warning" : "info"
        text: bar.noticeText(bar.notice)
        onClosed: bar.controller.dismiss()
        Button {
            objectName: "inlineAiRetry"
            visible: (bar.notice.failed || []).length > 0
            text: qsTr("Retry")
            onClicked: bar.controller.retry()
        }
        Button {
            objectName: "inlineAiOpenSettings"
            visible: bar.notice.end === "noModel"
            text: qsTr("Open Settings › AI")
            onClicked: bar.settingsRequested()
        }
    }

    Popup {
        id: resultsPopup
        objectName: "inlineAiResults"
        parent: bar.parent
        x: Math.max(0, (parent ? parent.width - width : 0) / 2)
        y: 8
        width: Math.min(640, parent ? parent.width - 16 : 640)
        height: Math.min(implicitHeight, parent ? parent.height - 16 : implicitHeight)
        padding: 0
        modal: false
        closePolicy: Popup.CloseOnEscape
        property var ticked: ({})
        readonly property int tickedCount: bar.results.filter(r => !r.stale && ticked[r.key] !== false).length
        onClosed: if (bar.results.length > 0) bar.controller.discard()
        Connections {
            target: bar
            function onResultsChanged() {
                if (bar.results.length === 0) {
                    resultsPopup.close()
                } else if (!resultsPopup.opened) {
                    resultsPopup.ticked = ({})
                    resultsPopup.open()
                }
            }
        }
        background: Rectangle {
            color: Theme.panel
            border.color: Theme.line
            radius: 4
        }
        contentItem: ColumnLayout {
            spacing: 0
            Accessible.role: Accessible.Dialog
            Accessible.name: resultsTitle.text
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 10
                spacing: 8
                Icon { iconRole: "ai-assistant"; size: 16 }
                Label {
                    id: resultsTitle
                    objectName: "inlineAiResultsTitle"
                    font.bold: true
                    textFormat: Text.PlainText
                    text: qsTr("%1 · %n result(s)", "", bar.results.length).arg(bar.progress.label || "")
                }
                Label {
                    color: Theme.muted
                    text: qsTr("Manual")
                }
                Item { Layout.fillWidth: true }
                IconToolButton {
                    iconRole: "window-close"
                    text: qsTr("Discard")
                    onClicked: bar.controller.discard()
                }
            }
            ListView {
                id: resultList
                objectName: "inlineAiResultList"
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, 320)
                Layout.leftMargin: 10
                Layout.rightMargin: 10
                clip: true
                model: bar.results
                ScrollBar.vertical: ScrollBar {}
                delegate: Rectangle {
                    id: row
                    required property var modelData
                    required property int index
                    width: resultList.width
                    height: rowLayout.implicitHeight + 10
                    color: rowMouse.containsMouse ? Theme.select : "transparent"
                    border.color: Theme.line
                    MouseArea {
                        id: rowMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: bar.controller.show(row.modelData.key)
                    }
                    RowLayout {
                        id: rowLayout
                        anchors.fill: parent
                        anchors.margins: 5
                        spacing: 8
                        CheckBox {
                            objectName: "inlineAiTick" + row.index
                            enabled: !row.modelData.stale
                            checked: !row.modelData.stale && resultsPopup.ticked[row.modelData.key] !== false
                            Accessible.name: qsTr("Apply line %1").arg(row.modelData.row)
                            onToggled: {
                                const t = Object.assign({}, resultsPopup.ticked)
                                t[row.modelData.key] = checked
                                resultsPopup.ticked = t
                            }
                        }
                        Label {
                            color: Theme.muted
                            text: row.modelData.row
                            Layout.preferredWidth: 32
                            horizontalAlignment: Text.AlignRight
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                textFormat: row.modelData.stale ? Text.PlainText : Text.RichText
                                color: row.modelData.stale ? Theme.muted : Theme.text
                                text: row.modelData.stale ? qsTr("Out of date: you edited line %1 after it was sent.").arg(row.modelData.row)
                                                          : row.modelData.diff
                            }
                            Label {
                                objectName: "inlineAiNeedsReview" + row.index
                                visible: !row.modelData.stale && !!row.modelData.needsReview
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                textFormat: Text.PlainText
                                color: Theme.muted
                                text: qsTr("Not approved by the reviewer: %1").arg((row.modelData.issues || []).join(" "))
                            }
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 10
                spacing: 8
                Label {
                    Layout.fillWidth: true
                    color: Theme.muted
                    elide: Text.ElideRight
                    text: qsTr("%1 of %2 ticked · click a row to select it in the Grid").arg(resultsPopup.tickedCount).arg(bar.results.length)
                }
                Button {
                    objectName: "inlineAiDiscard"
                    text: qsTr("Discard")
                    onClicked: bar.controller.discard()
                }
                PrimaryButton {
                    objectName: "inlineAiApply"
                    text: qsTr("Apply %1").arg(resultsPopup.tickedCount)
                    enabled: resultsPopup.tickedCount > 0
                    onClicked: {
                        const keys = bar.results.filter(r => !r.stale && resultsPopup.ticked[r.key] !== false).map(r => r.key)
                        resultsPopup.ticked = ({})
                        bar.controller.apply(keys)
                    }
                }
            }
        }
    }
}
