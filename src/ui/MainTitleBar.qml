pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.MainWindow"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Rectangle {
    id: bar
    objectName: "mainTitleBar"

    required property MainWindowFrame frame
    required property TitleBarMenus menus
    required property LayoutSwitcher switcher
    readonly property Window window: frame.window
    readonly property bool custom: frame.custom
    readonly property bool active: window !== null && window.active
    readonly property bool maximized: window !== null
        && (window.visibility === Window.Maximized || window.visibility === Window.FullScreen)
    property string title: window ? window.title : ""

    readonly property int titleGap: 32
    readonly property int titleMinimum: 80

    readonly property bool keyboardMode: keyboardTarget !== null
    property Item keyboardTarget: null
    property Item returnFocus: null

    implicitHeight: custom ? 32 : 27
    color: Theme.panel
    Accessible.role: Accessible.TitleBar
    Accessible.name: custom ? title : qsTr("Menus")

    Component.onCompleted: {
        menus.parent = lead
        more.parent = lead
        switcher.parent = lead
        frame.titleBar = bar
        frame.moveArea = moveArea
        frame.maximizeButton = maximizeButton
        frame.windowMenuButton = appIcon
        applyFold()
    }
    Binding { target: bar.menus; property: "showMnemonics"; value: bar.keyboardMode || bar.frame.altHeld }
    Binding { target: bar.menus; property: "keyboardTarget"; value: bar.keyboardTarget }
    Binding { target: bar.menus; property: "windowActive"; value: bar.active || !bar.custom }
    Binding { target: bar.switcher; property: "keyboardCurrent"; value: bar.keyboardTarget === bar.switcher }

    readonly property bool mirrored: LayoutMirroring.enabled
    function leadingOf(x: real, w: real): real { return mirrored ? width - x - w : x }
    readonly property real leadStart: leadingOf(lead.x, lead.width)
    readonly property real buttonsStart: custom ? leadingOf(buttons.x, buttons.width) : width

    // MUST KILL: switcherGap copies Main.qml's Layout.leftMargin by hand
    readonly property int switcherGap: 8
    readonly property int foldFrom: {
        const n = menus.count
        let total = 0
        const widths = []
        for (let i = 0; i < n; ++i) {
            const item = menus.itemAt(i)
            widths.push(item ? item.implicitWidth : 0)
            total += widths[i]
        }
        const room = buttonsStart - leadStart - 8 - switcher.implicitWidth - switcherGap
        if (total <= room)
            return n
        let used = more.implicitWidth
        for (let i = 0; i < n; ++i) {
            used += widths[i]
            if (used > room)
                return i
        }
        return n
    }
    onFoldFromChanged: applyFold()
    function applyFold() {
        for (let i = 0; i < menus.count; ++i) {
            const item = menus.itemAt(i)
            if (item)
                item.visible = i < foldFrom
        }
        if (keyboardTarget && !keyboardTarget.visible)
            keyboardTarget = more
    }
    Connections {
        target: bar.menus
        function onCountChanged() { bar.applyFold() }
    }

    function keyboardStops() {
        const stops = []
        for (let i = 0; i < menus.count; ++i) {
            const item = menus.itemAt(i)
            if (item && item.visible && item.enabled)
                stops.push(item)
        }
        if (more.visible)
            stops.push(more)
        if (switcher.visible && switcher.enabled)
            stops.push(switcher)
        return stops
    }
    function enterKeyboard() {
        if (keyboardMode)
            return
        const stops = keyboardStops()
        if (stops.length === 0)
            return
        returnFocus = window ? window.activeFocusItem : null
        keyboardTarget = stops[0]
        bar.forceActiveFocus(Qt.MenuBarFocusReason)
    }
    function leaveKeyboard(restore: bool) {
        if (!keyboardMode)
            return
        keyboardTarget = null
        const back = returnFocus
        returnFocus = null
        if (restore && back)
            back.forceActiveFocus(Qt.MenuBarFocusReason)
    }
    function step(by: int) {
        const stops = keyboardStops()
        const at = stops.indexOf(keyboardTarget)
        keyboardTarget = stops[(Math.max(at, 0) + by + stops.length) % stops.length]
    }
    function openStop(stop: Item) {
        leaveKeyboard(true)
        if (stop === switcher)
            switcher.openMenu()
        else if (stop === more)
            more.openMenu()
        else if (stop)
            stop.triggered()
    }
    function openFolded(index: int) {
        const item = menus.itemAt(index)
        if (!item || !item.menu)
            return
        folded.menu = item.menu
        folded.item = item
        item.menu.popup(more, 0, more.height)
    }
    // Menu.popup(more, …) reparents the menu; it goes back under its title once closed.
    QtObject {
        id: folded
        property Menu menu: null
        property Item item: null
    }
    Connections {
        target: folded.menu
        function onClosed() {
            folded.menu.parent = folded.item
            folded.menu = null
            folded.item = null
        }
    }
    function mnemonicIndex(letter: string): int {
        const wanted = letter.toLowerCase()
        for (let i = 0; i < menus.count; ++i) {
            const item = menus.itemAt(i)
            const at = item ? item.text.indexOf("&") : -1
            if (item && item.enabled && at >= 0 && at + 1 < item.text.length
                    && item.text[at + 1].toLowerCase() === wanted)
                return i
        }
        return -1
    }

    activeFocusOnTab: false
    onActiveFocusChanged: if (!activeFocus && keyboardMode) leaveKeyboard(false)
    // MUST KILL: a Rectangle faking Control.visualFocus so key_router.cpp leaves it its keys
    readonly property bool visualFocus: keyboardMode && activeFocus
    function ownsKey(event): bool {
        switch (event.key) {
        case Qt.Key_Left: case Qt.Key_Right: case Qt.Key_Home: case Qt.Key_End:
        case Qt.Key_Down: case Qt.Key_Up: case Qt.Key_Return: case Qt.Key_Enter:
        case Qt.Key_Space: case Qt.Key_Escape: case Qt.Key_F10:
            return true
        }
        return event.text.length === 1 && (event.modifiers & ~(Qt.ShiftModifier | Qt.AltModifier)) === 0
            && mnemonicIndex(event.text) >= 0
    }
    Keys.onShortcutOverride: event => { event.accepted = keyboardMode && ownsKey(event) }
    Keys.onPressed: event => {
        if (!keyboardMode)
            return
        event.accepted = true
        const forward = bar.mirrored ? Qt.Key_Left : Qt.Key_Right
        const back = bar.mirrored ? Qt.Key_Right : Qt.Key_Left
        switch (event.key) {
        case forward: step(1); return
        case back: step(-1); return
        case Qt.Key_Home: keyboardTarget = keyboardStops()[0]; return
        case Qt.Key_End: { const s = keyboardStops(); keyboardTarget = s[s.length - 1]; return }
        case Qt.Key_Down:
        case Qt.Key_Up:
        case Qt.Key_Return:
        case Qt.Key_Enter:
        case Qt.Key_Space:
            openStop(keyboardTarget)
            return
        case Qt.Key_Escape:
        case Qt.Key_F10:
            leaveKeyboard(true)
            return
        }
        if (event.text.length === 1 && (event.modifiers & ~(Qt.ShiftModifier | Qt.AltModifier)) === 0) {
            const index = mnemonicIndex(event.text)
            if (index >= 0) {
                const item = menus.itemAt(index)
                if (item.visible)
                    openStop(item)
                else {
                    leaveKeyboard(true)
                    openFolded(index)
                }
                return
            }
        }
        event.accepted = false
    }
    Connections {
        target: bar.frame
        function onAltTapped() {
            if (bar.keyboardMode)
                bar.leaveKeyboard(true)
            else if (!bar.closeMenus())
                bar.enterKeyboard()
        }
    }
    function closeMenus(): bool {
        let closed = false
        for (let i = 0; i < menus.count; ++i) {
            const menu = menus.menuAt(i)
            if (menu && menu.visible) {
                menu.dismiss()
                closed = true
            }
        }
        return closed
    }

    TitleBarMoveArea {
        id: moveArea
        objectName: "titleBarMoveArea"
        anchors.fill: parent
        frame: bar.frame
    }

    // On Windows WM_NCHITTEST's HTSYSMENU opens the window menu natively.
    Item {
        id: appIcon
        objectName: "titleBarAppIcon"
        visible: bar.custom
        width: visible ? 32 : 0
        height: 32
        anchors.left: parent.left
        anchors.leftMargin: visible ? 2 : 0
        anchors.verticalCenter: parent.verticalCenter
        Image {
            anchors.centerIn: parent
            source: "qrc:/hikari/ui/app-icon.png"
            sourceSize: Qt.size(16, 16)
            width: 16
            height: 16
            opacity: bar.active ? 1 : 0.55
        }
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onPressed: bar.frame.showWindowMenu(mapToItem(null, 0, parent.height))
        }
        Accessible.role: Accessible.Button
        Accessible.name: qsTr("Window menu")
        Accessible.onPressAction: bar.frame.showWindowMenu(appIcon.mapToItem(null, 0, appIcon.height))
    }

    // MUST KILL: lead is filled by reparenting menus, More and the switcher in Component.onCompleted
    RowLayout {
        id: lead
        objectName: "titleBarLead"
        spacing: 0
        anchors.left: appIcon.right
        anchors.leftMargin: bar.custom ? 0 : 2
        anchors.verticalCenter: parent.verticalCenter
    }
    ToolButton {
        id: more
        objectName: "titleBarMore"
        visible: bar.foldFrom < bar.menus.count
        focusPolicy: Qt.NoFocus
        leftPadding: 5
        rightPadding: 5
        implicitHeight: 24
        Layout.alignment: Qt.AlignVCenter
        contentItem: Item {
            implicitWidth: 14
            implicitHeight: 14
            Icon { anchors.centerIn: parent; iconRole: "menu-more"; size: 14 }
        }
        background: Rectangle {
            radius: 1
            readonly property bool current: bar.keyboardTarget === more
            color: more.down || more.hovered || moreMenu.visible || current ? Theme.raised : "transparent"
            border.width: current ? 1 : 0
            border.color: Theme.focus
        }
        Accessible.role: Accessible.ButtonMenu
        Accessible.name: qsTr("More menus")
        ToolTip.visible: hovered && !moreMenu.visible
        ToolTip.text: qsTr("More menus")
        function openMenu() { moreMenu.popup(more, 0, more.height) }
        onClicked: moreMenu.visible ? moreMenu.close() : openMenu()
        ShellMenu {
            id: moreMenu
            objectName: "titleBarMoreMenu"
            Instantiator {
                model: Math.max(0, bar.menus.count - bar.foldFrom)
                delegate: ShellMenuItem {
                    required property int index
                    readonly property int menuIndex: bar.foldFrom + index
                    text: { const m = bar.menus.menuAt(menuIndex); return m ? m.title : "" }
                    onTriggered: Qt.callLater(bar.openFolded, menuIndex)
                }
                onObjectAdded: (index, object) => moreMenu.insertItem(index, object)
                onObjectRemoved: (index, object) => moreMenu.removeItem(object)
            }
        }
    }

    Label {
        id: titleLabel
        objectName: "titleBarTitle"
        readonly property real from: bar.leadStart + lead.width + bar.titleGap
        readonly property real to: bar.buttonsStart - bar.titleGap
        readonly property real room: to - from
        readonly property bool centred: bar.width / 2 - implicitWidth / 2 >= from && bar.width / 2 + implicitWidth / 2 <= to
        visible: bar.custom && room >= bar.titleMinimum
        text: bar.title
        elide: Text.ElideRight
        maximumLineCount: 1
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        color: bar.active ? Theme.text : Theme.disabled
        width: Math.min(implicitWidth, Math.max(0, room))
        height: parent.height
        x: bar.leadingOf(centred ? (bar.width - width) / 2 : from + (room - width) / 2, width)
        Accessible.role: Accessible.StaticText
        Accessible.name: text
    }

    // On Windows the maximise button is the frame's HTMAXBUTTON (snap layouts); the system tracks its pointer.
    Row {
        id: buttons
        objectName: "titleBarButtons"
        visible: bar.custom
        anchors.right: parent.right
        anchors.top: parent.top
        TitleBarButton {
            active: bar.active
            objectName: "titleBarMinimize"
            iconRole: "window-minimize"
            text: qsTr("Minimise")
            onClicked: bar.window.showMinimized()
        }
        TitleBarButton {
            active: bar.active
            id: maximizeButton
            objectName: "titleBarMaximize"
            iconRole: bar.maximized ? "window-restore" : "window-maximize"
            text: bar.maximized ? qsTr("Restore") : qsTr("Maximise")
            shownHover: hovered || bar.frame.maximizeHovered
            shownDown: down || bar.frame.maximizePressed
            onClicked: bar.frame.toggleMaximized()
        }
        TitleBarButton {
            active: bar.active
            objectName: "titleBarClose"
            iconRole: "window-close"
            text: qsTr("Close")
            onClicked: bar.window.close()
        }
    }

    Rectangle {
        visible: Theme.highContrast
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.line
    }
}
