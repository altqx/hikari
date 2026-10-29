pragma Translator: "HikariSub.Video"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Hikari.Ui

// V5 (#184): the video's fullscreen window, legacy Fullscreen
// (HikariSub/VideoFullscreen.cpp) with VideoBox::SetFullscreen
// (VideoBox.cpp:741-855). A borderless window on the chosen monitor, black
// around the picture (the presenter's); the Video panel's picture, visual tools and zoom move
// into `stage` while it is shown (Main's videoStage), so what the panel shows
// is what fullscreen shows.
//
// FS1 (#275, owner-approved mockup on the card): a player plus the visual
// tools, after the MuseScore 4 reference and mpv's and VLC's controllers.
// At the bottom one bar: the seek bar across, then the transport (Previous
// file, Play / Pause, Play the current line, Stop, Next file), the times
// field, the current Line's number, times and text (read only,
// FS1-current-line) with Open editor, then the Visual tools menu, the
// speaker with the volume slider (FS1-speaker-gestures: a click mutes, the
// wheel sets the volume, a right click opens AV1's output device menu), the
// bar's mode and Exit full screen (FS1-exit-button-and-hint). Icon-only
// buttons name themselves in a tooltip and to assistive technology.
//
// The bar's mode (FS1-bar-mode, VIDEO_FULLSCREEN_BAR, Tab by default):
// Auto shows the bar and a top strip with the video's file name
// (FS1-file-name-strip) on any pointer movement and fades them out after
// 1.5 s without one, unless the video is paused or stopped, a menu is open
// or the pointer is over the bar (FS1-bar-auto-hide); Always keeps the bar
// and the picture ends above it (legacy "Show toolbar", m_PanelOnFullscreen);
// Never shows the picture only. The pointer hides over the picture whenever
// nothing holds the bar (FS1-cursor-hide). While a visual tool other than the
// crosshair is on, the tool rail shows at the left edge and the bar's top
// row is the tool's: VT1's inspector bar (#269) takes that row
// (`inspectorSlot`, FS1-tool-rail-contextual). With VIDEO_PROGRESS_BAR on, a progress bar
// with the times is drawn in the top right corner (RendererVideo::DrawProgressBar).
//
// Keys (Fullscreen::OnKeyPress and its accelerator table, SetAccels): the
// Video window's bindings, then Esc or B leaves (an open visual gesture is
// cancelled by Esc first, the accepted T1 policy), F leaves, the menu key
// opens the context menu, then the zoom mode's keys and the visual tool's;
// the Global bindings work here as in the main window. On entering, a hint
// names Esc for two seconds.
Window {
    id: fs
    objectName: "videoFullscreen"
    // Main's root: video, videoView, visualTools, hotkeys, runVideoHotkey,
    // bitmapTip, hotkeyGesture, editor, shell, app.
    required property var shell
    required property VideoFullscreenController controller
    readonly property Item stage: stageHolder
    readonly property Item keyTarget: keys
    readonly property alias contextMenu: fullscreenMenu
    readonly property bool cursorHidden: controller.cursorHidden
    // The bar's height while it is pinned (the tools' panel height).
    readonly property real pinnedPanelHeight: controller.pinned ? bar.height : 0
    // A visual tool other than the crosshair is on.
    readonly property bool toolOn: shell.visualTools.railEnabled
                                   && shell.visualTools.activeFamily > 0
    signal openVideoRequested()
    signal openSubtitlesRequested()
    signal aspectRatioRequested()
    signal fileQuestionRequested(bool next)
    signal audioSettingsRequested()

    // Fullscreen's wxFrame(parent, -1, emptyString, ...): no title of its
    // own (the platform shows the application's name for an empty one).
    title: ""
    // K2: the window in the theme's background; the picture keeps legacy's
    // black surround (SetBackgroundColour(L"#000000")) from the presenter,
    // which fills the stage.
    color: Theme.background
    flags: Qt.Window | Qt.FramelessWindowHint
    // On Wayland a window with a parent is a dialog of it (xdg_toplevel
    // set_parent): sway, for one, keeps the keyboard on the main window when
    // it opens on another output. The fullscreen window stands alone there
    // (V5-wayland-parent).
    transientParent: controller.ownedByMainWindow() ? shell : null
    visible: false

    onActiveChanged: if (active && controller.active) keys.forceActiveFocus()
    // Fullscreen::OnClose: the window manager's close (Alt+F4) leaves
    // fullscreen; the window itself stays for the next time. Hidden, it
    // lets the close through (the application's quit closes every window).
    onClosing: close => {
        if (fs.controller.active) {
            close.accepted = false
            fs.controller.leave()
        }
    }

    // A move over the picture with no button held wakes the bar and the pointer.
    // `at`: in the window.
    function pointerMoved(at) {
        fs.controller.pointerMoved(at)
    }
    function openMenuAt(point) {
        fullscreenMenu.at = point
        fullscreenMenu.popup(stageHolder, point)
    }
    // A bar menu opens above its button.
    // Its rows may come as it shows (the output menu's devices): its
    // bottom follows the button's top while it is open.
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
               || rail.menuOpen || toolBar.popoverOpen // VB1's More tools and More options
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

        // Fullscreen::OnKeyPress's own keys, unless a Global binding has them.
        function leaves(event) {
            return (event.key === Qt.Key_Escape || event.key === Qt.Key_B)
                    && fs.shell.hotkeys.actionFor(0, event.key, event.modifiers) === ""
        }
        // VT2-arrow-nudge: a tool's nudge (the arrows) before the bindings.
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
            } else if (event.key === Qt.Key_F) { // VideoBox::OnKeyPress: SetFullscreen()
                fs.controller.toggle(0)
                event.accepted = true
            } else if (event.key === Qt.Key_Menu) { // WXK_WINDOWS_MENU, the menu at the pointer
                fs.openMenuAt(fs.shell.videoView.cursorIn(stageHolder))
                event.accepted = true
            } else if (fs.shell.videoView.key(event.key, event.modifiers)) {
                event.accepted = true
            } else if (fs.shell.visualTools.key(event.key, event.modifiers, false, event.isAutoRepeat)) {
                event.accepted = true
            }
        }
        Keys.onReleased: event => event.accepted = fs.shell.visualTools.key(event.key, event.modifiers, true, event.isAutoRepeat)

        // Where Main's videoStage goes while fullscreen is shown.
        Item {
            id: stageHolder
            objectName: "videoFullscreenStage"
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: fs.controller.pinned ? parent.height - bar.height : parent.height
        }

        // FS1-file-name-strip: the video's file name (legacy's Videolabel),
        // with the bar in Auto.
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
            // A move over it keeps the bar, as one over the picture does.
            HoverHandler {
                cursorShape: Qt.ArrowCursor
                onPointChanged: if (hovered) fs.pointerMoved(point.scenePosition)
            }
            // Its clicks are not the picture's (pause on click, a tool's gesture).
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
                textFormat: Text.PlainText // a file's name, never markup
                maximumLineCount: 1
                clip: true
                color: Theme.text
                elide: Text.ElideMiddle
            }
        }

        // RendererVideo::DrawProgressBar: in the window's corner, over the
        // picture, drawn in white and black on any video (below the file
        // name's strip while it shows).
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
            // The black frame, the white one inside it and the bar.
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
            // DRAWOUTTEXT: white text with a black outline, the program's
            // font four points larger (Options.GetFont(4)).
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

        // FS1-tool-rail-contextual: the Video panel's rail, while a tool is on.
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
            // It stays when the bar hides, and its buttons take the pointer
            // from the picture (on X11; offscreen the hover goes through): a
            // move over it wakes the bar as well.
            HoverHandler {
                onPointChanged: if (hovered) fs.pointerMoved(point.scenePosition)
            }
        }

        // FS1-exit-button-and-hint: the way out, for two seconds after entering.
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

        // The bar (legacy Fullscreen's panel).
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
            // The bar takes the pointer: moves over it are not the picture's,
            // and it stays while the pointer is on it.
            HoverHandler {
                cursorShape: Qt.ArrowCursor
                onHoveredChanged: fs.controller.pointerOverBar = hovered
            }
            // Clicks between its controls are not the picture's either.
            MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }
            // VideoBox.cpp:519-534: the wheel below the picture is the volume's.
            WheelHandler {
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: event => fs.shell.videoView.panelWheel(Math.round(event.angleDelta.y / 120), event.modifiers)
            }

            ColumnLayout {
                id: barColumn
                anchors { left: parent.left; right: parent.right; top: parent.top
                          leftMargin: 8; rightMargin: 8; topMargin: 5 }
                spacing: 2
                // The active tool's row: VT1's inspector bar (#269), the
                // Video panel's (the tool, its options and values, the batch
                // picker), on the bar's own background.
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
                        dialogs: fs.visible // the Video panel's bar has them otherwise
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
                    // It follows the reported frame (the player's while
                    // playing, at the texts' pace) except while held, as
                    // legacy VideoSlider's `block` kept its thumb under the
                    // mouse; only its own moves seek.
                    Binding on value {
                        when: !fullscreenSeekBar.pressed
                        value: Math.max(0, fs.shell.video.frame)
                        restoreMode: Binding.RestoreNone
                    }
                    enabled: fs.shell.video.hasVideo
                    focusPolicy: Qt.NoFocus
                    Accessible.name: qsTr("Video position")
                    // A drag seeks at most every 50 ms and once more where it
                    // is let go (V2 user decision 2026-10-14).
                    onMoved: fs.shell.video.seekBarMoved(Math.round(value), pressed)
                    onPressedChanged: if (!pressed) fs.shell.video.seekBarReleased()
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    // Legacy's bitmap buttons (VideoFullscreen.cpp:57-66):
                    // the binding in the tooltip, and Shift+click maps it.
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
                        // The click goes up to VideoBox, which hands it to the
                        // frame's OnMenuSelected1 (VideoBox.cpp:185-188).
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
                    // The times field (ShowTimes into mstimes): a keyframe in the warning colour.
                    Label {
                        objectName: "fullscreenTimes"
                        text: fs.shell.video.times
                        color: fs.shell.video.keyframeShown ? Theme.warning : Theme.text
                        Accessible.name: qsTr("Video times")
                    }
                    ToolSeparator { visible: currentLine.visible }
                    // FS1-current-line: the current Line, read only.
                    RowLayout {
                        id: currentLine
                        objectName: "fullscreenLine"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        visible: fs.shell.editor.hasLine
                        spacing: 8
                        clip: true // a narrow window: the times and the text give way, never the buttons
                        Accessible.role: Accessible.StaticText
                        //: The full screen bar's current line: %1 its number, %2 its times, %3 its text
                        Accessible.name: qsTr("Line %1, %2: %3").arg(lineNumber.text).arg(fs.shell.editor.videoTimes).arg(lineText.text)
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
                            text: fs.shell.editor.videoTimes // the video's: committed without live editing
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
                            textFormat: Text.PlainText // a Line's text, never markup (no <img> fetched)
                            color: Theme.text
                            elide: Text.ElideRight
                            maximumLineCount: 1
                        }
                    }
                    // V5-open-editor-now: the context menu's Open editor, beside the Line.
                    IconButton {
                        objectName: "fullscreenOpenEditor"
                        iconRole: "editor"
                        text: qsTr("Open editor")
                        focusPolicy: Qt.NoFocus
                        onClicked: fs.shell.openEditorFromFullScreen() // Main's: the Line editor shown too (WS3)
                    }
                    Item { Layout.fillWidth: !currentLine.visible }
                    // FS1-tool-rail-contextual: the twelve tools, by name.
                    IconButton {
                        id: toolsButton
                        objectName: "fullscreenToolsButton"
                        // In application::visual::Family's order (on one line: icon_tests reads the roles from it).
                        iconRole: ["tool-crosshair", "tool-position", "tool-move", "tool-scale", "tool-rotate-z", "tool-rotate-xy", "tool-clip-rect", "tool-clip-vector", "tool-drawing", "tool-move-all", "tool-all-tags", "tool-perspective"][Math.max(0, fs.shell.visualTools.activeFamily)] ?? ""
                        text: qsTr("Visual tools")
                        enabled: fs.shell.visualTools.railEnabled
                        down: toolsMenu.visible
                        focusPolicy: Qt.NoFocus
                        onClicked: fs.popupAbove(toolsMenu, toolsButton)
                    }
                    ToolSeparator { visible: toolsButton.visible }
                    // FS1-speaker-gestures.
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
                        // The volume's even in the zoom mode, which takes the bar's wheel.
                        WheelHandler {
                            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                            onWheel: event => fs.shell.videoView.volumeWheel(Math.round(event.angleDelta.y / 120))
                        }
                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: fs.popupAbove(outputMenu, speaker)
                        }
                    }
                    // The Video panel's volume slider (V5-volume-slider).
                    VideoVolumeSlider {
                        sliderName: "fullscreenVolume"
                        view: fs.shell.videoView
                        hasVideo: fs.shell.video.hasVideo
                        showIcon: false
                        sliderWidth: 80
                    }
                    ToolSeparator {}
                    // FS1-bar-mode: the mode, and its menu.
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
                        // The context menu's: the binding, else Escape.
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

        // The context menu in this window (VideoBox::ContextMenu with
        // m_IsFullscreen: GetPopupMenuSelection(pos, m_FullScreenWindow)).
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
        // AV1's output device menu, from the speaker's right click.
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
                // VT3: in the rail's order; each item names its family's id.
                model: fs.shell.visualTools.railOrder
                delegate: ShellMenuItem {
                    required property int modelData
                    readonly property int family: modelData
                    objectName: "fullscreenTool" + family
                    // In application::visual::Family's order (on one line: icon_tests reads the roles from it).
                    iconRole: ["tool-crosshair", "tool-position", "tool-move", "tool-scale", "tool-rotate-z", "tool-rotate-xy", "tool-clip-rect", "tool-clip-vector", "tool-drawing", "tool-move-all", "tool-all-tags", "tool-perspective"][family] ?? ""
                    text: fs.shell.visualTools.families[family]?.name ?? ""
                    checkable: true
                    checked: fs.shell.visualTools.activeFamily === family
                    keepsMenuOpen: false
                    // Choosing the on tool again would go back to the crosshair.
                    onTriggered: if (fs.shell.visualTools.activeFamily !== family) fs.shell.visualTools.selectFamily(family)
                }
                onObjectAdded: (index, object) => toolsMenu.insertItem(index, object)
                onObjectRemoved: (index, object) => toolsMenu.removeItem(object)
            }
        }
    }
}
