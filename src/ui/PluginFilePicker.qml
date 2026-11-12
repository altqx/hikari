pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Plugins"
import QtQuick
import QtQuick.Dialogs as Dialogs
import Hikari.Ui

Item {
    id: picker
    required property var ui
    required property var messages
    readonly property var request: ui ? ui.fileRequest : null

    onRequestChanged: {
        files.owner = null
        folders.owner = null
        files.close()
        folders.close()
        const r = request
        if (!r)
            return
        if (r.kind === "folder") {
            ask(r)
            return
        }
        files.owner = r
        files.open()
    }

    function ask(r) {
        messages.ask({
            title: r.pluginName,
            text: r.writable ? qsTr("%1 asks for a folder to read and write: %2").arg(r.pluginName, r.title)
                             : qsTr("%1 asks for a folder to read: %2").arg(r.pluginName, r.title),
            detail: r.reason,
            plainText: true,
            dontAsk: true,
            dontAskText: qsTr("Remember this folder for %1").arg(r.pluginName),
            dontAskChecked: true,
            choices: [{ id: "choose", text: qsTr("Choose folder…"), role: "accept", isDefault: true },
                      { id: "cancel", text: qsTr("Cancel"), role: "reject" }]
        }, (id, remembered) => {
            if (picker.request !== r)
                return
            if (id !== "choose") {
                r.reject()
                return
            }
            folders.owner = r
            folders.remember = remembered
            folders.open()
        })
    }

    FileChooser {
        id: files
        objectName: "pluginFileDialog"
        property var owner: null
        readonly property bool saving: owner !== null && owner.kind === "save"
        title: !owner ? ""
             : owner.title.length > 0 ? qsTr("%1: %2").arg(owner.pluginName, owner.title)
             : saving ? qsTr("%1: save a file").arg(owner.pluginName)
             : qsTr("%1: choose a file").arg(owner.pluginName)
        fileMode: saving ? Dialogs.FileDialog.SaveFile : Dialogs.FileDialog.OpenFile
        nameFilters: owner ? owner.nameFilters.concat([qsTr("All files (*)")]) : []
        Binding on selectedFile {
            when: files.owner !== null && String(files.owner.file).length > 0
            value: files.owner ? files.owner.file : ""
        }
        onAccepted: if (owner) owner.accept(selectedFile, false)
        onRejected: if (owner) owner.reject()
    }

    FolderChooser {
        id: folders
        objectName: "pluginFolderDialog"
        property var owner: null
        property bool remember: true
        title: owner ? qsTr("%1: %2").arg(owner.pluginName, owner.title) : ""
        onAccepted: if (owner) owner.accept(selectedFolder, remember)
        onRejected: if (owner) owner.reject()
    }
}
