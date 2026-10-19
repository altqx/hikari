pragma Translator: "HikariSub.Audio"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

// AV1 (#238): the audio panel's output device menu, from its speaker button.
// The entries are Settings > Audio's device choice (H2): System default, the
// connected devices and a chosen device that is not connected, the stored
// choice checked; choosing one stores it as the dialog's OK does, so the
// outputs move at once. "Audio settings…" opens the dialog on its Audio page.
ShellMenu {
    id: menu
    required property var app
    property var rows: []
    signal settingsRequested()
    onAboutToShow: rows = app.audioOutputMenu()
    Instantiator {
        model: menu.rows
        delegate: ShellMenuItem {
            required property var modelData
            required property int index
            objectName: "audioOutput" + index
            text: modelData.label
            checkable: true
            checked: modelData.checked
            keepsMenuOpen: false
            onTriggered: {
                menu.app.chooseAudioOutput(modelData.id, modelData.name)
                menu.rows = menu.app.audioOutputMenu()
            }
        }
        onObjectAdded: (index, object) => menu.insertItem(index, object)
        onObjectRemoved: (index, object) => menu.removeItem(object)
    }
    MenuSeparator {}
    ShellMenuItem {
        objectName: "audioOutputSettings"
        iconRole: "settings-audio"
        text: qsTr("Audio settings…")
        onTriggered: menu.settingsRequested()
    }
}
