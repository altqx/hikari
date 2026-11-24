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
    readonly property bool working: collector.stage === 1
    readonly property var segmentActions: [0, 1, 3, 2]
    readonly property bool zip: collector.action === 2
    readonly property bool linking: collector.action === 3
    readonly property var hints: [qsTr("Lists the fonts the subtitles use and whether this computer has each one."),
                                  qsTr("Copies each font file the subtitles use into the folder."),
                                  qsTr("Puts each font file the subtitles use into a zip archive."),
                                  qsTr("Links each font file the subtitles use into the folder. The links break if the fonts move or are deleted.")]
    title: qsTr("Collect fonts")
    dialogId: "fontCollector"
    resizable: true
    defaultButton: startButton

    function openDialog() {
        if (runsOn) {
            runsOn = false
            open()
            return
        }
        collector.open()
        path.text = collector.directory
        actions.currentIndex = segmentActions.indexOf(collector.action)
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
    function changeOptions() { collector.changeOptions(segmentActions[actions.currentIndex], subsDirectory.checked) }
    function start(allTabs) {
        acknowledge.checked = false
        const r = collector.start(path.text, allTabs)
        if (r.message !== undefined) {
            messages.ask({ title: dialog.title, text: r.message, severity: r.missingPath ? "warning" : "error" }, () => {
                if (r.missingPath)
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
    function applyFonts() {
        const existing = collector.existingFiles()
        if (existing.length === 0) {
            collector.apply(acknowledge.checked, false)
            return
        }
        const shown = existing.slice(0, 10).join("\n")
        const extra = existing.length - 10
        messages.ask({
            title: dialog.title,
            text: qsTr("%n font files already exist in “%1”.", "", existing.length).arg(collector.copyPath),
            detail: extra > 0 ? shown + "\n" + qsTr("and %n more", "", extra) : shown,
            severity: "warning",
            choices: [{ id: "replace", text: qsTr("Replace"), role: "destructive" },
                      { id: "keep", text: qsTr("Keep existing"), role: "action" },
                      { id: "cancel", text: qsTr("Cancel"), role: "reject", isDefault: true }]
        }, id => {
            if (id === "replace" || id === "keep")
                dialog.collector.apply(acknowledge.checked, id === "keep")
        })
    }
    function choosePath() {
        const s = collector.chooserStart(path.text)
        if (!zip) {
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
    function styleName(row) {
        if (row.bold >= 700 && row.italic)
            return qsTr("Bold Italic")
        if (row.bold >= 700)
            return qsTr("Bold")
        if (row.italic)
            return qsTr("Italic")
        return row.bold === 400 ? qsTr("Regular") : qsTr("Weight %1").arg(row.bold)
    }
    function fileText(row) {
        switch (row.status) {
        case "missing":
            return qsTr("Not on this computer")
        case "notLinked":
            return Qt.platform.os === "windows"
                ? qsTr("Couldn’t link: turn on Developer Mode, or use a folder on the font’s drive")
                : qsTr("Couldn’t link")
        case "simulated":
            return row.file + " · " + (row.simulatedBold && row.simulatedItalic ? qsTr("simulated bold and italic")
                                       : row.simulatedBold ? qsTr("simulated bold") : qsTr("simulated italic"))
        }
        return row.file
    }
    function summary() {
        const rows = collector.rows
        if (rows.length === 0)
            return ""
        const missing = rows.filter(r => r.status === "missing").length
        const bold = rows.filter(r => r.status === "simulated" && r.simulatedBold).length
        const italic = rows.filter(r => r.status === "simulated" && !r.simulatedBold).length
        const parts = [qsTr("%n font(s)", "", rows.length)]
        if (missing > 0)
            parts.push(qsTr("%n missing", "", missing))
        if (bold > 0)
            parts.push(qsTr("%n made bold by the renderer", "", bold))
        if (italic > 0)
            parts.push(qsTr("%n made italic by the renderer", "", italic))
        const files = collector.filesToWrite
        const action = collector.stage === 1 || files === 0 ? ""
            : zip ? qsTr("Zip archive adds %n file(s).", "", files)
            : linking ? (collector.filesCopiedInstead > 0
                         ? qsTr("Link to a folder links %1 and copies %2 that have no file to link to.")
                               .arg(qsTr("%n file(s)", "", files - collector.filesCopiedInstead))
                               .arg(collector.filesCopiedInstead)
                         : qsTr("Link to a folder links %n file(s).", "", files))
            : collector.action === 1 ? qsTr("Copy to a folder copies %n file(s).", "", files) : ""
        return parts.join(" · ") + "." + (action.length > 0 ? " " + action : "")
    }
    property bool runsOn: false
    readonly property string runTitle: qsTr("Collecting fonts")
    readonly property real runProgress: collector.facesToCheck > 0 ? Math.min(1, collector.rows.length / collector.facesToCheck) : -1
    readonly property string doingText: collector.facesToCheck > 0
        ? qsTr("Checking · %1 of %n font(s)", "", collector.facesToCheck).arg(collector.rows.length)
        : qsTr("Checking fonts")
    onVisibleChanged: {
        if (visible || WindowFrame.remaking(dialog))
            return
        if (working)
            runsOn = true
        else if (!runsOn)
            collector.close()
    }

    component RowLabel: Label {
        Layout.alignment: Qt.AlignTop
        Layout.preferredHeight: Theme.controlHeight
        verticalAlignment: Text.AlignVCenter
        color: Theme.muted
        font: dialog.fontAt(12)
    }

    GridLayout {
        columns: 2
        columnSpacing: 12
        rowSpacing: 12
        Layout.fillWidth: true
        RowLabel { text: qsTr("Action") }
        ColumnLayout {
            spacing: 4
            enabled: !dialog.working
            Layout.fillWidth: true
            SegmentedButtons {
                id: actions
                objectName: "fontCollectorAction"
                Accessible.name: qsTr("Action")
                choices: [{ text: qsTr("Check fonts"), name: "fontCollectorOption0" },
                          { text: qsTr("Copy to a folder"), name: "fontCollectorOption1" },
                          { text: qsTr("Link to a folder"), name: "fontCollectorOption3" },
                          { text: qsTr("Zip archive"), name: "fontCollectorOption2" }]
                onActivated: index => {
                    currentIndex = index
                    dialog.changeOptions()
                }
            }
            Label {
                objectName: "fontCollectorActionHint"
                text: (dialog.hints[dialog.segmentActions[actions.currentIndex]] ?? "")
                      + (dialog.segmentActions[actions.currentIndex] === 3 && fromMkv.enabled && fromMkv.checked
                         ? "\n" + qsTr("Fonts from the MKV file are copied: there’s no file to link to.") : "")
                color: Theme.muted
                font: dialog.fontAt(12)
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }
        RowLabel { text: qsTr("Save to") }
        ColumnLayout {
            spacing: 4
            Layout.fillWidth: true
            RowLayout {
                spacing: 8
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
                    placeholderText: dialog.zip ? qsTr("Choose an archive") : qsTr("Choose a folder")
                    font: {
                        const f = dialog.fontAt(12)
                        f.family = "monospace"
                        return f
                    }
                    validator: RegularExpressionValidator {
                        regularExpression: Qt.platform.os === "windows" ? /[^\/*?"<>|]*/ : /[^*?"<>|]*/
                    }
                }
                Button {
                    objectName: "fontCollectorChoosePath"
                    flat: true
                    text: qsTr("Browse…")
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
        objectName: "fontCollectorList"
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredWidth: 640
        Layout.preferredHeight: Theme.gridHeaderHeight + 10 * Theme.gridRowHeight + 2
        Layout.minimumHeight: Theme.gridHeaderHeight + 4 * Theme.gridRowHeight + 2
        radius: Theme.popupRadius
        color: "transparent"
        border.color: Theme.line
        Accessible.role: Accessible.Table
        Accessible.name: qsTr("Fonts")
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 1
            spacing: 0
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: Theme.gridHeaderHeight
                color: Theme.raised
                topLeftRadius: Theme.popupRadius - 1
                topRightRadius: Theme.popupRadius - 1
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 12
                    Label {
                        text: qsTr("Font and style")
                        color: Theme.muted
                        font: dialog.fontAt(11, Font.Medium)
                        Layout.leftMargin: 16 + 8
                        Layout.preferredWidth: fontList.familyWidth
                    }
                    Label { text: qsTr("File"); color: Theme.muted; font: dialog.fontAt(11, Font.Medium); Layout.fillWidth: true }
                    Label {
                        text: qsTr("Lines")
                        color: Theme.muted
                        font: dialog.fontAt(11, Font.Medium)
                        horizontalAlignment: Text.AlignRight
                        Layout.minimumWidth: fontList.linesWidth
                    }
                }
            }
            ListView {
                id: fontList
                objectName: "fontCollectorRows"
                readonly property font rowFont: dialog.fontAt(12)
                readonly property font familyFont: dialog.fontAt(12, Font.DemiBold)
                readonly property font monoFont: {
                    const f = dialog.fontAt(12)
                    f.family = "monospace"
                    return f
                }
                readonly property real familyWidth: Math.min(width * 0.45, Math.ceil(Math.max(120, ...dialog.collector.rows.map(
                    r => familyMetrics.advanceWidth(r.family) + rowMetrics.advanceWidth(" " + dialog.styleName(r))))))
                readonly property real linesWidth: Math.ceil(lineMetrics.advanceWidth("00000"))
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: dialog.collector.rows
                currentIndex: -1
                keyNavigationEnabled: true
                ScrollBar.vertical: ScrollBar {}
                Keys.onReturnPressed: dialog.collector.chooseRow(currentIndex)
                Keys.onEnterPressed: dialog.collector.chooseRow(currentIndex)
                FontMetrics { id: familyMetrics; font: fontList.familyFont }
                FontMetrics { id: rowMetrics; font: fontList.rowFont }
                FontMetrics { id: lineMetrics; font: fontList.monoFont }
                delegate: ItemDelegate {
                    id: fontRow
                    required property var modelData
                    required property int index
                    readonly property string status: modelData.status
                    readonly property color statusColour: status === "found" ? Theme.success
                                                        : status === "simulated" ? Theme.warning : Theme.danger
                    objectName: "fontCollectorRow" + index
                    width: ListView.view.width
                    height: Theme.gridRowHeight
                    padding: 0
                    leftPadding: 12
                    rightPadding: 12
                    focusPolicy: Qt.NoFocus
                    Accessible.name: modelData.family + " " + dialog.styleName(modelData) + ": " + dialog.fileText(modelData)
                    Accessible.description: modelData.notes
                    ToolTip.visible: hovered && modelData.notes.length > 0
                    ToolTip.text: modelData.notes
                    background: Rectangle {
                        color: fontRow.ListView.isCurrentItem ? Theme.select : fontRow.hovered ? Theme.hover : "transparent"
                    }
                    onClicked: {
                        fontList.currentIndex = index
                        dialog.collector.chooseRow(index)
                    }
                    contentItem: RowLayout {
                        spacing: 12
                        RowLayout {
                            spacing: 8
                            Layout.preferredWidth: fontList.familyWidth + 16 + 8
                            Layout.maximumWidth: Layout.preferredWidth
                            Icon {
                                objectName: "fontCollectorRowGlyph"
                                iconRole: fontRow.status === "found" ? "karaoke-check" : "notice-warning"
                                size: 16
                                color: fontRow.statusColour
                                accentColor: fontRow.statusColour
                            }
                            Label {
                                text: fontRow.modelData.family
                                font: fontList.familyFont
                                elide: Text.ElideRight
                                Layout.maximumWidth: implicitWidth
                                Layout.fillWidth: true
                            }
                            Label {
                                text: dialog.styleName(fontRow.modelData)
                                font: fontList.rowFont
                                color: Theme.muted
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }
                        Label {
                            objectName: "fontCollectorRowFile"
                            text: dialog.fileText(fontRow.modelData)
                            font: fontRow.status === "found" || fontRow.status === "simulated" ? fontList.monoFont : fontList.rowFont
                            color: fontRow.status === "found" ? Theme.muted : fontRow.statusColour
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }
                        Label {
                            text: fontRow.modelData.lines
                            font: fontList.monoFont
                            horizontalAlignment: Text.AlignRight
                            Layout.minimumWidth: fontList.linesWidth
                        }
                    }
                }
                Label {
                    objectName: "fontCollectorLogEmpty"
                    anchors.fill: parent
                    anchors.margins: 12
                    visible: fontList.count === 0
                    wrapMode: Text.Wrap
                    color: Theme.muted
                    font: dialog.fontAt(12)
                    text: dialog.collector.log.length > 0 ? dialog.collector.logText.trim()
                        : qsTr("Start lists the fonts the subtitles use, and whether this computer has each one.")
                }
            }
        }
    }
    Label {
        objectName: "fontCollectorSummary"
        visible: text.length > 0
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        color: Theme.muted
        font: dialog.fontAt(12)
        text: dialog.summary()
    }
    ColumnLayout {
        objectName: "fontCollectorRunning"
        visible: dialog.working
        spacing: 4
        Layout.fillWidth: true
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 2
            color: Theme.line
            Rectangle {
                objectName: "fontCollectorProgress"
                height: parent.height
                width: parent.width * Math.max(0, dialog.runProgress)
                color: Theme.accent
            }
        }
        RowLayout {
            spacing: 8
            Layout.fillWidth: true
            Label {
                objectName: "fontCollectorDoing"
                font: dialog.fontAt(12)
                elide: Text.ElideRight
                Layout.fillWidth: true
                text: dialog.doingText
            }
            Label {
                text: qsTr("Closing the window keeps it going.")
                color: Theme.muted
                font: dialog.fontAt(12)
            }
            Button {
                objectName: "fontCollectorCancel"
                flat: true
                text: qsTr("Stop")
                onClicked: dialog.collector.cancel()
            }
        }
    }
    Rectangle {
        objectName: "fontCollectorReview"
        visible: dialog.collector.stage === 2
        color: Theme.raised
        radius: Theme.radius
        Layout.fillWidth: true
        implicitHeight: Theme.headerHeight
        RowLayout {
            id: reviewRow
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 4
            spacing: 8
            Icon {
                iconRole: dialog.collector.reviewIncomplete ? "notice-warning" : "notice-info"
                color: dialog.collector.reviewIncomplete ? Theme.warning : IconTheme.normal
                accentColor: dialog.collector.reviewIncomplete ? Theme.warning : IconTheme.accent
            }
            Label {
                objectName: "fontCollectorReviewText"
                Layout.fillWidth: true
                elide: Text.ElideMiddle
                font: dialog.fontAt(12)
                text: dialog.collector.reviewIncomplete ? qsTr("Some fonts were not found, so the collection is incomplete.")
                    : dialog.zip ? qsTr("Ready to write %1").arg(dialog.collector.copyPath)
                    : dialog.linking ? qsTr("Ready to link into %1").arg(dialog.collector.copyPath)
                    : qsTr("Ready to copy to %1").arg(dialog.collector.copyPath)
            }
            CheckBox {
                id: acknowledge
                objectName: "fontCollectorAcknowledge"
                visible: dialog.collector.reviewIncomplete
                text: dialog.zip ? qsTr("Write it, labelled incomplete")
                    : dialog.linking ? qsTr("Link it, labelled incomplete") : qsTr("Copy it, labelled incomplete")
            }
            Button {
                objectName: "fontCollectorApply"
                highlighted: true
                text: dialog.zip ? qsTr("Write archive") : dialog.linking ? qsTr("Link fonts") : qsTr("Copy fonts")
                enabled: !dialog.collector.reviewIncomplete || acknowledge.checked
                onClicked: dialog.applyFonts()
            }
        }
    }

    leadingActions: Button {
        objectName: "fontCollectorSaveFolder"
        flat: true
        text: qsTr("Show folder")
        visible: dialog.collector.action !== 0
        enabled: dialog.collector.canSaveFolder && !dialog.working
        onClicked: dialog.collector.saveFolder()
    }
    buttons: [
        FooterButton {
            objectName: "fontCollectorClose"
            text: qsTr("Close")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: dialog.close()
        },
        Button {
            id: startButton
            objectName: "fontCollectorStart"
            highlighted: true
            text: qsTr("Start")
            enabled: !dialog.working
            rightPadding: 12 + startChevron.width + 8
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: dialog.start(false)
            Rectangle {
                anchors.right: startChevron.left
                anchors.verticalCenter: parent.verticalCenter
                width: 1
                height: parent.height - 8
                color: Theme.onAccent
                visible: startButton.enabled
            }
            ToolButton {
                id: startChevron
                objectName: "fontCollectorStartMenu"
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 20
                height: parent.height
                enabled: startButton.enabled
                focusPolicy: Qt.NoFocus
                Accessible.name: qsTr("More ways to start")
                contentItem: Icon {
                    iconRole: "chevron-down"
                    size: 12
                    color: startButton.enabled ? Theme.onAccent : Theme.disabled
                    accentColor: color
                }
                background: null
                onClicked: startMenu.popup(startButton, 0, startButton.height)
            }
            Keys.onDownPressed: event => {
                if (event.modifiers & Qt.AltModifier)
                    startMenu.popup(startButton, 0, startButton.height)
                else
                    event.accepted = false
            }
        }
    ]
    ShellMenu {
        id: startMenu
        objectName: "fontCollectorStartMenuPopup"
        ShellMenuItem {
            objectName: "fontCollectorStartOnTabs"
            text: qsTr("Start on all tabs")
            onTriggered: dialog.start(true)
        }
    }

    MessageHost {
        id: messages
        objectName: "fontCollectorMessages"
        settings: dialog.settings
    }
    FolderChooser {
        id: folderDialog
        objectName: "fontCollectorFolderDialog"
        title: qsTr("Choose save folder")
        onAccepted: dialog.chosen(dialog.collector.localPath(selectedFolder))
    }
    FileChooser {
        id: archiveDialog
        objectName: "fontCollectorArchiveDialog"
        title: qsTr("Select the name of the archive")
        fileMode: Dialogs.FileDialog.SaveFile
        defaultSuffix: "zip"
        nameFilters: [qsTr("Archive files (*.zip)")]
        onAccepted: dialog.chosen(dialog.collector.localPath(selectedFile))
    }
}
