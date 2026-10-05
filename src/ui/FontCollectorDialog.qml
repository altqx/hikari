// Y8: legacy FontCollectorDialog "Font collector" (GLOBAL_OPEN_FONT_COLLECTOR,
// FontCollector.cpp:134-239 at 20d647c4), a modeless resizable window: the
// path with "Select a folder", the "Options" radio box (Check availability
// of fonts, Copy to selected folder, Zip), "Save to video / subtitles
// folder.", the log, then Start, "Start on tabs", "Save folder" and Close.
// The copy modes stage their output (routing #60): Start shows what Apply
// would write; an incomplete collection is written only after the
// acknowledgment and is labelled incomplete (routing #54). A double click in
// the log goes to a Style or Line as legacy's OnConsoleDoubleClick did.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs as Dialogs
import Hikari.Ui

Dialog {
    id: dialog
    objectName: "fontCollectorDialog"
    required property var collector
    // FontCollectorDialog's colours: WINDOW_TEXT, WINDOW_WARNING_ELEMENTS and
    // the success "#008000", from the theme (K2).
    readonly property color warningColour: Theme.warning
    readonly property color successColour: Theme.success
    readonly property bool working: collector.stage === 1
    title: qsTr("Font collector")
    // K1: the title with the set's font-collector icon (the Subtitles menu's FontCollector bitmap).
    header: IconDialogHeader { objectName: "fontCollectorDialogTitle"; iconRole: "font-collector"; text: dialog.title }
    modal: false
    closePolicy: working ? Popup.NoAutoClose : Popup.CloseOnEscape

    function openDialog() {
        collector.open()
        path.text = collector.directory
        options.itemAt(collector.action).checked = true
        subsDirectory.checked = collector.useSubsDirectory
        acknowledge.checked = false
        open()
        startButton.forceActiveFocus()
    }
    // ShowDialog: nothing when the window is already there.
    function showOnce() {
        if (!visible)
            openDialog()
    }
    function selectedAction() {
        for (let i = 0; i < options.count; ++i)
            if (options.itemAt(i).checked)
                return i
        return 0
    }
    function changeOptions() { collector.changeOptions(selectedAction(), subsDirectory.checked) }
    function start(allTabs) {
        acknowledge.checked = false
        const r = collector.start(path.text, allTabs)
        if (r.message !== undefined) {
            message.text = r.message
            message.open()
            if (r.message === qsTr("Select the folder where you want to copy fonts") || r.message === qsTr("Choose a name for the archive"))
                path.forceActiveFocus()
        } else if (r.question !== undefined) {
            replaceQuestion.text = r.question
            replaceQuestion.title = r.title
            replaceQuestion.open()
        }
    }
    function choosePath() {
        // OnButtonPath: a folder for "Copy to selected folder", else the archive.
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
            const colour = s.kind === 1 ? warningColour : s.kind === 2 ? successColour : palette.text
            out += "<span style=\"color:" + colour + "\">" + escaped(s.text) + "</span>"
        }
        return "<div style=\"white-space: pre-wrap\">" + out + "</div>"
    }
    onClosed: collector.close()

    ColumnLayout {
        anchors.fill: parent
        spacing: 4
        // SetEnterId(9879): Enter starts.
        Keys.onReturnPressed: if (!dialog.working) dialog.start(false)
        Keys.onEnterPressed: if (!dialog.working) dialog.start(false)
        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: path
                objectName: "fontCollectorPath"
                Layout.fillWidth: true
                Layout.minimumWidth: 150
                enabled: !dialog.working && dialog.collector.action !== 0
                onAccepted: dialog.start(false)
                Accessible.name: qsTr("Save folder")
                // HikariTextValidator with wxFILTER_EXCLUDE_CHAR_LIST: / * ? " < > |
                // on Windows; elsewhere '/' separates the folders, so it is
                // accepted (FC-slash-linux).
                validator: RegularExpressionValidator {
                    regularExpression: Qt.platform.os === "windows" ? /[^\/*?"<>|]*/ : /[^*?"<>|]*/
                }
            }
            Button {
                objectName: "fontCollectorChoosePath"
                text: qsTr("Select a folder")
                enabled: path.enabled
                onClicked: dialog.choosePath()
            }
        }
        GroupBox {
            title: qsTr("Options")
            Layout.fillWidth: true
            enabled: !dialog.working
            ColumnLayout {
                ButtonGroup { id: optionsGroup }
                Repeater {
                    id: options
                    model: [qsTr("Check availability of fonts"), qsTr("Copy to selected folder"), qsTr("Zip")]
                    RadioButton {
                        required property string modelData
                        required property int index
                        objectName: "fontCollectorOption" + index
                        text: modelData
                        ButtonGroup.group: optionsGroup
                        onToggled: if (checked) dialog.changeOptions()
                    }
                }
            }
        }
        CheckBox {
            id: subsDirectory
            objectName: "fontCollectorSubsDirectory"
            text: qsTr("Save to video / subtitles folder.")
            enabled: path.enabled
            ToolTip.text: qsTr("Saves to the video folder\nwhen demuxing fonts from an MKV file.")
            ToolTip.visible: hovered
            onToggled: dialog.changeOptions()
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 500
            Layout.preferredHeight: 400
            TextArea {
                id: console_
                objectName: "fontCollectorLog"
                readOnly: true
                wrapMode: TextEdit.Wrap
                textFormat: TextEdit.RichText
                text: dialog.html()
                Accessible.name: qsTr("Font collector log")
                TapHandler {
                    onDoubleTapped: (point) => dialog.collector.logDoubleClicked(console_.positionAt(point.position.x, point.position.y))
                }
            }
        }
        // The staged review: Apply writes what the log lists.
        RowLayout {
            objectName: "fontCollectorReview"
            visible: dialog.collector.stage === 2
            Layout.fillWidth: true
            CheckBox {
                id: acknowledge
                objectName: "fontCollectorAcknowledge"
                visible: dialog.collector.reviewIncomplete
                text: qsTr("Write this incomplete collection, labelled incomplete")
                Layout.fillWidth: true
            }
            Button {
                objectName: "fontCollectorApply"
                text: dialog.collector.action === 2 ? qsTr("Write archive") : qsTr("Copy fonts")
                enabled: !dialog.collector.reviewIncomplete || acknowledge.checked
                onClicked: dialog.collector.apply(acknowledge.checked)
            }
        }
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Button {
                id: startButton
                objectName: "fontCollectorStart"
                text: qsTr("Start")
                enabled: !dialog.working
                onClicked: dialog.start(false)
            }
            Button {
                objectName: "fontCollectorStartOnTabs"
                text: qsTr("Start on tabs")
                enabled: !dialog.working
                onClicked: dialog.start(true)
            }
            Button {
                objectName: "fontCollectorSaveFolder"
                text: qsTr("Save folder")
                enabled: dialog.collector.canSaveFolder && !dialog.working
                onClicked: dialog.collector.saveFolder()
            }
            Button {
                objectName: "fontCollectorCancel"
                text: qsTr("Cancel")
                visible: dialog.working
                onClicked: dialog.collector.cancel()
            }
            Button {
                objectName: "fontCollectorClose"
                text: qsTr("Close")
                enabled: !dialog.working
                onClicked: dialog.close()
            }
        }
    }

    Dialog {
        id: message
        objectName: "fontCollectorMessage"
        property alias text: messageLabel.text
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        standardButtons: Dialog.Ok
        Label { id: messageLabel }
    }
    Dialog {
        id: replaceQuestion
        objectName: "fontCollectorReplaceQuestion"
        property alias text: questionLabel.text
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        standardButtons: Dialog.Yes | Dialog.No
        Label { id: questionLabel }
        onAccepted: dialog.collector.confirmReplace(true)
        onRejected: dialog.collector.confirmReplace(false)
    }
    Dialogs.FolderDialog {
        id: folderDialog
        objectName: "fontCollectorFolderDialog"
        title: qsTr("Choose save folder")
        // Cancel keeps the previous path (FC-chooser-cancel).
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
