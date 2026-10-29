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
            notices.messages.ask({ title: title, text: text, details: details, severity: severity }, () => {
                if (--notices.asking === 0 && notices.pendingScript.length > 0) {
                    editorDialog.script = notices.pendingScript
                    notices.pendingScript = ""
                    editorDialog.open()
                }
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
