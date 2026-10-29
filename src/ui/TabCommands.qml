pragma Translator: "HikariSub.Tabs"
import QtQuick
import Hikari.Ui

Item {
    id: commands
    required property var app
    signal closeAllConfirmed()
    signal subtitlesWithVideo(string subtitles, string video)

    function confirmCloseAll() {
        const count = commands.app.tabs.length
        closeAllPrompt.ask({
            title: qsTr("Close all tabs"),
            // English has no plural catalog, so the singular is its own string.
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
