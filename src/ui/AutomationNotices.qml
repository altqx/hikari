// S4: the automation shell's legacy message boxes (HikariMessageBox: "Run
// the last loaded script", validation and load failures, "Cannot start
// editor.") and Automation::OnEdit's "Select a script editor" picker. As
// legacy's boxes are modal and come first, the picker asked for while a box
// shows opens once the box is closed.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs as Dialogs

Item {
    id: notices
    required property QtObject automation

    property var queue: []
    property string pendingScript: ""

    function showNext() {
        if (notice.visible || queue.length === 0)
            return
        const next = queue[0]
        queue = queue.slice(1)
        notice.title = next.title
        notice.text = next.text
        notice.open()
    }

    Connections {
        target: notices.automation
        function onNotice(title, text) {
            notices.queue = notices.queue.concat([{ title: title, text: text }])
            notices.showNext()
        }
        function onChooseScriptEditor(script) {
            if (notice.visible) {
                notices.pendingScript = script
                return
            }
            editorDialog.script = script
            editorDialog.open()
        }
    }

    Dialog {
        id: notice
        objectName: "automationNotice"
        property alias text: noticeLabel.text
        parent: Overlay.overlay
        anchors.centerIn: parent
        // A fixed width the text wraps in (a width from the text loops with the title's).
        width: Math.min(420, parent ? parent.width - 32 : 420)
        modal: true // legacy HikariMessageBox is modal
        standardButtons: Dialog.Ok
        Label {
            id: noticeLabel
            objectName: "automationNoticeText"
            width: notice.availableWidth
            wrapMode: Text.Wrap
            Accessible.role: Accessible.AlertMessage
        }
        onClosed: {
            if (notices.queue.length > 0) {
                notices.showNext()
            } else if (notices.pendingScript.length > 0) {
                editorDialog.script = notices.pendingScript
                notices.pendingScript = ""
                editorDialog.open()
            }
        }
    }

    // wxFileSelector(_("Select a script editor"), "", "C:\\Windows\\Notepad.exe",
    // "exe", _("Programs (*.exe)|*.exe|All files (*.*)|*.*"), wxFD_FILE_MUST_EXIST).
    Dialogs.FileDialog {
        id: editorDialog
        objectName: "scriptEditorDialog"
        property string script
        title: qsTr("Select a script editor")
        fileMode: Dialogs.FileDialog.OpenFile
        nameFilters: [qsTr("Programs (*.exe)"), qsTr("All files (*.*)")]
        // The default file only where it exists (an empty one is a warning).
        Component.onCompleted: if (Qt.platform.os === "windows") selectedFile = "file:///C:/Windows/Notepad.exe"
        onAccepted: notices.automation.editWith(selectedFile, script)
    }
}
