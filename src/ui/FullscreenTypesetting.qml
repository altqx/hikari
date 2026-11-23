pragma Translator: "HikariSub.Video"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Item {
    id: ts
    objectName: "fullscreenTypesetting"
    required property var shell
    required property VideoFullscreenController controller
    property bool dialogs: true
    readonly property bool narrow: width < 1400
    readonly property rect frameRect: Qt.rect(frame.x, frame.y, frame.width, frame.height)
    readonly property alias zoomReadoutSlot: zoomSlot
    readonly property alias timeSlot: timeSlot
    readonly property bool popupOpen: inspectorBar.popoverOpen
    signal pictureFocusRequested()

    readonly property var regions: [topBar, rail, frame, inspector, lineBox, statusRow]
    function regionOf(item) {
        for (let i = item; i; i = i.parent) {
            const at = regions.indexOf(i)
            if (at >= 0)
                return at
        }
        return -1
    }
    function focusRegion(at, reason) {
        const region = regions[at]
        if (region === frame)
            ts.pictureFocusRequested()
        else if (region === rail)
            rail.list.forceActiveFocus(reason)
        else if (region === topBar)
            modeChoice.focusCurrent(reason)
        else if (region === statusRow)
            (playPause.enabled ? playPause : statusRow).forceActiveFocus(reason)
        else if (region === inspector) {
            const first = inspectorBar.nextItemInFocusChain(true)
            if (first && ts.regionOf(first) === regions.indexOf(inspector))
                first.forceActiveFocus(reason)
            else
                inspector.forceActiveFocus(reason)
        } else
            region.forceActiveFocus(reason)
    }
    function nextRegion(from, back) {
        const at = ts.regionOf(from)
        const start = at < 0 ? regions.indexOf(frame) : at
        ts.focusRegion((start + (back ? regions.length - 1 : 1)) % regions.length,
                       back ? Qt.BacktabFocusReason : Qt.TabFocusReason)
    }

    component Split: Rectangle {
        color: Theme.line
    }

    Rectangle {
        id: topBar
        objectName: "fullscreenTopBar"
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 40
        color: Theme.background
        Accessible.role: Accessible.ToolBar
        Accessible.name: qsTr("Full screen typesetting")
        RowLayout {
            anchors { fill: parent; leftMargin: 8; rightMargin: 4 }
            spacing: 12
            SegmentedButtons {
                id: modeChoice
                objectName: "fullscreenModeChoice"
                function focusCurrent(reason) {
                    const choice = children[0].children.find(c => c.checked === true)
                    if (choice)
                        choice.forceActiveFocus(reason)
                }
                implicitHeight: 28
                currentIndex: 1
                choices: [
                    { name: "fullscreenModeSimple", text: qsTr("Simple"), iconRole: "media-play",
                      tip: qsTr("Simple: switch to full-screen watching · the frame keeps its place") },
                    { name: "fullscreenModeTypesetting", text: qsTr("Typesetting"), iconRole: "tool-position" }
                ]
                onActivated: index => {
                    if (index === 0)
                        ts.controller.mode = VideoFullscreenController.Mode.Simple
                }
            }
            Label {
                objectName: "fullscreenDocumentName"
                Layout.fillWidth: true
                text: ts.shell.shell.editingTitle
                textFormat: Text.PlainText
                color: Theme.muted
                elide: Text.ElideMiddle
            }
            Item {
                id: zoomSlot
                objectName: "fullscreenZoomReadoutSlot"
                implicitWidth: childrenRect.width
                implicitHeight: childrenRect.height
            }
            IconToolButton {
                objectName: "fullscreenTypesettingExit"
                iconRole: "fullscreen-exit"
                text: qsTr("Leave full screen")
                tip: ts.shell.bitmapTip(text, "VIDEO_FULL_SCREEN", 3, ShellMenus.keyText("Esc"))
                focusPolicy: Qt.TabFocus
                onClicked: if (!ts.shell.hotkeyGesture("VIDEO_FULL_SCREEN", 3, "bitmap")) ts.controller.leave()
            }
        }
    }
    Split {
        id: topSplit
        anchors { left: parent.left; right: parent.right; top: topBar.bottom }
        height: 1
    }

    Rectangle {
        id: statusRow
        objectName: "fullscreenStatusRow"
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 28
        color: Theme.background
        activeFocusOnTab: false
        Accessible.role: Accessible.StatusBar
        Accessible.name: qsTr("Video status")
        RowLayout {
            anchors { fill: parent; leftMargin: 4; rightMargin: 12 }
            spacing: 2
            IconToolButton {
                objectName: "fullscreenTypesettingPreviousFrame"
                iconRole: "frame-previous"
                text: qsTr("Previous frame")
                implicitHeight: 24
                enabled: ts.shell.video.hasVideo && ts.shell.video.frame > 0
                tip: ts.shell.bitmapTip(text, "GLOBAL_PREVIOUS_FRAME", 0)
                focusPolicy: Qt.TabFocus
                onClicked: if (!ts.shell.hotkeyGesture("GLOBAL_PREVIOUS_FRAME", 0, "bitmap")) ts.shell.video.stepFrames(-1)
            }
            IconToolButton {
                id: playPause
                objectName: "fullscreenTypesettingPlayPause"
                iconRole: ts.shell.video.playing ? "media-pause" : "media-play"
                text: ts.shell.video.playing ? qsTr("Pause") : qsTr("Play")
                implicitHeight: 24
                enabled: ts.shell.video.hasVideo
                tip: ts.shell.bitmapTip(qsTr("Play or pause"), "VIDEO_PLAY_PAUSE", 3)
                focusPolicy: Qt.TabFocus
                onClicked: if (!ts.shell.hotkeyGesture("VIDEO_PLAY_PAUSE", 3, "bitmap")) ts.shell.video.togglePlay()
            }
            IconToolButton {
                objectName: "fullscreenTypesettingNextFrame"
                iconRole: "frame-next"
                text: qsTr("Next frame")
                implicitHeight: 24
                enabled: ts.shell.video.hasVideo && ts.shell.video.frame + 1 < ts.shell.video.frameCount
                tip: ts.shell.bitmapTip(text, "GLOBAL_NEXT_FRAME", 0)
                focusPolicy: Qt.TabFocus
                onClicked: if (!ts.shell.hotkeyGesture("GLOBAL_NEXT_FRAME", 0, "bitmap")) ts.shell.video.stepFrames(1)
            }
            Item {
                id: timeSlot
                objectName: "fullscreenTimeSlot"
                Layout.leftMargin: 10
                implicitWidth: childrenRect.width
                implicitHeight: childrenRect.height
                Label {
                    objectName: "fullscreenTypesettingTime"
                    visible: timeSlot.children.length === 1
                    text: ts.controller.positionText
                    font.family: "monospace"
                    Accessible.name: qsTr("Video time, %1").arg(text)
                }
            }
            Label {
                objectName: "fullscreenFrameNumber"
                Layout.leftMargin: 10
                //: The full-screen status row: %1 the video's current frame number
                text: qsTr("Frame %1").arg(Math.max(0, ts.shell.video.frame))
                color: Theme.muted
                font.features: { "tnum": 1 }
            }
            Item { Layout.fillWidth: true }
            Label {
                id: lineOf
                objectName: "fullscreenLineOf"
                property int total: 0
                function count() { total = ts.shell.shell.lines.rowCount() }
                visible: ts.shell.shell.activeLineNumber > 0
                //: The full-screen status row: %1 the active Line's number, %2 how many Lines the file has
                text: qsTr("Line %1 of %2").arg(ts.shell.shell.activeLineNumber, total)
                color: Theme.muted
                font.features: { "tnum": 1 }
                Component.onCompleted: count()
                Connections {
                    target: ts.shell.shell.lines
                    function onRowsInserted() { lineOf.count() }
                    function onRowsRemoved() { lineOf.count() }
                    function onModelReset() { lineOf.count() }
                }
            }
            Label {
                objectName: "fullscreenVideoSize"
                Layout.leftMargin: 12
                text: ts.controller.videoSize
                color: Theme.muted
                font.features: { "tnum": 1 }
            }
        }
    }
    Split {
        anchors { left: parent.left; right: parent.right; bottom: statusRow.top }
        height: 1
    }

    Rectangle {
        id: railPane
        anchors { left: parent.left; top: topSplit.bottom; bottom: statusRow.top; bottomMargin: 1 }
        width: ts.narrow ? 160 : 188
        color: Theme.panel
        VisualToolRail {
            id: rail
            objectName: "fullscreenToolRail"
            anchors { fill: parent; topMargin: 4 }
            named: true
            tools: ts.shell.visualTools
            keyOf: symbol => ts.shell.boundKeys(symbol, 3)
        }
    }
    Split {
        anchors { left: railPane.right; top: railPane.top; bottom: railPane.bottom }
        width: 1
    }

    Rectangle {
        id: inspectorPane
        anchors { right: parent.right; top: topSplit.bottom; bottom: statusRow.top; bottomMargin: 1 }
        width: ts.narrow ? 288 : 320
        color: Theme.panel
        ScrollView {
            id: inspector
            objectName: "fullscreenInspector"
            anchors.fill: parent
            contentWidth: availableWidth
            activeFocusOnTab: false
            VisualToolBar {
                id: inspectorBar
                objectName: "fullscreenToolBar"
                width: inspector.availableWidth
                column: true
                padding: 12
                tools: ts.shell.visualTools
                background: null
                dialogs: ts.dialogs
                settings: ts.shell.app.settings
            }
        }
    }
    Split {
        anchors { right: inspectorPane.left; top: inspectorPane.top; bottom: inspectorPane.bottom }
        width: 1
    }

    Rectangle {
        id: lineBox
        objectName: "fullscreenLineBox"
        anchors { left: railPane.right; leftMargin: 1; right: inspectorPane.left; rightMargin: 1
                  bottom: statusRow.top; bottomMargin: 1 }
        height: lineColumn.implicitHeight + 16
        color: Theme.panel
        readonly property bool hasLine: ts.shell.editor.hasLine
        Accessible.role: Accessible.StaticText
        //: The full-screen line box's name: %1 the active Line's number
        Accessible.name: hasLine ? qsTr("Line %1 text").arg(ts.shell.shell.activeLineNumber) : noLine.text
        Accessible.description: hasLine ? lineText.text : ""
        ColumnLayout {
            id: lineColumn
            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter
                      leftMargin: 12; rightMargin: 12 }
            spacing: 4
            RowLayout {
                spacing: 8
                visible: lineBox.hasLine
                Label {
                    objectName: "fullscreenLineBoxNumber"
                    text: ts.shell.shell.activeLineNumber
                    leftPadding: 6
                    rightPadding: 6
                    font.features: { "tnum": 1 }
                    background: Rectangle {
                        color: Theme.raised
                        radius: Theme.radius
                    }
                }
            }
            Label {
                id: lineText
                objectName: "fullscreenLineBoxText"
                visible: lineBox.hasLine
                Layout.fillWidth: true
                text: ts.controller.lineLabel(ts.shell.editor.videoText)
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
            Label {
                id: noLine
                objectName: "fullscreenLineBoxEmpty"
                visible: !lineBox.hasLine
                Layout.fillWidth: true
                text: qsTr("No line at this time")
                color: Theme.muted
            }
        }
        Rectangle {
            anchors.fill: parent
            visible: lineBox.activeFocus
            color: "transparent"
            border.color: Theme.focus
        }
    }
    Split {
        anchors { left: lineBox.left; right: lineBox.right; bottom: lineBox.top }
        height: 1
    }

    Item {
        id: frame
        objectName: "fullscreenFrame"
        anchors { left: railPane.right; leftMargin: 1; right: inspectorPane.left; rightMargin: 1
                  top: topSplit.bottom; bottom: lineBox.top; bottomMargin: 1 }
    }
}
