pragma Translator: "HikariSub.Video"
import QtQuick
import Hikari.Ui

HidableQuestion {
    id: question
    objectName: "videoFileQuestion"
    required property var app
    required property VideoController video
    questionId: "videoFileWalk"
    property var askedFor: 0
    property Connections videoChanges: Connections {
        target: question.video
        function onChanged() {
            if (question.pending.length > 0 && question.video.opening() !== question.askedFor)
                question.withdraw()
        }
    }

    function ask(next, from) {
        const file = question.app.nextVideoFilePath(next)
        askedFor = question.video.opening()
        askWith({
            title: next ? qsTr("Next file") : qsTr("Previous file"),
            text: next ? qsTr("Open the next video in this folder?") : qsTr("Open the previous video in this folder?"),
            detail: question.app.nextVideoFileName(next),
            from: from || null,
            choices: [
                { id: "open", text: qsTr("Open"), role: "accept", isDefault: true },
                { id: "cancel", text: qsTr("Cancel"), role: "reject" }
            ]
        }, id => {
            if (id === "open")
                question.app.nextVideoFileIfStill(next, file)
        })
    }
}
