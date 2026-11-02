pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Export"
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: dialog
    objectName: "exportDialog"
    required property var app
    settings: app.settings
    dialogId: "export"
    title: qsTr("Export as")
    modal: true
    defaultButton: reported ? closeButton : exportButton
    initialFocus: formatChoice
    Component.onCompleted: IconTheme.setWindowIcon(dialog, "export")

    readonly property var formats: [
        { id: "srt", text: "SubRip (SRT)" },
        { id: "vtt", text: "WebVTT" },
        { id: "sbv", text: "YouTube SBV" },
        { id: "txt", text: qsTr("Plain text") },
        { id: "mdvd", text: "MicroDVD" },
        { id: "mpl2", text: "MPL2" },
        { id: "tmp", text: "TMPlayer" }
    ]
    readonly property string format: formats[Math.max(0, formatChoice.currentIndex)].id
    readonly property var lineChoices: ["all", "selected", "shown"]
    property var setup: ({})
    property var report: null
    readonly property bool reported: report !== null
    property string problem: ""
    readonly property bool fpsValid: format !== "mdvd" || (fpsNumber() >= 1 && fpsNumber() <= 1000)

    function openDialog() {
        setup = app.exportSetup()
        if (!setup.format)
            return
        report = null
        problem = ""
        const o = setup.options
        formatChoice.currentIndex = Math.max(0, formats.findIndex(f => f.id === setup.format))
        linesChoice.currentIndex = Math.max(0, lineChoices.indexOf(o.lines))
        merge.checked = o.mergeOverlaps
        srtColour.checked = o.srtFontColour
        srtAlignment.checked = o.srtAlignmentTags
        vttPositions.checked = o.vttPositions
        vttStyles.checked = o.vttStyles
        vttKaraoke.checked = o.vttKaraoke
        textTimes.checked = o.textTimes
        textActors.checked = o.textActors
        fps.text = Number(o.fps).toFixed(3).replace(/\.?0+$/, "")
        destination.text = suggestedPath()
        open()
    }
    function extension(id) {
        return setup.extensions ? setup.extensions[id] : "srt"
    }
    function suggestedPath() {
        const folder = setup.folder && setup.folder.toString() !== "" ? app.localPath(setup.folder) : ""
        const name = setup.name || qsTr("Untitled")
        return (folder.length > 0 ? folder + "/" : "") + name + "." + extension(format)
    }
    function withExtension(path) {
        const slash = Math.max(path.lastIndexOf("/"), path.lastIndexOf("\\"))
        const dot = path.lastIndexOf(".")
        const stem = dot > slash ? path.slice(0, dot) : path
        return stem + "." + extension(format)
    }
    function fpsNumber() {
        return Number(fps.text.replace(",", "."))
    }
    function choice(replace) {
        return {
            lines: lineChoices[linesChoice.currentIndex], mergeOverlaps: merge.checked, srtFontColour: srtColour.checked,
            srtAlignmentTags: srtAlignment.checked, vttPositions: vttPositions.checked, vttStyles: vttStyles.checked,
            vttKaraoke: vttKaraoke.checked, textTimes: textTimes.checked, textActors: textActors.checked,
            fps: fpsNumber(), replace: replace
        }
    }
    function run(replace) {
        const result = app.exportTo(app.fileUrl(destination.text.trim()), format, choice(replace))
        if (result.exists) {
            replaceQuestion.ask({
                title: qsTr("Export as"),
                text: qsTr("Replace “%1”?").arg(result.name),
                detail: qsTr("A file with this name already exists."),
                severity: "warning",
                choices: [
                    { id: "replace", text: qsTr("Replace"), role: "destructive" },
                    { id: "cancel", text: qsTr("Cancel"), role: "reject", isDefault: true }
                ]
            }, id => { if (id === "replace") dialog.run(true) })
            return
        }
        if (!result.ok) {
            problem = result.problem
            return
        }
        problem = ""
        report = result
        noteList.currentIndex = -1
        closeButton.forceActiveFocus()
    }
    function show(index) {
        if (report && index >= 0 && index < report.notes.length)
            app.showKaraokeLine(report.document, report.notes[index].line)
    }

    component Hint: Label {
        Layout.fillWidth: true
        Layout.preferredWidth: 280
        wrapMode: Text.Wrap
        color: Theme.muted
        font: dialog.fontAt(12)
    }
    component RowLabel: Label {
        Layout.preferredWidth: 72
        Layout.alignment: Qt.AlignVCenter
    }

    GridLayout {
        visible: !dialog.reported
        columns: 2
        columnSpacing: 8
        rowSpacing: 10
        Layout.fillWidth: true

        RowLabel { text: qsTr("Format") }
        ComboBox {
            id: formatChoice
            objectName: "exportFormat"
            model: dialog.formats
            textRole: "text"
            Layout.preferredWidth: 214
            Accessible.name: qsTr("Format")
            onActivated: destination.text = dialog.withExtension(destination.text)
        }

        RowLabel { text: qsTr("Lines") }
        SegmentedButtons {
            id: linesChoice
            objectName: "exportLines"
            Accessible.name: qsTr("Lines")
            choices: [
                { text: qsTr("All (%1)").arg(dialog.setup.all ?? 0), name: "exportLinesAll" },
                { text: qsTr("Selected (%1)").arg(dialog.setup.selected ?? 0), name: "exportLinesSelected" },
                { text: qsTr("Shown (%1)").arg(dialog.setup.shown ?? 0), name: "exportLinesShown" }
            ]
            onActivated: index => currentIndex = index
        }

        RowLabel { text: qsTr("Save to") }
        RowLayout {
            spacing: 8
            TextField {
                id: destination
                objectName: "exportDestination"
                selectByMouse: true
                Layout.fillWidth: true
                Layout.minimumWidth: 300
                Accessible.name: qsTr("Save to")
            }
            Button {
                objectName: "exportBrowse"
                text: qsTr("Browse…")
                onClicked: {
                    const path = destination.text
                    const slash = Math.max(path.lastIndexOf("/"), path.lastIndexOf("\\"))
                    if (slash > 0)
                        picker.currentFolder = dialog.app.fileUrl(path.slice(0, slash))
                    picker.selectedFile = dialog.app.fileUrl(path)
                    picker.nameFilters = [qsTr("%1 files (*.%2)").arg(dialog.formats[formatChoice.currentIndex].text)
                                              .arg(dialog.extension(dialog.format)),
                                          qsTr("All files (*)")]
                    picker.open()
                }
            }
        }

        RowLabel { text: qsTr("Options"); Layout.alignment: Qt.AlignTop; Layout.topMargin: 4 }
        ColumnLayout {
            spacing: 6
            Layout.fillWidth: true
            CheckBox {
                id: srtAlignment
                objectName: "exportSrtAlignment"
                visible: dialog.format === "srt"
                // lupdate reads "\\a" in a QML string as a bell character.
                text: qsTr("Keep alignment as %1 tags").arg("{\\an}")
            }
            CheckBox {
                id: srtColour
                objectName: "exportSrtColour"
                visible: dialog.format === "srt"
                text: qsTr("Text colours as font tags")
            }
            CheckBox {
                id: vttPositions
                objectName: "exportVttPositions"
                visible: dialog.format === "vtt"
                text: qsTr("Positions and alignment")
            }
            CheckBox {
                id: vttStyles
                objectName: "exportVttStyles"
                visible: dialog.format === "vtt"
                text: qsTr("Styles and colours")
            }
            CheckBox {
                id: vttKaraoke
                objectName: "exportVttKaraoke"
                visible: dialog.format === "vtt"
                text: qsTr("Karaoke timing")
            }
            CheckBox {
                id: textTimes
                objectName: "exportTextTimes"
                visible: dialog.format === "txt"
                text: qsTr("Start and end times")
            }
            CheckBox {
                id: textActors
                objectName: "exportTextActors"
                visible: dialog.format === "txt"
                text: qsTr("Actor names")
            }
            CheckBox {
                id: merge
                objectName: "exportMerge"
                visible: ["srt", "sbv", "mdvd", "mpl2"].indexOf(dialog.format) >= 0
                text: qsTr("Merge overlapping lines into one subtitle")
            }
            RowLayout {
                visible: dialog.format === "mdvd"
                spacing: 6
                Layout.leftMargin: merge.leftPadding
                Label { text: qsTr("Frame rate") }
                TextField {
                    id: fps
                    objectName: "exportFps"
                    Layout.preferredWidth: 80
                    horizontalAlignment: TextInput.AlignRight
                    selectByMouse: true
                    validator: RegularExpressionValidator { regularExpression: /^\d{0,4}([.,]\d{0,6})?$/ }
                    Accessible.name: qsTr("Frame rate")
                }
                Label { text: qsTr("fps"); color: Theme.muted }
                Button {
                    objectName: "exportFpsFromVideo"
                    visible: (dialog.setup.videoFps ?? 0) > 0
                    text: qsTr("From video")
                    onClicked: fps.text = Number(dialog.setup.videoFps).toFixed(3).replace(/\.?0+$/, "")
                }
            }
            Hint {
                visible: dialog.format === "tmp"
                text: qsTr("TMPlayer has no end times: overlapping lines are merged, and an empty subtitle clears the screen.")
            }
            Hint {
                visible: dialog.format === "sbv" || dialog.format === "txt"
                text: qsTr("Formatting, colours and positions are not kept.")
            }
        }

        Item { implicitWidth: 1; visible: dialog.problem.length > 0 }
        FieldError {
            objectName: "exportProblem"
            visible: dialog.problem.length > 0
            text: dialog.problem
            Layout.fillWidth: true
        }
    }

    ColumnLayout {
        visible: dialog.reported
        spacing: 10
        Layout.fillWidth: true
        Layout.fillHeight: true
        Label {
            objectName: "exportSummary"
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: dialog.reported ? qsTr("Exported %n line(s) to %1.", "", dialog.report.written)
                                        .arg(dialog.app.localPath(dialog.report.file).replace(/^.*[\\/]/, "")) : ""
        }
        Hint {
            objectName: "exportSkipped"
            visible: dialog.reported && (dialog.report.comments > 0 || dialog.report.empty > 0)
            text: !dialog.reported ? "" : [
                dialog.report.comments > 0 ? qsTr("%n comment line(s) skipped.", "", dialog.report.comments) : "",
                dialog.report.empty > 0 ? qsTr("%n line(s) with nothing to show skipped.", "", dialog.report.empty) : ""
            ].filter(s => s.length > 0).join(" ")
        }
        Label {
            visible: dialog.reported && dialog.report.notes.length > 0
            text: qsTr("What the format could not keep. Choose a line to show it.")
            color: Theme.muted
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }
        Frame {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 560
            Layout.preferredHeight: 240
            padding: 1
            background: Rectangle { color: Theme.field; border.color: Theme.line }
            Label {
                anchors.centerIn: parent
                objectName: "exportNothingLost"
                visible: noteList.count === 0
                color: Theme.muted
                text: qsTr("Nothing was dropped or approximated")
            }
            ListView {
                id: noteList
                objectName: "exportNotes"
                anchors.fill: parent
                clip: true
                model: dialog.reported ? dialog.report.notes : []
                keyNavigationEnabled: true
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Export report")
                // MUST KILL: clicking stops onCurrentIndexChanged showing the row a click shows itself
                property bool clicking: false
                onCurrentIndexChanged: if (activeFocus && !clicking) dialog.show(currentIndex)
                header: RowLayout {
                    visible: noteList.count > 0
                    width: ListView.view.width
                    height: visible ? implicitHeight + 8 : 0
                    spacing: 12
                    Label { text: qsTr("Line"); color: Theme.muted; Layout.preferredWidth: 48; Layout.leftMargin: 12 }
                    Label { text: qsTr("Dropped or approximated"); color: Theme.muted; Layout.fillWidth: true }
                }
                delegate: ItemDelegate {
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    highlighted: ListView.isCurrentItem
                    focusPolicy: Qt.NoFocus
                    Accessible.name: qsTr("Line %1: %2").arg(modelData.number).arg(modelData.losses)
                    contentItem: RowLayout {
                        spacing: 12
                        Label { text: modelData.number; Layout.preferredWidth: 48 }
                        Label { text: modelData.losses; Layout.fillWidth: true; elide: Text.ElideRight }
                    }
                    onClicked: {
                        noteList.forceActiveFocus()
                        noteList.clicking = true
                        noteList.currentIndex = index
                        noteList.clicking = false
                        dialog.show(index)
                    }
                }
                Keys.onReturnPressed: dialog.show(currentIndex)
                Keys.onEnterPressed: dialog.show(currentIndex)
            }
        }
    }

    buttons: [
        PrimaryButton {
            id: exportButton
            objectName: "exportRun"
            visible: !dialog.reported
            text: qsTr("Export")
            enabled: destination.text.trim().length > 0 && dialog.fpsValid
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: dialog.run(false)
        },
        Button {
            id: closeButton
            objectName: "exportClose"
            text: dialog.reported ? qsTr("Close") : qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: dialog.close()
        }
    ]

    FileDialog {
        id: picker
        objectName: "exportPicker"
        fileMode: FileDialog.SaveFile
        title: qsTr("Export as")
        onAccepted: {
            destination.text = dialog.app.localPath(selectedFile)
            dialog.run(true)
        }
    }

    LazyMessageBox {
        id: replaceQuestion
        name: "exportReplaceQuestion"
        settings: dialog.app.settings
    }
}
