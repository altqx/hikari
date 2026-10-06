// Y9: GRID_SUBS_FROM_MKV, "Load subtitles from an MKV/OGM file" (legacy
// SubsGrid::OnMkvSubs and Demux::GetSubtitles at 20d647c4): the question for
// a modified Document, "The file does not contain any subtitle tracks.", the
// "Choose subtitle track" list (HikariListBox: the first row preselected, OK
// or a double click takes a row, Cancel reads nothing) and the "Loading
// subtitles from Matroska." progress (ProgresDialog: the gauge, "Time
// elapsed", Cancel). The loaded Document replaces the tab's when it is
// complete (MatroskaController).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: flow
    required property var matroska
    // The shell's Save (HikariSubFrame::Save(false)); returns true when it
    // opened the Save dialog, whose end the shell reports with saveDone().
    required property var save
    property bool waitingForSave: false

    // SubsGrid::OnMkvSubs (SubsGrid.cpp:1134-1146).
    function begin() {
        if (!flow.matroska.available || flow.matroska.state !== 0)
            return
        if (flow.matroska.needsSaveQuestion())
            saveQuestion.open()
        else
            flow.matroska.start()
    }
    // The Save dialog closed (saved or not): legacy went on either way.
    function saveDone() {
        if (!flow.waitingForSave)
            return
        flow.waitingForSave = false
        flow.matroska.start()
    }

    Dialog {
        id: saveQuestion
        objectName: "mkvSaveQuestion"
        title: qsTr("Confirmation")
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        // HikariMessageBox(wxYES_NO | wxCANCEL): Yes saves (Hikari->Save(false))
        // then loads, No loads, Cancel does nothing.
        ColumnLayout {
            Label { text: qsTr("Save the file before loading subtitles from the MKV?") }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                Button {
                    objectName: "mkvSaveYes"
                    text: qsTr("Yes")
                    onClicked: {
                        saveQuestion.close()
                        if (flow.save())
                            flow.waitingForSave = true
                        else
                            flow.matroska.start()
                    }
                }
                Button {
                    objectName: "mkvSaveNo"
                    text: qsTr("No")
                    onClicked: {
                        saveQuestion.close()
                        flow.matroska.start()
                    }
                }
                Button {
                    objectName: "mkvSaveCancel"
                    text: qsTr("Cancel")
                    onClicked: saveQuestion.close()
                }
            }
        }
    }

    Dialog {
        id: noTracksMessage
        objectName: "mkvNoTracksMessage"
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        standardButtons: Dialog.Ok
        Label { text: qsTr("The file does not contain any subtitle tracks.") }
    }

    Dialog {
        id: trackChooser
        objectName: "mkvTrackChooser"
        title: qsTr("Choose subtitle track")
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        closePolicy: Popup.CloseOnEscape
        property var labels: []
        ListView {
            id: trackList
            objectName: "mkvTrackList"
            implicitWidth: 220
            implicitHeight: 160
            clip: true
            model: trackChooser.labels
            currentIndex: 0
            delegate: ItemDelegate {
                required property string modelData
                required property int index
                width: ListView.view.width
                text: modelData
                highlighted: ListView.isCurrentItem
                onClicked: trackList.currentIndex = index
                onDoubleClicked: trackChooser.accept()
            }
        }
        onAccepted: flow.matroska.choose(Math.max(0, trackList.currentIndex))
        onRejected: flow.matroska.cancel()
    }

    Dialog {
        id: progressDialog
        objectName: "mkvProgress"
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        closePolicy: Popup.NoAutoClose
        visible: flow.matroska.state === 3
        ColumnLayout {
            Label {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("Loading subtitles from Matroska.")
            }
            ProgressBar {
                objectName: "mkvProgressBar"
                Layout.preferredWidth: 300
                from: 0
                to: 100
                value: flow.matroska.progress
            }
            Label {
                objectName: "mkvElapsed"
                Layout.alignment: Qt.AlignHCenter
                text: flow.matroska.elapsed
            }
            Button {
                objectName: "mkvCancel"
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("Cancel")
                onClicked: flow.matroska.cancel()
            }
        }
    }

    Connections {
        target: flow.matroska
        function onNoTracks() { noTracksMessage.open() }
        function onChooseTrack(labels) {
            trackChooser.labels = labels
            trackList.currentIndex = 0
            trackChooser.open()
        }
    }
}
