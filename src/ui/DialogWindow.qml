pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T
import Hikari.Ui

Window {
    id: dialogWindow
    property string dialogId: ""
    property string guideId: dialogId
    property SettingsStore settings: null
    property bool resizable: false
    property bool modal: false
    property bool flush: false
    property bool closable: true
    property bool footerRule: false
    property T.AbstractButton defaultButton: null
    property Item initialFocus: null
    readonly property bool opened: visible
    default property alias content: body.data
    property alias leadingActions: leading.data
    property alias buttons: closing.contentData
    readonly property alias buttonBox: closing
    readonly property int naturalWidth: Math.ceil(column.implicitWidth)
    readonly property int naturalHeight: Math.ceil(column.implicitHeight)
    property QtObject returnWindow: null // a QWindow: a floating panel is no QML Window
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

    function open() { openFrom(null) }
    function openQuietly() { openFrom(null, true) }
    function openFrom(from, quiet) {
        if (visible) {
            if (quiet)
                return
            // raise() alone leaves a window the window system minimised.
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
        // MUST KILL: visible = true, not show(), because some dialogs define their own show()
        visible = true
        if (quiet !== true) {
            raise()
            requestActivate()
        }
        const first = initialFocus && initialFocus.visible ? initialFocus : defaultButton
        if (first && first.visible && first.enabled) {
            first.forceActiveFocus(Qt.TabFocusReason)
            // Qt's later window activation resets the focus reason and drops the ring; startFocus puts Tab's back.
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

    // Qt.application.font is read once; a Label's font follows the program font live.
    function fontAt(size, weight) {
        const base = fontSource.font
        const f = Qt.font({family: base.family, weight: weight === undefined ? Font.Normal : weight})
        if (base.pointSize > 0)
            f.pointSize = base.pointSize * size / 13
        else
            f.pixelSize = Math.round(base.pixelSize * size / 13)
        return f
    }

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
        target: dialogWindow.startFocus
        ignoreUnknownSignals: true
        function onFocusReasonChanged() {
            const first = dialogWindow.startFocus
            if (!first)
                return
            dialogWindow.startFocus = null
            if (first.focusReason === Qt.ActiveWindowFocusReason)
                first.focusReason = Qt.TabFocusReason
        }
    }
    onVisibleChanged: {
        if (visible || WindowFrame.remaking(dialogWindow))
            return
        placement.remember()
        startFocus = null
        const back = returnWindow
        const item = returnFocus
        returnWindow = null
        returnFocus = null
        if (back && back.visible) {
            if (active)
                back.requestActivate()
            if (item && item.visible && item.enabled && item.Window.window === back)
                item.forceActiveFocus()
        }
    }
    Connections {
        target: dialogWindow.transientParent
        ignoreUnknownSignals: true
        function onVisibleChanged() {
            const opener = dialogWindow.transientParent
            if (dialogWindow.visible && opener && !opener.visible && !WindowFrame.remaking(opener))
                dialogWindow.close()
        }
    }

    property string titleBarChoice: settings ? settings.value("appearance.titleBar") : "auto"
    Connections {
        target: dialogWindow.settings
        function onChanged(id) {
            if (id === "appearance.titleBar")
                dialogWindow.titleBarChoice = dialogWindow.settings.value(id)
        }
    }
    DialogFrame {
        id: dialogFrame
        window: dialogWindow
        resizable: dialogWindow.resizable
        closable: dialogWindow.closable
        custom: WindowFrame.customAvailable && !WindowFrame.usesSystemTitleBar(dialogWindow.titleBarChoice)
        // X11 sends the frame's margins after the frame is made: refit now and again later.
        onCustomChanged: {
            if (!dialogWindow.visible)
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
        window: dialogWindow
        dialogId: dialogWindow.dialogId
        settings: dialogWindow.settings
        resizable: dialogWindow.resizable
    }

    FocusScope {
        id: scope
        objectName: "dialogScope"
        anchors.fill: parent
        focus: true
        Accessible.role: Accessible.Dialog
        Accessible.name: dialogWindow.title
        Keys.onEscapePressed: event => {
            event.accepted = true
            if (dialogWindow.closable)
                dialogWindow.reject()
        }
        Keys.onReturnPressed: event => dialogWindow.enter(event)
        Keys.onEnterPressed: event => dialogWindow.enter(event)

        ColumnLayout {
            id: column
            anchors.fill: parent
            spacing: 0
            DialogTitleBar {
                frame: dialogFrame
                closable: dialogWindow.closable
                guideId: dialogWindow.guideId
                Layout.fillWidth: true
            }
            ColumnLayout {
                id: body
                objectName: "dialogBody"
                spacing: 12
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: dialogWindow.flush ? 0 : 24
                Layout.rightMargin: dialogWindow.flush ? 0 : 24
                Layout.topMargin: dialogWindow.flush ? 0 : dialogFrame.custom ? 12 : 24
            }
            Rectangle {
                visible: dialogWindow.footerRule && footer.visible
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
                Layout.margins: dialogWindow.footerRule ? 20 : 24
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
                visible: !footer.visible
                implicitHeight: 24
            }
        }
    }

    Label {
        id: fontSource
        visible: false
    }

    MainWindowEdges {
        objectName: "dialogEdges"
        frame: dialogFrame
        anchors.fill: parent
        z: 100
    }
}
