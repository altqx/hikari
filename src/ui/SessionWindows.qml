pragma Translator: "HikariSub.Session"
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Hikari.Ui

Item {
    id: windows
    required property var app
    required property var review
    property var pickerFolder: picker => ""
    property var rememberPickerFolder: (picker, file) => {}

    // MUST KILL: load() with no file means the last session (an empty path)
    function load(file) {
        const chosen = file === undefined ? "" : file
        windows.go(windows.app.reviewSession(chosen), chosen)
    }
    function restoreStart() { windows.go(windows.app.reviewStartupSession()) }
    function go(result, file) {
        if (result.network && result.network.length > 0) {
            windows.askNetwork(file, result.network)
            return
        }
        if (!result.ok)
            return
        if (result.rows.length === 0)
            windows.app.finishClose()
        else
            windows.review(result.rows)
    }
    function chooseSessionToLoad() { windows.openPicker(loadDialog) }
    function chooseSessionToSave() { windows.openPicker(saveDialog) }
    function openPicker(dialog) {
        const folder = windows.pickerFolder("sessions")
        dialog.currentFolder = folder.length > 0 ? folder : windows.app.sessionFolder()
        dialog.open()
    }

    readonly property StartupOffer offer: StartupOffer {
        app: windows.app
        onRestoreRequested: windows.restoreStart()
    }
    Component.onCompleted: {
        const kind = windows.app.startupSession()
        if (kind === "load")
            Qt.callLater(() => {
                windows.load()
                windows.app.startupSessionDecided()
            })
        else
            windows.app.startupSessionDecided()
        windows.offer.begin(kind === "load")
    }

    FileDialog {
        id: loadDialog
        objectName: "sessionLoadDialog"
        title: qsTr("Load session")
        nameFilters: [qsTr("Sessions (*.hksession *.kls)"), qsTr("All files (*)")]
        fileMode: FileDialog.OpenFile
        onAccepted: {
            windows.rememberPickerFolder("sessions", selectedFile)
            windows.load(selectedFile)
        }
    }
    FileDialog {
        id: saveDialog
        objectName: "sessionSaveDialog"
        title: qsTr("Save session")
        nameFilters: [qsTr("Sessions (*.hksession)")]
        defaultSuffix: "hksession"
        fileMode: FileDialog.SaveFile
        onAccepted: {
            windows.rememberPickerFolder("sessions", selectedFile)
            windows.save(selectedFile, false)
        }
    }
    function save(file, replace) {
        const result = windows.app.saveSessionTo(file, replace)
        if (result === "readonly")
            windows.warnReadOnly(file)
        else if (result === "exists")
            windows.askReplace(file)
    }
    function fileName(file) {
        return windows.app.localPath(file).replace(/^.*[\\/]/, "")
    }
    function askReplace(file) {
        const name = windows.fileName(file).replace(/\.kls$/i, ".hksession")
        readOnlyMessages.ask({
            title: qsTr("Save session"),
            text: qsTr("“%1” already exists.").arg(name),
            detail: qsTr("Sessions are saved as .hksession files. Replace the one there?"),
            severity: "warning",
            choices: [
                { id: "replace", text: qsTr("Replace"), role: "accept" },
                { id: "cancel", text: qsTr("Cancel"), role: "reject", isDefault: true }
            ]
        }, id => {
            if (id === "replace")
                windows.save(file, true)
        })
    }
    function askNetwork(file, hosts) {
        readOnlyMessages.ask({
            title: qsTr("Load session"),
            text: qsTr("This session opens files on another computer."),
            detail: qsTr("“%1” names files on %2. Opening it connects to them.")
                .arg(windows.fileName(file)).arg(hosts.join(", ")),
            severity: "warning",
            choices: [
                { id: "open", text: qsTr("Open"), role: "accept" },
                { id: "cancel", text: qsTr("Cancel"), role: "reject", isDefault: true }
            ]
        }, id => {
            if (id === "open")
                windows.go(windows.app.reviewSession(file, hosts), file)
        })
    }
    function warnReadOnly(file) {
        readOnlyMessages.ask({
            title: qsTr("Save session"),
            text: qsTr("“%1” is read-only.").arg(windows.fileName(file)),
            detail: qsTr("Save the session under another name, or make the file writable and save again."),
            severity: "warning",
            choices: [
                { id: "saveAs", text: qsTr("Save as…"), role: "accept", isDefault: true },
                { id: "cancel", text: qsTr("Cancel"), role: "reject" }
            ]
        }, id => {
            if (id === "saveAs")
                windows.openPicker(saveDialog)
        })
    }
    MessageHost {
        id: readOnlyMessages
        objectName: "sessionMessages"
        settings: windows.app.settings
    }

    function kindName(kind) {
        switch (kind) {
        case "subtitles": return qsTr("subtitles")
        case "video": return qsTr("video")
        case "audio": return qsTr("audio")
        case "keyframes": return qsTr("keyframes")
        }
        return kind
    }
    function entryText(entry) {
        const name = entry.path.replace(/^.*[\\/]/, "")
        switch (entry.kind) {
        case "subtitles": return qsTr("%1, this tab’s subtitles, was not found.").arg(name)
        case "video": return qsTr("%1, this tab’s video, was not found.").arg(name)
        case "audio": return qsTr("%1, this tab’s audio, was not found.").arg(name)
        case "keyframes": return qsTr("%1, this tab’s keyframes, was not found.").arg(name)
        }
        return qsTr("%1 was not found.").arg(name)
    }
    function retry(entry) { windows.app.retryRestore(entry.row) }
    function remove(entry) { windows.app.removeRestore(entry.row) }
    function relink(entry) {
        relinkDialog.row = entry.row
        relinkDialog.kind = entry.kind
        relinkDialog.title = qsTr("Find “%1”").arg(entry.path.replace(/^.*[\\/]/, ""))
        relinkDialog.nameFilters = [({
            subtitles: qsTr("Subtitles (*.ass *.ssa *.srt *.sub *.txt *.mpl)"),
            video: qsTr("Video (*.mkv *.mp4 *.avi *.mov *.webm *.ts *.m2ts *.wmv)"),
            audio: qsTr("Audio and video files (*.wav *.w64 *.flac *.ac3 *.aac *.ogg *.mp3 *.mp4 *.m4a *.mkv *.avi)"),
            keyframes: qsTr("Keyframes file (*.txt *.pass *.stats *.log)")
        })[entry.kind] || qsTr("All files (*)"), qsTr("All files (*)")]
        const missing = windows.app.folderOf(windows.app.fileUrl(entry.path))
        const kept = windows.pickerFolder(entry.kind)
        if (missing.length > 0)
            relinkDialog.currentFolder = missing
        else if (kept.length > 0)
            relinkDialog.currentFolder = kept
        relinkDialog.open()
    }
    FileDialog {
        id: relinkDialog
        objectName: "relinkDialog"
        property int row: -1
        property string kind: ""
        fileMode: FileDialog.OpenFile
        onAccepted: {
            windows.rememberPickerFolder(kind, selectedFile)
            windows.app.relinkRestore(row, selectedFile)
        }
    }
}
