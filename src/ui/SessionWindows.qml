import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Hikari.Ui

// P6: session restore. The startup prompts (legacy hikarisubApp::OnInit),
// the session file dialogs (legacy OnExternalSession) and the unresolved
// entries a restore left (the accepted lifecycle choice: they stay visible
// with Retry, Relink and Remove).
Item {
    id: windows
    required property var app
    // The close review of the open tabs: a function(rows).
    required property var review

    // GLOBAL_LOAD_LAST_SESSION (no file) or GLOBAL_LOAD_EXTERNAL_SESSION.
    function load(file) {
        const result = windows.app.reviewSession(file === undefined ? "" : file)
        if (!result.ok)
            return
        if (result.rows.length === 0)
            windows.app.finishClose()
        else
            windows.review(result.rows)
    }
    function chooseSessionToLoad() { loadDialog.open() }
    function chooseSessionToSave() { saveDialog.open() }

    Component.onCompleted: {
        const kind = windows.app.startupSession()
        if (kind === "load")
            Qt.callLater(() => windows.load())
        else if (kind === "ask")
            askPrompt.open()
        else if (kind === "crash")
            crashPrompt.open()
    }

    Connections {
        target: windows.app
        function onSessionRestored(unresolved) {
            if (unresolved > 0)
                unresolvedWindow.show()
        }
    }

    MessageDialog {
        id: askPrompt
        objectName: "loadSessionPrompt"
        title: qsTr("Prompt")
        text: qsTr("Load last session?")
        buttons: MessageDialog.Yes | MessageDialog.No
        onButtonClicked: (button, role) => {
            if (button === MessageDialog.Yes)
                windows.load()
            else if (windows.app.lastSessionCrashed())
                crashPrompt.open()
        }
    }
    MessageDialog {
        id: crashPrompt
        objectName: "crashSessionPrompt"
        title: qsTr("Prompt")
        text: qsTr("The program crashed or was closed improperly.\nLoad the last session with the latest autosaved subtitles?")
        buttons: MessageDialog.Yes | MessageDialog.No
        onButtonClicked: (button, role) => {
            if (button === MessageDialog.Yes)
                windows.load()
        }
    }
    FileDialog {
        id: loadDialog
        title: qsTr("Choose session file")
        currentFolder: windows.app.sessionFolder()
        nameFilters: [qsTr("Session file (*.kls)")]
        fileMode: FileDialog.OpenFile
        onAccepted: windows.load(selectedFile)
    }
    FileDialog {
        id: saveDialog
        title: qsTr("Save session file")
        currentFolder: windows.app.sessionFolder()
        nameFilters: [qsTr("Session file (*.kls)")]
        defaultSuffix: "kls"
        fileMode: FileDialog.SaveFile
        onAccepted: {
            if (windows.app.saveSessionTo(selectedFile) === "readonly")
                readOnlyNotice.open()
        }
    }
    MessageDialog {
        id: readOnlyNotice
        title: qsTr("Warning")
        text: qsTr("Chosen file is read only,\nplease save with different name or change file attribute.")
        buttons: MessageDialog.Ok
        onButtonClicked: saveDialog.open()
    }

    Window {
        color: Theme.panel // K2: a Window draws white unless told
        id: unresolvedWindow
        objectName: "unresolvedRestores"
        title: qsTr("Session entries not restored")
        width: 560
        height: 300
        flags: Qt.Dialog
        property int relinkRow: -1
        function kindName(kind) {
            switch (kind) {
            case "subtitles": return qsTr("Subtitles")
            case "video": return qsTr("Video")
            case "audio": return qsTr("Audio")
            case "keyframes": return qsTr("Keyframes")
            }
            return kind
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            Label {
                text: qsTr("These files of the session could not be found. The tabs stay open; retry, choose the file, or remove the entry.")
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            ListView {
                id: unresolvedList
                objectName: "unresolvedList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: windows.app.unresolvedRestores
                Accessible.role: Accessible.List
                Accessible.name: unresolvedWindow.title
                delegate: RowLayout {
                    id: entry
                    required property int index
                    required property var modelData
                    width: ListView.view.width
                    Label {
                        text: qsTr("Tab %1 (%2) – %3: %4").arg(entry.modelData.tab + 1).arg(entry.modelData.title)
                              .arg(unresolvedWindow.kindName(entry.modelData.kind)).arg(entry.modelData.path)
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                    Button {
                        objectName: "restoreRetry" + entry.index
                        text: qsTr("Retry")
                        onClicked: windows.app.retryRestore(entry.modelData.row)
                    }
                    Button {
                        objectName: "restoreRelink" + entry.index
                        text: qsTr("Relink…")
                        onClicked: {
                            unresolvedWindow.relinkRow = entry.modelData.row
                            relinkDialog.open()
                        }
                    }
                    Button {
                        objectName: "restoreRemove" + entry.index
                        text: qsTr("Remove")
                        onClicked: windows.app.removeRestore(entry.modelData.row)
                    }
                }
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Button {
                    objectName: "unresolvedClose"
                    text: qsTr("Close")
                    onClicked: unresolvedWindow.close()
                }
            }
        }
        FileDialog {
            id: relinkDialog
            title: qsTr("Choose the file")
            fileMode: FileDialog.OpenFile
            onAccepted: windows.app.relinkRestore(unresolvedWindow.relinkRow, selectedFile)
        }
    }
    function showUnresolved() { unresolvedWindow.show() }
}
