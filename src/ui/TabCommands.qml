import QtQuick
import QtQuick.Controls

// P9: two legacy questions around tabs and opening, as in-window dialogs:
//  - "Close all tabs" asks "All tabs will be closed, continue?" first, No
//    being the default (Notebook::OnTabSel, Notebook.cpp:978-980); Yes goes
//    on to the close review of every tab (closeAllConfirmed).
//  - Opening a video looks for subtitles named as the video beside it and
//    asks "Load subtitles named "<name>"?" (HikariSubFrame::OpenFile,
//    HikariSubFrame.cpp:1342-1355): Yes loads them into the tab, then the
//    video (subtitlesWithVideo); No opens only the video.
Item {
    id: commands
    required property var app
    signal closeAllConfirmed()
    signal subtitlesWithVideo(string subtitles, string video)

    function confirmCloseAll() {
        closeAllPrompt.open()
    }
    function openVideoFile(path) {
        const found = app.openVideoFile(path)
        if (found.subtitles.length > 0) {
            subtitlesPrompt.subtitles = found.subtitles
            subtitlesPrompt.video = path
            subtitlesPrompt.open()
        } else {
            app.openVideo(path)
        }
    }

    Dialog {
        id: closeAllPrompt
        objectName: "closeAllPrompt"
        parent: Overlay.overlay
        anchors.centerIn: parent
        title: qsTr("Prompt")
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        Label { text: qsTr("All tabs will be closed, continue?") }
        // wxNO is the default button.
        onOpened: standardButton(Dialog.No).forceActiveFocus()
        onAccepted: commands.closeAllConfirmed()
    }
    Dialog {
        id: subtitlesPrompt
        objectName: "subtitlesFromVideoPrompt"
        parent: Overlay.overlay
        anchors.centerIn: parent
        title: qsTr("Confirmation")
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        property string subtitles
        property string video
        Label {
            objectName: "subtitlesFromVideoText"
            text: qsTr("Load subtitles named \"%1\"?").arg(subtitlesPrompt.subtitles.replace(/^.*[\\/]/, ""))
        }
        onAccepted: commands.subtitlesWithVideo(subtitles, video)
        onRejected: commands.app.openVideo(video)
    }
}
