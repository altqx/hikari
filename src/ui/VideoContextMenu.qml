pragma Translator: "HikariSub.Video"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

ShellMenu {
    // Not `menu`: a MenuItem delegate's own `menu` property would shadow that id.
    id: videoMenu
    objectName: "videoContextMenu"
    required property var shell
    property point at
    property var recent: ({ subtitles: [], videos: [] })
    property string itemPrefix: "videoMenu"
    property int monitors: 1
    readonly property bool fullscreen: shell.videoView.fullscreen
    signal openVideoRequested()
    signal openSubtitlesRequested()
    signal aspectRatioRequested()

    property var filters: []
    property var streams: []
    property var chapters: []
    onAboutToShow: {
        recent = shell.videoView.recentFiles()
        monitors = shell.videoFullscreen.monitorCount()
        filters = shell.app.playerFilters()
        streams = shell.video.hasVideo ? shell.video.streams() : []
        chapters = shell.video.hasVideo ? shell.video.chapters() : []
    }

    // MUST KILL: boundKeys and hotkeyGesture take a magic context int (3 Video window, 0 Global)
    function keys(symbol) {
        const key = videoMenu.shell.boundKeys(symbol, 3)
        return key.length ? "\t" + key : ""
    }
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
        iconRole: "copy-position"
        objectName: videoMenu.itemPrefix + "CopyCoords"
        visible: !videoMenu.fullscreen
        height: visible ? implicitHeight : 0
        text: qsTr("Copy video position") + videoMenu.keys("VIDEO_COPY_COORDS")
        onTriggered: if (!videoMenu.gesture("VIDEO_COPY_COORDS")) videoMenu.shell.visualTools.copyCoordinates(videoMenu.at.x, videoMenu.at.y)
    }
    ShellMenuItem {
        objectName: videoMenu.itemPrefix + "PlayPause"
        iconRole: videoMenu.shell.video.playing ? "media-pause" : "media-play"
        text: (videoMenu.shell.video.playing ? qsTr("Pause") : qsTr("Play")) + videoMenu.keys("VIDEO_PLAY_PAUSE")
        enabled: videoMenu.shell.video.hasVideo
        onTriggered: if (!videoMenu.gesture("VIDEO_PLAY_PAUSE")) videoMenu.shell.video.togglePlay()
    }
    ShellMenuItem {
        objectName: videoMenu.itemPrefix + "Stop"
        iconRole: "media-stop"
        text: qsTr("Stop") + videoMenu.keys("VIDEO_STOP")
        enabled: videoMenu.shell.video.playing
        onTriggered: if (!videoMenu.gesture("VIDEO_STOP")) videoMenu.shell.video.stop()
    }
    ShellMenuItem {
        id: fullScreenItem
        objectName: videoMenu.itemPrefix + "FullScreen"
        iconRole: videoMenu.fullscreen ? "fullscreen-exit" : "fullscreen"
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
            iconRole: "monitor"
            objectName: videoMenu.itemPrefix + "Monitor" + (index + 1)
            text: videoMenu.fullscreen ? qsTr("Switch full screen to monitor %1").arg(index + 2)
                                  : qsTr("Open in full screen on monitor %1").arg(index + 2)
            enabled: videoMenu.shell.video.hasVideo
            onTriggered: videoMenu.shell.videoFullscreen.showOn(index + 1)
        }
        onObjectAdded: (index, object) => videoMenu.insertItem(videoMenu.indexOfItem(fullScreenItem) + 1 + index, object)
        onObjectRemoved: (index, object) => videoMenu.removeItem(object)
    }
    FullscreenBarMenu {
        objectName: videoMenu.itemPrefix + "FullscreenBar"
        iconRole: "show-toolbar"
        title: qsTr("Full screen bar") + (videoMenu.fullscreen ? videoMenu.keys("VIDEO_FULLSCREEN_BAR") : "")
        controller: videoMenu.shell.videoFullscreen
        itemPrefix: videoMenu.itemPrefix + "FullscreenBar"
    }
    ShellMenuItem {
        objectName: videoMenu.itemPrefix + "OpenEditor"
        iconRole: "editor"
        text: qsTr("Open editor")
        enabled: videoMenu.fullscreen
        onTriggered: videoMenu.shell.openEditorFromFullScreen()
    }
    ShellMenu {
        id: recentSubtitlesMenu
        objectName: videoMenu.itemPrefix + "RecentSubtitles"
        iconRole: "recent-subtitles"
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
        iconRole: "recent-video"
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
        iconRole: "open-video"
        text: qsTr("Open video") + (videoMenu.shell.boundKeys("GLOBAL_OPEN_VIDEO", 0).length
                                    ? "\t" + videoMenu.shell.boundKeys("GLOBAL_OPEN_VIDEO", 0) : "")
        onTriggered: videoMenu.openVideoRequested()
    }
    ShellMenuItem {
        objectName: videoMenu.itemPrefix + "OpenSubtitles"
        iconRole: "open-subtitles"
        text: qsTr("Open subtitles") + (videoMenu.shell.boundKeys("GLOBAL_OPEN_SUBS", 0).length
                                        ? "\t" + videoMenu.shell.boundKeys("GLOBAL_OPEN_SUBS", 0) : "")
        onTriggered: videoMenu.openSubtitlesRequested()
    }
    ShellMenuItem {
        iconRole: "progress-bar"
        objectName: videoMenu.itemPrefix + "ProgressBar"
        text: qsTr("Show / hide progress bar") + videoMenu.keys("VIDEO_HIDE_PROGRESS_BAR")
        enabled: videoMenu.fullscreen
        onTriggered: if (!videoMenu.gesture("VIDEO_HIDE_PROGRESS_BAR")) videoMenu.shell.videoView.toggleProgressBar()
    }
    ShellMenuItem {
        iconRole: "render-cost"
        objectName: videoMenu.itemPrefix + "RenderCost"
        text: qsTr("Show / hide render cost") + (videoMenu.shell.boundKeys("GLOBAL_VIDEO_RENDER_COST", 0).length
                                                 ? "\t" + videoMenu.shell.boundKeys("GLOBAL_VIDEO_RENDER_COST", 0) : "")
        enabled: videoMenu.shell.video.hasVideo
        onTriggered: if (!videoMenu.shell.hotkeyGesture("GLOBAL_VIDEO_RENDER_COST", 0)) videoMenu.shell.videoView.toggleRenderCost()
    }
    ShellMenuItem {
        iconRole: "aspect-ratio"
        objectName: videoMenu.itemPrefix + "AspectRatio"
        text: qsTr("Change aspect ratio") + videoMenu.keys("VIDEO_ASPECT_RATIO")
        onTriggered: if (!videoMenu.gesture("VIDEO_ASPECT_RATIO")) videoMenu.aspectRatioRequested()
    }
    ShellMenuItem {
        objectName: videoMenu.itemPrefix + "SaveSubbedFrame"
        iconRole: "frame-snapshot-subtitles"
        text: qsTr("Save frame with subtitles as PNG") + videoMenu.keys("VIDEO_SAVE_SUBBED_FRAME_TO_PNG")
        enabled: videoMenu.shell.videoView.canSnapshot
        onTriggered: if (!videoMenu.gesture("VIDEO_SAVE_SUBBED_FRAME_TO_PNG")) videoMenu.shell.videoView.snapshot("VIDEO_SAVE_SUBBED_FRAME_TO_PNG")
    }
    ShellMenuItem {
        objectName: videoMenu.itemPrefix + "CopySubbedFrame"
        iconRole: "frame-copy-subtitles"
        text: qsTr("Copy frame with subtitles to clipboard") + videoMenu.keys("VIDEO_COPY_SUBBED_FRAME_TO_CLIPBOARD")
        enabled: videoMenu.shell.videoView.canSnapshot
        onTriggered: if (!videoMenu.gesture("VIDEO_COPY_SUBBED_FRAME_TO_CLIPBOARD")) videoMenu.shell.videoView.snapshot("VIDEO_COPY_SUBBED_FRAME_TO_CLIPBOARD")
    }
    ShellMenuItem {
        objectName: videoMenu.itemPrefix + "SaveFrame"
        iconRole: "frame-snapshot"
        text: qsTr("Save frame as PNG") + videoMenu.keys("VIDEO_SAVE_FRAME_TO_PNG")
        enabled: videoMenu.shell.videoView.canSnapshot
        onTriggered: if (!videoMenu.gesture("VIDEO_SAVE_FRAME_TO_PNG")) videoMenu.shell.videoView.snapshot("VIDEO_SAVE_FRAME_TO_PNG")
    }
    ShellMenuItem {
        objectName: videoMenu.itemPrefix + "CopyFrame"
        iconRole: "frame-copy"
        text: qsTr("Copy frame to clipboard") + videoMenu.keys("VIDEO_COPY_FRAME_TO_CLIPBOARD")
        enabled: videoMenu.shell.videoView.canSnapshot
        onTriggered: if (!videoMenu.gesture("VIDEO_COPY_FRAME_TO_CLIPBOARD")) videoMenu.shell.videoView.snapshot("VIDEO_COPY_FRAME_TO_CLIPBOARD")
    }
    MenuSeparator {}
    ShellMenuItem {
        id: unloadItem
        objectName: videoMenu.itemPrefix + "UnloadVideo"
        iconRole: "close-video"
        text: qsTr("Unload video") + videoMenu.keys("VIDEO_DELETE_FILE")
        enabled: videoMenu.shell.video.loaded
        onTriggered: if (!videoMenu.gesture("VIDEO_DELETE_FILE")) videoMenu.shell.video.unloadVideo()
    }
    Instantiator {
        model: videoMenu.filters.length > 0 ? 1 : 0
        delegate: ShellMenu {
            id: filtersMenu
            iconRole: "video-filters"
            objectName: "videoMenuFilters"
            title: qsTr("Filters")
            Instantiator {
                model: videoMenu.filters
                delegate: ShellMenuItem {
                    required property var modelData
                    required property int index
                    objectName: "videoMenuFilter" + index
                    text: modelData.name
                    enabled: modelData.enabled
                    onTriggered: {
                        const app = videoMenu.shell.app
                        const name = modelData.name
                        const at = videoMenu.at
                        const owner = videoMenu.parent
                        Qt.callLater(() => app.showPlayerFilterProperties(name, owner, at.x, at.y))
                    }
                }
                onObjectAdded: (index, object) => filtersMenu.insertItem(index, object)
                onObjectRemoved: (index, object) => filtersMenu.removeItem(object)
            }
        }
        onObjectAdded: (index, object) => videoMenu.insertMenu(videoMenu.indexOfItem(unloadItem) + 1, object)
        onObjectRemoved: (index, object) => videoMenu.removeMenu(object)
    }
    MenuSeparator {
        id: streamsSeparator
        objectName: videoMenu.itemPrefix + "StreamsSeparator"
        visible: videoMenu.streams.length > 0
        height: visible ? implicitHeight : 0
    }
    Instantiator {
        model: videoMenu.streams.length
        delegate: ShellMenuItem {
            required property int index
            readonly property var row: videoMenu.streams[index] ?? ({})
            objectName: videoMenu.itemPrefix + "Stream" + index
            text: row.label ?? ""
            checkable: true
            checked: row.checked === true
            keepsMenuOpen: false
            onTriggered: {
                videoMenu.shell.video.selectStream(index)
                checked = Qt.binding(() => row.checked === true)
            }
        }
        onObjectAdded: (index, object) => videoMenu.insertItem(videoMenu.indexOfItem(streamsSeparator) + 1 + index, object)
        onObjectRemoved: (index, object) => videoMenu.removeItem(object)
    }
    MenuSeparator {
        id: chaptersSeparator
        objectName: videoMenu.itemPrefix + "ChaptersSeparator"
        visible: videoMenu.chapters.length > 0
        height: visible ? implicitHeight : 0
    }
    Instantiator {
        model: videoMenu.chapters.length
        delegate: ShellMenuItem {
            required property int index
            readonly property var row: videoMenu.chapters[index] ?? ({})
            objectName: videoMenu.itemPrefix + "Chapter" + index
            text: (row.label ?? "") + "\t" + (row.time ?? "")
            checkable: true
            checked: row.checked === true
            keepsMenuOpen: false
            onTriggered: {
                videoMenu.shell.video.seekChapter(index)
                checked = Qt.binding(() => row.checked === true)
            }
        }
        onObjectAdded: (index, object) => videoMenu.insertItem(videoMenu.indexOfItem(chaptersSeparator) + 1 + index, object)
        onObjectRemoved: (index, object) => videoMenu.removeItem(object)
    }
}
