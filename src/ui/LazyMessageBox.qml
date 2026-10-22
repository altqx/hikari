// PU1 (#303): a message box (MessageBox.qml) made at its first question,
// not at the program's start: every window made at the start costs it (on
// Windows a native event filter apiece), and most questions are never asked.
// ask(options, done) as MessageBox's; `box` is null until then.
import QtQuick
import Hikari.Ui

Loader {
    id: loader
    // The message box's objectName (tests and UI Automation find it by it).
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
