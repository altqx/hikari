pragma Translator: "HikariSub.QuickPick"
import QtQuick
import Hikari.Ui

Item {
    id: host
    property var popups: null
    property real hangFrom: 8
    readonly property var shown: popups ? popups.shown : null
    readonly property alias pick: pick
    readonly property alias box: box

    signal showing()

    onShownChanged: {
        pick.finish()
        box.finish()
        if (!shown)
            return
        showing()
        if (shown.kind === "pick") {
            pick.show(shown.value)
            pick.model.setCurrentById(shown.active)
        } else {
            box.show(shown.value, shown.selectionFrom, shown.selectionTo)
        }
    }

    Connections {
        target: host.shown
        function onValueSet() {
            if (host.shown.kind === "pick")
                pick.text = host.shown.value
            else
                box.setText(host.shown.value, host.shown.selectionFrom, host.shown.selectionTo)
        }
        function onActiveSet() {
            pick.model.setCurrentById(host.shown.active)
        }
    }

    QuickPick {
        id: pick
        objectName: "pluginQuickPick"
        hangFrom: host.hangFrom
        title: host.shown ? host.shown.title : ""
        step: host.shown ? host.shown.step : ""
        owner: host.shown ? host.shown.pluginName : ""
        placeholder: host.shown ? host.shown.placeholder : ""
        busy: host.shown ? host.shown.busy : false
        back: host.shown ? host.shown.back : false
        titleButtons: host.shown ? host.shown.buttons : []
        countText: qsTr("%n results", "", pick.model.count)
        model.items: host.shown && host.shown.kind === "pick" ? host.shown.items : []
        model.many: host.shown ? host.shown.many : false
        model.matchOn: host.shown ? host.shown.matchOn : []
        model.keepOrder: host.shown ? host.shown.keepOrder : false
        model.filtering: host.shown ? host.shown.filtering : true
        onTextEdited: (before, after) => host.shown.edit(after)
        onAccepted: item => host.shown.accept([item.id])
        onSecondary: item => {
            if (item.id !== undefined && (item.disabled ?? "").length === 0)
                host.shown.accept([item.id])
        }
        onPicked: ids => host.shown.accept(ids)
        onBackRequested: host.shown.goBack()
        onButtonPressed: (button, item) => host.shown.press(button, item)
        onCancelled: if (host.shown) host.shown.dismiss()
    }
    Connections {
        target: pick.model
        function onCurrentChanged() {
            if (pick.visible && host.shown && host.shown.live && pick.model.currentId.length > 0)
                host.shown.activate(pick.model.currentId)
        }
    }

    InputBox {
        id: box
        hangFrom: host.hangFrom
        title: host.shown ? host.shown.title : ""
        step: host.shown ? host.shown.step : ""
        owner: host.shown ? host.shown.pluginName : ""
        placeholder: host.shown ? host.shown.placeholder : ""
        prompt: host.shown ? host.shown.prompt : ""
        password: host.shown ? host.shown.password : false
        busy: host.shown ? host.shown.busy : false
        back: host.shown ? host.shown.back : false
        severity: host.shown ? host.shown.severity : ""
        message: host.shown ? host.shown.message : ""
        onEdited: text => host.shown.edit(text)
        onAccepted: host.shown.accept()
        onBackRequested: host.shown.goBack()
        onCancelled: if (host.shown) host.shown.dismiss()
    }
}
