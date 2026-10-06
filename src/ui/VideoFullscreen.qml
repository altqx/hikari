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
// At the bottom the panel (legacy `panel`): the seek bar across, then the
// transport (Previous file, Play / Pause, Play the current line, Stop, Next
// file), "Show toolbar", the times field and the video's file name, and
// with "Show toolbar" the visual tool row (legacy VideoToolbar). Legacy's
// volume slider shows only for a DirectShow video (VideoFullscreen.cpp:
// 164-168); the rewrite always indexes (V3-indexing-retired), so it is not
// shown, and the wheel over the panel still sets the volume.
// Icon-only buttons name themselves in a tooltip and to assistive
// technology. With "Show toolbar" the panel stays and the picture ends above
// it (m_PanelOnFullscreen); without, the picture takes the whole window, the
// panel shows when the pointer goes below the picture and hides when it comes
// back over it, and the pointer hides after a second without moving
// (VideoBox::OnIdle). With VIDEO_PROGRESS_BAR on, a progress bar with the
// times is drawn in the top right corner (RendererVideo::DrawProgressBar).
//
// Keys (Fullscreen::OnKeyPress and its accelerator table, SetAccels): the
// Video window's bindings, then Esc or B leaves (an open visual gesture is
// cancelled by Esc first, the accepted T1 policy), F leaves, the menu key
// opens the context menu, then the zoom mode's keys and the visual tool's;
// the Global bindings work here as in the main window.
Window {
    id: fs
    objectName: "videoFullscreen"
    // Main's root: video, videoView, visualTools, hotkeys, runVideoHotkey,
    // bitmapTip, hotkeyGesture, app.
    required property var shell
    required property VideoFullscreenController controller
    readonly property Item stage: stageHolder
    readonly property Item keyTarget: keys
    readonly property alias contextMenu: fullscreenMenu
    // VideoBox::OnIdle hid the pointer (the picture's cursor follows it).
    readonly property bool cursorHidden: idle.cursorHidden
    // The panel's height while it is pinned (the tools' panel height).
    readonly property real pinnedPanelHeight: controller.showToolbar ? panel.height : 0
    signal openVideoRequested()
    signal openSubtitlesRequested()
    signal aspectRatioRequested()
    signal fileQuestionRequested(bool next)

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
    // it opens on another output. The fullscreen window stands alone there.
    transientParent: Qt.platform.pluginName.startsWith("wayland") ? null : shell
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

    // A move over the picture: the panel shows or hides (the picture's
    // coordinates are the window's: without "Show toolbar" it fills it).
    function pointerMoved(y) {
        idle.cursorHidden = false
        if (fs.controller.pointerMoved(y, fs.height, panel.implicitHeight))
            keys.forceActiveFocus() // panel->Show(false); SetFocus()
        if (!fs.controller.panelShown && !fullscreenMenu.opened)
            idle.restart() // idletime.Start(1000, true)
    }
    function openMenuAt(point) {
        fullscreenMenu.at = point
        fullscreenMenu.popup(stageHolder, point)
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
        Keys.onShortcutOverride: event => event.accepted = fs.shell.hotkeys.actionFor(3, event.key, event.modifiers) !== ""
                                                          || keys.leaves(event)
        Keys.onPressed: event => {
            const action = fs.shell.hotkeys.actionFor(3, event.key, event.modifiers)
            if (action !== "") {
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
            height: fs.controller.showToolbar ? parent.height - panel.height : parent.height
        }

        // VideoBox::OnIdle: without moving for a second, the pointer hides
        // over the picture while the panel is hidden.
        Timer {
            id: idle
            property bool cursorHidden: false
            interval: 1000
            onTriggered: cursorHidden = !fs.controller.panelShown && !fullscreenMenu.opened
        }

        // RendererVideo::DrawProgressBar: in the window's corner, over the
        // picture, drawn in white and black on any video.
        Item {
            id: progress
            objectName: "videoFullscreenProgress"
            anchors.fill: parent
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

        // Legacy Fullscreen's panel.
        Rectangle {
            id: panel
            objectName: "videoFullscreenPanel"
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            implicitHeight: panelColumn.implicitHeight + 8
            height: implicitHeight
            visible: fs.controller.panelShown
            color: Theme.panel
            z: 1
            // The panel takes the pointer: moves over it are not the picture's.
            HoverHandler { cursorShape: Qt.ArrowCursor }
            // VideoBox.cpp:519-534: the wheel below the picture is the volume's.
            WheelHandler {
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: event => fs.shell.videoView.panelWheel(Math.round(event.angleDelta.y / 120), event.modifiers)
            }

            ColumnLayout {
                id: panelColumn
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 4 }
                spacing: 2
                Slider {
                    objectName: "fullscreenSlider"
                    Layout.fillWidth: true
                    from: 0
                    to: Math.max(0, fs.shell.video.frameCount - 1)
                    stepSize: 1
                    value: Math.max(0, fs.shell.video.frame)
                    enabled: fs.shell.video.hasVideo
                    focusPolicy: Qt.NoFocus
                    Accessible.name: qsTr("Video position")
                    onMoved: fs.shell.video.showFrameAt(Math.round(value))
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
                    // "Show toolbar" (legacy a check box after the buttons).
                    IconButton {
                        objectName: "fullscreenShowToolbar"
                        iconRole: "show-toolbar"
                        text: qsTr("Show toolbar")
                        checkable: true
                        checked: fs.controller.showToolbar
                        focusPolicy: Qt.NoFocus
                        onToggled: fs.controller.showToolbar = checked
                    }
                    // The times field (ShowTimes into mstimes): a keyframe in the warning colour.
                    Label {
                        objectName: "fullscreenTimes"
                        Layout.leftMargin: 8
                        text: fs.shell.video.times
                        color: fs.shell.video.keyframeShown ? Theme.warning : Theme.text
                        Accessible.name: qsTr("Video times")
                    }
                    // Videolabel: the video's file name.
                    Label {
                        objectName: "fullscreenVideoName"
                        Layout.fillWidth: true
                        Layout.leftMargin: 12
                        text: fs.controller.videoName
                        color: Theme.text
                        elide: Text.ElideMiddle
                    }
                }
                // The toolbar row (legacy vToolbar): the visual tool families
                // and the active family's options.
                RowLayout {
                    objectName: "fullscreenToolbar"
                    Layout.fillWidth: true
                    visible: fs.controller.showToolbar
                    spacing: 1
                    Repeater {
                        model: fs.shell.visualTools.families
                        delegate: IconToolButton {
                            required property var modelData
                            required property int index
                            objectName: "fullscreenTool" + index
                            // In application::visual::Family's order, as the rail's.
                            iconRole: ["tool-crosshair", "tool-position", "tool-move", "tool-scale", "tool-rotate-z", "tool-rotate-xy", "tool-clip-rect", "tool-clip-vector", "tool-drawing", "tool-move-all", "tool-all-tags"][index] ?? ""
                            text: modelData.name
                            checkable: true
                            checked: fs.shell.visualTools.activeFamily === index
                            enabled: fs.shell.visualTools.railEnabled
                            focusPolicy: Qt.NoFocus
                            onClicked: fs.shell.visualTools.selectFamily(index)
                        }
                    }
                    ToolSeparator { visible: toolOptions.visible }
                    VisualToolOptions {
                        id: toolOptions
                        objectName: "fullscreenToolOptions"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        clip: true
                        tools: fs.shell.visualTools
                    }
                    Item { Layout.fillWidth: !toolOptions.visible }
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
    }
}
