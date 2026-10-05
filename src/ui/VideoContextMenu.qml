import QtQuick
import QtQuick.Controls
import Hikari.Ui

// V4: the video's context menu, legacy VideoBox::ContextMenu
// (VideoBox.cpp:921-1059) in its order. Each Video window item shows its
// binding; Shift alone with a click maps that hotkey for the Video window
// instead of running it (legacy maps the ids from 2000 to 2099,
// VideoBox.cpp:1027-1033: not the Global ids, the recent files, the
// monitors or the open commands).
//
// V5: "Full screen" / "Exit full screen" (VIDEO_FULL_SCREEN, "F" or
// "Escape" unless it has a binding of its own), one entry per monitor after
// the first ("Open in full screen on monitor %i", in fullscreen "Switch full
// screen to monitor %i", numbered from 2) and "Open editor" (GLOBAL_EDITOR,
// fullscreen only) after Stop; "Copy video position" only out of fullscreen;
// "Show / hide progress bar" in fullscreen only. The fullscreen window has
// its own instance (`itemPrefix` names its items). The root's id is not
// `menu`: inside a delegate that name is the MenuItem's own `menu` property
// (the submenu holding it), which shadows an id of the outer context.
//
// Left to its card: V3 adds "Remove video" as "Unload video"
// (VIDEO_DELETE_FILE) after the separator, then the streams and the
// chapters.
ShellMenu {
    id: videoMenu
    objectName: "videoContextMenu"
    // Main's root: video, videoView, videoFullscreen, visualTools, hotkeys,
    // hotkeyGesture, boundKeys, openSubtitles, app.
    required property var shell
    // Where the menu opened, in the video area (VIDEO_COPY_COORDS copies there).
    property point at
    property var recent: ({ subtitles: [], videos: [] })
    // V5: the items' objectName prefix, and the monitors when it opened
    // (GetMonitorRect1 on each ContextMenu).
    property string itemPrefix: "videoMenu"
    property int monitors: 1
    readonly property bool fullscreen: shell.videoView.fullscreen
    signal openVideoRequested()
    signal openSubtitlesRequested()
    signal aspectRatioRequested()

    onAboutToShow: {
        recent = shell.videoView.recentFiles()
        monitors = shell.videoFullscreen.monitorCount()
    }

    function keys(symbol) {
        const key = videoMenu.shell.boundKeys(symbol, 3)
        return key.length ? "\t" + key : ""
    }
    // VIDEO_HOTKEY ids: Shift alone maps the Video window's hotkey.
    function gesture(symbol) {
        return videoMenu.shell.hotkeyGesture(symbol, 3)
    }
    function indexOfItem(item) {
        for (let i = 0; i < videoMenu.count; ++i)
            if (videoMenu.itemAt(i) === item)
                return i
        return videoMenu.count
    }

    ShellMenuItem {
        objectName: videoMenu.itemPrefix + "CopyCoords"
        // `if (!m_IsFullscreen && editor)`
        visible: !videoMenu.fullscreen
        height: visible ? implicitHeight : 0
        text: qsTr("Copy video position") + videoMenu.keys("VIDEO_COPY_COORDS")
        onTriggered: if (!videoMenu.gesture("VIDEO_COPY_COORDS")) videoMenu.shell.visualTools.copyCoordinates(videoMenu.at.x, videoMenu.at.y)
    }
    ShellMenuItem {
        objectName: videoMenu.itemPrefix + "PlayPause"
        text: (videoMenu.shell.video.playing ? qsTr("Pause") : qsTr("Play")) + videoMenu.keys("VIDEO_PLAY_PAUSE")
        enabled: videoMenu.shell.video.hasVideo
        onTriggered: if (!videoMenu.gesture("VIDEO_PLAY_PAUSE")) videoMenu.shell.video.togglePlay()
    }
    ShellMenuItem {
        objectName: videoMenu.itemPrefix + "Stop"
        text: qsTr("Stop") + videoMenu.keys("VIDEO_STOP")
        enabled: videoMenu.shell.video.playing
        onTriggered: if (!videoMenu.gesture("VIDEO_STOP")) videoMenu.shell.video.stop()
    }
    // V5 (VideoBox.cpp:934-950).
    ShellMenuItem {
        id: fullScreenItem
        objectName: videoMenu.itemPrefix + "FullScreen"
        text: {
            const key = videoMenu.shell.boundKeys("VIDEO_FULL_SCREEN", 3)
            return videoMenu.fullscreen ? qsTr("Exit full screen") + "\t" + (key.length ? key : qsTr("Escape"))
                                   : qsTr("Full screen") + "\t" + (key.length ? key : "F")
        }
        enabled: videoMenu.shell.video.hasVideo
        onTriggered: if (!videoMenu.gesture("VIDEO_FULL_SCREEN")) videoMenu.shell.videoFullscreen.toggle(0)
    }
    Instantiator {
        model: Math.max(0, videoMenu.monitors - 1)
        delegate: ShellMenuItem {
            required property int index
            objectName: videoMenu.itemPrefix + "Monitor" + (index + 1)
            text: videoMenu.fullscreen ? qsTr("Switch full screen to monitor %1").arg(index + 2)
                                  : qsTr("Open in full screen on monitor %1").arg(index + 2)
            enabled: videoMenu.shell.video.hasVideo
            onTriggered: videoMenu.shell.videoFullscreen.showOn(index + 1)
        }
        onObjectAdded: (index, object) => videoMenu.insertItem(videoMenu.indexOfItem(fullScreenItem) + 1 + index, object)
        onObjectRemoved: (index, object) => videoMenu.removeItem(object)
    }
    ShellMenuItem {
        objectName: videoMenu.itemPrefix + "OpenEditor"
        // menu->SetWindow(GLOBAL_HOTKEY): the Global binding.
        text: qsTr("Open editor") + (videoMenu.shell.boundKeys("GLOBAL_EDITOR", 0).length
                                     ? "\t" + videoMenu.shell.boundKeys("GLOBAL_EDITOR", 0) : "")
        enabled: videoMenu.fullscreen
        onTriggered: videoMenu.shell.app.openEditorFromFullScreen()
    }
    ShellMenu {
        id: recentSubtitlesMenu
        objectName: videoMenu.itemPrefix + "RecentSubtitles"
        title: qsTr("Recently opened subtitles")
        Instantiator {
            model: videoMenu.recent.subtitles
            delegate: ShellMenuItem {
                required property var modelData
                required property int index
                objectName: videoMenu.itemPrefix + "RecentSubtitles" + index
                text: modelData.label
                onTriggered: videoMenu.shell.openSubtitles(modelData.path)
            }
            onObjectAdded: (index, object) => recentSubtitlesMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => recentSubtitlesMenu.removeItem(object)
        }
    }
    ShellMenu {
        id: recentVideosMenu
        objectName: videoMenu.itemPrefix + "RecentVideos"
        title: qsTr("Recently opened videos")
        Instantiator {
            model: videoMenu.recent.videos
            delegate: ShellMenuItem {
                required property var modelData
                required property int index
                objectName: videoMenu.itemPrefix + "RecentVideos" + index
                text: modelData.label
                onTriggered: videoMenu.shell.video.openVideo(modelData.path)
            }
            onObjectAdded: (index, object) => recentVideosMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => recentVideosMenu.removeItem(object)
        }
    }
    ShellMenuItem {
        objectName: videoMenu.itemPrefix + "OpenVideo"
        text: qsTr("Open video") + (videoMenu.shell.boundKeys("GLOBAL_OPEN_VIDEO", 0).length
                                    ? "\t" + videoMenu.shell.boundKeys("GLOBAL_OPEN_VIDEO", 0) : "")
        onTriggered: videoMenu.openVideoRequested()
    }
    ShellMenuItem {
        objectName: videoMenu.itemPrefix + "OpenSubtitles"
        text: qsTr("Open subtitles") + (videoMenu.shell.boundKeys("GLOBAL_OPEN_SUBS", 0).length
                                        ? "\t" + videoMenu.shell.boundKeys("GLOBAL_OPEN_SUBS", 0) : "")
        onTriggered: videoMenu.openSubtitlesRequested()
    }
    ShellMenuItem {
        objectName: videoMenu.itemPrefix + "ProgressBar"
        text: qsTr("Show / hide progress bar") + videoMenu.keys("VIDEO_HIDE_PROGRESS_BAR")
        enabled: videoMenu.fullscreen
        onTriggered: if (!videoMenu.gesture("VIDEO_HIDE_PROGRESS_BAR")) videoMenu.shell.videoView.toggleProgressBar()
    }
    ShellMenuItem {
        objectName: videoMenu.itemPrefix + "AspectRatio"
        text: qsTr("Change aspect ratio") + videoMenu.keys("VIDEO_ASPECT_RATIO")
        onTriggered: if (!videoMenu.gesture("VIDEO_ASPECT_RATIO")) videoMenu.aspectRatioRequested()
    }
    ShellMenuItem {
        objectName: videoMenu.itemPrefix + "SaveSubbedFrame"
        text: qsTr("Save frame with subtitles as PNG") + videoMenu.keys("VIDEO_SAVE_SUBBED_FRAME_TO_PNG")
        enabled: videoMenu.shell.videoView.canSnapshot
        onTriggered: if (!videoMenu.gesture("VIDEO_SAVE_SUBBED_FRAME_TO_PNG")) videoMenu.shell.videoView.snapshot("VIDEO_SAVE_SUBBED_FRAME_TO_PNG")
    }
    ShellMenuItem {
        objectName: videoMenu.itemPrefix + "CopySubbedFrame"
        text: qsTr("Copy frame with subtitles to clipboard") + videoMenu.keys("VIDEO_COPY_SUBBED_FRAME_TO_CLIPBOARD")
        enabled: videoMenu.shell.videoView.canSnapshot
        onTriggered: if (!videoMenu.gesture("VIDEO_COPY_SUBBED_FRAME_TO_CLIPBOARD")) videoMenu.shell.videoView.snapshot("VIDEO_COPY_SUBBED_FRAME_TO_CLIPBOARD")
    }
    ShellMenuItem {
        objectName: videoMenu.itemPrefix + "SaveFrame"
        text: qsTr("Save frame as PNG") + videoMenu.keys("VIDEO_SAVE_FRAME_TO_PNG")
        enabled: videoMenu.shell.videoView.canSnapshot
        onTriggered: if (!videoMenu.gesture("VIDEO_SAVE_FRAME_TO_PNG")) videoMenu.shell.videoView.snapshot("VIDEO_SAVE_FRAME_TO_PNG")
    }
    ShellMenuItem {
        objectName: videoMenu.itemPrefix + "CopyFrame"
        text: qsTr("Copy frame to clipboard") + videoMenu.keys("VIDEO_COPY_FRAME_TO_CLIPBOARD")
        enabled: videoMenu.shell.videoView.canSnapshot
        onTriggered: if (!videoMenu.gesture("VIDEO_COPY_FRAME_TO_CLIPBOARD")) videoMenu.shell.videoView.snapshot("VIDEO_COPY_FRAME_TO_CLIPBOARD")
    }
    MenuSeparator {}
}
