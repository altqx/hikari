pragma Translator: "HikariSub.Automation"
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs as Dialogs
import Hikari.Ui

Item {
    id: notices
    required property QtObject automation
    property ToastStack toasts: null
    property MessageHost messages: null

    property int asking: 0
    property string pendingScript: ""
    readonly property int pathsShown: 8
    function shownPaths(paths) {
        const extra = paths.length - pathsShown
        const shown = paths.slice(0, pathsShown).join("\n")
        return extra > 0 ? shown + "\n" + qsTr("and %n more", "", extra) : shown
    }
    function answered() {
        if (--asking === 0 && pendingScript.length > 0) {
            editorDialog.script = pendingScript
            pendingScript = ""
            editorDialog.open()
        }
    }

    Connections {
        target: notices.automation
        function onNotice(severity, title, text, details) {
            if (severity !== "error") {
                if (notices.toasts)
                    notices.toasts.post(severity, title, text, { details: details })
                return
            }
            if (!notices.messages)
                return
            ++notices.asking
            notices.messages.ask({ title: title, text: text, details: details, severity: severity }, notices.answered)
        }
        function onTrustRequested(id, scripts, folders) {
            if (!notices.messages) {
                notices.automation.answerTrust(id, "skip")
                return
            }
            ++notices.asking
            notices.messages.ask({
                title: qsTr("Automation"),
                text: qsTr("This subtitle file wants to run %n script(s) on your computer.", "", scripts.length),
                detail: qsTr("Scripts can read, change and delete your files. Load them only if you trust where this subtitle file came from.")
                        + "\n\n" + notices.shownPaths(scripts),
                details: scripts.join("\n"),
                plainText: true,
                severity: "warning",
                choices: [
                    { id: "load", text: qsTr("Load"), role: "accept" },
                    { id: "always", text: folders.length === 1 ? qsTr("Always trust scripts in this folder")
                                                              : qsTr("Always trust scripts in these folders"), role: "action" },
                    { id: "skip", text: qsTr("Don't load"), role: "reject", isDefault: true }
                ]
            }, answer => {
                notices.automation.answerTrust(id, answer)
                notices.answered()
            })
        }
        function onChooseScriptEditor(script) {
            if (notices.asking > 0) {
                notices.pendingScript = script
                return
            }
            editorDialog.script = script
            editorDialog.open()
        }
    }

    Dialogs.FileDialog {
        id: editorDialog
        objectName: "scriptEditorDialog"
        property string script
        title: qsTr("Select a script editor")
        fileMode: Dialogs.FileDialog.OpenFile
        nameFilters: [qsTr("Programs (*.exe)"), qsTr("All files (*.*)")]
        Component.onCompleted: if (Qt.platform.os === "windows") selectedFile = "file:///C:/Windows/Notepad.exe"
        onAccepted: notices.automation.editWith(selectedFile, script)
    }
}
