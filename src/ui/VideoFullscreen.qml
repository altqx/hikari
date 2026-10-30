pragma Translator: "HikariSub.Video"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Hikari.Ui

Window {
    id: fs
    objectName: "videoFullscreen"
    required property var shell
    required property VideoFullscreenController controller
    readonly property Item stage: stageHolder
    readonly property Item keyTarget: keys
    readonly property alias contextMenu: fullscreenMenu
    readonly property bool cursorHidden: controller.cursorHidden
    readonly property real pinnedPanelHeight: controller.pinned ? bar.height : 0
    readonly property bool toolOn: shell.visualTools.railEnabled
                                   && shell.visualTools.activeFamily > 0
    signal openVideoRequested()
    signal openSubtitlesRequested()
    signal aspectRatioRequested()
    signal fileQuestionRequested(bool next)
    signal audioSettingsRequested()

    title: ""
    color: Theme.background
    flags: Qt.Window | Qt.FramelessWindowHint
    // On Wayland a parented window is its dialog (xdg_toplevel set_parent): sway keeps the keyboard on the parent.
    transientParent: controller.ownedByMainWindow() ? shell : null
    visible: false

    onActiveChanged: if (active && controller.active) keys.forceActiveFocus()
    onClosing: close => {
        if (fs.controller.active) {
            close.accepted = false
            fs.controller.leave()
        }
    }

    function pointerMoved(at) {
        fs.controller.pointerMoved(at)
    }
    function openMenuAt(point) {
        fullscreenMenu.at = point
        fullscreenMenu.popup(stageHolder, point)
    }
    function popupAbove(menu, button) {
        const place = () => {
            if (menu.visible)
                menu.y = button.mapToItem(menu.parent, 0, 0).y - menu.height
        }
        const done = () => {
            menu.heightChanged.disconnect(place)
            menu.closed.disconnect(done)
        }
        menu.popup(button, 0, -menu.implicitHeight)
        menu.heightChanged.connect(place)
        menu.closed.connect(done)
        place()
    }

    Binding {
        target: fs.controller
        property: "menuOpen"
        value: fullscreenMenu.opened || outputMenu.opened || modeMenu.opened || toolsMenu.opened
               || rail.menuOpen || toolBar.popoverOpen
    }
    Connections {
        target: fs.controller
        function onActiveChanged() {
            if (fs.controller.active)
                hint.show()
        }
    }

    FocusScope {
        id: keys
        objectName: "videoFullscreenKeys"
        anchors.fill: parent
        focus: true
        Accessible.role: Accessible.Pane
        Accessible.name: qsTr("Full screen video")

        function leaves(event) {
            return (event.key === Qt.Key_Escape || event.key === Qt.Key_B)
                    && fs.shell.hotkeys.actionFor(0, event.key, event.modifiers) === ""
        }
        Keys.onShortcutOverride: event => event.accepted = fs.shell.visualTools.takesKey(event.key, event.modifiers)
                                                          || fs.shell.hotkeys.actionFor(3, event.key, event.modifiers) !== ""
                                                          || keys.leaves(event)
        Keys.onPressed: event => {
            const action = fs.shell.hotkeys.actionFor(3, event.key, event.modifiers)
            if (fs.shell.visualTools.takesKey(event.key, event.modifiers)) {
                fs.shell.visualTools.key(event.key, event.modifiers, false, event.isAutoRepeat)
                event.accepted = true
            } else if (action !== "") {
                fs.shell.runVideoHotkey(action)
                event.accepted = true
            } else if (event.key === Qt.Key_Escape && fs.shell.visualTools.escape()) {
                event.accepted = true
            } else if (keys.leaves(event)) {
                fs.controller.leave()
                event.accepted = true
            } else if (event.key === Qt.Key_F) {
                fs.controller.toggle(0)
                event.accepted = true
            } else if (event.key === Qt.Key_Menu) {
                fs.openMenuAt(fs.shell.videoView.cursorIn(stageHolder))
                event.accepted = true
            } else if (fs.shell.videoView.key(event.key, event.modifiers)) {
                event.accepted = true
            } else if (fs.shell.visualTools.key(event.key, event.modifiers, false, event.isAutoRepeat)) {
                event.accepted = true
            }
        }
        Keys.onReleased: event => event.accepted = fs.shell.visualTools.key(event.key, event.modifiers, true, event.isAutoRepeat)

        Item {
            id: stageHolder
            objectName: "videoFullscreenStage"
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: fs.controller.pinned ? parent.height - bar.height : parent.height
        }

        Rectangle {
            id: titleStrip
            objectName: "videoFullscreenTitle"
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: 32
            z: 3
            color: Theme.panel
            opacity: fs.controller.barShown && !fs.controller.pinned ? 1 : 0
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: 200 } }
            HoverHandler {
                cursorShape: Qt.ArrowCursor
                onPointChanged: if (hovered) fs.pointerMoved(point.scenePosition)
            }
            MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }
            Rectangle {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: 1
                color: Theme.line
            }
            Label {
                objectName: "fullscreenVideoName"
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter
                          leftMargin: 12; rightMargin: 12 }
                text: fs.controller.videoName
                textFormat: Text.PlainText
                maximumLineCount: 1
                clip: true
                color: Theme.text
                elide: Text.ElideMiddle
            }
        }

        Item {
            id: progress
            objectName: "videoFullscreenProgress"
            anchors { fill: parent; topMargin: titleStrip.visible ? titleStrip.height : 0 }
            z: 2
            visible: fs.controller.active && fs.shell.videoView.progressBar && fs.shell.video.hasVideo
            enabled: false
            TextMetrics {
                id: progressMetrics
                font: progressText.font
                text: progressText.text
            }
            readonly property var layout: fs.controller.progressBar(width, Math.ceil(progressMetrics.advanceWidth),
                                                                     Math.ceil(progressMetrics.height))
            Rectangle {
                x: progress.layout.frame.x; y: progress.layout.frame.y
                width: progress.layout.frame.width; height: progress.layout.frame.height
                color: "transparent"
                border.color: "#000000"
            }
            Rectangle {
                x: progress.layout.inner.x; y: progress.layout.inner.y
                width: progress.layout.inner.width; height: progress.layout.inner.height
                color: "transparent"
                border.color: "#ffffff"
            }
            Rectangle {
                objectName: "videoFullscreenProgressBar"
                x: progress.layout.bar.x; y: progress.layout.bar.y
                width: Math.max(0, progress.layout.bar.width); height: progress.layout.bar.height
                color: "#ffffff"
            }
            Text {
                id: progressText
                objectName: "videoFullscreenProgressText"
                x: progress.layout.text.x; y: progress.layout.text.y
                text: fs.controller.progressText
                color: "#ffffff"
                style: Text.Outline
                styleColor: "#000000"
                font.pointSize: Qt.application.font.pointSize + 4
            }
        }

        VisualToolRail {
            id: rail
            objectName: "fullscreenToolRail"
            tools: fs.shell.visualTools
            keyOf: symbol => fs.shell.boundKeys(symbol, 3)
            visible: fs.toolOn
            z: 3
            x: 8
            y: titleStrip.visible ? titleStrip.height + 8 : 8
            height: Math.min(implicitHeight, (bar.visible ? bar.y : fs.height) - y - 8)
            // On X11 its buttons take the hover from the picture.
            HoverHandler {
                onPointChanged: if (hovered) fs.pointerMoved(point.scenePosition)
            }
        }

        Rectangle {
            id: hint
            objectName: "videoFullscreenHint"
            anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: 40 }
            z: 4
            width: hintText.implicitWidth + 32
            height: hintText.implicitHeight + 14
            radius: height / 2
            color: Theme.panel
            border.color: Theme.line
            opacity: 0
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: 200 } }
            function show() {
                opacity = 1
                hintTimer.restart()
            }
            Timer {
                id: hintTimer
                interval: 2000
                onTriggered: hint.opacity = 0
            }
            Label {
                id: hintText
                objectName: "videoFullscreenHintText"
                anchors.centerIn: parent
                //: Shown for two seconds on entering full screen video
                text: qsTr("Press Esc to exit full screen")
                color: Theme.text
            }
        }

        Rectangle {
            id: bar
            objectName: "videoFullscreenPanel"
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            implicitHeight: barColumn.implicitHeight + 10
            height: implicitHeight
            color: Theme.panel
            z: 3
            opacity: fs.controller.barShown ? 1 : 0
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: 200 } }
            Rectangle {
                anchors { left: parent.left; right: parent.right; top: parent.top }
                height: 1
                color: Theme.line
            }
            HoverHandler {
                cursorShape: Qt.ArrowCursor
                onHoveredChanged: fs.controller.pointerOverBar = hovered
            }
            MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }
            WheelHandler {
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: event => fs.shell.videoView.panelWheel(Math.round(event.angleDelta.y / 120), event.modifiers)
            }

            ColumnLayout {
                id: barColumn
                anchors { left: parent.left; right: parent.right; top: parent.top
                          leftMargin: 8; rightMargin: 8; topMargin: 5 }
                spacing: 2
                RowLayout {
                    id: inspectorSlot
                    objectName: "fullscreenInspectorSlot"
                    Layout.fillWidth: true
                    visible: fs.toolOn
                    VisualToolBar {
                        id: toolBar
                        objectName: "fullscreenToolBar"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        tools: fs.shell.visualTools
                        background: null
                        dialogs: fs.visible
                        settings: fs.shell.app.settings
                    }
                }
                Slider {
                    objectName: "fullscreenSlider"
                    id: fullscreenSeekBar
                    Layout.fillWidth: true
                    from: 0
                    to: Math.max(0, fs.shell.video.frameCount - 1)
                    stepSize: 1
                    Binding on value {
                        when: !fullscreenSeekBar.pressed
                        value: Math.max(0, fs.shell.video.frame)
                        restoreMode: Binding.RestoreNone
                    }
                    enabled: fs.shell.video.hasVideo
                    focusPolicy: Qt.NoFocus
                    Accessible.name: qsTr("Video position")
                    onMoved: fs.shell.video.seekBarMoved(Math.round(value), pressed)
                    onPressedChanged: if (!pressed) fs.shell.video.seekBarReleased()
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    IconButton {
                        objectName: "fullscreenPreviousFile"
                        iconRole: "media-previous-file"
                        text: qsTr("Previous video file")
                        focusPolicy: Qt.NoFocus
                        tip: fs.shell.bitmapTip(qsTr("Previous video file"), "VIDEO_PREVIOUS_FILE", 3)
                        onClicked: if (!fs.shell.hotkeyGesture("VIDEO_PREVIOUS_FILE", 3, "bitmap")) fs.fileQuestionRequested(false)
                    }
                    IconButton {
                        objectName: "fullscreenPlayPause"
                        iconRole: fs.shell.video.playing ? "media-pause" : "media-play"
                        text: fs.shell.video.playing ? qsTr("Pause") : qsTr("Play")
                        enabled: fs.shell.video.hasVideo
                        focusPolicy: Qt.NoFocus
                        tip: fs.shell.bitmapTip(qsTr("Play / Pause"), "VIDEO_PLAY_PAUSE", 3)
                        onClicked: if (!fs.shell.hotkeyGesture("VIDEO_PLAY_PAUSE", 3, "bitmap")) fs.shell.video.togglePlay()
                    }
                    IconButton {
                        objectName: "fullscreenPlayLine"
                        iconRole: "play-line"
                        text: qsTr("Play the current line")
                        enabled: fs.shell.video.hasVideo
                        focusPolicy: Qt.NoFocus
                        tip: fs.shell.bitmapTip(qsTr("Play the current line"), "GLOBAL_PLAY_ACTUAL_LINE", 0)
                        onClicked: if (!fs.shell.hotkeyGesture("GLOBAL_PLAY_ACTUAL_LINE", 0, "bitmap")) fs.shell.runGlobalHotkey("GLOBAL_PLAY_ACTUAL_LINE")
                    }
                    IconButton {
                        objectName: "fullscreenStop"
                        iconRole: "media-stop"
                        text: qsTr("Stop")
                        enabled: fs.shell.video.hasVideo
                        focusPolicy: Qt.NoFocus
                        tip: fs.shell.bitmapTip(qsTr("Stop"), "VIDEO_STOP", 3)
                        onClicked: if (!fs.shell.hotkeyGesture("VIDEO_STOP", 3, "bitmap")) fs.shell.video.stop()
                    }
                    IconButton {
                        objectName: "fullscreenNextFile"
                        iconRole: "media-next-file"
                        text: qsTr("Next file")
                        focusPolicy: Qt.NoFocus
                        tip: fs.shell.bitmapTip(qsTr("Next file"), "VIDEO_NEXT_FILE", 3)
                        onClicked: if (!fs.shell.hotkeyGesture("VIDEO_NEXT_FILE", 3, "bitmap")) fs.fileQuestionRequested(true)
                    }
                    ToolSeparator {}
                    Label {
                        objectName: "fullscreenTimes"
                        text: fs.shell.video.times
                        color: fs.shell.video.keyframeShown ? Theme.warning : Theme.text
                        Accessible.name: qsTr("Video times")
                    }
                    ToolSeparator { visible: currentLine.visible }
                    RowLayout {
                        id: currentLine
                        objectName: "fullscreenLine"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        visible: fs.shell.editor.hasLine
                        spacing: 8
                        clip: true
                        Accessible.role: Accessible.StaticText
                        //: The full screen bar's current line: %1 its number, %2 its times, %3 its text
                        Accessible.name: qsTr("Line %1, %2: %3").arg(lineNumber.text, fs.shell.editor.videoTimes, lineText.text)
                        Label {
                            id: lineNumber
                            objectName: "fullscreenLineNumber"
                            visible: fs.shell.shell.activeLineNumber > 0
                            text: fs.shell.shell.activeLineNumber
                            color: Theme.muted
                            font.pointSize: Math.max(1, Qt.application.font.pointSize - 1)
                            leftPadding: 4
                            rightPadding: 4
                            background: Rectangle {
                                color: "transparent"
                                border.color: Theme.line
                                radius: 2
                            }
                        }
                        Label {
                            objectName: "fullscreenLineTimes"
                            text: fs.shell.editor.videoTimes
                            color: Theme.muted
                            Layout.minimumWidth: 0
                            Layout.maximumWidth: implicitWidth
                            elide: Text.ElideRight
                        }
                        Label {
                            id: lineText
                            objectName: "fullscreenLineText"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            text: fs.controller.lineLabel(fs.shell.editor.videoText)
                            textFormat: Text.PlainText
                            color: Theme.text
                            elide: Text.ElideRight
                            maximumLineCount: 1
                        }
                    }
                    IconButton {
                        objectName: "fullscreenOpenEditor"
                        iconRole: "editor"
                        text: qsTr("Open editor")
                        focusPolicy: Qt.NoFocus
                        onClicked: fs.shell.openEditorFromFullScreen()
                    }
                    Item { Layout.fillWidth: !currentLine.visible }
                    IconButton {
                        id: toolsButton
                        objectName: "fullscreenToolsButton"
                        // MUST KILL: iconRole list mirrors application::visual::Family order and must stay on one line for icon_tests
                        iconRole: ["tool-crosshair", "tool-position", "tool-move", "tool-scale", "tool-rotate-z", "tool-rotate-xy", "tool-clip-rect", "tool-clip-vector", "tool-drawing", "tool-move-all", "tool-all-tags", "tool-perspective"][Math.max(0, fs.shell.visualTools.activeFamily)] ?? ""
                        text: qsTr("Visual tools")
                        enabled: fs.shell.visualTools.railEnabled
                        down: toolsMenu.visible
                        focusPolicy: Qt.NoFocus
                        onClicked: fs.popupAbove(toolsMenu, toolsButton)
                    }
                    ToolSeparator { visible: toolsButton.visible }
                    IconButton {
                        id: speaker
                        objectName: "fullscreenSpeaker"
                        iconRole: fs.shell.videoView.muted ? "volume-muted" : "volume"
                        text: fs.shell.videoView.muted ? qsTr("Unmute") : qsTr("Mute")
                        //: The full screen speaker's tooltip: %1 is Mute or Unmute
                        tip: qsTr("%1 · Wheel: volume · Right-click: output device").arg(text)
                        enabled: fs.shell.video.hasVideo
                        down: outputMenu.visible
                        focusPolicy: Qt.NoFocus
                        onClicked: fs.shell.videoView.toggleMute()
                        WheelHandler {
                            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                            onWheel: event => fs.shell.videoView.volumeWheel(Math.round(event.angleDelta.y / 120))
                        }
                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: fs.popupAbove(outputMenu, speaker)
                        }
                    }
                    VideoVolumeSlider {
                        sliderName: "fullscreenVolume"
                        view: fs.shell.videoView
                        hasVideo: fs.shell.video.hasVideo
                        showIcon: false
                        sliderWidth: 80
                    }
                    ToolSeparator {}
                    IconTextButton {
                        id: modeButton
                        objectName: "fullscreenBarMode"
                        iconRole: "show-toolbar"
                        flat: true
                        text: modeMenu.names[fs.controller.barMode]
                        tip: fs.shell.bitmapTip(qsTr("Full screen bar"), "VIDEO_FULLSCREEN_BAR", 3)
                        down: modeMenu.visible
                        focusPolicy: Qt.NoFocus
                        Accessible.name: qsTr("Full screen bar: %1").arg(text)
                        onClicked: if (!fs.shell.hotkeyGesture("VIDEO_FULLSCREEN_BAR", 3, "bitmap")) fs.popupAbove(modeMenu, modeButton)
                    }
                    IconButton {
                        objectName: "fullscreenExit"
                        iconRole: "fullscreen-exit"
                        text: qsTr("Exit full screen")
                        tip: {
                            const key = fs.shell.boundKeys("VIDEO_FULL_SCREEN", 3)
                            return qsTr("Exit full screen") + " (" + (key.length ? key : qsTr("Escape")) + ")"
                        }
                        focusPolicy: Qt.NoFocus
                        onClicked: fs.controller.leave()
                    }
                }
            }
        }

        VideoContextMenu {
            id: fullscreenMenu
            objectName: "videoFullscreenMenu"
            shell: fs.shell
            itemPrefix: "fullscreenMenu"
            onOpenVideoRequested: fs.openVideoRequested()
            onOpenSubtitlesRequested: fs.openSubtitlesRequested()
            onAspectRatioRequested: fs.aspectRatioRequested()
            onClosed: keys.forceActiveFocus()
        }
        AudioOutputMenu {
            id: outputMenu
            objectName: "fullscreenAudioOutputMenu"
            app: fs.shell.app
            onSettingsRequested: fs.audioSettingsRequested()
            onClosed: keys.forceActiveFocus()
        }
        ShellMenu {
            id: modeMenu
            objectName: "fullscreenBarModeMenu"
            readonly property var names: [qsTr("Auto"), qsTr("Always"), qsTr("Never")]
            onClosed: keys.forceActiveFocus()
            Instantiator {
                model: [qsTr("Show on pointer movement, hide when idle"), qsTr("Keep the bar; the picture ends above it"),
                        qsTr("Picture only")]
                delegate: ShellMenuItem {
                    required property string modelData
                    required property int index
                    objectName: "fullscreenBar" + ["Auto", "Always", "Never"][index]
                    text: modeMenu.names[index]
                    help: modelData
                    checkable: true
                    checked: fs.controller.barMode === index
                    keepsMenuOpen: false
                    onTriggered: fs.controller.barMode = index
                }
                onObjectAdded: (index, object) => modeMenu.insertItem(index, object)
                onObjectRemoved: (index, object) => modeMenu.removeItem(object)
            }
        }
        ShellMenu {
            id: toolsMenu
            objectName: "fullscreenToolsMenu"
            onClosed: keys.forceActiveFocus()
            Instantiator {
                model: fs.shell.visualTools.railOrder
                delegate: ShellMenuItem {
                    required property int modelData
                    readonly property int family: modelData
                    objectName: "fullscreenTool" + family
                    // MUST KILL: iconRole list mirrors application::visual::Family order and must stay on one line for icon_tests
                    iconRole: ["tool-crosshair", "tool-position", "tool-move", "tool-scale", "tool-rotate-z", "tool-rotate-xy", "tool-clip-rect", "tool-clip-vector", "tool-drawing", "tool-move-all", "tool-all-tags", "tool-perspective"][family] ?? ""
                    text: fs.shell.visualTools.families[family]?.name ?? ""
                    checkable: true
                    checked: fs.shell.visualTools.activeFamily === family
                    keepsMenuOpen: false
                    onTriggered: if (fs.shell.visualTools.activeFamily !== family) fs.shell.visualTools.selectFamily(family)
                }
                onObjectAdded: (index, object) => toolsMenu.insertItem(index, object)
                onObjectRemoved: (index, object) => toolsMenu.removeItem(object)
            }
        }
    }
}
