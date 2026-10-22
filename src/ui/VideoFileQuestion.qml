pragma Translator: "HikariSub.Video"
import QtQuick
import Hikari.Ui

// V3: VideoBox::OnPrew / OnNext's question (VideoBox.cpp:889-910) before
// the previous or next file of the folder is indexed: legacy asked while
// "Open video with FFMS2" was on, and the rewrite always indexes
// (V3-indexing-retired). Open walks the folder (App.nextVideoFile).
// PU4 (#306, PU4-file-question): the message box (NT1's MessageHost) titled
// with the command, naming the file it opens (none named when the walk
// would open none), with Open and Cancel for legacy's Yes and No, and
// "Don't ask again".
HidableQuestion {
    id: question
    objectName: "videoFileQuestion"
    required property var app
    questionId: "videoFileWalk"

    // `from`: the window it opens over (the fullscreen video window's
    // buttons), else the one with the keyboard.
    function ask(next, from) {
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
                question.app.nextVideoFile(next)
        })
    }
}
