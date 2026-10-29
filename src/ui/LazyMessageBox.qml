import QtQuick
import Hikari.Ui

Loader {
    id: loader
    property string name: "messageBox"
    property SettingsStore settings: null
    readonly property MessageBox box: item as MessageBox
    readonly property bool opened: box !== null && box.visible
    active: false
    sourceComponent: MessageBox {
        objectName: loader.name
        settings: loader.settings
    }

    function ask(options, done) {
        active = true
        box.ask(options, done)
    }
    function close() {
        if (box)
            box.reject()
    }
}
