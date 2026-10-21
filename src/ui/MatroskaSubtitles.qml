// Y9: GRID_SUBS_FROM_MKV, "Load subtitles from an MKV/OGM file" (legacy
// SubsGrid::OnMkvSubs and Demux::GetSubtitles at 20d647c4): the question for
// a modified Document, "The file does not contain any subtitle tracks.", the
// "Choose subtitle track" list (HikariListBox: the first row preselected, OK
// or a double click takes a row, Cancel reads nothing) and the "Loading
// subtitles from Matroska." progress (ProgresDialog: the gauge, "Time
// elapsed", Cancel). The loaded Document replaces the tab's when it is
// complete (MatroskaController).
pragma Translator: "HikariSub.Matroska"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Item {
    id: flow
    required property var matroska
    // The shell's Save (HikariSubFrame::Save(false)); returns true when it
    // opened the Save dialog, whose end the shell reports with saveDone().
    required property var save
    property SettingsStore settings: null
    property bool waitingForSave: false

    // SubsGrid::OnMkvSubs (SubsGrid.cpp:1134-1146).
    function begin() {
        if (!flow.matroska.available || flow.matroska.state !== 0)
            return
        flow.ask()
    }
    // File > Open on an MKV file (HikariSubFrame.cpp:1021-1028): OnMkvSubs
    // on that file, in a new tab when the tab has subtitles of a file.
    function beginFile(path) {
        if (!flow.matroska.beginFile(path))
            return
        flow.ask()
    }
    function ask() {
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
        property bool answered: false
        // Cancel, Escape or a press outside: File > Open's file is not read.
        onClosed: {
            if (!saveQuestion.answered)
                flow.matroska.cancel()
            saveQuestion.answered = false
        }
        ColumnLayout {
            Label { text: qsTr("Save the file before loading subtitles from the MKV?") }
            WindowFooter {
                Button {
                    objectName: "mkvSaveYes"
                    text: qsTr("Yes")
                    DialogButtonBox.buttonRole: DialogButtonBox.YesRole
                    onClicked: {
                        saveQuestion.answered = true
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
                    DialogButtonBox.buttonRole: DialogButtonBox.NoRole
                    onClicked: {
                        saveQuestion.answered = true
                        saveQuestion.close()
                        flow.matroska.start()
                    }
                }
                Button {
                    objectName: "mkvSaveCancel"
                    text: qsTr("Cancel")
                    DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                    onClicked: saveQuestion.close()
                }
            }
        }
    }

    Dialog {
        id: noTracksMessage
        objectName: "mkvNoTracksMessage"
        title: qsTr("Load subtitles from an MKV/OGM file") // TI1-popup-titles
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        standardButtons: Dialog.Ok
        Label { text: qsTr("The file does not contain any subtitle tracks.") }
    }

    // Legacy's "Choose subtitle track" list, on PK1's list chooser in its
    // compact window: the first row chosen, Open or a double click takes it.
    ListChooser {
        id: trackChooser
        objectName: "mkvTrackChooser"
        settings: flow.settings
        function ask(labels) {
            choose({
                title: qsTr("Choose the subtitle track"),
                rows: labels,
                acceptText: qsTr("Open")
            }, row => {
                if (row >= 0)
                    flow.matroska.choose(row)
                else
                    flow.matroska.cancel()
            })
        }
    }

    Dialog {
        id: progressDialog
        objectName: "mkvProgress"
        title: qsTr("Load subtitles from an MKV/OGM file")
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
        function onChooseTrack(labels) { trackChooser.ask(labels) }
    }
}
