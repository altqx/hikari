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
    required property ColourPickerController colourPicker
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
        { id: "tmp", text: "TMPlayer" },
        { id: "ttml", text: "TTML (IMSC1)" },
        { id: "dfxp", text: "DFXP" },
        { id: "srv3", text: qsTr("YouTube timed text (srv3)") }
    ]
    readonly property string format: formats[Math.max(0, formatChoice.currentIndex)].id
    readonly property var lineChoices: ["all", "selected", "shown"]
    property var setup: ({})
    property var report: null
    readonly property bool reported: report !== null
    property string problem: ""
    readonly property bool ttml: format === "ttml" || format === "dfxp"
    readonly property bool usesFrames: format === "mdvd" || (ttml && (ttmlNetflix.checked || ttmlFrames.checked))
    readonly property bool fpsValid: !usesFrames || (fpsNumber() >= 1 && fpsNumber() <= 1000)
    property var srv3Rows: []
    property int srv3Revision: 0
    readonly property int srv3Hidden: {
        srv3Revision
        return srv3Rows.filter(r => !r[lineChoices[Math.max(0, linesChoice.currentIndex)]]).length
    }
    readonly property var srv3Shadows: [
        { bit: 1, text: qsTr("Glow"), name: qsTr("Glow shadow for %1") },
        { bit: 4, text: qsTr("Soft"), name: qsTr("Soft shadow for %1") },
        { bit: 2, text: qsTr("Hard"), name: qsTr("Hard shadow for %1") },
        { bit: 8, text: qsTr("Bevel"), name: qsTr("Bevel shadow for %1") }
    ]
    readonly property var srv3Colours: [
        { key: "text", icon: "colour-primary", tip: qsTr("Current word text"), name: qsTr("Current word text colour for %1") },
        { key: "outline", icon: "colour-outline", tip: qsTr("Current word outline"), name: qsTr("Current word outline colour for %1") },
        { key: "shadow", icon: "colour-shadow", tip: qsTr("Current word shadow"), name: qsTr("Current word shadow colour for %1") }
    ]

    function openDialog() {
        setup = app.exportSetup()
        if (!setup.format)
            return
        report = null
        problem = ""
        srv3Rows = JSON.parse(JSON.stringify(setup.srv3Styles || []))
        srv3All.checked = false
        ++srv3Revision
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
        ttmlNetflix.checked = o.ttmlNetflix
        ttmlFrames.checked = o.ttmlFrames
        language.text = o.language
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
            ttmlNetflix: ttmlNetflix.checked, ttmlFrames: ttmlFrames.checked, language: language.text.trim(),
            fps: fpsNumber(), replace: replace, srv3Styles: srv3Rows
        }
    }
    function srv3Set(index, key, value) {
        srv3Rows[index].value[key] = value
        ++srv3Revision
    }
    function srv3Changed(row) {
        return JSON.stringify(row.value) !== JSON.stringify(row["default"])
    }
    function srv3Reset(index) {
        srv3Rows[index].value = JSON.parse(JSON.stringify(srv3Rows[index]["default"]))
        ++srv3Revision
    }
    function srv3Hex(c) {
        return "#" + [c.r, c.g, c.b].map(v => v.toString(16).padStart(2, "0")).join("").toUpperCase()
    }
    function pickSrv3Colour(swatch, index, key, name) {
        srv3Colour.index = index
        srv3Colour.key = key
        srv3Popover.openFor(swatch, srv3Colour, () => srv3Colour.begin(), { title: name, alpha: false, clear: true, focus: swatch })
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
            IconTextButton {
                objectName: "exportBrowse"
                iconRole: "folder-open"
                text: qsTr("Browse…")
                onClicked: {
                    const path = destination.text
                    const slash = Math.max(path.lastIndexOf("/"), path.lastIndexOf("\\"))
                    if (slash > 0)
                        picker.currentFolder = dialog.app.fileUrl(path.slice(0, slash))
                    picker.selectedFile = dialog.app.fileUrl(path)
                    picker.nameFilters = [qsTr("%1 files (*.%2)").arg(dialog.formats[formatChoice.currentIndex].text,
                                                                         dialog.extension(dialog.format)),
                                          qsTr("All files (*)")]
                    picker.open()
                }
            }
        }

        RowLabel { text: qsTr("Options"); Layout.alignment: Qt.AlignTop; Layout.topMargin: 4 }
        ColumnLayout {
            spacing: 6
            Layout.fillWidth: true
            Frame {
                objectName: "exportSrv3Styles"
                visible: dialog.format === "srv3" && dialog.srv3Rows.length > 0
                Layout.fillWidth: true
                padding: 1
                background: Rectangle { color: Theme.field; border.color: Theme.line }
                Accessible.role: Accessible.Table
                Accessible.name: qsTr("srv3 style options")
                ColumnLayout {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    spacing: 0
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 10
                        Layout.rightMargin: 6
                        Layout.preferredHeight: 28
                        spacing: 10
                        Label { text: qsTr("Style"); color: Theme.muted; font: dialog.fontAt(12); Layout.fillWidth: true }
                        Label { text: qsTr("Shadow"); color: Theme.muted; font: dialog.fontAt(12); Layout.preferredWidth: 220 }
                        Label { text: qsTr("Karaoke"); color: Theme.muted; font: dialog.fontAt(12); Layout.preferredWidth: 62 }
                        Row {
                            spacing: 6
                            Layout.preferredWidth: 102
                            Repeater {
                                model: dialog.srv3Colours
                                Item {
                                    required property var modelData
                                    width: 30
                                    height: 16
                                    Icon { anchors.centerIn: parent; iconRole: parent.modelData.icon; size: 14 }
                                    HoverHandler { id: colourHover }
                                    ToolTip.visible: colourHover.hovered
                                    ToolTip.text: modelData.tip
                                }
                            }
                        }
                        Item { Layout.preferredWidth: 26 }
                    }
                    Repeater {
                        model: dialog.srv3Rows.length
                        FocusScope {
                            id: srv3Row
                            required property int index
                            readonly property var row: { dialog.srv3Revision; return dialog.srv3Rows[index] }
                            readonly property var value: { dialog.srv3Revision; return Object.assign({}, row.value) }
                            readonly property bool changed: { dialog.srv3Revision; return dialog.srv3Changed(row) }
                            function step(from, delta) {
                                const controls = [glow.itemAt(0), glow.itemAt(1), glow.itemAt(2), glow.itemAt(3), karaoke,
                                                  swatches.itemAt(0), swatches.itemAt(1), swatches.itemAt(2), reset]
                                const usable = controls.filter(c => c && c.enabled && c.visible)
                                const at = usable.indexOf(from)
                                const next = usable[at + delta]
                                if (next)
                                    next.forceActiveFocus(Qt.TabFocusReason)
                            }
                            objectName: "exportSrv3Row" + index
                            visible: srv3All.checked || row[dialog.lineChoices[Math.max(0, linesChoice.currentIndex)]] === true
                            Layout.fillWidth: true
                            Layout.preferredHeight: 34
                            Rectangle { anchors.fill: parent; color: srv3Row.activeFocus ? Theme.select : "transparent" }
                            Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.line }
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 6
                                spacing: 10
                                RowLayout {
                                    spacing: 6
                                    Layout.fillWidth: true
                                    Rectangle {
                                        width: 6; height: 6; radius: 3
                                        color: srv3Row.changed ? Theme.accent : "transparent"
                                    }
                                    Label {
                                        text: srv3Row.row.name
                                        elide: Text.ElideRight
                                        Layout.fillWidth: srv3Row.row.builtIn !== true
                                        Layout.maximumWidth: implicitWidth
                                    }
                                    Label {
                                        visible: srv3Row.row.builtIn === true
                                        text: "YTSubConverter"
                                        color: Theme.muted
                                        font: dialog.fontAt(11)
                                        leftPadding: 6
                                        rightPadding: 6
                                        background: Rectangle { color: "transparent"; border.color: Theme.line; radius: height / 2 }
                                    }
                                    Item { Layout.fillWidth: true; visible: srv3Row.row.builtIn === true }
                                }
                                Row {
                                    spacing: 3
                                    Layout.preferredWidth: 220
                                    Repeater {
                                        id: glow
                                        model: dialog.srv3Shadows
                                        Button {
                                            required property var modelData
                                            required property int index
                                            objectName: "exportSrv3Shadow" + modelData.bit
                                            width: 52
                                            height: 24
                                            checkable: true
                                            enabled: srv3Row.row.hasShadow === true
                                            checked: (srv3Row.value.shadows & modelData.bit) !== 0
                                            text: modelData.text
                                            font: dialog.fontAt(12)
                                            activeFocusOnTab: index === 0 && srv3Row.row.hasShadow === true
                                            Accessible.name: modelData.name.arg(srv3Row.row.name)
                                            ToolTip.visible: hovered && !enabled
                                            ToolTip.text: qsTr("%1 has no shadow (its Shadow is 0), so no shadow type applies.").arg(srv3Row.row.name)
                                            onToggled: {
                                                dialog.srv3Set(srv3Row.index, "shadows", checked ? srv3Row.value.shadows | modelData.bit
                                                                                                 : srv3Row.value.shadows & ~modelData.bit)
                                                checked = Qt.binding(() => (srv3Row.value.shadows & modelData.bit) !== 0)
                                            }
                                            Keys.onLeftPressed: srv3Row.step(this, -1)
                                            Keys.onRightPressed: srv3Row.step(this, 1)
                                        }
                                    }
                                }
                                CheckBox {
                                    id: karaoke
                                    objectName: "exportSrv3Karaoke"
                                    Layout.preferredWidth: 62
                                    checked: srv3Row.value.karaoke === true
                                    activeFocusOnTab: srv3Row.row.hasShadow !== true
                                    Accessible.name: qsTr("Karaoke for %1").arg(srv3Row.row.name)
                                    onToggled: {
                                        dialog.srv3Set(srv3Row.index, "karaoke", checked)
                                        checked = Qt.binding(() => srv3Row.value.karaoke === true)
                                    }
                                    Keys.onLeftPressed: srv3Row.step(this, -1)
                                    Keys.onRightPressed: srv3Row.step(this, 1)
                                }
                                Row {
                                    spacing: 6
                                    Layout.preferredWidth: 102
                                    Repeater {
                                        id: swatches
                                        model: dialog.srv3Colours
                                        Button {
                                            id: swatch
                                            required property var modelData
                                            readonly property string colour: srv3Row.value[modelData.key] || ""
                                            objectName: "exportSrv3Colour_" + modelData.key
                                            width: 30
                                            height: 22
                                            enabled: srv3Row.value.karaoke === true
                                            activeFocusOnTab: false
                                            Accessible.name: modelData.name.arg(srv3Row.row.name)
                                                             + ", " + (colour.length > 0 ? colour : qsTr("no colour"))
                                            ToolTip.visible: hovered
                                            ToolTip.text: colour.length > 0 ? modelData.tip + " " + colour : modelData.tip + ": " + qsTr("no colour")
                                            onClicked: dialog.pickSrv3Colour(swatch, srv3Row.index, modelData.key, modelData.tip + " · " + srv3Row.row.name)
                                            Keys.onLeftPressed: srv3Row.step(this, -1)
                                            Keys.onRightPressed: srv3Row.step(this, 1)
                                            contentItem: Item {}
                                            background: Rectangle {
                                                color: swatch.colour.length > 0 ? swatch.colour : Theme.field
                                                border.color: swatch.activeFocus ? Theme.focus : Theme.line
                                                border.width: swatch.activeFocus ? 2 : 1
                                                radius: 2
                                                opacity: swatch.enabled ? 1 : 0.45
                                                clip: true
                                                Rectangle {
                                                    visible: swatch.colour.length === 0
                                                    anchors.centerIn: parent
                                                    width: parent.width * 1.2
                                                    height: 1
                                                    rotation: -35
                                                    color: Theme.danger
                                                }
                                            }
                                        }
                                    }
                                }
                                IconToolButton {
                                    id: reset
                                    objectName: "exportSrv3Reset"
                                    Layout.preferredWidth: 26
                                    iconRole: "restore-default"
                                    text: qsTr("Reset %1").arg(srv3Row.row.name)
                                    opacity: srv3Row.changed ? 1 : 0
                                    enabled: srv3Row.changed
                                    activeFocusOnTab: false
                                    onClicked: dialog.srv3Reset(srv3Row.index)
                                    Keys.onLeftPressed: srv3Row.step(this, -1)
                                    Keys.onRightPressed: srv3Row.step(this, 1)
                                }
                            }
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        Layout.margins: 6
                        Layout.leftMargin: 10
                        wrapMode: Text.Wrap
                        color: Theme.muted
                        font: dialog.fontAt(12)
                        text: qsTr("Shadow is how YouTube draws the style’s shadow; an outline is always drawn as a glow. Current-word colours apply to karaoke lines.")
                        Rectangle { anchors.bottom: parent.top; anchors.bottomMargin: 6; width: parent.width + 16; x: -10; height: 1; color: Theme.line }
                    }
                }
            }
            CheckBox {
                id: srv3All
                objectName: "exportSrv3All"
                visible: dialog.format === "srv3" && dialog.srv3Hidden > 0
                text: qsTr("Show all styles (%1)").arg(dialog.srv3Rows.length)
            }
            CheckBox {
                id: srtAlignment
                objectName: "exportSrtAlignment"
                visible: dialog.format === "srt"
                // lupdate reads "\\a" in a QML string as a bell character.
                text: qsTr("Alignment as %1 tags").arg("{\\an}")
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
                id: ttmlNetflix
                objectName: "exportTtmlNetflix"
                visible: dialog.ttml
                text: qsTr("Netflix timed-text style")
            }
            CheckBox {
                id: ttmlFrames
                objectName: "exportTtmlFrames"
                visible: dialog.ttml && !ttmlNetflix.checked
                text: qsTr("Times in frames")
            }
            RowLayout {
                visible: dialog.ttml
                spacing: 6
                Layout.leftMargin: merge.leftPadding
                Label { text: qsTr("Language") }
                TextField {
                    id: language
                    objectName: "exportLanguage"
                    Layout.preferredWidth: 80
                    selectByMouse: true
                    placeholderText: "en"
                    validator: RegularExpressionValidator { regularExpression: /^[A-Za-z0-9-]{0,35}$/ }
                    Accessible.name: qsTr("Language")
                }
            }
            CheckBox {
                id: merge
                objectName: "exportMerge"
                visible: ["srt", "sbv", "mdvd", "mpl2"].indexOf(dialog.format) >= 0
                text: qsTr("Merge overlapping lines into one subtitle")
            }
            RowLayout {
                visible: dialog.usesFrames
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
                IconTextButton {
                    objectName: "exportFpsFromVideo"
                    iconRole: "fps-from-video"
                    visible: (dialog.setup.videoFps ?? 0) > 0
                    text: qsTr("Use video frame rate")
                    onClicked: fps.text = Number(dialog.setup.videoFps).toFixed(3).replace(/\.?0+$/, "")
                }
            }
            Hint {
                visible: dialog.ttml && ttmlNetflix.checked
                text: qsTr("Text stays white and centred at the top or bottom, only italics are kept, and times are in frames.")
            }
            Hint {
                visible: dialog.format === "tmp"
                text: qsTr("TMPlayer has no end times: overlapping lines are merged, and an empty subtitle clears the screen.")
            }
            Hint {
                objectName: "exportSrv3Hint"
                visible: dialog.format === "srv3"
                text: qsTr("Converted as YTSubConverter 1.6.5 converts it. Fonts and graphics attached to the file are not exported.")
            }
            Hint {
                visible: dialog.format === "srv3" && dialog.srv3Rows.length > 0
                text: qsTr("Changed options are saved in the subtitle file when you export.")
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
            text: dialog.reported ? qsTr("Exported %n line(s) to “%1”.", "", dialog.report.written)
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
        Hint {
            objectName: "exportSrv3Saved"
            visible: dialog.reported && (dialog.report.srv3Styles ?? 0) > 0
            text: dialog.reported ? qsTr("Style options for %n style(s) saved in the subtitle file.", "", dialog.report.srv3Styles ?? 0) : ""
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
                    Accessible.name: qsTr("Line %1: %2").arg(modelData.number, modelData.losses)
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

    ColourPopover {
        id: srv3Popover
        objectName: "exportSrv3ColourPopover"
        picker: dialog.colourPicker
    }
    QtObject {
        id: srv3Colour
        property int index: 0
        property string key: "text"
        property string before: ""
        function begin() {
            before = dialog.srv3Rows[index].value[key] || ""
            const c = before.length === 7 ? before : "#FFFFFF"
            return { r: parseInt(c.slice(1, 3), 16), g: parseInt(c.slice(3, 5), 16), b: parseInt(c.slice(5, 7), 16), a: 0 }
        }
        function changeColour(c) {
            dialog.srv3Set(index, key, dialog.srv3Hex(c))
            return true
        }
        function clearColour() {
            dialog.srv3Set(index, key, "")
        }
        function endDialog(accepted) {
            if (!accepted)
                dialog.srv3Set(index, key, before)
        }
    }
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
