pragma ComponentBehavior: Bound
// PK1 (#301): a popover, one of the kit's four kinds of popup
// (docs/ux/visual-language.md "Popups"): anchored to the control that
// opened it, below it, or above it where there is no room below; centred
// in the window without one. The colour popover's surface
// (PopoverBackground) with its padding of 12.
//
// It edits in place: a press outside it, Enter, or its control again
// closes it and keeps what changed (kept()); Esc closes it and the opener
// puts the value before back (reverted()). In a window of its own when the
// host window is too small for it, so it can reach past the host.
//
// `title` is its accessible name, and a heading at its top with
// `showTitle` (when the control that opens it does not already say what
// it edits).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Popup {
    id: popover
    property Item anchor: null
    // A part of the anchor to open at (a word in a text field), in the
    // anchor's coordinates; empty: the whole anchor.
    property rect anchorRect: Qt.rect(0, 0, 0, 0)
    property string title: ""
    property bool showTitle: false
    // Open for an edit: false once it has been kept or reverted.
    property bool active: false
    // The item that had the keyboard before, given it back at the end.
    property Item returnFocus: null
    // The item that takes the keyboard when it opens (else the first one).
    property Item initialFocus: null
    default property alias content: column.data
    readonly property alias heading: headingLabel

    signal kept()
    signal reverted()

    // Opens below `anchorItem` (or centred in `host`'s window without one).
    // Clicking the anchor of a popover that is open closes it: a press
    // that closed this popover on its own anchor opens nothing.
    function openAt(anchorItem, host, area) {
        if (active)
            finish(true)
        anchorRect = area || Qt.rect(0, 0, 0, 0)
        if (anchorItem && anchorItem === pressedAnchor && Date.now() - pressedAt < 1000) {
            pressedAnchor = null
            return false
        }
        pressedAnchor = null
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
        return true
    }
    function keep() { finish(true) }
    function revert() { finish(false) }
    function finish(keeping) {
        if (!active)
            return
        // The value being typed first: an editable SpinBox takes its text
        // when it loses the keyboard, which Enter alone does not do.
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
        // Below the anchor, or above it when only there is room: in the host
        // window, or on the screen for a popover in its own window.
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
    // Esc is the popover's own (reverted); a press outside keeps the change.
    closePolicy: Popup.CloseOnPressOutside
    // A press outside: kept. On the anchor of a popover in its own window
    // the same press reaches the anchor too (openAt). Once that press has
    // been delivered, only an anchor it went to stays recorded.
    onClosed: {
        const pressed = active && popupType === Popup.Window
        const was = anchor
        finish(true)
        if (pressed) {
            pressedAnchor = was
            pressedAt = Date.now()
            Qt.callLater(keepPressedAnchor)
        }
    }
    // An anchor with no `pressed` (a text field) keeps the record for the
    // press's second (openAt): only a button that is up drops it.
    function keepPressedAnchor() {
        if (pressedAnchor && pressedAnchor.pressed === false)
            pressedAnchor = null
    }
    property Item pressedAnchor: null
    property double pressedAt: 0

    background: PopoverBackground {}

    contentItem: FocusScope {
        focus: true
        implicitWidth: column.implicitWidth
        implicitHeight: column.implicitHeight
        Accessible.role: Accessible.Dialog
        Accessible.name: popover.title
        // The popover's keys come before the application's shortcuts.
        Keys.onShortcutOverride: event => event.accepted = event.modifiers === Qt.NoModifier
                                 && [Qt.Key_Escape, Qt.Key_Return, Qt.Key_Enter].includes(event.key)
        Keys.onEscapePressed: popover.revert()
        Keys.onReturnPressed: popover.keep()
        Keys.onEnterPressed: popover.keep()
        // Takes the keyboard from an editor at the end (finish()).
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
