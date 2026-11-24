pragma Translator: "HikariSub.QuickPick"
import QtQuick
import Hikari.Ui

Item {
    id: host
    property real hangFrom: 8
    readonly property alias pick: pick
    readonly property alias box: box
    readonly property bool asking: reply !== null
    property var reply: null
    property var asker: null
    property var check: null

    signal showing()

    function pickFrom(spec, done) {
        cancel()
        showing()
        reply = done
        asker = spec.from ?? null
        pick.title = spec.title ?? ""
        pick.placeholder = spec.placeholder ?? ""
        pick.model.items = spec.items
        pick.show("")
        pick.model.first()
    }
    function askFor(spec, done) {
        cancel()
        showing()
        reply = done
        asker = spec.from ?? null
        check = spec.check ?? null
        box.title = spec.title ?? ""
        box.prompt = spec.prompt ?? ""
        box.message = ""
        box.show(spec.value ?? "", -1)
    }
    function answer(value) {
        const done = reply
        dismiss()
        if (done)
            done(value)
    }
    function cancel() {
        if (asking)
            answer(null)
    }
    function dismissFrom(from) {
        if (asking && asker === from)
            dismiss()
    }
    function dismiss() {
        reply = null
        asker = null
        check = null
        pick.finish()
        box.finish()
    }
    function problemWith(text) {
        return check ? check(text) : ""
    }

    QuickPick {
        id: pick
        objectName: "appQuickPick"
        hangFrom: host.hangFrom
        countText: qsTr("%n results", "", pick.model.count)
        onAccepted: item => host.answer(item.id)
        onCancelled: host.cancel()
    }

    InputBox {
        id: box
        objectName: "appInputBox"
        hangFrom: host.hangFrom
        severity: message.length > 0 ? "error" : ""
        onEdited: text => box.message = host.problemWith(text)
        onAccepted: text => {
            box.message = host.problemWith(text)
            if (box.message.length === 0)
                host.answer(text)
        }
        onCancelled: host.cancel()
    }
}
