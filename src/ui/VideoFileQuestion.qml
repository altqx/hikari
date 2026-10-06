import QtQuick
import QtQuick.Controls
import Hikari.Ui

// V3: VideoBox::OnPrew / OnNext's question (VideoBox.cpp:889-910) before
// the previous or next file of the folder is indexed: legacy asked while
// "Open video with FFMS2" was on, and the rewrite always indexes
// (V3-indexing-retired). Yes walks the folder (App.nextVideoFile).
Dialog {
    id: dialog
    objectName: "videoFileQuestion"
    required property var app
    property bool next: true
    title: qsTr("Confirmation")
    modal: true
    standardButtons: Dialog.Yes | Dialog.No
    function ask(forward) {
        next = forward
        open()
    }
    Label {
        objectName: "videoFileQuestionText"
        text: dialog.next ? qsTr("Are you sure you want to index the next video?")
                          : qsTr("Are you sure you want to index the previous video?")
    }
    onAccepted: dialog.app.nextVideoFile(dialog.next)
}
