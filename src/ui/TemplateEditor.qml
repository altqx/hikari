pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.TemplateEditor"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Pane {
    id: root
    required property TemplateEditorController controller
    property VideoController video: null
    implicitWidth: 520
    implicitHeight: 640
    padding: 0
    readonly property size minimumSize: Qt.size(360, 260)
    readonly property bool narrow: width < 520
    property bool previewUnfolded: false
    readonly property Item toolbar: headerBar

    readonly property font monoFont: Qt.font({ family: "monospace", pixelSize: Qt.application.font.pixelSize })

    function milliseconds(seconds) {
        const ms = seconds * 1000
        //: A time a template took: %1 milliseconds
        return ms < 1000 ? qsTr("%1 ms").arg(ms < 10 ? ms.toFixed(1) : Math.round(ms))
                         //: A time a template took: %1 seconds
                         : qsTr("%1 s").arg(seconds.toFixed(2))
    }
    // Without a catalog Qt picks no English plural form, so one is spelled out.
    function lineCount(n) {
        return n === 1 ? qsTr("1 line") : qsTr("%n lines", "", n)
    }
    function counted(n, one, many) {
        return n === 1 ? one : many
    }
    function karaokeLineCount(n) {
        return counted(n, qsTr("1 karaoke line"), qsTr("%n karaoke lines", "", n))
    }
    function rowStatus(row) {
        if (row.seconds < 0)
            return ""
        if (row.kind === "code")
            return milliseconds(row.seconds)
        return lineCount(row.generated) + " · " + milliseconds(row.seconds)
    }

    RowLayout {
        id: headerBar
        objectName: "templateHeaderBar"
        visible: parent !== root.contentItem
        spacing: 2
        IconToolButton {
            objectName: "templatePreviewOnVideo"
            iconRole: "show"
            text: qsTr("Preview on the video")
            checkable: true
            checked: root.controller.previewOnVideo
            onToggled: root.controller.previewOnVideo = checked
            Accessible.checkable: true
            Accessible.checked: checked
        }
        IconToolButton {
            id: loopButton
            objectName: "templateLoop"
            iconRole: "loop"
            text: qsTr("Loop the line")
            checkable: true
            enabled: root.video !== null && root.video.hasVideo && root.controller.source !== 0
            checked: root.video !== null && root.video.looping
            property var span: null
            function loopSource() {
                span = [root.controller.sourceStartMs, root.controller.sourceEndMs]
                root.video.loopRange(span[0], span[1])
            }
            onToggled: {
                if (checked)
                    loopSource()
                else
                    root.video.pause()
                checked = Qt.binding(() => root.video !== null && root.video.looping)
            }
            Connections {
                target: root.controller
                function onPreviewChanged() {
                    if (!root.video || !root.video.looping || !loopButton.span)
                        return
                    if (root.controller.source === 0)
                        root.video.pause()
                    else if (loopButton.span[0] !== root.controller.sourceStartMs
                             || loopButton.span[1] !== root.controller.sourceEndMs)
                        loopButton.loopSource()
                }
            }
            Accessible.checkable: true
            Accessible.checked: checked
        }
    }

    component SectionTitle: RowLayout {
        id: sectionTitle
        property string title
        property string note
        spacing: 8
        Label {
            text: sectionTitle.title
            font.bold: true
        }
        Label {
            Layout.fillWidth: true
            text: sectionTitle.note
            textFormat: Text.PlainText // AutoText would fetch an <img> in a Line's field
            color: Theme.muted
            elide: Text.ElideRight
        }
    }
    component KeyCap: Rectangle {
        id: cap
        property string key
        implicitWidth: Math.max(implicitHeight, capText.implicitWidth + 6)
        implicitHeight: capText.implicitHeight + 2
        radius: 2
        color: "transparent"
        border.width: 1
        border.color: Theme.muted
        Label {
            id: capText
            anchors.centerIn: parent
            text: cap.key
            font.family: root.monoFont.family
            font.pixelSize: Math.max(10, Qt.application.font.pixelSize - 2)
            font.bold: true
        }
    }
    component Rule: Rectangle {
        Layout.fillWidth: true
        implicitHeight: 1
        color: Theme.line
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Label {
            objectName: "templateUnavailable"
            visible: !root.controller.available
            Layout.fillWidth: true
            Layout.margins: 12
            wrapMode: Text.Wrap
            color: Theme.muted
            text: qsTr("The karaoke templater runs on an open subtitle file once its script (kara-templater.lua) is loaded.")
        }

        Flickable {
            id: scroller
            objectName: "templateScroller"
            visible: root.controller.available
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: width
            contentHeight: body.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            ColumnLayout {
                id: body
                width: scroller.width
                spacing: 0

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 12
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        SectionTitle {
                            objectName: "templateSummary"
                            Layout.fillWidth: true
                            title: qsTr("Templates")
                            note: root.controller.templateCount + root.controller.codeCount === 0 ? ""
                                  //: The Templates section's counts: "2 template lines · 1 code line · 18 karaoke lines"
                                  : [root.counted(root.controller.templateCount, qsTr("1 template line"),
                                                  qsTr("%n template lines", "", root.controller.templateCount)),
                                     root.counted(root.controller.codeCount, qsTr("1 code line"),
                                                  qsTr("%n code lines", "", root.controller.codeCount)),
                                     root.karaokeLineCount(root.controller.karaokeCount)].join(" · ")
                        }
                        Button {
                            id: newButton
                            objectName: "templateNew"
                            text: qsTr("New")
                            icon.source: IconTheme.image("add", enabled ? IconTheme.normal : IconTheme.disabled,
                                                         enabled ? IconTheme.accent : IconTheme.disabled, false)
                            icon.color: "transparent"
                            highlighted: root.controller.templateCount + root.controller.codeCount === 0
                            enabled: root.controller.editable
                            down: newMenu.visible
                            onClicked: newMenu.popup(newButton, newButton.width - newMenu.width, newButton.height)
                            Accessible.role: Accessible.ButtonMenu
                        }
                    }

                    Label {
                        objectName: "templateEmpty"
                        visible: root.controller.templateCount + root.controller.codeCount === 0
                        Layout.fillWidth: true
                        Layout.leftMargin: 12
                        wrapMode: Text.Wrap
                        textFormat: Text.StyledText
                        text: "<b>" + qsTr("This document has no template lines yet.") + "</b><br>"
                              + qsTr("A template line is a Comment whose Effect starts with “template”: Apply turns each karaoke line of its Style into new lines.")
                    }

                    Rectangle {
                        visible: root.controller.templateCount + root.controller.codeCount > 0
                        Layout.fillWidth: true
                        implicitHeight: templateRows.implicitHeight + 2
                        color: Theme.field
                        border.width: 1
                        border.color: Theme.line
                        Column {
                            id: templateRows
                            objectName: "templateList"
                            x: 1
                            y: 1
                            width: parent.width - 2
                            Repeater {
                                model: root.controller.templates
                                delegate: Rectangle {
                                    id: templateRow
                                    required property var modelData
                                    required property int index
                                    readonly property bool current: modelData.id === root.controller.current
                                    readonly property bool hasProblem: modelData.problem === true
                                    objectName: "templateRow_" + index
                                    width: templateRows.width
                                    implicitHeight: rowLine.implicitHeight + 10
                                    color: current ? Theme.select : rowMouse.containsMouse ? Theme.raised : "transparent"
                                    Accessible.role: Accessible.ListItem
                                    Accessible.name: qsTr("Line %1, %2").arg(modelData.number, modelData.effect)
                                    Accessible.selected: current
                                    Rectangle {
                                        visible: templateRow.current
                                        width: 3
                                        height: parent.height
                                        color: templateRow.hasProblem ? Theme.danger : Theme.accent
                                    }
                                    RowLayout {
                                        id: rowLine
                                        anchors.fill: parent
                                        anchors.leftMargin: 6
                                        anchors.rightMargin: 8
                                        spacing: 8
                                        Item {
                                            implicitWidth: 14
                                            implicitHeight: 14
                                            Icon {
                                                objectName: "templateRowProblem_" + templateRow.index
                                                anchors.centerIn: parent
                                                visible: templateRow.hasProblem
                                                iconRole: "problem"
                                                size: 14
                                                color: Theme.danger
                                                accentColor: Theme.danger
                                            }
                                            Rectangle {
                                                objectName: "templateRowChanged_" + templateRow.index
                                                anchors.centerIn: parent
                                                visible: !templateRow.hasProblem && templateRow.modelData.changed
                                                width: 6
                                                height: 6
                                                radius: 3
                                                color: Theme.accent
                                            }
                                        }
                                        Label {
                                            Layout.preferredWidth: 28
                                            horizontalAlignment: Text.AlignRight
                                            text: templateRow.modelData.number
                                            color: Theme.muted
                                        }
                                        Label {
                                            Layout.preferredWidth: root.narrow ? -1 : 170
                                            Layout.fillWidth: root.narrow
                                            elide: Text.ElideRight
                                            font: root.monoFont
                                            textFormat: Text.StyledText
                                            text: {
                                                const effect = templateRow.modelData.effect
                                                const space = effect.indexOf(" ")
                                                const head = space < 0 ? effect : effect.slice(0, space)
                                                const rest = space < 0 ? "" : effect.slice(space)
                                                return "<b><font color=\"" + Theme.syntax[9] + "\">" + Qt.escapeHtml(head)
                                                       + "</font></b><font color=\"" + Theme.syntax[10] + "\">"
                                                       + Qt.escapeHtml(rest) + "</font>"
                                            }
                                        }
                                        Label {
                                            visible: !root.narrow
                                            Layout.preferredWidth: 44
                                            elide: Text.ElideRight
                                            text: templateRow.modelData.style
                                            textFormat: Text.PlainText
                                        }
                                        Label {
                                            visible: !root.narrow
                                            Layout.fillWidth: true
                                            elide: Text.ElideRight
                                            font: root.monoFont
                                            color: Theme.muted
                                            text: templateRow.modelData.text
                                            textFormat: Text.PlainText
                                        }
                                        Label {
                                            objectName: "templateRowStatus_" + templateRow.index
                                            horizontalAlignment: Text.AlignRight
                                            font.bold: templateRow.hasProblem
                                            color: templateRow.hasProblem ? Theme.danger : Theme.muted
                                            text: templateRow.hasProblem ? qsTr("Problem") : root.rowStatus(templateRow.modelData)
                                        }
                                    }
                                    MouseArea {
                                        id: rowMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        onClicked: root.controller.current = templateRow.modelData.id
                                    }
                                }
                            }
                        }
                    }
                }

                Rule {}

                ColumnLayout {
                    objectName: "templateLine"
                    visible: root.controller.current !== 0
                    Layout.fillWidth: true
                    Layout.margins: 12
                    spacing: 8
                    SectionTitle {
                        objectName: "templateLineTitle"
                        Layout.fillWidth: true
                        title: qsTr("Line %1").arg(root.controller.currentNumber)
                        note: root.controller.effect
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: root.narrow ? 4 : 6
                        columnSpacing: 8
                        rowSpacing: 6
                        Label {
                            text: qsTr("Effect")
                            Layout.columnSpan: root.narrow ? 4 : 1
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.columnSpan: root.narrow ? 4 : 1
                            spacing: 0
                            TextField {
                                id: effectField
                                objectName: "templateEffect"
                                Layout.fillWidth: true
                                font: root.monoFont
                                enabled: root.controller.editable
                                text: root.controller.effect
                                onTextEdited: root.controller.setEffect(text)
                                onEditingFinished: root.controller.commit()
                                Accessible.name: qsTr("Effect")
                            }
                            ToolButton {
                                id: effectPickerButton
                                objectName: "templateEffectPicker"
                                implicitHeight: effectField.implicitHeight
                                implicitWidth: 22
                                padding: 0
                                enabled: root.controller.editable
                                down: effectPicker.visible
                                text: qsTr("Choose what the template runs for")
                                display: AbstractButton.IconOnly
                                Accessible.name: text
                                Accessible.role: Accessible.ButtonDropDown
                                ToolTip.text: text
                                ToolTip.visible: hovered
                                contentItem: Label {
                                    text: "⌄"
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    color: Theme.muted
                                }
                                onClicked: effectPicker.visible ? effectPicker.close() : effectPicker.open()
                            }
                        }
                        Label {
                            text: qsTr("Style")
                        }
                        ComboBox {
                            id: styleBox
                            objectName: "templateStyle"
                            Layout.fillWidth: root.narrow
                            Layout.preferredWidth: root.narrow ? -1 : 120
                            enabled: root.controller.editable
                            model: root.controller.styles
                            currentIndex: root.controller.styles.indexOf(root.controller.style)
                            displayText: root.controller.style
                            delegate: ItemDelegate {
                                required property string modelData
                                required property int index
                                width: styleBox.width
                                highlighted: styleBox.highlightedIndex === index
                                contentItem: Label {
                                    text: parent.modelData
                                    textFormat: Text.PlainText
                                    elide: Text.ElideRight
                                }
                            }
                            onActivated: index => root.controller.setStyle(textAt(index))
                            Accessible.name: qsTr("Style")
                        }
                        Label {
                            text: qsTr("Layer")
                        }
                        SpinBox {
                            id: layerBox
                            objectName: "templateLayer"
                            Layout.preferredWidth: 80
                            enabled: root.controller.editable
                            editable: true
                            from: -10000000
                            to: 10000000
                            value: root.controller.layer
                            onValueModified: root.controller.setLayer(value)
                            Accessible.name: qsTr("Layer")
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: Math.max(64, textField.implicitHeight + 2)
                        color: Theme.field
                        border.width: 1
                        border.color: textField.activeFocus ? Theme.focus
                                      : root.controller.problemRange.length > 0 ? Theme.danger : Theme.line
                        TextArea {
                            id: textField
                            objectName: "templateText"
                            anchors.fill: parent
                            anchors.margins: 1
                            background: null
                            wrapMode: TextEdit.Wrap
                            font: root.monoFont
                            enabled: root.controller.editable
                            property bool loading: false
                            // The highlighter's formatting notifies the same text again, later.
                            property string seen: ""
                            function load() {
                                if (text === root.controller.text)
                                    return
                                loading = true
                                const at = cursorPosition
                                text = root.controller.text
                                seen = text
                                cursorPosition = Math.min(at, length)
                                loading = false
                            }
                            Component.onCompleted: load()
                            Connections {
                                target: root.controller
                                function onChanged() { textField.load() }
                            }
                            onTextChanged: {
                                if (loading || text === seen)
                                    return
                                seen = text
                                root.controller.setText(text)
                                completion.update()
                            }
                            onCursorPositionChanged: if (completion.visible) completion.update()
                            onActiveFocusChanged: {
                                if (!activeFocus) {
                                    completion.close()
                                    root.controller.commit()
                                }
                            }
                            Keys.onPressed: event => {
                                if (!completion.visible)
                                    return
                                if (event.key === Qt.Key_Down || event.key === Qt.Key_Up) {
                                    completion.step(event.key === Qt.Key_Down ? 1 : -1)
                                    event.accepted = true
                                } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                                           || event.key === Qt.Key_Tab) {
                                    completion.insert(completion.chosen)
                                    event.accepted = true
                                } else if (event.key === Qt.Key_Escape) {
                                    completion.close()
                                    event.accepted = true
                                }
                            }
                            Accessible.name: qsTr("Template text")
                            Accessible.description: root.controller.problem

                            EditorHighlighter {
                                id: textMarks
                                objectName: "templateTextHighlighter"
                                document: textField.textDocument
                                runs: {
                                    root.controller.currentIsCode
                                    return root.controller.colourRuns(textField.text)
                                }
                                colours: Theme.syntax
                                ranges: root.controller.problemRange
                                colour: Theme.danger
                            }
                        }
                    }

                    RowLayout {
                        objectName: "templateProblem"
                        visible: root.controller.problem !== ""
                        Layout.fillWidth: true
                        spacing: 6
                        Icon {
                            Layout.alignment: Qt.AlignTop
                            iconRole: "problem"
                            color: Theme.danger
                            accentColor: Theme.danger
                        }
                        Label {
                            objectName: "templateProblemText"
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            textFormat: Text.StyledText
                            HoverHandler {
                                id: problemHover
                            }
                            ToolTip {
                                id: problemTip
                                objectName: "templateProblemToolTip"
                                visible: problemHover.hovered && root.controller.problem.indexOf("\n") >= 0
                                text: root.controller.problem
                                contentItem: Label {
                                    text: problemTip.text
                                    textFormat: Text.PlainText
                                    wrapMode: Text.Wrap
                                }
                            }
                            text: {
                                const range = root.controller.problemRange
                                const where = range.length === 2 ? textField.text.slice(range[0], range[1]) : ""
                                const line = (root.controller.currentIsCode ? qsTr("Code line: %1") : qsTr("Template line: %1"))
                                             .arg(root.controller.currentNumber)
                                //: A template's problem: "Template line: 3, in !math.rnd(1,5)!: attempt to call a nil value"
                                // Lua's message without its chunk name ([string "…"]:1:).
                                const said = root.controller.problem.split("\n")[0].replace(/\[string ".*?"\]:\d+: /, "")
                                return where !== "" ? qsTr("%1, in %2: %3").arg("<b>" + line + "</b>", "<tt>" + Qt.escapeHtml(where) + "</tt>",
                                                                                  Qt.escapeHtml(said))
                                                    : "<b>" + line + "</b>: " + Qt.escapeHtml(said)
                            }
                        }
                    }

                    Flow {
                        objectName: "templateHint"
                        Layout.fillWidth: true
                        spacing: 4
                        KeyCap { key: "$" }
                        Label {
                            text: qsTr("inserts a variable,")
                            color: Theme.muted
                        }
                        KeyCap { key: "!" }
                        Label {
                            text: qsTr("starts a Lua expression. Both open a list of what fits.")
                            color: Theme.muted
                        }
                    }
                }

                Rule {
                    visible: root.controller.current !== 0
                }

                ColumnLayout {
                    id: previewSection
                    objectName: "templatePreview"
                    Layout.fillWidth: true
                    Layout.margins: 12
                    spacing: 8
                    readonly property bool folded: root.narrow && !root.previewUnfolded
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Label {
                            objectName: "templatePreviewFold"
                            visible: root.narrow
                            text: previewSection.folded ? "›" : "⌄"
                            color: Theme.muted
                        }
                        SectionTitle {
                            objectName: "templatePreviewTitle"
                            Layout.fillWidth: true
                            title: root.controller.source === 0 ? qsTr("Preview")
                                                                : qsTr("Preview on line %1").arg(root.controller.sourceNumber)
                            note: root.controller.source === 0 ? ""
                                  : root.controller.sourceStyle + " · " + root.controller.sourceText
                        }
                        Label {
                            objectName: "templatePreviewKept"
                            visible: root.controller.previewKept
                            text: qsTr("Kept from the last run that worked")
                            color: Theme.warning
                            font.bold: true
                        }
                        Label {
                            objectName: "templatePreviewSummary"
                            visible: !root.controller.previewKept && root.controller.hasPreview
                            //: The preview's Lines and the run's time: "6 lines · 3.8 ms"
                            text: root.lineCount(root.controller.previewLines.length) + " · "
                                  + root.milliseconds(root.controller.previewMs / 1000)
                            color: Theme.muted
                        }
                        TapHandler {
                            enabled: root.narrow
                            onTapped: root.previewUnfolded = !root.previewUnfolded
                        }
                    }
                    RowLayout {
                        id: trustRow
                        objectName: "templatePreviewTrust"
                        visible: !previewSection.folded && !root.controller.trusted && root.controller.source !== 0
                                 && root.controller.templateCount > 0
                        Layout.fillWidth: true
                        spacing: 8
                        Label {
                            objectName: "templatePreviewTrustText"
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            color: Theme.muted
                            text: qsTr("This file's templates run Lua code with your rights. Preview them if you trust the file.")
                        }
                        Button {
                            objectName: "templatePreviewTrustButton"
                            text: qsTr("Preview")
                            icon.source: IconTheme.image("play-line", enabled ? IconTheme.normal : IconTheme.disabled,
                                                         enabled ? IconTheme.accent : IconTheme.disabled, false)
                            icon.color: "transparent"
                            onClicked: root.controller.trustDocument()
                        }
                    }
                    Label {
                        objectName: "templatePreviewEmpty"
                        visible: !previewSection.folded && !trustRow.visible
                                 && (root.controller.source === 0 || !root.controller.hasPreview)
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: Theme.muted
                        text: root.controller.templateCount === 0
                              ? qsTr("No templates yet. Add one with New to preview it here.")
                              : root.controller.sourceUnknown
                              ? qsTr("These templates made the active line without a karaoke line. Apply them to the whole file to preview it.")
                              : root.controller.source === 0
                              ? qsTr("Select a karaoke line to preview the templates on it.")
                              : root.controller.previewPending
                                ? qsTr("Running the templates on line %1…").arg(root.controller.sourceNumber)
                                : qsTr("No preview of line %1: the templates stopped with a problem.")
                                  .arg(root.controller.sourceNumber)
                    }
                    Rectangle {
                        visible: !previewSection.folded && root.controller.hasPreview
                        Layout.fillWidth: true
                        implicitHeight: previewRows.implicitHeight + 2
                        color: Theme.field
                        border.width: 1
                        border.color: Theme.line
                        Column {
                            id: previewRows
                            objectName: "templatePreviewList"
                            x: 1
                            y: 1
                            width: parent.width - 2
                            opacity: root.controller.previewKept ? 0.6 : 1
                            RowLayout {
                                width: previewRows.width
                                spacing: 8
                                Label {
                                    Layout.leftMargin: 8
                                    Layout.preferredWidth: 16
                                    //: The preview's column: the Line's layer
                                    text: qsTr("L")
                                    color: Theme.muted
                                }
                                Label {
                                    Layout.preferredWidth: 72
                                    text: qsTr("Start")
                                    color: Theme.muted
                                }
                                Label {
                                    Layout.preferredWidth: 72
                                    text: qsTr("End")
                                    color: Theme.muted
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: qsTr("Text")
                                    color: Theme.muted
                                }
                            }
                            Repeater {
                                model: root.controller.previewLines
                                delegate: Item {
                                    id: previewRow
                                    required property var modelData
                                    required property int index
                                    objectName: "templatePreviewRow_" + index
                                    width: previewRows.width
                                    implicitHeight: previewLine.implicitHeight + 6
                                    Accessible.role: Accessible.ListItem
                                    Accessible.name: modelData.start + " " + modelData.end + " " + modelData.text
                                    Rectangle {
                                        visible: previewRow.modelData.own
                                        width: 3
                                        height: parent.height
                                        color: Theme.accent
                                    }
                                    RowLayout {
                                        id: previewLine
                                        anchors.fill: parent
                                        spacing: 8
                                        Label {
                                            Layout.leftMargin: 8
                                            Layout.preferredWidth: 16
                                            text: previewRow.modelData.layer
                                        }
                                        Label {
                                            Layout.preferredWidth: 72
                                            text: previewRow.modelData.start
                                        }
                                        Label {
                                            Layout.preferredWidth: 72
                                            text: previewRow.modelData.end
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            Layout.rightMargin: 8
                                            elide: Text.ElideRight
                                            font: root.monoFont
                                            color: Theme.muted
                                            text: previewRow.modelData.text
                                            textFormat: Text.PlainText
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        Rule {
            visible: root.controller.available
        }

        RowLayout {
            objectName: "templateFooter"
            visible: root.controller.available
            Layout.fillWidth: true
            Layout.margins: 10
            spacing: 6
            Icon {
                visible: root.controller.hasProblems
                iconRole: "problem"
                color: Theme.danger
                accentColor: Theme.danger
            }
            Rectangle {
                visible: !root.controller.hasProblems && Object.keys(root.controller.lastApply).length === 0
                         && root.controller.changedCount > 0
                implicitWidth: 6
                implicitHeight: 6
                radius: 3
                color: Theme.accent
            }
            Label {
                objectName: "templateStatus"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                text: {
                    const c = root.controller
                    const applied = c.lastApply
                    if (c.hasProblems)
                        return qsTr("Apply is off while a template has a problem.")
                    if (applied.lines !== undefined && applied.problems > 0)
                        //: After an Apply with problems: %n the Lines written, %1 the karaoke Lines they came from ("1 karaoke line", "%n karaoke lines")
                        return qsTr("Applied with problems: %n lines from %1; the log lists the problems. Undo removes them in one step.",
                                    "", applied.lines).arg(root.karaokeLineCount(applied.karaokeLines))
                    if (applied.lines !== undefined)
                        //: After Apply: %n the Lines written, %1 the karaoke Lines they came from ("1 karaoke line", "%n karaoke lines"), %2 the run's time
                        return qsTr("Applied: %n lines from %1 in %2 s. Undo removes them in one step.", "", applied.lines)
                               .arg(root.karaokeLineCount(applied.karaokeLines), applied.seconds.toFixed(2))
                    if (c.changedCount === 0)
                        return ""
                    if (root.narrow)
                        return qsTr("Changed since Apply")
                    return c.changedCount === 1
                           ? qsTr("Line %1 changed since the last Apply; the generated lines are older.").arg(c.changedNumber)
                           : qsTr("%n template lines changed since the last Apply; the generated lines are older.",
                                  "", c.changedCount)
                }
            }
            Row {
                spacing: 0
                Button {
                    id: applyButton
                    objectName: "templateApply"
                    text: qsTr("Apply")
                    highlighted: true
                    enabled: root.controller.canApply
                    onClicked: root.controller.apply(false)
                    Accessible.description: qsTr("Apply to every karaoke line")
                }
                Button {
                    id: applyArrow
                    objectName: "templateApplyArrow"
                    implicitWidth: 20
                    implicitHeight: applyButton.implicitHeight
                    padding: 0
                    highlighted: true
                    enabled: root.controller.canApply
                    down: applyMenu.visible
                    text: qsTr("More ways to apply")
                    display: AbstractButton.IconOnly
                    Accessible.name: text
                    Accessible.role: Accessible.ButtonMenu
                    ToolTip.text: text
                    ToolTip.visible: hovered
                    contentItem: Label {
                        text: "⌄"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        color: applyArrow.palette.buttonText
                    }
                    onClicked: applyMenu.popup(applyButton, 0, applyButton.height)
                }
            }
        }
    }

    ShellMenu {
        id: applyMenu
        objectName: "templateApplyMenu"
        ShellMenuItem {
            objectName: "templateApplyAll"
            iconRole: "karaoke"
            text: qsTr("Apply to every karaoke line")
            onTriggered: root.controller.apply(false)
        }
        ShellMenuItem {
            objectName: "templateApplySelected"
            iconRole: "select-lines"
            text: qsTr("Apply to the selected lines")
            help: root.controller.canApplySelected
                ? qsTr("Only the selected lines are regenerated; the other lines' output stays as it is.")
                : qsTr("Select a karaoke line, or a line generated from one since the file was opened.")
            enabled: root.controller.canApplySelected
            onTriggered: root.controller.apply(true)
        }
    }

    ShellMenu {
        id: newMenu
        objectName: "templateNewMenu"
        Instantiator {
            model: [
                { effect: "template syl", text: qsTr("Template for each syllable") },
                { effect: "template line", text: qsTr("Template for each line") },
                { effect: "template pre-line", text: qsTr("Template before each line") },
                { effect: "template char", text: qsTr("Template for each character") },
                { effect: "template furi", text: qsTr("Template for each furigana syllable") },
                { effect: "code once", code: true, text: qsTr("Code run once") },
                { effect: "code line", code: true, text: qsTr("Code for each line") },
                { effect: "code syl", code: true, text: qsTr("Code for each syllable") },
                { effect: "code furi", code: true, text: qsTr("Code for each furigana syllable") }
            ]
            delegate: ShellMenuItem {
                required property var modelData
                required property int index
                objectName: "templateNew_" + index
                iconRole: modelData.code ? "run-script" : "template-editor"
                text: modelData.text + "\t" + modelData.effect
                onTriggered: root.controller.insertTemplate(modelData.effect)
            }
            onObjectAdded: (index, object) => newMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => newMenu.removeItem(object)
        }
        MenuSeparator {}
        ShellMenu {
            id: presetMenu
            objectName: "templatePresetMenu"
            title: qsTr("From a preset")
            iconRole: "template-editor"
            Instantiator {
                model: root.controller.presets
                delegate: ShellMenuItem {
                    required property var modelData
                    required property int index
                    objectName: "templatePreset_" + index
                    iconRole: "template-editor"
                    text: modelData.title
                    help: modelData.description
                    ToolTip.visible: hovered
                    ToolTip.text: modelData.description
                    onTriggered: root.controller.insertPreset(index)
                }
                onObjectAdded: (index, object) => presetMenu.insertItem(index, object)
                onObjectRemoved: (index, object) => presetMenu.removeItem(object)
            }
        }
        MenuSeparator {}
        Label {
            width: 300
            leftPadding: 12
            rightPadding: 12
            topPadding: 2
            bottomPadding: 6
            wrapMode: Text.Wrap
            color: Theme.muted
            font.pixelSize: Math.max(10, Qt.application.font.pixelSize - 1)
            text: qsTr("The new line goes above the first karaoke line, with that line's Style.")
        }
    }

    Popup {
        id: effectPicker
        objectName: "templateEffectPopover"
        parent: effectField
        y: effectField.height + 2
        width: Math.max(effectField.width + effectPickerButton.width, 320)
        padding: 0
        readonly property var parts: root.controller.effectParts(root.controller.effect)
        readonly property bool code: parts.kind === "code"
        readonly property string className: parts.className !== "" ? parts.className : code ? "once" : "syl"
        readonly property var classes: code
            ? [{ name: "once", text: qsTr("Once") }, { name: "line", text: qsTr("Line") },
               { name: "syl", text: qsTr("Syllable") }, { name: "furi", text: qsTr("Furigana syllable") }]
            : [{ name: "pre-line", text: qsTr("Line, before its syllables") }, { name: "line", text: qsTr("Line") },
               { name: "syl", text: qsTr("Syllable") }, { name: "char", text: qsTr("Character") },
               { name: "furi", text: qsTr("Furigana syllable") }]
        readonly property var modifiers: code
            ? [{ word: "noblank", text: qsTr("Skip blank syllables") },
               { word: "all", text: qsTr("Every Style, not only %1").arg(root.controller.style) },
               { word: "loop 2", text: qsTr("Repeat") }]
            : [{ word: "noblank", text: qsTr("Skip blank syllables") },
               { word: "keeptags", text: qsTr("Keep the line's override tags") },
               { word: "all", text: qsTr("Every Style, not only %1").arg(root.controller.style) },
               { word: "loop 2", text: qsTr("Repeat") },
               { word: "fx name", text: qsTr("Only syllables marked with an effect") },
               { word: "notext", text: qsTr("No text, tags only") }]
        function hasModifier(word) {
            return root.controller.hasModifier(root.controller.effect, word)
        }
        background: Rectangle {
            color: Theme.panel
            radius: 4
            border.width: 1
            border.color: Theme.line
        }
        component PickerRow: ItemDelegate {
            id: pickerRow
            property string keyword
            property bool on
            property bool radio
            implicitHeight: 28
            leftPadding: 10
            rightPadding: 10
            Accessible.role: radio ? Accessible.RadioButton : Accessible.CheckBox
            Accessible.checkable: true
            Accessible.checked: on
            contentItem: RowLayout {
                spacing: 8
                Label {
                    textFormat: Text.PlainText
                    Layout.preferredWidth: 14
                    horizontalAlignment: Text.AlignHCenter
                    text: pickerRow.on ? (pickerRow.radio ? "●" : "✓") : (pickerRow.radio ? "○" : "")
                    color: pickerRow.on ? Theme.accent : Theme.muted
                }
                Label {
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    text: pickerRow.text
                    elide: Text.ElideRight
                }
                Label {
                    textFormat: Text.PlainText
                    text: pickerRow.keyword
                    font: root.monoFont
                    color: Theme.muted
                }
            }
        }
        contentItem: Column {
            topPadding: 6
            bottomPadding: 6
            Label {
                textFormat: Text.PlainText
                leftPadding: 10
                bottomPadding: 2
                text: qsTr("Runs for each")
                font.bold: true
                color: Theme.muted
            }
            Repeater {
                model: effectPicker.classes
                delegate: PickerRow {
                    required property var modelData
                    required property int index
                    objectName: "templateEffectClass_" + modelData.name
                    width: effectPicker.width
                    radio: true
                    text: modelData.text
                    keyword: modelData.name
                    on: effectPicker.className === modelData.name
                    onClicked: root.controller.setEffect(root.controller.effectWithClass(root.controller.effect, modelData.name))
                }
            }
            Label {
                textFormat: Text.PlainText
                leftPadding: 10
                topPadding: 6
                bottomPadding: 2
                text: qsTr("Modifiers")
                font.bold: true
                color: Theme.muted
            }
            Repeater {
                model: effectPicker.modifiers
                delegate: PickerRow {
                    required property var modelData
                    objectName: "templateEffectModifier_" + modelData.word.split(" ")[0]
                    width: effectPicker.width
                    text: modelData.text
                    keyword: modelData.word === "loop 2" ? "loop n" : modelData.word
                    on: effectPicker.hasModifier(modelData.word)
                    onClicked: root.controller.setEffect(root.controller.effectWithModifier(root.controller.effect,
                                                                                            modelData.word, !on))
                }
            }
            Rectangle {
                width: effectPicker.width
                height: 1
                color: Theme.line
            }
            Label {
                textFormat: Text.PlainText
                width: effectPicker.width
                leftPadding: 10
                rightPadding: 10
                topPadding: 6
                wrapMode: Text.Wrap
                text: qsTr("The Effect text keeps the standard template syntax; this picker only writes it for you.")
                color: Theme.muted
                font.pixelSize: Math.max(10, Qt.application.font.pixelSize - 1)
            }
        }
    }

    Popup {
        id: completion
        objectName: "templateCompletion"
        parent: textField
        x: Math.min(textField.cursorRectangle.x, Math.max(0, textField.width - width))
        y: textField.cursorRectangle.y + textField.cursorRectangle.height + 2
        width: Math.min(420, Math.max(280, root.width - 40))
        padding: 0
        focus: false
        closePolicy: Popup.CloseOnPressOutsideParent
        property var entries: []
        property int chosen: 0
        property string inserted: ""
        property bool inserting: false
        readonly property bool variables: entries.length > 0 && entries[0].name.startsWith("$")
        function update() {
            if (inserting || textField.text === inserted)
                return
            inserted = ""
            const found = root.controller.completions(textField.text, textField.cursorPosition)
            entries = found
            chosen = 0
            if (found.length === 0) {
                close()
                return
            }
            if (variables)
                root.controller.requestValues()
            if (!visible)
                open()
        }
        function step(by) {
            chosen = (chosen + by + entries.length) % entries.length
            completionList.positionViewAtIndex(chosen, ListView.Contain)
        }
        function insert(index) {
            const entry = entries[index]
            if (!entry)
                return
            const at = textField.cursorPosition
            close()
            inserted = textField.text.slice(0, entry.from) + entry.insert + textField.text.slice(at)
            inserting = true
            textField.remove(entry.from, at)
            textField.insert(entry.from, entry.insert)
            inserting = false
        }
        background: Rectangle {
            color: Theme.panel
            radius: 4
            border.width: 1
            border.color: Theme.line
        }
        contentItem: Column {
            Label {
                width: completion.width
                leftPadding: 10
                rightPadding: 10
                topPadding: 6
                bottomPadding: 4
                elide: Text.ElideRight
                font.bold: true
                color: Theme.muted
                text: !completion.variables ? qsTr("Template functions")
                      : root.controller.source === 0 ? qsTr("Syllable variables")
                      //: The variables list's title: their values on the current Line's first syllable
                      : qsTr("Syllable variables · values for line %1, syllable 1").arg(root.controller.sourceNumber)
            }
            ListView {
                id: completionList
                objectName: "templateCompletionList"
                width: completion.width
                height: Math.min(contentHeight, 8 * 26)
                clip: true
                model: completion.entries
                currentIndex: completion.chosen
                delegate: ItemDelegate {
                    id: entryRow
                    required property var modelData
                    required property int index
                    objectName: "templateCompletion_" + index
                    width: completionList.width
                    implicitHeight: 26
                    leftPadding: 10
                    rightPadding: 10
                    onClicked: completion.insert(index)
                    background: Rectangle {
                        color: entryRow.index === completion.chosen ? Theme.select
                               : entryRow.hovered ? Theme.raised : "transparent"
                    }
                    contentItem: RowLayout {
                        spacing: 10
                        Label {
                            Layout.preferredWidth: 90
                            text: entryRow.modelData.name
                            font.family: root.monoFont.family
                            font.bold: true
                            color: Theme.syntax[6]
                            elide: Text.ElideRight
                        }
                        Label {
                            objectName: "templateCompletionValue_" + entryRow.index
                            visible: completion.variables
                            Layout.preferredWidth: 48
                            horizontalAlignment: Text.AlignRight
                            text: root.controller.values[entryRow.modelData.name.slice(1)] ?? ""
                            textFormat: Text.PlainText
                            font.bold: true
                            elide: Text.ElideRight
                        }
                        Label {
                            Layout.fillWidth: true
                            text: entryRow.modelData.help
                            color: Theme.muted
                            elide: Text.ElideRight
                        }
                    }
                }
            }
            Rectangle {
                width: completion.width
                height: 1
                color: Theme.line
            }
            Row {
                leftPadding: 10
                topPadding: 4
                bottomPadding: 6
                spacing: 4
                KeyCap { key: "↑" }
                KeyCap { key: "↓" }
                Label {
                    text: qsTr("choose ·")
                    color: Theme.muted
                }
                KeyCap { key: qsTr("Enter") }
                Label {
                    text: qsTr("insert ·")
                    color: Theme.muted
                }
                KeyCap { key: qsTr("Esc") }
                Label {
                    text: qsTr("close")
                    color: Theme.muted
                }
            }
        }
    }
}
