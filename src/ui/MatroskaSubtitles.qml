// Y9: GRID_SUBS_FROM_MKV, "Load subtitles from an MKV/OGM file" (legacy
// SubsGrid::OnMkvSubs and Demux::GetSubtitles at 20d647c4): the question for
// a modified Document (a message box), "The file does not contain any
// subtitle tracks." (a toast) and the "Choose subtitle track" list
// (HikariListBox: the first row preselected, OK or a double click takes a
// row, Cancel reads nothing). The "Loading subtitles from Matroska." progress
// (ProgresDialog: the gauge, "Time elapsed", Cancel) is the shell's bar
// (PU1-mkv-notices). The loaded Document replaces the tab's when it is
// complete (MatroskaController), or opens in a new tab when that tab changed
// while it was read.
pragma Translator: "HikariSub.Matroska"
import QtQuick
import Hikari.Ui

Item {
    id: flow
    required property var matroska
    // The shell's Save (HikariSubFrame::Save(false)); returns true when it
    // opened the Save dialog, whose end the shell reports with saveDone().
    required property var save
    property SettingsStore settings: null
    property bool waitingForSave: false
    // PU1-disk-change-bar: the Documents whose write the load waits for,
    // and the tab that asked.
    property var writes: []
    property var writeTab: undefined
    // The shell's OK-only notice: notify(severity, title, text).
    required property var notify
    // The editing target's tab name, for the question.
    property var documentTitle: () => ""
    // The MKV file the next start() reads: beginFile's, else the tab's video.
    property string file: ""
    property var videoName: () => ""
    function sourceName() { return flow.file.length > 0 ? flow.file.replace(/^.*[\\/]/, "") : flow.videoName() }

    // SubsGrid::OnMkvSubs (SubsGrid.cpp:1134-1146).
    function begin() {
        if (!flow.matroska.available || flow.matroska.state !== 0)
            return
        flow.file = ""
        flow.ask()
    }
    // File > Open on an MKV file (HikariSubFrame.cpp:1021-1028): OnMkvSubs
    // on that file, in a new tab when the tab has subtitles of a file.
    function beginFile(path) {
        if (!flow.matroska.beginFile(path))
            return
        flow.file = path
        flow.ask()
    }
    function ask() {
        if (flow.matroska.needsSaveQuestion())
            saveQuestion.askFor()
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
    // PU1-disk-change-bar: the save over a file changed on disk was not
    // made: Save and load loads nothing over the unsaved work.
    function saveCancelled() {
        flow.writes = []
        flow.writeTab = undefined
        if (!flow.waitingForSave)
            return
        flow.waitingForSave = false
        flow.matroska.cancel()
    }
    // Save over it was chosen: the load goes on once the write is done
    // (written), and is cancelled if it fails, so the work it was to save
    // is not replaced.
    function awaitWrite(ids, tab) {
        if (!flow.waitingForSave)
            return
        flow.writes = ids.slice()
        flow.writeTab = tab
    }
    // `current`: the editing target's tab row. The load goes on in the tab
    // that asked, with its work written (another tab chosen meanwhile, or
    // work an earlier write didn't cover, cancels it).
    function written(id, current) {
        if (flow.writes.indexOf(id) < 0)
            return
        flow.writes = flow.writes.filter(w => w !== id)
        if (flow.writes.length > 0)
            return
        if (current.id === flow.writeTab && !current.modified) {
            flow.writeTab = undefined
            flow.saveDone()
        } else {
            flow.saveCancelled()
        }
    }
    function writeFailed(id) {
        if (flow.writes.indexOf(id) >= 0)
            flow.saveCancelled()
    }

    // PU1-mkv-notices: OnMkvSubs' question (HikariMessageBox(wxYES_NO |
    // wxCANCEL) under "Confirmation") on the kit's message box: Save and load
    // saves (Hikari->Save(false)) then loads, Load without saving loads,
    // Cancel (Esc, Close) does nothing and File > Open's file is not read.
    LazyMessageBox {
        id: saveQuestion
        name: "mkvSaveQuestion"
        settings: flow.settings
        function askFor() {
            ask({
                title: qsTr("Load subtitles from MKV"),
                text: qsTr("Save the changes to “%1” first?").arg(flow.documentTitle()),
                detail: qsTr("The subtitles from %1 replace this tab’s.").arg(flow.sourceName()),
                severity: "warning",
                choices: [
                    { id: "save", text: qsTr("Save and load"), role: "accept", isDefault: true },
                    { id: "load", text: qsTr("Load without saving"), role: "destructive" },
                    { id: "cancel", text: qsTr("Cancel"), role: "reject" }
                ]
            }, id => {
                if (id === "save") {
                    if (flow.save())
                        flow.waitingForSave = true
                    else
                        flow.matroska.start()
                } else if (id === "load") {
                    flow.matroska.start()
                } else {
                    flow.matroska.cancel()
                }
            })
        }
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

    Connections {
        target: flow.matroska
        // PU1-mkv-notices, NT1: an info toast.
        function onNoTracks() {
            flow.notify("info", qsTr("Load subtitles from an MKV/OGM file"),
                        qsTr("The file does not contain any subtitle tracks."))
        }
        function onChooseTrack(labels) { trackChooser.ask(labels) }
    }
}
