// NT1 (#302): the application's message box (PK1's MessageBox.qml), made
// at the first question rather than at the start (a window made at Main's
// start costs Windows CI dearly, PK1). It opens over the window that has
// the keyboard, modal to it; a question asked while another shows waits
// for it. A QtObject, not an Item: in a Controls Dialog an Item would be
// another child of its content and change its size.
//
// ask(options, done): MessageBox.ask's options (title, text, detail,
// details, severity, choices, dontAsk) and done(id, dontAskAgain).
import QtQuick
import Hikari.Ui

QtObject {
    id: host
    property SettingsStore settings: null
    // The box once made (tests).
    property MessageBox box: null
    property var queue: []
    property bool asking: false

    function ask(options, done) {
        queue = queue.concat([{ options: options, done: done }])
        if (!asking)
            next()
    }
    function next() {
        if (asking || queue.length === 0)
            return
        const question = queue[0]
        queue = queue.slice(1)
        asking = true
        if (!box)
            box = boxComponent.createObject(null, { settings: host.settings })
        box.ask(question.options, (id, again) => {
            asking = false
            if (question.done)
                question.done(id, again)
            Qt.callLater(host.next)
        })
    }

    readonly property Component boxComponent: Component { MessageBox {} }
    onSettingsChanged: if (box) box.settings = settings
}
