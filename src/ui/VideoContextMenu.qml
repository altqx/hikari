import QtQuick
import QtQuick.Controls
import Hikari.Ui

// V4: the video's context menu, legacy VideoBox::ContextMenu
// (VideoBox.cpp:921-1059) in its order, out of fullscreen. Each Video
// window item shows its binding; Shift alone with a click maps that
// hotkey for the Video window instead of running it (legacy maps the ids
// from 2000 to 2099, VideoBox.cpp:1027-1033: not the Global ids, the recent
// files or the open commands).
//
// Left to their cards: V5 adds "Full screen" (VIDEO_FULL_SCREEN), the
// monitors and "Open editor" (GLOBAL_EDITOR) after Stop, and enables "Show
// / hide progress bar" in fullscreen; V3 adds "Remove video" as "Unload
// video" (VIDEO_DELETE_FILE) after the separator, then the streams and the
// chapters.
ShellMenu {
    id: menu
    objectName: "videoContextMenu"
    // Main's root: video, videoView, visualTools, hotkeys, hotkeyGesture,
    // boundKeys, openSubtitles.
    required property var shell
    // Where the menu opened, in the video area (VIDEO_COPY_COORDS copies there).
    property point at
    property var recent: ({ subtitles: [], videos: [] })
    signal openVideoRequested()
    signal openSubtitlesRequested()
    signal aspectRatioRequested()

    onAboutToShow: recent = shell.videoView.recentFiles()

    function keys(symbol) {
        const key = menu.shell.boundKeys(symbol, 3)
        return key.length ? "\t" + key : ""
    }
    // VIDEO_HOTKEY ids: Shift alone maps the Video window's hotkey.
    function gesture(symbol) {
        return menu.shell.hotkeyGesture(symbol, 3)
    }

    ShellMenuItem {
        objectName: "videoMenuCopyCoords"
        text: qsTr("Copy video position") + menu.keys("VIDEO_COPY_COORDS")
        onTriggered: if (!menu.gesture("VIDEO_COPY_COORDS")) menu.shell.visualTools.copyCoordinates(menu.at.x, menu.at.y)
    }
    ShellMenuItem {
        objectName: "videoMenuPlayPause"
        text: (menu.shell.video.playing ? qsTr("Pause") : qsTr("Play")) + menu.keys("VIDEO_PLAY_PAUSE")
        enabled: menu.shell.video.hasVideo
        onTriggered: if (!menu.gesture("VIDEO_PLAY_PAUSE")) menu.shell.video.togglePlay()
    }
    ShellMenuItem {
        objectName: "videoMenuStop"
        text: qsTr("Stop") + menu.keys("VIDEO_STOP")
        enabled: menu.shell.video.playing
        onTriggered: if (!menu.gesture("VIDEO_STOP")) menu.shell.video.stop()
    }
    ShellMenu {
        id: recentSubtitlesMenu
        objectName: "videoMenuRecentSubtitles"
        title: qsTr("Recently opened subtitles")
        Instantiator {
            model: menu.recent.subtitles
            delegate: ShellMenuItem {
                required property var modelData
                required property int index
                objectName: "videoMenuRecentSubtitles" + index
                text: modelData.label
                onTriggered: menu.shell.openSubtitles(modelData.path)
            }
            onObjectAdded: (index, object) => recentSubtitlesMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => recentSubtitlesMenu.removeItem(object)
        }
    }
    ShellMenu {
        id: recentVideosMenu
        objectName: "videoMenuRecentVideos"
        title: qsTr("Recently opened videos")
        Instantiator {
            model: menu.recent.videos
            delegate: ShellMenuItem {
                required property var modelData
                required property int index
                objectName: "videoMenuRecentVideos" + index
                text: modelData.label
                onTriggered: menu.shell.video.openVideo(modelData.path)
            }
            onObjectAdded: (index, object) => recentVideosMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => recentVideosMenu.removeItem(object)
        }
    }
    ShellMenuItem {
        objectName: "videoMenuOpenVideo"
        text: qsTr("Open video") + (menu.shell.boundKeys("GLOBAL_OPEN_VIDEO", 0).length
                                    ? "\t" + menu.shell.boundKeys("GLOBAL_OPEN_VIDEO", 0) : "")
        onTriggered: menu.openVideoRequested()
    }
    ShellMenuItem {
        objectName: "videoMenuOpenSubtitles"
        text: qsTr("Open subtitles") + (menu.shell.boundKeys("GLOBAL_OPEN_SUBS", 0).length
                                        ? "\t" + menu.shell.boundKeys("GLOBAL_OPEN_SUBS", 0) : "")
        onTriggered: menu.openSubtitlesRequested()
    }
    ShellMenuItem {
        objectName: "videoMenuProgressBar"
        text: qsTr("Show / hide progress bar") + menu.keys("VIDEO_HIDE_PROGRESS_BAR")
        enabled: false // fullscreen only (V5)
        onTriggered: if (!menu.gesture("VIDEO_HIDE_PROGRESS_BAR")) menu.shell.videoView.toggleProgressBar()
    }
    ShellMenuItem {
        objectName: "videoMenuAspectRatio"
        text: qsTr("Change aspect ratio") + menu.keys("VIDEO_ASPECT_RATIO")
        onTriggered: if (!menu.gesture("VIDEO_ASPECT_RATIO")) menu.aspectRatioRequested()
    }
    ShellMenuItem {
        objectName: "videoMenuSaveSubbedFrame"
        text: qsTr("Save frame with subtitles as PNG") + menu.keys("VIDEO_SAVE_SUBBED_FRAME_TO_PNG")
        enabled: menu.shell.videoView.canSnapshot
        onTriggered: if (!menu.gesture("VIDEO_SAVE_SUBBED_FRAME_TO_PNG")) menu.shell.videoView.snapshot("VIDEO_SAVE_SUBBED_FRAME_TO_PNG")
    }
    ShellMenuItem {
        objectName: "videoMenuCopySubbedFrame"
        text: qsTr("Copy frame with subtitles to clipboard") + menu.keys("VIDEO_COPY_SUBBED_FRAME_TO_CLIPBOARD")
        enabled: menu.shell.videoView.canSnapshot
        onTriggered: if (!menu.gesture("VIDEO_COPY_SUBBED_FRAME_TO_CLIPBOARD")) menu.shell.videoView.snapshot("VIDEO_COPY_SUBBED_FRAME_TO_CLIPBOARD")
    }
    ShellMenuItem {
        objectName: "videoMenuSaveFrame"
        text: qsTr("Save frame as PNG") + menu.keys("VIDEO_SAVE_FRAME_TO_PNG")
        enabled: menu.shell.videoView.canSnapshot
        onTriggered: if (!menu.gesture("VIDEO_SAVE_FRAME_TO_PNG")) menu.shell.videoView.snapshot("VIDEO_SAVE_FRAME_TO_PNG")
    }
    ShellMenuItem {
        objectName: "videoMenuCopyFrame"
        text: qsTr("Copy frame to clipboard") + menu.keys("VIDEO_COPY_FRAME_TO_CLIPBOARD")
        enabled: menu.shell.videoView.canSnapshot
        onTriggered: if (!menu.gesture("VIDEO_COPY_FRAME_TO_CLIPBOARD")) menu.shell.videoView.snapshot("VIDEO_COPY_FRAME_TO_CLIPBOARD")
    }
    MenuSeparator {}
}
