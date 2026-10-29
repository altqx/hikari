pragma Translator: "HikariSub.Fonts"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs as Dialogs
import Hikari.Ui

DialogWindow {
    id: dialog
    objectName: "fontCollectorDialog"
    required property var collector
    readonly property color warningColour: Theme.warning
    readonly property color successColour: Theme.success
    readonly property bool working: collector.stage === 1
    readonly property var hints: [qsTr("Lists the fonts the subtitles use and whether this computer has each one."),
                                  qsTr("Copies each font file the subtitles use into the folder."),
                                  qsTr("Puts each font file the subtitles use into a zip archive.")]
    title: qsTr("Font collector")
    dialogId: "fontCollector"
    resizable: true
    defaultButton: startButton

    function openDialog() {
        collector.open()
        path.text = collector.directory
        actions.currentIndex = collector.action
        subsDirectory.checked = collector.useSubsDirectory
        fromMkv.checked = collector.fromMkv
        acknowledge.checked = false
        open()
        startButton.forceActiveFocus()
    }
    function showOnce() {
        if (visible)
            open()
        else
            openDialog()
    }
    function changeOptions() { collector.changeOptions(actions.currentIndex, subsDirectory.checked) }
    function start(allTabs) {
        acknowledge.checked = false
        const r = collector.start(path.text, allTabs)
        if (r.message !== undefined) {
            // MUST KILL: missingPath recognises the error by comparing its translated message text
            const missingPath = r.message === qsTr("Select the folder where you want to copy fonts")
                             || r.message === qsTr("Choose a name for the archive")
            messages.ask({ title: dialog.title, text: r.message, severity: missingPath ? "warning" : "error" }, () => {
                if (missingPath)
                    path.forceActiveFocus()
            })
        } else if (r.question !== undefined) {
            const name = r.archive.replace(/^.*[\\/]/, "")
            messages.ask({
                title: dialog.title,
                text: qsTr("“%1” already exists.").arg(name),
                detail: qsTr("Writing the new archive replaces it."),
                severity: "warning",
                choices: [{ id: "replace", text: qsTr("Replace"), role: "destructive" },
                          { id: "cancel", text: qsTr("Cancel"), role: "reject", isDefault: true }]
            }, id => {
                if (id === "replace")
                    dialog.collector.confirmReplace(true)
            })
        }
    }
    function choosePath() {
        const s = collector.chooserStart(path.text)
        if (collector.action === 1) {
            if (s.folder.length > 0)
                folderDialog.currentFolder = collector.fileUrl(s.folder)
            folderDialog.open()
        } else {
            if (s.folder.length > 0)
                archiveDialog.currentFolder = collector.fileUrl(s.folder)
            archiveDialog.selectedFile = s.name.length > 0 ? collector.fileUrl(s.folder + "/" + s.name) : ""
            archiveDialog.open()
        }
    }
    function chosen(value) {
        collector.chooseDirectory(value)
        path.text = value
    }
    function escaped(text) {
        return text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
    }
    function html() {
        let out = ""
        for (const s of collector.log) {
            const colour = s.kind === 1 ? warningColour : s.kind === 2 ? successColour : Theme.text
            out += "<span style=\"color:" + colour + "\">" + escaped(s.text) + "</span>"
        }
        return "<div style=\"white-space: pre-wrap\">" + out + "</div>"
    }
    onClosing: close => {
        if (working)
            close.accepted = false
    }
    onVisibleChanged: if (!visible && !WindowFrame.remaking(dialog)) collector.close()

    GridLayout {
        columns: 2
        columnSpacing: 12
        rowSpacing: 10
        Layout.fillWidth: true
        Label { text: qsTr("Action"); Layout.alignment: Qt.AlignTop; Layout.topMargin: 6 }
        ColumnLayout {
            spacing: 6
            enabled: !dialog.working
            Layout.fillWidth: true
            SegmentedButtons {
                id: actions
                objectName: "fontCollectorAction"
                Accessible.name: qsTr("Action")
                choices: [{ text: qsTr("Check fonts"), name: "fontCollectorOption0" },
                          { text: qsTr("Copy to a folder"), name: "fontCollectorOption1" },
                          { text: qsTr("Zip archive"), name: "fontCollectorOption2" }]
                onActivated: index => {
                    currentIndex = index
                    dialog.changeOptions()
                }
            }
            Label {
                objectName: "fontCollectorActionHint"
                text: dialog.hints[actions.currentIndex] ?? ""
                color: Theme.muted
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }
        Label { text: qsTr("Save to"); Layout.alignment: Qt.AlignTop; Layout.topMargin: 6 }
        ColumnLayout {
            spacing: 4
            Layout.fillWidth: true
            RowLayout {
                Layout.fillWidth: true
                TextField {
                    id: path
                    objectName: "fontCollectorPath"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 150
                    Layout.preferredWidth: 420
                    enabled: !dialog.working && dialog.collector.action !== 0
                    onAccepted: dialog.start(false)
                    Accessible.name: qsTr("Save to")
                    placeholderText: dialog.collector.action === 2 ? qsTr("Choose an archive") : qsTr("Choose a folder")
                    validator: RegularExpressionValidator {
                        regularExpression: Qt.platform.os === "windows" ? /[^\/*?"<>|]*/ : /[^*?"<>|]*/
                    }
                }
                IconToolButton {
                    objectName: "fontCollectorChoosePath"
                    iconRole: "folder-open"
                    text: qsTr("Select a folder")
                    enabled: path.enabled
                    onClicked: dialog.choosePath()
                }
            }
            CheckBox {
                id: subsDirectory
                objectName: "fontCollectorSubsDirectory"
                text: qsTr("Write beside the subtitles, in a “Fonts” folder")
                enabled: path.enabled
                ToolTip.text: qsTr("Beside the video when the fonts come from the MKV file.")
                ToolTip.visible: hovered
                onToggled: dialog.changeOptions()
            }
            CheckBox {
                id: fromMkv
                objectName: "fontCollectorFromMkv"
                text: qsTr("Take the fonts from the loaded MKV file")
                enabled: !dialog.working && dialog.collector.fromMkvEnabled
                onToggled: dialog.collector.setFromMkv(checked)
            }
        }
    }
    Rectangle {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.topMargin: 6
        Layout.preferredWidth: 560
        Layout.preferredHeight: 260
        Layout.minimumHeight: 120
        color: Theme.field
        border.color: Theme.line
        ScrollView {
            anchors.fill: parent
            anchors.margins: 1
            TextArea {
                id: console_
                objectName: "fontCollectorLog"
                readOnly: true
                wrapMode: TextEdit.Wrap
                textFormat: TextEdit.RichText
                text: dialog.html()
                background: null
                Accessible.name: qsTr("Font collector log")
                TapHandler {
                    onDoubleTapped: (point) => dialog.collector.logDoubleClicked(console_.positionAt(point.position.x, point.position.y))
                }
            }
        }
        Label {
            objectName: "fontCollectorLogEmpty"
            anchors.centerIn: parent
            visible: dialog.collector.log.length === 0
            color: Theme.muted
            text: qsTr("Start lists the fonts the subtitles use.")
        }
    }
    Rectangle {
        objectName: "fontCollectorReview"
        visible: dialog.collector.stage === 2
        color: Theme.raised
        border.color: Theme.line
        Layout.fillWidth: true
        implicitHeight: reviewRow.implicitHeight + 12
        RowLayout {
            id: reviewRow
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 6
            spacing: 10
            Icon {
                iconRole: dialog.collector.reviewIncomplete ? "notice-warning" : "about"
                color: dialog.collector.reviewIncomplete ? Theme.warning : IconTheme.normal
                accentColor: dialog.collector.reviewIncomplete ? Theme.warning : IconTheme.accent
            }
            Label {
                objectName: "fontCollectorReviewText"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: dialog.collector.reviewIncomplete ? qsTr("Some fonts were not found, so the collection is incomplete.")
                    : dialog.collector.action === 2 ? qsTr("Ready to write %1").arg(dialog.collector.copyPath)
                    : qsTr("Ready to copy to %1").arg(dialog.collector.copyPath)
            }
            CheckBox {
                id: acknowledge
                objectName: "fontCollectorAcknowledge"
                visible: dialog.collector.reviewIncomplete
                text: dialog.collector.action === 2 ? qsTr("Write it, labelled incomplete") : qsTr("Copy it, labelled incomplete")
            }
            PrimaryButton {
                objectName: "fontCollectorApply"
                text: dialog.collector.action === 2 ? qsTr("Write archive") : qsTr("Copy fonts")
                enabled: !dialog.collector.reviewIncomplete || acknowledge.checked
                onClicked: dialog.collector.apply(acknowledge.checked)
            }
        }
    }

    leadingActions: Button {
        objectName: "fontCollectorSaveFolder"
        text: qsTr("Show folder")
        visible: dialog.collector.action !== 0
        enabled: dialog.collector.canSaveFolder && !dialog.working
        onClicked: dialog.collector.saveFolder()
    }
    buttons: [
        Button {
            objectName: "fontCollectorStartOnTabs"
            text: qsTr("Start on all tabs")
            enabled: !dialog.working
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: dialog.start(true)
        },
        PrimaryButton {
            id: startButton
            objectName: "fontCollectorStart"
            text: qsTr("Start")
            enabled: !dialog.working
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: dialog.start(false)
        },
        FooterButton {
            objectName: "fontCollectorCancel"
            text: qsTr("Stop")
            visible: dialog.working
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: dialog.collector.cancel()
        },
        FooterButton {
            objectName: "fontCollectorClose"
            text: qsTr("Close")
            visible: !dialog.working
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: dialog.close()
        }
    ]

    MessageHost {
        id: messages
        objectName: "fontCollectorMessages"
        settings: dialog.settings
    }
    Dialogs.FolderDialog {
        id: folderDialog
        objectName: "fontCollectorFolderDialog"
        title: qsTr("Choose save folder")
        onAccepted: dialog.chosen(dialog.collector.localPath(selectedFolder))
    }
    Dialogs.FileDialog {
        id: archiveDialog
        objectName: "fontCollectorArchiveDialog"
        title: qsTr("Select the name of the archive")
        fileMode: Dialogs.FileDialog.SaveFile
        defaultSuffix: "zip"
        nameFilters: [qsTr("Archive files (*.zip)")]
        onAccepted: dialog.chosen(dialog.collector.localPath(selectedFile))
    }
}
