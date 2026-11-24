pragma Translator: "HikariSub.Agent"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

QuickPick {
    id: picker
    objectName: "modelPicker"

    property var itemsFor: filter => []
    property Item within: null
    property real withinLeft: 0
    property real withinRight: 0
    property real anchorX: 0
    property real anchorTop: 0
    property real anchorBottom: 0
    readonly property bool opensAbove: anchorTop >= (parent ? parent.height : 0) - anchorBottom

    signal chosen(string value)
    signal actionRequested(string action, string key)

    function openAt(control) {
        const top = control.mapToItem(parent, 0, 0)
        anchorX = top.x
        anchorTop = top.y
        anchorBottom = top.y + control.height
        const box = within.mapToItem(parent, 0, 0)
        withinLeft = box.x
        withinRight = box.x + within.width
        model.items = itemsFor("")
        show("")
    }
    function refresh() {
        if (visible)
            model.items = itemsFor(text)
    }
    function take(value) {
        restoreFocus = null
        finish()
        chosen(value)
    }

    placeholder: qsTr("Search models")
    emptyText: qsTr("No matching models")
    accessibleName: qsTr("Model")
    width: Math.max(280, Math.min(360, (within ? within.width : 376) - 16))
    x: Math.max(withinLeft + 8, Math.min(anchorX, withinRight - width - 8))
    y: opensAbove ? Math.max(0, anchorTop - height - 4) : anchorBottom + 4
    Component.onCompleted: {
        model.keepOrder = true
        model.matchOn = ["description"]
    }
    onTextChanged: refresh()
    onAccepted: item => {
        if (item.action === "retry") {
            actionRequested(item.action, item.key)
            return
        }
        if (item.action !== undefined) {
            finish()
            actionRequested(item.action, item.key)
            return
        }
        take(item.value)
    }
}
