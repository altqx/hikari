pragma Translator: "HikariSub.Tabs"
import QtQuick
import Hikari.Ui

// P9: two legacy questions around tabs and opening, on the kit's message box
// (PU1-message-boxes):
//  - "Close all tabs" asks first, Cancel being the default (legacy "All tabs
//    will be closed, continue?" with No the default, Notebook::OnTabSel,
//    Notebook.cpp:978-980); Close all tabs goes on to the close review of
//    every tab (closeAllConfirmed).
//  - Opening a video looks for subtitles named as the video beside it and
//    asks (legacy "Load subtitles named "<name>"?", HikariSubFrame::OpenFile,
//    HikariSubFrame.cpp:1342-1355): Open both loads them into the tab, then
//    the video (subtitlesWithVideo); Video only, Esc or Close opens only the
//    video.
Item {
    id: commands
    required property var app
    signal closeAllConfirmed()
    signal subtitlesWithVideo(string subtitles, string video)

    function confirmCloseAll() {
        const count = commands.app.tabs.length
        closeAllPrompt.ask({
            title: qsTr("Close all tabs"),
            // English has no plural catalog: one says "the tab".
            text: count === 1 ? qsTr("Close the tab?") : qsTr("Close all %n tabs?", "", count),
            detail: qsTr("You are asked about unsaved changes next."),
            choices: [
                { id: "close", text: qsTr("Close all tabs"), role: "accept" },
                { id: "cancel", text: qsTr("Cancel"), role: "reject", isDefault: true }
            ]
        }, id => {
            if (id === "close")
                commands.closeAllConfirmed()
        })
    }
    function openVideoFile(path) {
        const found = app.openVideoFile(path)
        if (found.subtitles.length > 0) {
            const name = s => s.replace(/^.*[\\/]/, "")
            subtitlesPrompt.ask({
                title: qsTr("Open video"),
                text: qsTr("Open “%1” with the video?").arg(name(found.subtitles)),
                detail: qsTr("It is next to %1 and has the same name.").arg(name(path)),
                choices: [
                    { id: "both", text: qsTr("Open both"), role: "accept", isDefault: true },
                    { id: "video", text: qsTr("Video only"), role: "reject" }
                ]
            }, id => {
                if (id === "both")
                    commands.subtitlesWithVideo(found.subtitles, path)
                else
                    commands.app.openVideo(path)
            })
        } else {
            app.openVideo(path)
        }
    }

    LazyMessageBox {
        id: closeAllPrompt
        name: "closeAllPrompt"
        settings: commands.app.settings
    }
    LazyMessageBox {
        id: subtitlesPrompt
        name: "subtitlesFromVideoPrompt"
        settings: commands.app.settings
    }
}
