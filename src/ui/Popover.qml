pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Popup {
    id: popover
    property Item anchor: null
    property rect anchorRect: Qt.rect(0, 0, 0, 0)
    property string title: ""
    property bool showTitle: false
    property bool active: false
    property Item returnFocus: null
    property Item initialFocus: null
    default property alias content: column.data
    readonly property alias heading: headingLabel

    signal kept()
    signal reverted()

    function openAt(anchorItem, host, area) {
        if (active)
            finish(true)
        anchorRect = area || Qt.rect(0, 0, 0, 0)
        anchor = anchorItem && anchorItem.visible ? anchorItem : null
        const base = anchor || host || anchorItem
        parent = anchor ? anchor : (base && base.Window.window ? base.Window.window.contentItem : base)
        const win = parent ? parent.Window.window : null
        returnFocus = win ? win.activeFocusItem : null
        active = true
        const w = implicitWidth + 2 * margins, h = implicitHeight + 2 * margins
        popupType = win && (win.width < w || win.height < h) ? Popup.Window : Popup.Item
        open()
        place()
        if (initialFocus)
            initialFocus.forceActiveFocus()
    }
    function keep() { finish(true) }
    function revert() { finish(false) }
    function finish(keeping) {
        if (!active)
            return
        // Qt's editable SpinBox commits its typed text only when it loses focus.
        if (keeping)
            editEnd.forceActiveFocus()
        active = false
        if (keeping)
            kept()
        else
            reverted()
        if (visible)
            close()
        if (returnFocus && returnFocus.visible)
            returnFocus.forceActiveFocus()
        returnFocus = null
    }

    function place() {
        if (!parent)
            return
        const win = parent.Window.window
        const h = implicitHeight
        if (!anchor) {
            x = Math.round((parent.width - implicitWidth) / 2)
            y = Math.round((parent.height - h) / 2)
            return
        }
        const part = anchorRect.width > 0 || anchorRect.height > 0
        const ax = part ? anchorRect.x : 0, ay = part ? anchorRect.y : 0
        const ah = part ? anchorRect.height : anchor.height
        x = Math.round(ax)
        const own = popupType === Popup.Window && win && win.screen
        const top = (own ? anchor.mapToGlobal(0, 0).y - win.screen.virtualY : anchor.mapToItem(null, 0, 0).y) + ay
        const room = own ? win.screen.desktopAvailableHeight : win ? win.height : top + ah + 4 + h + 8
        const below = room - (top + ah + 4) - 8
        y = Math.round(ay + (below >= h || top - 4 - 8 < h ? ah + 4 : -h - 4))
    }
    onImplicitHeightChanged: if (visible) place()

    modal: true
    dim: false
    focus: true
    padding: 12
    margins: 8
    closePolicy: Popup.CloseOnPressOutside | Popup.CloseMultiple
    onClosed: finish(true)

    background: PopoverBackground {}

    contentItem: FocusScope {
        focus: true
        implicitWidth: column.implicitWidth
        implicitHeight: column.implicitHeight
        Accessible.role: Accessible.Dialog
        Accessible.name: popover.title
        Keys.onShortcutOverride: event => event.accepted = event.modifiers === Qt.NoModifier
                                 && [Qt.Key_Escape, Qt.Key_Return, Qt.Key_Enter].includes(event.key)
        Keys.onEscapePressed: popover.revert()
        Keys.onReturnPressed: popover.keep()
        Keys.onEnterPressed: popover.keep()
        Item { id: editEnd; activeFocusOnTab: false }
        ColumnLayout {
            id: column
            anchors.fill: parent
            spacing: 8
            Label {
                textFormat: Text.PlainText
                id: headingLabel
                visible: popover.showTitle && popover.title.length > 0
                text: popover.title
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
        }
    }
}
