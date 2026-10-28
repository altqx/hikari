pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Reports"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

// CR1 (#452, CR1-mockups, approved 2026-10-28): a crash or freeze report,
// on the dialog frame, modeless. Built on this machine and shown in full
// before anything goes (CrashReports):
// * "start": the report the last run left, at the first start after it
//   crashed or was closed frozen, with PU1's crash offer (Restore session,
//   Autosaves…) as Your work; closing the window keeps that offer;
// * "freeze": a freeze the window came back from (the notice's Report…);
// * "sent": the last report "Always send crash reports" sent, read only.
// Send report sends it once, usage data on or off; Open GitHub issue opens
// a Bug issue with the same text (the note as What happened); Don't send,
// Esc and × drop it and send nothing. The note goes in as typed.
DialogWindow {
    id: report
    objectName: "crashReport"
    required property var reports // CrashReports
    property var app: null // copyText
    property StartupOffer offer: null
    // "start", "freeze" or "sent".
    property string source: "start"
    // "crash" or "freeze".
    property string kind: "crash"
    property bool expanded: false
    readonly property bool sent: source === "sent"
    readonly property bool recovery: source === "start" && offer !== null && offer.kind === "crash"
    readonly property string reportText: sent ? reports.sentText() : reports.text(source, note.text)
    // A Send report or Open GitHub issue answered it (closing drops it).
    property bool answered: false
    // The note of the last Send report (the failure notice's Try again).
    property string lastNote: ""
    signal autosavesRequested()
    // Open GitHub issue: too long for a link, the report went to the
    // clipboard and the blank form opened.
    signal linkTooLong()

    dialogId: "crashReport"
    title: sent ? qsTr("Crash report")
         : kind === "crash" ? qsTr("HikariSub closed unexpectedly")
         : source === "freeze" ? qsTr("Report a freeze") : qsTr("HikariSub stopped responding")
    defaultButton: sent ? closeButton : sendButton.visible ? sendButton : gitHubButton
    initialFocus: sent ? closeButton : note

    function showReport(from, reportKind) {
        source = from
        kind = reportKind || (from === "freeze" ? "freeze" : reports.pending)
        note.text = ""
        expanded = from === "sent"
        answered = false
        open()
    }
    // "today at 14:32", "yesterday at 14:32", or the date and time.
    function whenText(when) {
        if (!when || isNaN(when.getTime()))
            return ""
        const time = when.toLocaleTimeString(Qt.locale(), Locale.ShortFormat)
        const day = new Date(when.getFullYear(), when.getMonth(), when.getDate())
        const now = new Date()
        const today = new Date(now.getFullYear(), now.getMonth(), now.getDate())
        const days = Math.round((today - day) / 86400000)
        if (days === 0)
            return qsTr("today at %1").arg(time)
        if (days === 1)
            return qsTr("yesterday at %1").arg(time)
        return qsTr("on %1 at %2").arg(when.toLocaleDateString(Qt.locale(), Locale.ShortFormat), time)
    }
    function answer() {
        answered = true
        accept()
    }
    onRejected: if (!sent && !answered) reports.dismiss(source)

    RowLayout {
        spacing: 14
        Layout.preferredWidth: 572
        Icon {
            iconRole: report.kind === "crash" ? "notice-error" : "notice-warning"
            color: report.kind === "crash" ? Theme.danger : Theme.warning
            accentColor: color
            size: 32
            Layout.alignment: Qt.AlignTop
        }
        ColumnLayout {
            spacing: 4
            Layout.fillWidth: true
            Label {
                objectName: "crashReportHeadline"
                readonly property string when: report.whenText(report.reports.when(report.source))
                readonly property int seconds: report.reports.seconds(report.source)
                text: report.sent ? qsTr("A crash report was sent.")
                    : report.kind === "crash" ? qsTr("HikariSub crashed %1.").arg(when)
                    : report.source === "freeze" ? qsTr("HikariSub stopped responding for %n seconds %1.", "", seconds).arg(when)
                    : qsTr("HikariSub stopped responding %1 and was closed.").arg(when)
                font: report.fontAt(15, Font.DemiBold)
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Label {
                text: report.sent ? qsTr("“Always send crash reports” is on. This is what was sent.")
                    : report.kind === "crash" ? qsTr("A report helps find the cause. You can read all of it before it goes.")
                    : report.source === "freeze" ? qsTr("It works again. A report shows where it was stuck.")
                    : qsTr("It froze for 10 seconds or more. A report shows where it was stuck.")
                color: Theme.muted
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
    }

    // PU1's crash offer, unchanged.
    SectionHead {
        visible: report.recovery
        title: qsTr("Your work")
    }
    RowLayout {
        visible: report.recovery
        spacing: 10
        Layout.fillWidth: true
        Icon { iconRole: "last-session"; Layout.alignment: Qt.AlignTop }
        Label {
            objectName: "crashReportWork"
            text: report.offer ? report.offer.detail : ""
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Button {
            objectName: "crashReportRestore"
            visible: report.offer !== null && report.offer.restorable
            text: qsTranslate("HikariSub.Session", "Restore session")
            Layout.alignment: Qt.AlignTop
            onClicked: report.offer.restore()
        }
        Button {
            objectName: "crashReportAutosaves"
            visible: report.offer !== null && report.offer.autosaveCount > 0
            text: qsTranslate("HikariSub.Session", "Autosaves…")
            Layout.alignment: Qt.AlignTop
            onClicked: report.autosavesRequested()
        }
    }

    SectionHead {
        visible: !report.sent
        title: qsTr("Report")
        note: qsTr("Nothing is sent until you press Send report")
    }
    TextArea {
        id: note
        objectName: "crashReportNote"
        visible: !report.sent
        placeholderText: report.kind === "crash" ? qsTr("What were you doing when it closed? (optional)")
                                                 : qsTr("What were you doing when it froze? (optional)")
        wrapMode: TextEdit.Wrap
        Accessible.name: qsTr("Note")
        Layout.fillWidth: true
        Layout.preferredHeight: 56
    }
    FormSection {
        objectName: "crashReportContents"
        ruled: false
        title: qsTr("What the report contains")
        summary: report.kind === "crash" ? qsTr("crash record, system, settings, last 200 log lines")
                                         : qsTr("freeze record, system, settings, last 200 log lines")
        expanded: report.expanded
        onToggled: on => report.expanded = on
        Item {
            implicitHeight: 220
            Layout.fillWidth: true
            ScrollView {
                anchors.fill: parent
                TextArea {
                    objectName: "crashReportText"
                    text: report.reportText
                    textFormat: TextEdit.PlainText
                    readOnly: true
                    selectByMouse: true
                    font.family: "monospace"
                    Accessible.name: qsTr("The whole report")
                    background: Rectangle {
                        color: Theme.field
                        border.width: 1
                        border.color: parent.activeFocus ? Theme.focus : Theme.line
                    }
                }
            }
            IconButton {
                objectName: "crashReportCopy"
                iconRole: "edit-copy"
                text: qsTr("Copy the report")
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: 4
                onClicked: if (report.app) report.app.copyText(report.reportText)
            }
        }
        Label {
            text: qsTr("File names become file1, file2… Your subtitle file, its text and your folders are never in a report.")
            color: Theme.muted
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }

    leadingActions: [
        CheckBox {
            objectName: "crashReportAlwaysSend"
            visible: !report.sent && report.reports.canSend
            text: qsTr("Always send crash reports")
            checked: report.settings ? report.settings.value("privacy.alwaysSendCrashReports") === true : false
            onToggled: if (report.settings) report.settings.setValue("privacy.alwaysSendCrashReports", checked)
        }
    ]
    buttons: [
        PrimaryButton {
            id: sendButton
            // Hidden, it gives back its slot (FooterButton).
            implicitWidth: visible ? Math.max(80, implicitContentWidth + leftPadding + rightPadding)
                                   : -(DialogButtonBox.buttonBox ? DialogButtonBox.buttonBox.spacing : 0)
            objectName: "crashReportSend"
            visible: !report.sent && report.reports.canSend
            enabled: !report.reports.sending
            text: qsTr("Send report")
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: {
                report.lastNote = note.text
                if (report.reports.send(report.source, note.text))
                    report.answer()
            }
        },
        FooterButton {
            id: gitHubButton
            objectName: "crashReportGitHub"
            visible: !report.sent
            text: qsTr("Open GitHub issue")
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: {
                const result = report.reports.openOnGitHub(report.source, note.text)
                report.answer()
                if (result.complete === false)
                    report.linkTooLong()
            }
        },
        FooterButton {
            objectName: "crashReportDontSend"
            visible: !report.sent
            text: qsTr("Don't send")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: report.close()
        },
        PrimaryButton {
            id: closeButton
            // Hidden, it gives back its slot (FooterButton).
            implicitWidth: visible ? Math.max(80, implicitContentWidth + leftPadding + rightPadding)
                                   : -(DialogButtonBox.buttonBox ? DialogButtonBox.buttonBox.spacing : 0)
            objectName: "crashReportClose"
            visible: report.sent
            text: qsTr("Close")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: report.close()
        }
    ]

    // A section's bold title over a rule, a muted note at its trailing end.
    component SectionHead: ColumnLayout {
        id: head
        property string title
        property string note
        spacing: 4
        Layout.fillWidth: true
        Layout.topMargin: 6
        RowLayout {
            Layout.fillWidth: true
            Label { text: head.title; font.bold: true }
            Item { Layout.fillWidth: true }
            Label {
                visible: text.length > 0
                text: head.note
                color: Theme.muted
                font: report.fontAt(12)
            }
        }
        Rectangle { color: Theme.line; implicitHeight: 1; Layout.fillWidth: true }
    }
}
