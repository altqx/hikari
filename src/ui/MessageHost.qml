import QtQuick
import Hikari.Ui

// A QtObject, not an Item: in a Controls Dialog an Item would be another child of its content and change its size.
QtObject {
    id: host
    property SettingsStore settings: null
    property MessageBox box: null
    property var queue: []
    property bool asking: false
    property var current: null

    function ask(options, done) {
        const question = { options: options, done: done }
        queue = queue.concat([question])
        if (!asking)
            next()
        return question
    }
    function withdraw(question) {
        if (!question)
            return
        if (queue.indexOf(question) >= 0) {
            queue = queue.filter(q => q !== question)
            const reject = (question.options.choices || []).find(c => c.role === "reject")
            if (question.done)
                question.done(reject ? reject.id : "", false)
        } else if (question === current && box) {
            box.answer(box.rejectId())
        }
    }
    function next() {
        if (asking || queue.length === 0)
            return
        const question = queue[0]
        queue = queue.slice(1)
        asking = true
        current = question
        if (!box)
            box = boxComponent.createObject(null, { settings: host.settings })
        const from = question.options.from
        const options = from && !from.visible ? Object.assign({}, question.options, { from: null }) : question.options
        box.ask(options, (id, again) => {
            asking = false
            current = null
            if (question.done)
                question.done(id, again)
            Qt.callLater(host.next)
        })
    }

    readonly property Component boxComponent: Component { MessageBox {} }
    onSettingsChanged: if (box) box.settings = settings
}
