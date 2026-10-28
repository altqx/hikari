pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T
import Hikari.Ui

// DG1 (#222): the frame of every dialog, a real top-level window
// (docs/ux/visual-language.md "Dialogs"; MS1, docs/research/musescore-shell.md
// §4 and §7 DG1):
// * the title bar in SH1's style (DialogTitleBar: the title, Close, and
//   Maximise only while `resizable`) on HikariSub's frame (DialogFrame), or
//   the window system's with "Use system title bar";
// * transient for the window it opened from, which it closes with;
// * the first open centred on that window, later ones where it was left
//   and as big, per `dialogId` (DialogPlacement);
// * `modal`: modal to that window only (questions that block an operation);
//   modeless by default, and opening a shown dialog again raises it;
// * Esc closes it (rejected; a control that takes Esc takes it first);
//   Enter presses the focused button, else `defaultButton` (a multiline
//   field keeps its Enter); the keyboard starts on `initialFocus`, else on
//   the default button; on closing it goes back to the control that had it
//   in the opener (or the opener's own, when a menu opened the dialog);
// * the content in a column inside the 24-wide margins, then the footer:
//   `leadingActions` at the leading edge and `buttons` at the trailing one
//   in the platform's order (WindowFooter: OK before Close on Windows and
//   KDE, after it on GNOME and macOS).
Window {
    id: dialog
    // The id its place is kept under (dialog_placement.h); empty: centred
    // on every open, nothing kept.
    property string dialogId: ""
    // DX1 (#367): its page of the user guide (guide_links.h), which User
    // guide (F1) and the title bar's "?" open; the dialogId unless the
    // window names another (the Automation windows, one id per script).
    property string guideId: dialogId
    // Where its place is kept and "Use system title bar" is read.
    property SettingsStore settings: null
    property bool resizable: false
    property bool modal: false
    // PF1 (#225): the content from edge to edge under the title bar, without
    // the 24-wide margins (a window with a side list, Settings); the footer
    // keeps them.
    property bool flush: false
    // PU7 (#309): false: no Close on the title bar, the window system's
    // frame included (DialogFrame drops its close hint), and Esc and the
    // window system's close do nothing (a wait the window cannot end).
    property bool closable: true
    // SY1: a line parts the body from the footer, whose top and bottom
    // margins are 20 (the Style manager; Settings draws its own).
    property bool footerRule: false
    property T.AbstractButton defaultButton: null
    property Item initialFocus: null
    readonly property bool opened: visible
    default property alias content: body.data
    property alias leadingActions: leading.data
    property alias buttons: closing.contentData
    // The footer's DialogButtonBox, for buttons made at run time (MessageBox).
    readonly property alias buttonBox: closing
    // The content's natural size, the window's for a fixed-size dialog and
    // its smallest for a resizable one, up to the screen's room.
    readonly property int naturalWidth: Math.ceil(column.implicitWidth)
    readonly property int naturalHeight: Math.ceil(column.implicitHeight)
    // Where the keyboard goes back to.
    property QtObject returnWindow: null // (a QWindow: a floating panel is no QML Window)
    property Item returnFocus: null
    property bool accepting: false
    property Item startFocus: null

    signal accepted()
    signal rejected()

    flags: Qt.Dialog
    color: Theme.panel
    modality: modal ? Qt.WindowModal : Qt.NonModal
    minimumWidth: Math.min(naturalWidth, placement.room.width)
    minimumHeight: Math.min(naturalHeight, placement.room.height)
    maximumWidth: resizable ? 16777215 : minimumWidth
    maximumHeight: resizable ? 16777215 : minimumHeight

    // Shows the dialog (or raises it while shown), opened from the window
    // with the keyboard.
    function open() { openFrom(null) }
    // Shows it without taking the keyboard from the window or program in
    // use (a notice the program gives by itself); a shown one stays as it is.
    function openQuietly() { openFrom(null, true) }
    // The same, opened from `from` (a window; null: the one with the keyboard).
    function openFrom(from, quiet) {
        if (visible) {
            if (quiet)
                return
            // Minimised by the window system: raising alone leaves it so.
            placement.unminimize()
            raise()
            requestActivate()
            return
        }
        const opener = from || placement.opener()
        returnWindow = opener
        returnFocus = opener && !placement.inPopup(opener.activeFocusItem) ? opener.activeFocusItem : null
        if (opener)
            transientParent = opener
        placement.place(opener, column)
        placement.showWithoutActivating(quiet === true)
        // (Not show(): a dialog may name a function of its own so.)
        visible = true
        if (quiet !== true) {
            raise()
            requestActivate()
        }
        const first = initialFocus && initialFocus.visible ? initialFocus : defaultButton
        if (first && first.visible && first.enabled) {
            first.forceActiveFocus(Qt.TabFocusReason)
            // The window's activation, which can come later, gives the item
            // its own focus reason and so drops the ring: Tab's again then.
            startFocus = first
        } else {
            scope.forceActiveFocus()
        }
    }
    function accept() {
        accepting = true
        accepted()
        close()
        accepting = false
    }
    function reject() { close() }

    // The body font at `size` against its 13 (visual-language.md's type
    // roles), in `weight`. From a label's font, which follows the program
    // font live; Qt.application.font is read once.
    function fontAt(size, weight) {
        const base = fontSource.font
        const f = Qt.font({family: base.family, weight: weight === undefined ? Font.Normal : weight})
        if (base.pointSize > 0)
            f.pointSize = base.pointSize * size / 13
        else
            f.pixelSize = Math.round(base.pixelSize * size / 13)
        return f
    }

    // Enter: the focused button presses itself; anything else (a check box
    // too, as on Windows: Space ticks it) presses the default button.
    function enter(event) {
        const item = activeFocusItem
        if (item && typeof item.click === "function" && item.hasOwnProperty("autoExclusive") && !item.checkable) {
            item.click()
            event.accepted = true
        } else if (defaultButton && defaultButton.visible && defaultButton.enabled) {
            defaultButton.click()
            event.accepted = true
        }
    }

    onClosing: close => {
        if (!closable && !accepting) {
            close.accepted = false
            return
        }
        if (!accepting)
            rejected()
    }
    Connections {
        target: dialog.startFocus
        ignoreUnknownSignals: true
        function onFocusReasonChanged() {
            const first = dialog.startFocus
            if (!first)
                return
            dialog.startFocus = null
            if (first.focusReason === Qt.ActiveWindowFocusReason)
                first.focusReason = Qt.TabFocusReason
        }
    }
    onVisibleChanged: {
        // (Hidden while its frame is made again is no closing.)
        if (visible || WindowFrame.remaking(dialog))
            return
        placement.remember()
        startFocus = null
        const back = returnWindow
        const item = returnFocus
        returnWindow = null
        returnFocus = null
        if (back && back.visible) {
            // The opener comes forward only when the keyboard was the
            // dialog's: a notice the program closes while another window
            // or program is in use takes nothing from it.
            if (active)
                back.requestActivate()
            if (item && item.visible && item.enabled && item.Window.window === back)
                item.forceActiveFocus()
        }
    }
    // Closed with the window it opened from (the main window's closing ends
    // its dialogs; a floating panel's hiding, the dialogs it opened).
    Connections {
        target: dialog.transientParent
        ignoreUnknownSignals: true
        function onVisibleChanged() {
            const opener = dialog.transientParent
            if (dialog.visible && opener && !opener.visible && !WindowFrame.remaking(opener))
                dialog.close()
        }
    }

    property string titleBarChoice: settings ? settings.value("appearance.titleBar") : "auto"
    Connections {
        target: dialog.settings
        function onChanged(id) {
            if (id === "appearance.titleBar")
                dialog.titleBarChoice = dialog.settings.value(id)
        }
    }
    DialogFrame {
        id: frame
        window: dialog
        resizable: dialog.resizable
        closable: dialog.closable
        // (customAvailable is read for its notification: the desktop can change in tests.)
        custom: WindowFrame.customAvailable && !WindowFrame.usesSystemTitleBar(dialog.titleBarChoice)
        // A shown dialog on its screen with the new frame's room: now, and
        // again once the window system has given the frame's margins (X11
        // sends them after the frame is made).
        onCustomChanged: {
            if (!dialog.visible)
                return
            Qt.callLater(placement.refit)
            refitAgain.restart()
        }
    }
    Timer {
        id: refitAgain
        interval: 250
        onTriggered: placement.refit()
    }
    DialogPlacement {
        id: placement
        window: dialog
        dialogId: dialog.dialogId
        settings: dialog.settings
        resizable: dialog.resizable
    }

    FocusScope {
        id: scope
        objectName: "dialogScope"
        anchors.fill: parent
        focus: true
        Accessible.role: Accessible.Dialog
        Accessible.name: dialog.title
        Keys.onEscapePressed: event => {
            event.accepted = true
            if (dialog.closable)
                dialog.reject()
        }
        Keys.onReturnPressed: event => dialog.enter(event)
        Keys.onEnterPressed: event => dialog.enter(event)

        ColumnLayout {
            id: column
            anchors.fill: parent
            spacing: 0
            DialogTitleBar {
                frame: frame
                closable: dialog.closable
                guideId: dialog.guideId
                Layout.fillWidth: true
            }
            ColumnLayout {
                id: body
                objectName: "dialogBody"
                spacing: 12
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: dialog.flush ? 0 : 24
                Layout.rightMargin: dialog.flush ? 0 : 24
                Layout.topMargin: dialog.flush ? 0 : frame.custom ? 12 : 24
            }
            Rectangle {
                visible: dialog.footerRule && footer.visible
                color: Theme.line
                implicitHeight: 1
                Layout.fillWidth: true
            }
            RowLayout {
                id: footer
                objectName: "dialogFooter"
                visible: closing.count > 0 || leading.visibleChildren.length > 0
                spacing: 8
                Layout.fillWidth: true
                Layout.margins: dialog.footerRule ? 20 : 24
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                RowLayout {
                    id: leading
                    spacing: 8
                }
                Item { Layout.fillWidth: true }
                WindowFooter {
                    id: closing
                    spacing: 8
                    Layout.fillWidth: false
                }
            }
            Item {
                // The body's bottom margin without a footer.
                visible: !footer.visible
                implicitHeight: 24
            }
        }
    }

    Label {
        id: fontSource
        visible: false
    }

    // The boundary and (while resizable) the resize band of the frameless
    // window on Wayland and X11.
    MainWindowEdges {
        objectName: "dialogEdges"
        frame: frame
        anchors.fill: parent
        z: 100
    }
}
