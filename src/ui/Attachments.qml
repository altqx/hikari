pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Attachments"
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs as Dialogs
import QtQuick.Layouts
import Hikari.Ui

Item {
    id: attachments
    required property var app
    property SettingsStore settings: null
    property var rows: []
    property var selected: []
    property int anchor: -1
    readonly property DialogWindow window: windowLoader.item as DialogWindow
    visible: false

    function reload() {
        rows = app.attachmentRows()
        selected = selected.filter(i => i < rows.length)
        if (anchor >= rows.length)
            anchor = -1
    }
    function show() {
        reload()
        windowLoader.active = true
        window.open()
    }
    function select(index, modifiers) {
        if ((modifiers & Qt.ShiftModifier) && anchor >= 0) {
            const range = []
            for (let i = Math.min(anchor, index); i <= Math.max(anchor, index); ++i)
                range.push(i)
            selected = range
            return
        }
        anchor = index
        if (modifiers & Qt.ControlModifier)
            selected = selected.includes(index) ? selected.filter(i => i !== index) : selected.concat([index]).sort((a, b) => a - b)
        else
            selected = [index]
    }
    function attach(fonts, files) {
        if (files.length === 0)
            return
        const warnings = app.attachmentWarnings(fonts, files)
        if (warnings.length === 0) {
            app.attachFiles(fonts, files)
            return
        }
        question.ask({
            title: fonts ? qsTr("Attach fonts") : qsTr("Attach graphics"),
            text: warnings.join("\n"),
            detail: qsTr("Attach them anyway?"),
            severity: "warning",
            choices: [
                { id: "attach", text: qsTr("Attach"), role: "accept" },
                { id: "cancel", text: qsTr("Cancel"), role: "reject", isDefault: true }
            ]
        }, id => {
            if (id === "attach")
                app.attachFiles(fonts, files)
        })
    }
    function extractTo(queue, failed) {
        if (queue.length === 0) {
            if (failed.length > 0)
                question.ask({ title: qsTr("Extract"), severity: "error",
                               text: qsTr("Could not write %1.").arg(failed.map(f => "“" + f + "”").join(", ")) })
            return
        }
        const next = queue[0]
        const rest = queue.slice(1)
        const name = rows[next.row].name
        const written = next.file.toString() === "" ? "failed" : app.extractAttachment(next.row, next.file, false)
        if (written !== "exists") {
            extractTo(rest, written === "failed" ? failed.concat([name]) : failed)
            return
        }
        question.ask({
            title: qsTr("Extract"),
            text: qsTr("“%1” already exists. Replace it?").arg(decodeURIComponent(next.file.toString().split("/").pop())),
            severity: "warning",
            choices: [
                { id: "replace", text: qsTr("Replace"), role: "destructive" },
                { id: "cancel", text: qsTr("Cancel"), role: "reject", isDefault: true }
            ]
        }, id => {
            const again = id === "replace" ? app.extractAttachment(next.row, next.file, true) : "skipped"
            extractTo(rest, again === "failed" ? failed.concat([name]) : failed)
        })
    }
    function extract() {
        if (selected.length === 1) {
            saveDialog.selectedFile = app.attachmentFile(saveDialog.currentFolder, selected[0])
            saveDialog.open()
        } else if (selected.length > 1) {
            folderDialog.open()
        }
    }

    Connections {
        target: attachments.app
        function onAttachmentsChanged() {
            if (attachments.window && attachments.window.visible)
                attachments.reload()
        }
    }

    Loader {
        id: windowLoader
        active: false
        sourceComponent: DialogWindow {
            id: attachmentsWindow
            objectName: "attachmentsWindow"
            dialogId: "attachments"
            settings: attachments.settings
            resizable: true
            title: qsTr("Attachments")
            initialFocus: list

            Rectangle {
                color: Theme.field
                border.color: Theme.line
                radius: 4
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 560
                Layout.preferredHeight: 300

                Rectangle {
                    id: header
                    x: 1; y: 1
                    width: parent.width - 2
                    height: 26
                    color: Theme.raised
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 14
                        spacing: 12
                        Label { text: qsTr("Name"); color: Theme.muted; font: attachmentsWindow.fontAt(12); Layout.fillWidth: true }
                        Label { text: qsTr("Type"); color: Theme.muted; font: attachmentsWindow.fontAt(12); Layout.preferredWidth: 90 }
                        Label { text: qsTr("Size"); color: Theme.muted; font: attachmentsWindow.fontAt(12); Layout.preferredWidth: 90 }
                    }
                }
                Label {
                    objectName: "attachmentsEmpty"
                    visible: list.count === 0
                    anchors.centerIn: parent
                    width: parent.width - 48
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    color: Theme.muted
                    text: attachments.app.canAttach ? qsTr("These subtitles have no attachments.")
                                                    : qsTr("Only ASS subtitles hold attachments.")
                }
                ListView {
                    id: list
                    objectName: "attachmentsList"
                    x: 1
                    y: header.height + 1
                    width: parent.width - 2
                    height: parent.height - header.height - 2
                    clip: true
                    model: attachments.rows
                    focus: true
                    activeFocusOnTab: true
                    currentIndex: attachments.anchor
                    Accessible.role: Accessible.List
                    Accessible.name: qsTr("Attachments")
                    ScrollBar.vertical: ScrollBar {}
                    Keys.onUpPressed: event => attachments.select(Math.max(0, attachments.anchor - 1), event.modifiers)
                    Keys.onDownPressed: event => attachments.select(Math.min(count - 1, attachments.anchor + 1), event.modifiers)
                    Keys.onDeletePressed: if (attachments.selected.length > 0 && attachments.app.canAttach) attachments.app.removeAttachments(attachments.selected)
                    delegate: Rectangle {
                        id: row
                        required property var modelData
                        required property int index
                        readonly property bool chosen: attachments.selected.includes(index)
                        objectName: "attachmentRow" + index
                        width: ListView.view.width
                        implicitHeight: 30
                        color: chosen ? Theme.select : "transparent"
                        Accessible.role: Accessible.ListItem
                        Accessible.name: modelData.name
                        Accessible.selected: chosen
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 14
                            spacing: 12
                            Label {
                                objectName: "attachmentName" + row.index
                                text: row.modelData.name
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }
                            Label {
                                text: row.modelData.font ? qsTr("Font") : qsTr("Graphic")
                                Layout.preferredWidth: 90
                            }
                            Label {
                                text: Qt.locale().formattedDataSize(row.modelData.size, 1)
                                Layout.preferredWidth: 90
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: mouse => {
                                list.forceActiveFocus()
                                attachments.select(row.index, mouse.modifiers)
                            }
                        }
                    }
                }
            }
            Label {
                text: qsTr("Attached fonts are used to draw these subtitles, and travel with the file. Extracting writes a copy; the subtitles do not change.")
                color: Theme.muted
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            leadingActions: [
                IconTextButton {
                    iconRole: "attach"
                    objectName: "attachmentsAttachFont"
                    text: qsTr("Attach font…")
                    enabled: attachments.app.canAttach
                    onClicked: fontDialog.open()
                },
                IconTextButton {
                    iconRole: "attach"
                    objectName: "attachmentsAttachGraphic"
                    text: qsTr("Attach graphic…")
                    enabled: attachments.app.canAttach
                    onClicked: graphicDialog.open()
                },
                IconTextButton {
                    iconRole: "export"
                    objectName: "attachmentsExtract"
                    text: qsTr("Extract…")
                    enabled: attachments.selected.length > 0
                    onClicked: attachments.extract()
                },
                IconTextButton {
                    iconRole: "delete"
                    objectName: "attachmentsRemove"
                    text: qsTr("Remove")
                    enabled: attachments.selected.length > 0 && attachments.app.canAttach
                    onClicked: attachments.app.removeAttachments(attachments.selected)
                }
            ]
            buttons: [
                PrimaryButton {
                    id: closeButton
                    objectName: "attachmentsClose"
                    text: qsTr("Close")
                    DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                    onClicked: attachmentsWindow.reject()
                }
            ]
            defaultButton: closeButton
        }
    }

    LazyMessageBox {
        id: question
        name: "attachmentsQuestion"
        settings: attachments.settings
    }
    FileChooser {
        id: fontDialog
        objectName: "attachmentsFontDialog"
        title: qsTr("Attach fonts")
        fileMode: Dialogs.FileDialog.OpenFiles
        nameFilters: [qsTr("Font files (*.ttf *.ttc *.otf *.otc)"), qsTr("All files (*)")]
        onAccepted: attachments.attach(true, selectedFiles)
    }
    FileChooser {
        id: graphicDialog
        objectName: "attachmentsGraphicDialog"
        title: qsTr("Attach graphics")
        fileMode: Dialogs.FileDialog.OpenFiles
        nameFilters: [qsTr("Graphics (*.bmp *.gif *.jpg *.jpeg *.ico *.png *.wmf *.emf)"), qsTr("All files (*)")]
        onAccepted: attachments.attach(false, selectedFiles)
    }
    FileChooser {
        id: saveDialog
        objectName: "attachmentsSaveDialog"
        title: qsTr("Extract")
        fileMode: Dialogs.FileDialog.SaveFile
        options: Dialogs.FileDialog.DontConfirmOverwrite
        onAccepted: attachments.extractTo([{ row: attachments.selected[0], file: selectedFile }], [])
    }
    FolderChooser {
        id: folderDialog
        objectName: "attachmentsFolderDialog"
        title: qsTr("Extract to folder")
        onAccepted: attachments.extractTo(attachments.selected.map(r => ({ row: r, file: attachments.app.attachmentFile(selectedFolder, r) })), [])
    }
}
