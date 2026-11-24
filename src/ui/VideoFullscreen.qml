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
    readonly property Item keyTarget: keyScope
    readonly property alias contextMenu: fullscreenMenu
    readonly property bool cursorHidden: controller.cursorHidden
    property var cancelPick: () => false
    readonly property real pinnedPanelHeight: controller.stripReserved ? strip.height : 0
    readonly property bool typesetting: controller.mode === VideoFullscreenController.Mode.Typesetting
    readonly property color ink: "#EEF0F2"
    readonly property color inkMuted: Qt.rgba(0xEE / 255, 0xF0 / 255, 0xF2 / 255, 0.62)
    signal openVideoRequested()
    signal openSubtitlesRequested()
    signal aspectRatioRequested()
    signal audioSettingsRequested()
    signal fullscreenSettingsRequested()

    title: ""
    color: "#000000"
    flags: Qt.Window | Qt.FramelessWindowHint
    // On Wayland a parented window is its dialog (xdg_toplevel set_parent): sway keeps the keyboard on the parent.
    transientParent: controller.ownedByMainWindow() ? shell : null
    visible: false

    onActiveChanged: if (active && controller.active) keyScope.forceActiveFocus()
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
    function openStripMenu(point) {
        stripMenu.popup(strip, point)
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
        value: fullscreenMenu.opened || outputMenu.opened || stripMenu.opened || strip.holdsFocus
    }
    Connections {
        target: fs.controller
        function onActiveChanged() {
            if (fs.controller.active)
                hint.show()
        }
    }

    FocusScope {
        id: keyScope
        objectName: "videoFullscreenKeys"
        anchors.fill: parent
        focus: true
        Accessible.role: Accessible.Pane
        Accessible.name: qsTr("Full screen video")

        function typesettingKey(event) {
            return fs.shell.hotkeys.actionFor(0, event.key, event.modifiers) === "GLOBAL_FULL_SCREEN_TYPESETTING"
        }
        function leaves(event) {
            return (event.key === Qt.Key_Escape || event.key === Qt.Key_B)
                    && fs.shell.hotkeys.actionFor(0, event.key, event.modifiers) === ""
        }
        MenuKey.onRequested: fs.openMenuAt(fs.shell.videoView.cursorIn(stageHolder))
        Keys.onShortcutOverride: event => event.accepted = fs.shell.visualTools.takesKey(event.key, event.modifiers)
                                                          || fs.shell.hotkeys.actionFor(3, event.key, event.modifiers) !== ""
                                                          || keyScope.typesettingKey(event) || keyScope.leaves(event)
        function stripGroups() {
            const centred = [previousFrame, playPause, nextFrame].find(b => b.enabled) ?? modeSwitch
            return [leadingGroup, centred, modeSwitch]
        }
        function focusPicture() {
            for (let i = fs.activeFocusItem; i && i !== keyScope; i = i.parent)
                i.focus = false
            keyScope.forceActiveFocus(Qt.OtherFocusReason)
        }
        function nextGroup(back) {
            const groups = keyScope.stripGroups()
            const at = groups.findIndex(g => g.activeFocus)
            const next = at < 0 ? (back ? groups.length - 1 : 0) : at + (back ? -1 : 1)
            if (next < 0 || next >= groups.length)
                keyScope.focusPicture()
            else
                groups[next].forceActiveFocus(back ? Qt.BacktabFocusReason : Qt.TabFocusReason)
        }
        Keys.onPressed: event => {
            const action = fs.shell.hotkeys.actionFor(3, event.key, event.modifiers)
            const back = (event.modifiers & Qt.ShiftModifier) !== 0
            if (event.key === Qt.Key_Escape && fs.cancelPick()) {
                event.accepted = true
            } else if (event.key === Qt.Key_F6 && action === "" && typesetting.item) {
                typesetting.item.nextRegion(fs.activeFocusItem, back)
                event.accepted = true
            } else if (event.key === Qt.Key_F6 && action === "" && fs.controller.barShown) {
                keyScope.nextGroup(back)
                event.accepted = true
            } else if (event.key === Qt.Key_Escape && fs.activeFocusItem !== keyScope
                       && (strip.holdsFocus || typesetting.item && typesetting.item.regionOf(fs.activeFocusItem) >= 0)) {
                keyScope.focusPicture()
                event.accepted = true
            } else if (fs.shell.visualTools.takesKey(event.key, event.modifiers)) {
                fs.shell.visualTools.key(event.key, event.modifiers, false, event.isAutoRepeat)
                event.accepted = true
            } else if (action !== "") {
                fs.shell.runVideoHotkey(action)
                event.accepted = true
            } else if (keyScope.typesettingKey(event)) {
                fs.shell.runGlobalHotkey("GLOBAL_FULL_SCREEN_TYPESETTING")
                event.accepted = true
            } else if (event.key === Qt.Key_Escape && fs.shell.visualTools.escape()) {
                event.accepted = true
            } else if (keyScope.leaves(event)) {
                fs.controller.leave()
                event.accepted = true
            } else if (event.key === Qt.Key_F) {
                fs.controller.toggle(0)
                event.accepted = true
            } else if (fs.shell.videoView.key(event.key, event.modifiers)) {
                event.accepted = true
            } else if (fs.shell.visualTools.key(event.key, event.modifiers, false, event.isAutoRepeat)) {
                event.accepted = true
            }
        }
        Keys.onReleased: event => event.accepted = fs.shell.visualTools.key(event.key, event.modifiers, true, event.isAutoRepeat)

        Loader {
            id: typesetting
            anchors.fill: parent
            active: fs.typesetting && fs.controller.active
            sourceComponent: FullscreenTypesetting {
                shell: fs.shell
                controller: fs.controller
                onPictureFocusRequested: keyScope.focusPicture()
            }
        }

        Item {
            id: stageHolder
            objectName: "videoFullscreenStage"
            readonly property rect frame: typesetting.item ? typesetting.item.frameRect
                                                           : Qt.rect(0, 0, parent.width,
                                                                     fs.controller.stripReserved ? parent.height - strip.height
                                                                                                 : parent.height)
            x: frame.x
            y: frame.y
            width: frame.width
            height: frame.height
        }

        Rectangle {
            id: seekingShade
            objectName: "videoFullscreenSeeking"
            anchors.fill: stageHolder
            z: 1
            color: Qt.rgba(0, 0, 0, 0.45)
            visible: false
            Timer {
                interval: 250
                running: fs.controller.seeking
                onTriggered: seekingShade.visible = true
            }
            Connections {
                target: fs.controller
                function onSeekingChanged() {
                    if (!fs.controller.seeking)
                        seekingShade.visible = false
                }
            }
            Column {
                anchors.centerIn: parent
                spacing: 8
                BusyIndicator {
                    anchors.horizontalCenter: parent.horizontalCenter
                    implicitWidth: 34
                    implicitHeight: 34
                    running: seekingShade.visible
                }
                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Seeking…")
                    color: fs.ink
                }
            }
        }

        Rectangle {
            id: failure
            objectName: "videoFullscreenFailure"
            anchors.fill: stageHolder
            z: 1
            color: "#000000"
            visible: fs.controller.failedFrame >= 0
            Column {
                anchors.centerIn: parent
                width: Math.min(parent.width - 48, 640)
                spacing: 8
                Icon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    iconRole: "notice-warning"
                    size: 34
                    color: fs.ink
                    accentColor: fs.ink
                }
                Label {
                    objectName: "videoFullscreenFailureText"
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    textFormat: Text.PlainText
                    //: Full screen, when a frame can't be decoded: %1 the frame number, %2 the video's file name
                    text: qsTr("Can’t decode frame %1 of %2. Playback stopped.").arg(fs.controller.failedFrame, fs.controller.videoName)
                    color: fs.ink
                }
                Label {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: qsTr("Seek to another time, or press Esc to leave full screen.")
                    color: fs.inkMuted
                }
            }
        }

        Item {
            id: progress
            objectName: "videoFullscreenProgress"
            anchors.fill: stageHolder
            z: 2
            visible: fs.controller.active && fs.controller.overlays && fs.shell.videoView.progressBar && fs.shell.video.hasVideo
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


        Rectangle {
            id: hint
            objectName: "videoFullscreenHint"
            anchors { horizontalCenter: stageHolder.horizontalCenter; top: stageHolder.top; topMargin: 40 }
            z: 4
            width: hintText.implicitWidth + 32
            height: hintText.implicitHeight + 14
            radius: height / 2
            color: "#1B1D21"
            border.color: Qt.rgba(1, 1, 1, 0.18)
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
                color: fs.ink
            }
        }

        Item {
            id: strip
            objectName: "videoFullscreenPanel"
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            readonly property bool over: fs.controller.barPosition === VideoFullscreenController.BarPosition.Over
            readonly property int showBar: fs.controller.barMode
            readonly property bool holdsFocus: {
                for (let i = fs.activeFocusItem; i; i = i.parent)
                    if (i === strip)
                        return true
                return false
            }
            MenuKey.onRequested: fs.openStripMenu(Qt.point(16, 0))
            height: Math.max(over ? 0 : 112, stripColumn.implicitHeight + 24)
            z: 3
            opacity: fs.controller.barShown ? 1 : 0
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: 200 } }
            Rectangle {
                anchors.fill: parent
                visible: !strip.over
                color: "#000000"
            }
            Rectangle {
                anchors.fill: parent
                visible: strip.over
                gradient: Gradient {
                    GradientStop { position: 0; color: Qt.rgba(0, 0, 0, 0) }
                    GradientStop { position: 1; color: Qt.rgba(0, 0, 0, 0.82) }
                }
            }
            Rectangle {
                anchors { left: parent.left; right: parent.right; top: parent.top }
                height: 1
                color: "#ffffff"
                visible: Theme.highContrast
            }
            HoverHandler {
                cursorShape: Qt.ArrowCursor
                onHoveredChanged: fs.controller.pointerOverBar = hovered
            }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.AllButtons
                onClicked: mouse => {
                    if (mouse.button === Qt.RightButton)
                        fs.openStripMenu(Qt.point(mouse.x, mouse.y))
                }
            }
            WheelHandler {
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: event => fs.shell.videoView.panelWheel(Math.round(event.angleDelta.y / 120), event.modifiers)
            }

            ColumnLayout {
                id: stripColumn
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom
                          leftMargin: 16; rightMargin: 16; bottomMargin: 12 }
                spacing: 6
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Label {
                        objectName: "fullscreenStartTime"
                        text: fs.controller.positionText
                        color: fs.inkMuted
                        font.family: "monospace"
                        font.pixelSize: 12
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
                        Accessible.name: qsTr("Seek bar")
                        onMoved: fs.shell.video.seekBarMoved(Math.round(value), pressed)
                        onPressedChanged: if (!pressed) fs.shell.video.seekBarReleased()
                        Item {
                            id: ticks
                            objectName: "fullscreenKeyframeTicks"
                            x: fullscreenSeekBar.leftPadding
                            y: fullscreenSeekBar.topPadding + (fullscreenSeekBar.availableHeight - height) / 2
                            width: fullscreenSeekBar.availableWidth
                            height: 8
                            z: -1
                            property var positions: []
                            function refresh() { positions = fs.controller.keyframeTicks(Math.round(width)) }
                            onWidthChanged: refresh()
                            Connections {
                                target: fs.controller
                                function onKeyframesChanged() { ticks.refresh() }
                            }
                            Connections {
                                target: fs.shell.video
                                function onFrameCountChanged() { ticks.refresh() }
                            }
                            Repeater {
                                model: ticks.positions
                                Rectangle {
                                    required property int modelData
                                    x: modelData
                                    width: 1
                                    height: ticks.height
                                    color: Qt.rgba(1, 1, 1, 0.3)
                                }
                            }
                        }
                    }
                    Label {
                        objectName: "fullscreenLength"
                        text: fs.controller.lengthText
                        color: fs.inkMuted
                        font.family: "monospace"
                        font.pixelSize: 12
                    }
                }
                Item {
                    Layout.fillWidth: true
                    implicitHeight: 34
                    RowLayout {
                        id: leadingGroup
                        objectName: "fullscreenLine"
                        anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                        width: Math.max(0, centredGroup.x - 16)
                        spacing: 8
                        clip: true
                        activeFocusOnTab: false
                        readonly property bool hasLine: fs.shell.editor.hasLine
                        Accessible.role: Accessible.StaticText
                        //: The full screen strip's current line: %1 its number, %2 its text
                        Accessible.name: hasLine ? qsTr("Line %1: %2").arg(fs.shell.shell.activeLineNumber, lineText.text)
                                                 : noLine.text
                        Label {
                            id: lineNumber
                            objectName: "fullscreenLineNumber"
                            visible: leadingGroup.hasLine && fs.shell.shell.activeLineNumber > 0
                            text: fs.shell.shell.activeLineNumber
                            color: fs.inkMuted
                            font.family: "monospace"
                        }
                        Label {
                            id: lineText
                            objectName: "fullscreenLineText"
                            visible: leadingGroup.hasLine
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            text: fs.controller.lineLabel(fs.shell.editor.videoText)
                            textFormat: Text.PlainText
                            color: fs.ink
                            elide: Text.ElideRight
                            maximumLineCount: 1
                        }
                        Label {
                            id: noLine
                            objectName: "fullscreenNoLine"
                            visible: !leadingGroup.hasLine
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            //: The full screen strip when no Line is at the current time
                            text: qsTr("No line at this time")
                            color: fs.inkMuted
                            elide: Text.ElideRight
                        }
                    }
                    Rectangle {
                        x: leadingGroup.x - 4
                        y: leadingGroup.y - 4
                        width: leadingGroup.width + 8
                        height: leadingGroup.height + 8
                        visible: leadingGroup.activeFocus
                        color: "transparent"
                        border.color: fs.ink
                        radius: 4
                    }
                    RowLayout {
                        id: centredGroup
                        objectName: "fullscreenTransport"
                        anchors.centerIn: parent
                        spacing: 2
                        FullscreenStripButton {
                            id: previousFrame
                            objectName: "fullscreenPreviousFrame"
                            iconRole: "frame-previous"
                            text: qsTr("Previous frame")
                            enabled: fs.shell.video.hasVideo && fs.shell.video.frame > 0
                            tip: fs.shell.bitmapTip(text, "GLOBAL_PREVIOUS_FRAME", 0)
                            onClicked: if (!fs.shell.hotkeyGesture("GLOBAL_PREVIOUS_FRAME", 0, "bitmap")) fs.shell.video.stepFrames(-1)
                        }
                        FullscreenStripButton {
                            id: playPause
                            objectName: "fullscreenPlayPause"
                            iconRole: fs.shell.video.playing ? "media-pause" : "media-play"
                            text: fs.shell.video.playing ? qsTr("Pause") : qsTr("Play")
                            enabled: fs.shell.video.hasVideo
                            tip: fs.shell.bitmapTip(qsTr("Play or pause"), "VIDEO_PLAY_PAUSE", 3)
                            onClicked: if (!fs.shell.hotkeyGesture("VIDEO_PLAY_PAUSE", 3, "bitmap")) fs.shell.video.togglePlay()
                        }
                        FullscreenStripButton {
                            id: nextFrame
                            objectName: "fullscreenNextFrame"
                            iconRole: "frame-next"
                            text: qsTr("Next frame")
                            enabled: fs.shell.video.hasVideo && fs.shell.video.frame + 1 < fs.shell.video.frameCount
                            tip: fs.shell.bitmapTip(text, "GLOBAL_NEXT_FRAME", 0)
                            onClicked: if (!fs.shell.hotkeyGesture("GLOBAL_NEXT_FRAME", 0, "bitmap")) fs.shell.video.stepFrames(1)
                        }
                        FullscreenStripButton {
                            objectName: "fullscreenPlayLine"
                            iconRole: "play-line"
                            text: qsTr("Play the current line")
                            enabled: fs.shell.video.hasVideo
                            tip: fs.shell.bitmapTip(text, "GLOBAL_PLAY_ACTUAL_LINE", 0)
                            onClicked: if (!fs.shell.hotkeyGesture("GLOBAL_PLAY_ACTUAL_LINE", 0, "bitmap")) fs.shell.runGlobalHotkey("GLOBAL_PLAY_ACTUAL_LINE")
                        }
                        FullscreenStripButton {
                            objectName: "fullscreenStop"
                            iconRole: "media-stop"
                            text: qsTr("Stop")
                            enabled: fs.shell.video.hasVideo
                            tip: fs.shell.bitmapTip(text, "VIDEO_STOP", 3)
                            onClicked: if (!fs.shell.hotkeyGesture("VIDEO_STOP", 3, "bitmap")) fs.shell.video.stop()
                        }
                        Label {
                            objectName: "fullscreenTime"
                            Layout.leftMargin: 10
                            text: fs.controller.positionText
                            color: fs.ink
                            opacity: fs.controller.failedFrame >= 0 ? 0.5 : 1
                            font.family: "monospace"
                            font.pixelSize: 20
                            Accessible.name: qsTr("Video time, %1").arg(text)
                        }
                    }
                    RowLayout {
                        id: trailingGroup
                        objectName: "fullscreenTrailing"
                        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                        spacing: 2
                        FullscreenStripButton {
                            id: modeSwitch
                            objectName: "fullscreenTypeset"
                            iconRole: "tool-position"
                            label: qsTr("Typeset")
                            text: label
                            tip: qsTr("Typeset: switch to full-screen typesetting · the frame keeps its place")
                            onClicked: fs.controller.mode = VideoFullscreenController.Mode.Typesetting
                        }
                        FullscreenStripButton {
                            id: speaker
                            objectName: "fullscreenSpeaker"
                            iconRole: fs.shell.videoView.muted ? "volume-muted" : "volume"
                            text: fs.shell.videoView.muted ? qsTr("Unmute") : qsTr("Mute")
                            //: The full screen speaker's tooltip: %1 is Mute or Unmute
                            tip: qsTr("%1 · Wheel: volume · Right-click: output device").arg(text)
                            enabled: fs.shell.video.hasVideo
                            down: outputMenu.visible
                            onClicked: fs.shell.videoView.toggleMute()
                            MenuKey.onRequested: fs.popupAbove(outputMenu, speaker)
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
                        FullscreenStripButton {
                            objectName: "fullscreenExit"
                            iconRole: "fullscreen-exit"
                            text: qsTr("Leave full screen")
                            tip: fs.shell.bitmapTip(text, "VIDEO_FULL_SCREEN", 3, ShellMenus.keyText("Esc"))
                            onClicked: if (!fs.shell.hotkeyGesture("VIDEO_FULL_SCREEN", 3, "bitmap")) fs.controller.leave()
                        }
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
            onFullscreenSettingsRequested: fs.fullscreenSettingsRequested()
            onClosed: keyScope.forceActiveFocus()
        }
        AudioOutputMenu {
            id: outputMenu
            objectName: "fullscreenAudioOutputMenu"
            app: fs.shell.app
            onSettingsRequested: fs.audioSettingsRequested()
            onClosed: if (!strip.holdsFocus) keyScope.forceActiveFocus()
        }
        FullscreenBarMenu {
            id: stripMenu
            objectName: "fullscreenStripMenu"
            controller: fs.controller
            onSettingsRequested: fs.fullscreenSettingsRequested()
            onClosed: keyScope.forceActiveFocus()
        }
    }
}
