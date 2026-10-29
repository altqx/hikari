pragma Translator: "HikariSub.Matroska"
import QtQuick
import Hikari.Ui

Item {
    id: flow
    required property var matroska
    // MUST KILL: save returns whether the Save dialog opened; the shell then reports its end through saveDone()
    required property var save
    property SettingsStore settings: null
    property bool waitingForSave: false
    property var writes: []
    property var writeTab: undefined
    required property var notify
    property var documentTitle: () => ""
    property string file: ""
    property var videoName: () => ""
    function sourceName() { return flow.file.length > 0 ? flow.file.replace(/^.*[\\/]/, "") : flow.videoName() }

    function begin() {
        if (!flow.matroska.available || flow.matroska.state !== 0)
            return
        flow.file = ""
        flow.ask()
    }
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
    function saveDone() {
        if (!flow.waitingForSave)
            return
        flow.waitingForSave = false
        flow.matroska.start()
    }
    function saveCancelled() {
        flow.writes = []
        flow.writeTab = undefined
        if (!flow.waitingForSave)
            return
        flow.waitingForSave = false
        flow.matroska.cancel()
    }
    function awaitWrite(ids, tab) {
        if (!flow.waitingForSave)
            return
        flow.writes = ids.slice()
        flow.writeTab = tab
    }
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
        function onNoTracks() {
            flow.notify("info", qsTr("Load subtitles from an MKV/OGM file"),
                        qsTr("The file does not contain any subtitle tracks."))
        }
        function onChooseTrack(labels) { trackChooser.ask(labels) }
    }
}
