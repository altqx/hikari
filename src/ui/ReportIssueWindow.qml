pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Reports"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: issue
    objectName: "reportIssue"
    required property var reports
    property var app: null
    property var mainWindow: null
    property string kind: "bug"
    property string page: "form"
    property bool complete: true
    property string screenshot: ""
    property string subtitleZip: ""
    property string subtitleName: ""
    readonly property var kinds: ["bug", "feature", "question"]
    readonly property var kindNames: [qsTr("Bug"), qsTr("Feature request"), qsTr("Question")]
    readonly property bool bug: kind === "bug"
    readonly property bool ready: titleField.text.trim().length > 0 && first.text.trim().length > 0
    signal browserFailed()

    dialogId: "reportIssue"
    resizable: true
    title: qsTr("Report an issue")
    defaultButton: page === "form" ? openButton : doneButton
    initialFocus: page === "form" ? titleField : doneButton

    function openForm() {
        reports.discardAttachments()
        reports.beginIssue()
        subtitleName = reports.subtitleFileName()
        screenshot = mainWindow ? reports.takeScreenshot(mainWindow) : ""
        subtitleZip = ""
        page = "form"
        kind = "bug"
        titleField.text = ""
        first.text = ""
        second.text = ""
        third.text = ""
        withLog.checked = true
        withScreenshot.checked = screenshot.length > 0
        withSubtitle.checked = false
        systemSection.expanded = false
        logSection.expanded = false
        open()
    }
    function fields() {
        const ids = bug ? ["what-happened", "steps", "expected"] : kind === "feature" ? ["wanted", "why"] : ["question"]
        const texts = [first.text, second.text, third.text]
        const out = {}
        ids.forEach((id, i) => out[id] = texts[i])
        return out
    }
    function openOnGitHub() {
        if (bug && withSubtitle.checked && subtitleZip.length === 0)
            subtitleZip = reports.zipSubtitleFile()
        const result = reports.openIssue(kind, titleField.text, fields(), bug && withLog.checked)
        if (result.opened === false) {
            browserFailed()
            return
        }
        complete = result.complete !== false
        page = "finish"
        doneButton.forceActiveFocus(Qt.TabFocusReason)
    }
    onVisibleChanged: if (!visible && !WindowFrame.remaking(issue)) reports.discardAttachments()

    GridLayout {
        visible: issue.page === "form"
        columns: 2
        columnSpacing: 16
        rowSpacing: 10
        Layout.preferredWidth: 572
        Layout.fillWidth: true
        Layout.fillHeight: true

        Label { text: qsTr("Kind") }
        SegmentedButtons {
            objectName: "reportIssueKind"
            choices: issue.kinds.map((k, i) => ({ text: issue.kindNames[i], name: "reportIssueKind_" + k }))
            currentIndex: issue.kinds.indexOf(issue.kind)
            Accessible.name: qsTr("Kind")
            onActivated: index => issue.kind = issue.kinds[index]
            Layout.preferredWidth: 360
        }
        Label { text: qsTr("Title") }
        TextField {
            id: titleField
            objectName: "reportIssueTitle"
            placeholderText: qsTr("A short summary")
            Accessible.name: qsTr("Title")
            Layout.fillWidth: true
        }
        Label {
            text: issue.bug ? qsTr("What happened") : issue.kind === "feature" ? qsTr("What you'd like") : qsTr("Your question")
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 6
        }
        TextArea {
            id: first
            objectName: "reportIssueFirst"
            placeholderText: issue.bug ? "" : issue.kind === "feature" ? qsTr("Describe the feature.")
                                                                       : qsTr("Ask away. Name the panel or command it is about.")
            wrapMode: TextEdit.Wrap
            Accessible.name: issue.bug ? qsTr("What happened") : issue.kind === "feature" ? qsTr("What you'd like") : qsTr("Your question")
            Layout.fillWidth: true
            Layout.fillHeight: issue.kind === "question"
            Layout.preferredHeight: issue.kind === "question" ? 108 : 56
        }
        Label {
            visible: issue.kind !== "question"
            text: issue.bug ? qsTr("Steps to reproduce") : qsTr("What it would help with")
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 6
        }
        TextArea {
            id: second
            objectName: "reportIssueSecond"
            visible: issue.kind !== "question"
            placeholderText: issue.bug ? "" : qsTr("What are you trying to do?")
            wrapMode: TextEdit.Wrap
            Accessible.name: issue.bug ? qsTr("Steps to reproduce") : qsTr("What it would help with")
            Layout.fillWidth: true
            Layout.preferredHeight: 56
        }
        Label {
            visible: issue.bug
            text: qsTr("What you expected")
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 6
        }
        TextArea {
            id: third
            objectName: "reportIssueThird"
            visible: issue.bug
            wrapMode: TextEdit.Wrap
            Accessible.name: qsTr("What you expected")
            Layout.fillWidth: true
            Layout.preferredHeight: 36
        }

        ColumnLayout {
            spacing: 8
            Layout.columnSpan: 2
            Layout.fillWidth: true
            Layout.topMargin: 6
            Label { text: qsTr("Attached"); font.bold: true }
            Rectangle { color: Theme.line; implicitHeight: 1; Layout.fillWidth: true }

            RowLayout {
                spacing: 8
                Layout.fillWidth: true
                FormSection {
                    id: systemSection
                    objectName: "reportIssueSystem"
                    ruled: false
                    expanded: false
                    title: qsTr("System and settings")
                    summary: issue.visible ? issue.reports.systemSummary() : ""
                    onToggled: on => systemSection.expanded = on
                    Preview { text: systemSection.expanded ? issue.reports.issueSystemText() : "" }
                }
                Label {
                    text: qsTr("Always included")
                    color: Theme.muted
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: 2
                }
            }
            RowLayout {
                visible: issue.bug
                spacing: 4
                Layout.fillWidth: true
                CheckBox {
                    id: withLog
                    objectName: "reportIssueWithLog"
                    checked: true
                    Accessible.name: qsTr("Log excerpt")
                    Layout.alignment: Qt.AlignTop
                }
                FormSection {
                    id: logSection
                    objectName: "reportIssueLog"
                    ruled: false
                    expanded: false
                    title: qsTr("Log excerpt")
                    summary: qsTr("last 200 lines, file names replaced")
                    onToggled: on => logSection.expanded = on
                    Layout.topMargin: 4
                    Preview { text: logSection.expanded ? issue.reports.issueLogText() : "" }
                }
            }
            RowLayout {
                visible: issue.bug && issue.screenshot.length > 0
                spacing: 8
                Layout.fillWidth: true
                CheckBox {
                    id: withScreenshot
                    objectName: "reportIssueWithScreenshot"
                    text: qsTr("Screenshot of the window")
                    Layout.fillWidth: true
                }
                Image {
                    objectName: "reportIssueThumbnail"
                    source: issue.screenshot.length > 0 && issue.app ? issue.app.fileUrl(issue.screenshot) : ""
                    cache: false
                    asynchronous: true
                    fillMode: Image.PreserveAspectFit
                    sourceSize.width: 196
                    Layout.preferredWidth: 98
                    Layout.preferredHeight: 62
                }
            }
            RowLayout {
                visible: issue.bug && issue.subtitleName.length > 0
                spacing: 8
                Layout.fillWidth: true
                CheckBox {
                    id: withSubtitle
                    objectName: "reportIssueWithSubtitle"
                    text: qsTr("Subtitle file")
                }
                Label {
                    text: qsTr("%1, as a .zip to drag into the issue").arg(issue.subtitleName)
                    color: Theme.muted
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }
            }
        }
    }

    ColumnLayout {
        visible: issue.page === "finish"
        spacing: 12
        Layout.preferredWidth: 472
        Layout.fillHeight: true
        Layout.alignment: Qt.AlignTop
        Label {
            text: qsTr("Finish on GitHub")
            font: issue.fontAt(15, Font.DemiBold)
        }
        Step {
            objectName: "reportIssueOpened"
            iconRole: issue.complete ? "report-issue" : "notice-warning"
            warning: !issue.complete
            heading: issue.complete ? qsTr("The %1 form opened in your browser, filled in.").arg(issue.kindNames[issue.kinds.indexOf(issue.kind)])
                                    : qsTr("The report is too long for a link, so it is on the clipboard.")
            detail: issue.complete ? qsTr("Check it there and press Create. You need a GitHub account.")
                                   : qsTr("GitHub opened an empty %1 form. Paste into its first field (Ctrl+V).").arg(issue.kindNames[issue.kinds.indexOf(issue.kind)])
            IconTextButton {
                iconRole: "edit-copy"
                visible: !issue.complete
                text: qsTr("Copy again")
                onClicked: issue.reports.copyIssue(issue.kind, titleField.text, issue.fields(), issue.bug && withLog.checked)
            }
        }
        Step {
            visible: issue.bug && withScreenshot.checked && issue.screenshot.length > 0
            iconRole: "screenshot"
            heading: qsTr("Add the screenshot")
            detail: qsTr("Drag it into the description, or copy it and paste it there.")
            IconTextButton {
                iconRole: "edit-copy"
                objectName: "reportIssueCopyImage"
                text: qsTr("Copy image")
                onClicked: issue.reports.copyImage(issue.screenshot)
            }
            IconTextButton {
                iconRole: "show-in-folder"
                text: qsTr("Show file")
                onClicked: if (issue.app) issue.app.showInFolder(issue.screenshot)
            }
        }
        Step {
            visible: issue.bug && withSubtitle.checked && issue.subtitleZip.length > 0
            iconRole: "open-subtitles"
            heading: qsTr("Add the subtitle file")
            detail: qsTr("Drag the .zip into the description. Only you choose to share it.")
            IconTextButton {
                iconRole: "show-in-folder"
                objectName: "reportIssueShowZip"
                text: qsTr("Show file")
                onClicked: if (issue.app) issue.app.showInFolder(issue.subtitleZip)
            }
        }
        Item { Layout.fillHeight: true }
    }

    leadingActions: [
        IconTextButton {
            iconRole: "edit-copy"
            objectName: "reportIssueCopy"
            visible: issue.page === "form"
            text: qsTr("Copy")
            onClicked: issue.reports.copyIssue(issue.kind, titleField.text, issue.fields(), issue.bug && withLog.checked)
        },
        IconTextButton {
            iconRole: "open-link"
            objectName: "reportIssueAgain"
            visible: issue.page === "finish"
            text: qsTr("Open the form again")
            onClicked: if (issue.reports.openIssue(issue.kind, titleField.text, issue.fields(), issue.bug && withLog.checked).opened === false)
                           issue.browserFailed()
        }
    ]
    buttons: [
        PrimaryButton {
            id: openButton
            // A hidden button still takes the DialogButtonBox's spacing; a negative width gives it back.
            implicitWidth: visible ? Math.max(80, implicitContentWidth + leftPadding + rightPadding)
                                   : -(DialogButtonBox.buttonBox ? DialogButtonBox.buttonBox.spacing : 0)
            objectName: "reportIssueOpen"
            visible: issue.page === "form"
            enabled: issue.ready
            text: qsTr("Open on GitHub")
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: issue.openOnGitHub()
        },
        FooterButton {
            objectName: "reportIssueCancel"
            visible: issue.page === "form"
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: issue.close()
        },
        PrimaryButton {
            id: doneButton
            implicitWidth: visible ? Math.max(80, implicitContentWidth + leftPadding + rightPadding)
                                   : -(DialogButtonBox.buttonBox ? DialogButtonBox.buttonBox.spacing : 0)
            objectName: "reportIssueDone"
            visible: issue.page === "finish"
            text: qsTr("Done")
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: issue.accept()
        }
    ]

    component Preview: ScrollView {
        id: preview
        property alias text: previewText.text
        Layout.fillWidth: true
        Layout.preferredHeight: 150
        TextArea {
            id: previewText
            textFormat: TextEdit.PlainText
            readOnly: true
            selectByMouse: true
            font.family: "monospace"
            background: Rectangle {
                color: Theme.field
                border.width: 1
                border.color: Theme.line
            }
        }
    }
    component Step: RowLayout {
        id: step
        property string iconRole
        property string heading
        property string detail
        property bool warning: false
        default property alias actions: stepActions.data
        spacing: 12
        Layout.fillWidth: true
        Icon {
            iconRole: step.iconRole
            color: step.warning ? Theme.warning : Theme.text
            accentColor: step.warning ? Theme.warning : Theme.accent
            size: 20
            Layout.alignment: Qt.AlignTop
        }
        ColumnLayout {
            spacing: 4
            Layout.fillWidth: true
            Label { text: step.heading; font.bold: true; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Label { text: step.detail; color: Theme.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            RowLayout {
                id: stepActions
                spacing: 8
                visible: children.length > 0
                Layout.topMargin: 4
            }
        }
    }
}
