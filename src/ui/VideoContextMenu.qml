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

    // MUST KILL: hotkeyGesture takes a magic context int (3 Video window, 0 Global)
    function gesture(symbol) {
        return videoMenu.shell.hotkeyGesture(symbol, 3)
    }
    function indexIn(menu, item) {
        for (let i = 0; i < menu.count; ++i)
            if (menu.itemAt(i) === item)
                return i
        return menu.count
    }

    component FullscreenItem: ShellMenuItem {
        visible: videoMenu.fullscreen
        height: visible ? implicitHeight : 0
    }
    component FullscreenSeparator: MenuSeparator {
        visible: videoMenu.fullscreen
        height: visible ? implicitHeight : 0
    }
    component SnapshotItem: ShellMenuItem {
        enabled: videoMenu.shell.videoView.canSnapshot
        onTriggered: if (!videoMenu.gesture(command)) videoMenu.shell.videoView.snapshot(command)
    }

    ShellMenuItem {
        iconRole: "copy-position"
        objectName: videoMenu.itemPrefix + "CopyCoords"
        visible: !videoMenu.fullscreen
        height: visible ? implicitHeight : 0
        command: "VIDEO_COPY_COORDS"
        text: qsTr("Copy video position")
        onTriggered: if (!videoMenu.gesture("VIDEO_COPY_COORDS")) videoMenu.shell.visualTools.copyCoordinates(videoMenu.at.x, videoMenu.at.y)
    }
    ShellMenuItem {
        objectName: videoMenu.itemPrefix + "PlayPause"
        iconRole: videoMenu.shell.video.playing ? "media-pause" : "media-play"
        command: "VIDEO_PLAY_PAUSE"
        text: (videoMenu.shell.video.playing ? qsTr("Pause") : qsTr("Play"))
        enabled: videoMenu.shell.video.hasVideo
        onTriggered: if (!videoMenu.gesture("VIDEO_PLAY_PAUSE")) videoMenu.shell.video.togglePlay()
    }
    ShellMenuItem {
        objectName: videoMenu.itemPrefix + "Stop"
        iconRole: "media-stop"
        command: "VIDEO_STOP"
        text: qsTr("Stop")
        enabled: videoMenu.shell.video.playing
        onTriggered: if (!videoMenu.gesture("VIDEO_STOP")) videoMenu.shell.video.stop()
    }
    ShellMenuItem {
        iconRole: "fullscreen"
        objectName: videoMenu.itemPrefix + "FullScreen"
        text: qsTr("Full screen")
        command: "VIDEO_FULL_SCREEN"
        shortcutText: MenuKeys.text(command, commandWindow) || ShellMenus.keyText(videoMenu.fullscreen ? "Esc" : "F")
        checkable: true
        checked: videoMenu.fullscreen
        keepsMenuOpen: false
        enabled: videoMenu.shell.video.hasVideo
        onTriggered: {
            checked = Qt.binding(() => videoMenu.fullscreen)
            if (!videoMenu.gesture("VIDEO_FULL_SCREEN"))
                videoMenu.shell.videoFullscreen.toggle(0)
        }
    }
    ShellMenu {
        id: monitorsMenu
        objectName: videoMenu.itemPrefix + "FullScreenOn"
        iconRole: "monitor"
        title: qsTr("Full screen on")
        shown: videoMenu.monitors > 1
        enabled: videoMenu.shell.video.hasVideo
        Instantiator {
            model: Math.max(0, videoMenu.monitors - 1)
            delegate: ShellMenuItem {
                required property int index
                iconRole: "monitor"
                objectName: videoMenu.itemPrefix + "Monitor" + (index + 1)
                //: A monitor in the Full screen on submenu; %1 is its number
                text: qsTr("Monitor %1").arg(index + 2)
                onTriggered: videoMenu.shell.videoFullscreen.showOn(index + 1)
            }
            onObjectAdded: (index, object) => monitorsMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => monitorsMenu.removeItem(object)
        }
    }
    FullscreenItem {
        objectName: videoMenu.itemPrefix + "OpenEditor"
        iconRole: "editor"
        text: qsTr("Open editor")
        onTriggered: videoMenu.shell.openEditorFromFullScreen()
    }
    FullscreenSeparator {}
    FullscreenItem {
        objectName: videoMenu.itemPrefix + "OpenVideo"
        iconRole: "open-video"
        text: qsTr("Open video…")
        command: "GLOBAL_OPEN_VIDEO"
        onTriggered: videoMenu.openVideoRequested()
    }
    FullscreenItem {
        objectName: videoMenu.itemPrefix + "OpenSubtitles"
        iconRole: "open-subtitles"
        text: qsTr("Open subtitles…")
        command: "GLOBAL_OPEN_SUBS"
        onTriggered: videoMenu.openSubtitlesRequested()
    }
    ShellMenu {
        id: recentMenu
        objectName: videoMenu.itemPrefix + "Recent"
        iconRole: "recent-subtitles"
        title: qsTr("Recently opened")
        shown: videoMenu.fullscreen
        MenuHeading {
            id: recentSubtitlesHeading
            text: qsTr("Subtitles")
        }
        MenuSeparator {}
        MenuHeading {
            id: recentVideosHeading
            text: qsTr("Videos")
        }
        Instantiator {
            model: videoMenu.recent.subtitles
            delegate: ShellMenuItem {
                required property var modelData
                required property int index
                objectName: videoMenu.itemPrefix + "RecentSubtitles" + index
                iconRole: "recent-subtitles"
                text: modelData.label
                onTriggered: videoMenu.shell.openSubtitles(modelData.path)
            }
            onObjectAdded: (index, object) => recentMenu.insertItem(videoMenu.indexIn(recentMenu, recentSubtitlesHeading) + 1 + index, object)
            onObjectRemoved: (index, object) => recentMenu.removeItem(object)
        }
        Instantiator {
            model: videoMenu.recent.videos
            delegate: ShellMenuItem {
                required property var modelData
                required property int index
                objectName: videoMenu.itemPrefix + "RecentVideos" + index
                iconRole: "recent-video"
                text: modelData.label
                onTriggered: videoMenu.shell.video.openVideo(modelData.path)
            }
            onObjectAdded: (index, object) => recentMenu.insertItem(videoMenu.indexIn(recentMenu, recentVideosHeading) + 1 + index, object)
            onObjectRemoved: (index, object) => recentMenu.removeItem(object)
        }
    }
    MenuSeparator {}
    ShellMenu {
        objectName: videoMenu.itemPrefix + "ExportFrame"
        iconRole: "frame-snapshot"
        title: qsTr("Export frame")
        SnapshotItem {
            objectName: videoMenu.itemPrefix + "SaveFrame"
            iconRole: "frame-snapshot"
            command: "VIDEO_SAVE_FRAME_TO_PNG"
            text: qsTr("Save as PNG…")
        }
        SnapshotItem {
            objectName: videoMenu.itemPrefix + "CopyFrame"
            iconRole: "frame-copy"
            command: "VIDEO_COPY_FRAME_TO_CLIPBOARD"
            text: qsTr("Copy")
        }
        MenuSeparator {}
        SnapshotItem {
            objectName: videoMenu.itemPrefix + "SaveSubbedFrame"
            iconRole: "frame-snapshot-subtitles"
            command: "VIDEO_SAVE_SUBBED_FRAME_TO_PNG"
            text: qsTr("Save with subtitles as PNG…")
        }
        SnapshotItem {
            objectName: videoMenu.itemPrefix + "CopySubbedFrame"
            iconRole: "frame-copy-subtitles"
            command: "VIDEO_COPY_SUBBED_FRAME_TO_CLIPBOARD"
            text: qsTr("Copy with subtitles")
        }
    }
    ShellMenu {
        id: streamsMenu
        objectName: videoMenu.itemPrefix + "AudioTrack"
        iconRole: "video-streams"
        title: qsTr("Audio track")
        shown: videoMenu.streams.length > 0
        Instantiator {
            model: videoMenu.streams.length
            delegate: MenuRadioItem {
                required property int index
                readonly property var row: videoMenu.streams[index] ?? ({})
                objectName: videoMenu.itemPrefix + "Stream" + index
                text: row.label ?? ""
                checked: row.checked === true
                onTriggered: {
                    videoMenu.shell.video.selectStream(index)
                    checked = Qt.binding(() => row.checked === true)
                }
            }
            onObjectAdded: (index, object) => streamsMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => streamsMenu.removeItem(object)
        }
    }
    ShellMenu {
        id: chaptersMenu
        objectName: videoMenu.itemPrefix + "Chapters"
        iconRole: "chapters"
        title: qsTr("Chapters")
        shown: videoMenu.chapters.length > 0
        Instantiator {
            model: videoMenu.chapters.length
            delegate: MenuRadioItem {
                required property int index
                readonly property var row: videoMenu.chapters[index] ?? ({})
                objectName: videoMenu.itemPrefix + "Chapter" + index
                text: row.label ?? ""
                detailText: row.time ?? ""
                checked: row.checked === true
                onTriggered: {
                    videoMenu.shell.video.seekChapter(index)
                    checked = Qt.binding(() => row.checked === true)
                }
            }
            onObjectAdded: (index, object) => chaptersMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => chaptersMenu.removeItem(object)
        }
    }
    MenuSeparator {}
    FullscreenItem {
        iconRole: "progress-bar"
        objectName: videoMenu.itemPrefix + "ProgressBar"
        command: "VIDEO_HIDE_PROGRESS_BAR"
        text: qsTr("Progress bar")
        checkable: true
        checked: videoMenu.shell.videoView.progressBar
        onTriggered: {
            checked = Qt.binding(() => videoMenu.shell.videoView.progressBar)
            if (!videoMenu.gesture("VIDEO_HIDE_PROGRESS_BAR"))
                videoMenu.shell.videoView.toggleProgressBar()
        }
    }
    ShellMenuItem {
        iconRole: "render-cost"
        objectName: videoMenu.itemPrefix + "RenderCost"
        text: qsTr("Render cost")
        command: "GLOBAL_VIDEO_RENDER_COST"
        checkable: true
        checked: videoMenu.shell.videoView.renderCost
        enabled: videoMenu.shell.video.hasVideo
        onTriggered: {
            checked = Qt.binding(() => videoMenu.shell.videoView.renderCost)
            if (!videoMenu.shell.hotkeyGesture("GLOBAL_VIDEO_RENDER_COST", 0))
                videoMenu.shell.videoView.toggleRenderCost()
        }
    }
    FullscreenBarMenu {
        objectName: videoMenu.itemPrefix + "FullscreenBar"
        iconRole: "show-toolbar"
        title: qsTr("Full screen bar")
        shown: videoMenu.fullscreen
        shortcutText: videoMenu.fullscreen ? MenuKeys.text("VIDEO_FULLSCREEN_BAR", 3) : ""
        controller: videoMenu.shell.videoFullscreen
        itemPrefix: videoMenu.itemPrefix + "FullscreenBar"
    }
    ShellMenuItem {
        iconRole: "aspect-ratio"
        objectName: videoMenu.itemPrefix + "AspectRatio"
        command: "VIDEO_ASPECT_RATIO"
        text: qsTr("Change aspect ratio")
        onTriggered: if (!videoMenu.gesture("VIDEO_ASPECT_RATIO")) videoMenu.aspectRatioRequested()
    }
    ShellMenuItem {
        id: resetZoomItem
        iconRole: "zoom-reset"
        objectName: videoMenu.itemPrefix + "ResetZoom"
        command: "GLOBAL_RESET_VIDEO_ZOOM"
        text: qsTr("Turn off video zoom")
        enabled: videoMenu.shell.videoView.zoomed
        onTriggered: if (!videoMenu.shell.hotkeyGesture("GLOBAL_RESET_VIDEO_ZOOM", 0)) videoMenu.shell.videoView.resetZoom()
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
        onObjectAdded: (index, object) => videoMenu.insertMenu(videoMenu.indexIn(videoMenu, resetZoomItem) + 1, object)
        onObjectRemoved: (index, object) => videoMenu.removeMenu(object)
    }
    MenuSeparator {}
    ShellMenuItem {
        objectName: videoMenu.itemPrefix + "UnloadVideo"
        iconRole: "close-video"
        command: "VIDEO_DELETE_FILE"
        text: qsTr("Unload video")
        enabled: videoMenu.shell.video.loaded
        onTriggered: if (!videoMenu.gesture("VIDEO_DELETE_FILE")) videoMenu.shell.video.unloadVideo()
    }
}
