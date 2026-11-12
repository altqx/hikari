pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.NodeEffects"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Pane {
    id: root
    objectName: "nodeSourceLines"
    required property NodeEffectsController controller
    property VideoController video: null
    padding: 0
    implicitWidth: 420
    implicitHeight: 360
    readonly property size minimumSize: Qt.size(260, 160)
    readonly property Item toolbar: headerBar
    readonly property font monoFont: Qt.font({ family: "monospace", pixelSize: Qt.application.font.pixelSize })
    property int anchorRow: -1

    function time(ms) {
        const cs = Math.floor(Math.max(0, ms) / 10)
        const h = Math.floor(cs / 360000), m = Math.floor(cs / 6000) % 60, s = Math.floor(cs / 100) % 60
        return h + ":" + String(m).padStart(2, "0") + ":" + String(s).padStart(2, "0") + "." + String(cs % 100).padStart(2, "0")
    }
    function lineCount(n) {
        return n === 1 ? qsTr("1 line") : qsTr("%n lines", "", n)
    }
    function pick(row, modifiers) {
        const rows = controller.sources
        const id = rows[row].line
        if (modifiers & Qt.ShiftModifier && anchorRow >= 0) {
            const from = Math.min(anchorRow, row), to = Math.max(anchorRow, row)
            controller.sourceSelection = rows.slice(from, to + 1).map(r => r.line)
        } else if (modifiers & Qt.ControlModifier) {
            const chosen = controller.sourceSelection.slice()
            const at = chosen.indexOf(id)
            if (at >= 0)
                chosen.splice(at, 1)
            else
                chosen.push(id)
            controller.sourceSelection = chosen
            anchorRow = row
        } else {
            controller.sourceSelection = [id]
            anchorRow = row
        }
        controller.source = id
    }

    RowLayout {
        id: headerBar
        objectName: "nodeSourceHeader"
        visible: parent !== root.contentItem
        spacing: 2
        IconToolButton {
            objectName: "nodePreviewOnVideo"
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
            objectName: "nodeLoop"
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
                function onChanged() {
                    if (!root.video || !root.video.looping || !loopButton.span)
                        return
                    if (root.controller.source === 0)
                        root.video.pause()
                    else if (loopButton.span[0] !== root.controller.sourceStartMs || loopButton.span[1] !== root.controller.sourceEndMs)
                        loopButton.loopSource()
                }
            }
            Accessible.checkable: true
            Accessible.checked: checked
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ColumnLayout {
            objectName: "nodePreview"
            visible: root.controller.hasGraph
            Layout.fillWidth: true
            Layout.margins: 10
            spacing: 6
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Label {
                    objectName: "nodePreviewTitle"
                    text: root.controller.source === 0 ? qsTr("Preview") : qsTr("Preview on line %1").arg(root.controller.sourceNumber)
                    font.bold: true
                }
                Item {
                    Layout.fillWidth: true
                }
                Label {
                    objectName: "nodePreviewKept"
                    visible: root.controller.previewKept
                    text: qsTr("Kept from the last run that worked")
                    color: Theme.warning
                    font.bold: true
                }
                Label {
                    objectName: "nodePreviewSummary"
                    visible: !root.controller.previewKept && root.controller.previewLines.length > 0
                    color: Theme.muted
                    //: The preview's Lines and the run's time: "6 lines · 3.8 ms"
                    text: root.lineCount(root.controller.previewLines.length) + " · "
                          + qsTr("%1 ms").arg(root.controller.previewMs < 10 ? root.controller.previewMs.toFixed(1)
                                                                             : Math.round(root.controller.previewMs))
                }
            }
            Label {
                objectName: "nodePreviewEmpty"
                visible: root.controller.previewLines.length === 0
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.muted
                text: root.controller.source === 0 && root.controller.typesetting
                      ? qsTr("No lines to preview on. Select lines in the grid, then choose Subtitles › Typesetting graph….")
                      : root.controller.source === 0 ? qsTr("No karaoke line of this graph’s style to preview on.")
                      : root.controller.hasLua && !root.controller.trusted ? qsTr("Nothing runs until the file’s Lua is trusted.")
                      : root.controller.problems.length > 0 ? qsTr("No preview: the graph has problems.")
                      : qsTr("The graph writes no lines for line %1.").arg(root.controller.sourceNumber)
            }
            Rectangle {
                visible: root.controller.previewLines.length > 0
                Layout.fillWidth: true
                implicitHeight: Math.min(previewList.contentHeight, 120) + 2
                color: Theme.field
                border.width: 1
                border.color: Theme.line
                opacity: root.controller.previewKept ? 0.6 : 1
                ListView {
                    id: previewList
                    objectName: "nodePreviewLines"
                    anchors.fill: parent
                    anchors.margins: 1
                    clip: true
                    model: root.controller.previewLines
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    delegate: RowLayout {
                        id: previewRow
                        required property var modelData
                        width: previewList.width
                        spacing: 8
                        Label {
                            Layout.leftMargin: 6
                            Layout.preferredWidth: 20
                            horizontalAlignment: Text.AlignRight
                            text: previewRow.modelData.layer
                            color: Theme.muted
                        }
                        Label {
                            text: root.time(previewRow.modelData.startMs)
                            color: Theme.muted
                            font: root.monoFont
                        }
                        Label {
                            Layout.fillWidth: true
                            Layout.rightMargin: 6
                            text: previewRow.modelData.text
                            font: root.monoFont
                            elide: Text.ElideRight
                            textFormat: Text.PlainText
                        }
                    }
                }
            }
        }

        Rectangle {
            visible: root.controller.hasGraph
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.line
        }

        RowLayout {
            visible: root.controller.hasGraph
            Layout.fillWidth: true
            Layout.margins: 10
            Layout.bottomMargin: 4
            spacing: 8
            Label {
                text: qsTr("Source lines")
                font.bold: true
            }
            Label {
                objectName: "nodeSourceSummary"
                Layout.fillWidth: true
                color: Theme.muted
                elide: Text.ElideRight
                textFormat: Text.PlainText
                text: !root.controller.typesetting
                      //: Which karaoke lines the graph applies to: "24 karaoke lines, Default" or "24 karaoke lines, all styles"
                      ? (root.controller.sources.length === 1 ? qsTr("1 karaoke line") : qsTr("%n karaoke lines", "", root.controller.sources.length))
                        + ", " + (root.controller.allStyles ? qsTr("all styles") : root.controller.style)
                      : (root.controller.sources.length === 1 ? qsTr("1 selected line") : qsTr("%n selected lines", "", root.controller.sources.length))
            }
        }

        ListView {
            id: sourceList
            objectName: "nodeSourceList"
            visible: root.controller.hasGraph
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.controller.sources
            boundsBehavior: Flickable.StopAtBounds
            activeFocusOnTab: true
            currentIndex: root.controller.sources.findIndex(s => s.line === root.controller.source)
            highlightMoveDuration: 0
            ScrollBar.vertical: ScrollBar {}
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Source lines")
            Keys.onUpPressed: event => { if (currentIndex > 0) root.pick(currentIndex - 1, event.modifiers) }
            Keys.onDownPressed: event => { if (currentIndex < count - 1) root.pick(currentIndex + 1, event.modifiers) }
            delegate: Rectangle {
                id: sourceRow
                required property var modelData
                required property int index
                readonly property bool current: modelData.line === root.controller.source
                objectName: "nodeSource_" + index
                width: sourceList.width
                implicitHeight: sourceLine.implicitHeight + 8
                color: current ? Theme.select : modelData.selected ? Qt.alpha(Theme.select, 0.5)
                     : sourceMouse.containsMouse ? Theme.raised : "transparent"
                Accessible.role: Accessible.ListItem
                Accessible.name: qsTr("Line %1, %2").arg(modelData.number, modelData.text)
                Accessible.selected: modelData.selected
                Rectangle {
                    visible: sourceRow.current
                    width: 3
                    height: parent.height
                    color: Theme.accent
                }
                RowLayout {
                    id: sourceLine
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 10
                    Label {
                        Layout.preferredWidth: 28
                        horizontalAlignment: Text.AlignRight
                        text: sourceRow.modelData.number
                        color: Theme.muted
                    }
                    Label {
                        text: root.time(sourceRow.modelData.startMs)
                        color: Theme.muted
                        font: root.monoFont
                    }
                    Label {
                        Layout.fillWidth: true
                        text: sourceRow.modelData.text
                        elide: Text.ElideRight
                        textFormat: Text.PlainText
                    }
                }
                MouseArea {
                    id: sourceMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: mouse => {
                        sourceList.forceActiveFocus()
                        root.pick(sourceRow.index, mouse.modifiers)
                    }
                }
            }
        }

        Label {
            objectName: "nodeSourcesNone"
            visible: !root.controller.hasGraph
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 12
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.Wrap
            color: Theme.muted
            text: qsTr("Pick or add a graph to see the karaoke lines it applies to.")
        }
    }
}
