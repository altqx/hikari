pragma Translator: "HikariSub.QuickPick"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

Popup {
    id: pick
    objectName: "quickPick"

    property string title: ""
    property string step: ""
    property string owner: ""
    property string placeholder: ""
    property string foot: ""
    property string emptyText: qsTr("No matches")
    property string accessibleName: title
    property string countText: ""
    property bool busy: false
    property real hangFrom: 8
    property alias text: field.text
    readonly property alias list: list
    readonly property QuickPickModel model: QuickPickModel {
        filter: pick.filterText
    }
    property string filterText: field.text
    readonly property int visibleRows: 12

    signal textEdited(string before, string after)
    signal accepted(var item)
    signal secondary(var item)
    signal cancelled()

    property Item restoreFocus: null
    property bool answered: false
    property string lastText: ""

    function show(initialText) {
        answered = false
        restoreFocus = parent && parent.Window.window ? parent.Window.window.activeFocusItem : null
        field.text = initialText ?? ""
        lastText = field.text
        open()
        field.forceActiveFocus()
        field.cursorPosition = field.text.length
    }
    function finish() {
        answered = true
        close()
    }
    function cancel() {
        if (!visible)
            return
        close()
    }
    function choose() {
        const item = model.currentItem()
        if (item.id === undefined || (item.disabled ?? "").length > 0)
            return
        accepted(item)
    }

    parent: Overlay.overlay
    x: Math.round((parent ? parent.width - width : 0) / 2)
    y: hangFrom
    width: Math.max(360, Math.min(600, (parent ? parent.width : 600) - 48))
    padding: 6
    modal: false
    focus: true
    closePolicy: Popup.CloseOnPressOutside
    onClosed: {
        if (!answered)
            cancelled()
        answered = true
        if (restoreFocus)
            restoreFocus.forceActiveFocus()
        restoreFocus = null
    }

    Connections {
        target: pick.parent ? pick.parent.Window.window : null
        function onActiveChanged() {
            if (pick.visible && !pick.parent.Window.window.active)
                pick.cancel()
        }
    }

    background: Rectangle {
        color: Theme.panel
        radius: 6
        border.color: Theme.line
        Rectangle {
            z: -1
            x: 0; y: 2
            width: parent.width
            height: parent.height
            radius: parent.radius
            color: "black"
            opacity: 0.25
            visible: !Theme.highContrast
        }
    }

    contentItem: Column {
        spacing: 0

        Item {
            id: titleRow
            objectName: "quickPickTitleRow"
            width: parent.width
            height: visible ? 30 : 0
            visible: pick.title.length > 0 || pick.owner.length > 0 || pick.step.length > 0
            Text {
                anchors.left: parent.left
                anchors.leftMargin: 6
                anchors.right: trailing.left
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                text: pick.title
                textFormat: Text.PlainText
                elide: Text.ElideRight
                font.weight: Font.DemiBold
                color: Theme.text
            }
            Text {
                id: trailing
                objectName: "quickPickTitleTrailing"
                anchors.right: parent.right
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                text: [pick.step, pick.owner].filter(t => t.length > 0).join("  ")
                textFormat: Text.PlainText
                color: Theme.muted
            }
        }

        Rectangle {
            width: parent.width
            height: 28
            radius: 4
            color: Theme.field
            border.color: field.activeFocus ? Theme.accent : Theme.line
            TextField {
                id: field
                objectName: "quickPickField"
                anchors.fill: parent
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                background: null
                placeholderText: pick.placeholder
                placeholderTextColor: Theme.muted
                color: Theme.text
                verticalAlignment: TextInput.AlignVCenter
                Accessible.role: Accessible.ComboBox
                Accessible.editable: true
                Accessible.name: pick.accessibleName
                Accessible.description: pick.placeholder
                onTextEdited: {
                    const before = pick.lastText
                    pick.lastText = text
                    pick.textEdited(before, text)
                }
                Keys.onPressed: event => {
                    const mods = event.modifiers & (Qt.ShiftModifier | Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)
                    switch (event.key) {
                    case Qt.Key_Up:
                    case Qt.Key_Down:
                        if (mods !== 0)
                            return
                        pick.model.move(event.key === Qt.Key_Up ? -1 : 1)
                        break
                    case Qt.Key_PageUp:
                    case Qt.Key_PageDown:
                        if (mods !== 0)
                            return
                        pick.model.page(event.key === Qt.Key_PageUp ? -1 : 1, pick.visibleRows)
                        break
                    case Qt.Key_Return:
                    case Qt.Key_Enter:
                        if (mods === Qt.ShiftModifier)
                            pick.secondary(pick.model.currentItem())
                        else if (mods === 0)
                            pick.choose()
                        else
                            return
                        break
                    case Qt.Key_Escape:
                        pick.cancel()
                        break
                    default:
                        return
                    }
                    event.accepted = true
                }
            }
            Rectangle {
                objectName: "quickPickBusy"
                anchors.bottom: parent.bottom
                height: 2
                width: parent.width / 3
                color: Theme.accent
                visible: pick.busy
                NumberAnimation on x {
                    running: pick.busy && pick.visible
                    from: 0
                    to: field.parent.width - field.parent.width / 3
                    duration: 900
                    loops: Animation.Infinite
                }
            }
        }

        Item { width: 1; height: 6 }

        ListView {
            id: list
            objectName: "quickPickList"
            width: parent.width
            height: Math.min(contentHeight, pick.visibleRows * 24)
            clip: true
            model: pick.model
            currentIndex: pick.model.current
            highlightFollowsCurrentItem: false
            boundsBehavior: Flickable.StopAtBounds
            onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)
            Accessible.role: Accessible.List
            Accessible.name: pick.accessibleName
            ScrollBar.vertical: ScrollBar {
                width: 6
                policy: list.contentHeight > list.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
            }
            delegate: QuickPickRow {
                width: ListView.view.width
                current: index === pick.model.current
                many: pick.model.many
                onToggled: {
                    pick.model.current = index
                    pick.model.toggle(index)
                }
                onClicked: {
                    pick.model.current = index
                    pick.choose()
                }
            }
        }

        Text {
            objectName: "quickPickEmpty"
            width: parent.width
            height: visible ? 24 : 0
            visible: pick.model.count === 0 && pick.emptyText.length > 0
            leftPadding: 6
            verticalAlignment: Text.AlignVCenter
            text: pick.emptyText
            textFormat: Text.PlainText
            color: Theme.muted
            elide: Text.ElideRight
        }

        Item {
            objectName: "quickPickFoot"
            width: parent.width
            height: visible ? 24 : 0
            visible: pick.foot.length > 0
            Rectangle {
                width: parent.width
                height: 1
                color: Theme.line
            }
            Text {
                anchors.fill: parent
                anchors.leftMargin: 6
                verticalAlignment: Text.AlignVCenter
                text: pick.foot
                textFormat: Text.PlainText
                font.pixelSize: 12
                color: Theme.muted
                elide: Text.ElideRight
            }
        }
    }

    Connections {
        target: pick.model
        function onCurrentChanged() {
            if (!pick.visible)
                return
            const item = pick.model.currentItem()
            if (item.label === undefined)
                return
            const parts = [item.label, item.description ?? "", item.shortcut ?? ""].filter(t => t.length > 0)
            if ((item.disabled ?? "").length > 0)
                parts.push(qsTr("unavailable: %1").arg(item.disabled))
            field.Accessible.announce(parts.join(", "))
        }
        function onCountChanged() { countTimer.restart() }
    }
    Timer {
        id: countTimer
        interval: 300
        onTriggered: if (pick.visible)
            field.Accessible.announce(pick.model.count === 0 ? pick.emptyText : pick.countText, Accessible.Polite)
    }
}
