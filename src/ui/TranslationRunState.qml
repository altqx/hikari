pragma Translator: "HikariSub.Translation"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

ColumnLayout {
    id: runState
    objectName: "translationRunState"
    required property InlineAiBar bar
    signal providersRequested()
    signal detailsRequested()
    signal failureShown(string text)
    readonly property bool translating: bar.progress.action === "translate"
    readonly property bool running: translating && bar.running
    readonly property bool noticed: translating && bar.noticed
    readonly property bool failed: noticed && ((bar.notice.failed || []).length > 0
                                               || bar.notice.end === "noModel" || bar.notice.end === "dailyCap")
    readonly property font smallFont: TypeRoles.sized(fontSource.font, 12, Font.Normal)
    visible: !!bar.controller && bar.controller.on && (running || noticed)
    spacing: 0

    readonly property string failure: qsTr("AI translation stopped.") + " " + bar.noticeText(bar.notice)
    onFailedChanged: if (failed) failureShown(failure)

    Label {
        id: fontSource
        visible: false
    }
    Rectangle {
        objectName: "translationRunTrack"
        visible: runState.running
        color: Theme.line
        implicitHeight: 2
        Layout.fillWidth: true
        Rectangle {
            objectName: "translationRunLine"
            color: Theme.accent
            height: parent.height
            width: parent.width * Math.min(1, (runState.bar.progress.done || 0) / Math.max(1, runState.bar.progress.total || 0))
        }
    }
    RowLayout {
        objectName: "translationRunRow"
        visible: runState.running
        spacing: 8
        Layout.fillWidth: true
        Layout.leftMargin: 8
        Layout.rightMargin: 4
        Layout.preferredHeight: Theme.headerHeight
        Label {
            objectName: "translationRunText"
            text: qsTr("AI translation · %1 of %n line(s)", "", runState.bar.progress.total || 0).arg(runState.bar.progress.done || 0)
            textFormat: Text.PlainText
            font: { const f = runState.smallFont; f.features = { "tnum": 1 }; return f }
            color: Theme.muted
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
        Button {
            objectName: "translationRunStop"
            text: qsTranslate("HikariSub.InlineAi", "Stop")
            flat: true
            implicitHeight: Theme.controlHeight
            topPadding: 0
            bottomPadding: 0
            onClicked: runState.bar.controller.stop()
        }
    }
    RowLayout {
        objectName: "translationRunNotice"
        visible: runState.noticed
        spacing: 8
        Layout.fillWidth: true
        Layout.leftMargin: 8
        Layout.rightMargin: 4
        Layout.topMargin: 4
        Layout.bottomMargin: 4
        Icon {
            iconRole: runState.failed || runState.bar.warns ? "notice-warning" : "notice-info"
            color: runState.failed ? Theme.danger : runState.bar.warns ? Theme.warning : Theme.muted
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 4
        }
        Label {
            objectName: "translationRunNoticeText"
            text: runState.failed ? runState.failure : runState.bar.noticeText(runState.bar.notice)
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
        }
        Button {
            objectName: "translationRunProviders"
            visible: runState.failed
            text: qsTr("Providers…")
            flat: true
            implicitHeight: Theme.controlHeight
            topPadding: 0
            bottomPadding: 0
            Layout.alignment: Qt.AlignTop
            onClicked: runState.providersRequested()
        }
        Button {
            objectName: "translationRunDetails"
            visible: runState.failed
            text: qsTr("Details")
            flat: true
            implicitHeight: Theme.controlHeight
            topPadding: 0
            bottomPadding: 0
            Layout.alignment: Qt.AlignTop
            onClicked: runState.detailsRequested()
        }
        IconToolButton {
            objectName: "translationRunDismiss"
            iconRole: "window-close"
            text: qsTr("Dismiss")
            Layout.alignment: Qt.AlignTop
            onClicked: runState.bar.controller.dismiss()
        }
    }
}
