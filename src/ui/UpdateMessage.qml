pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.MainWindow"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

// DG1 (#222): the update check's and the installer's notices on the dialog
// frame: a type icon (information, or the warning glyph), the text up to
// about 400 wide, and OK. Titled with the command that gave it ("Check for
// updates") or, for the installer's notices, "Update" (DG1-update-titles).
// Fixed size, modeless.
DialogWindow {
    id: message
    objectName: "updateMessage"
    property alias text: label.text
    property bool warning: false
    dialogId: "updateMessage"
    defaultButton: ok

    // Shows `text` under `title`, as a warning or not.
    function showMessage(title, text, warning) {
        message.title = title
        message.text = text
        message.warning = warning
        open()
    }

    RowLayout {
        spacing: 16
        Layout.topMargin: 8
        Icon {
            objectName: "updateMessageIcon"
            Layout.alignment: Qt.AlignTop
            iconRole: message.warning ? "notice-warning" : "about"
            size: 32
        }
        Label {
            id: label
            objectName: "updateMessageText"
            wrapMode: Text.WordWrap
            topPadding: 6
            lineHeight: 1.15
            Layout.preferredWidth: Math.min(implicitWidth, 400)
            Layout.maximumWidth: 400
        }
    }

    buttons: [
        PrimaryButton {
            id: ok
            objectName: "updateMessageOk"
            text: qsTr("OK")
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: message.accept()
        }
    ]
}
