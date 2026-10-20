pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.MainWindow"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

// SH1 (#220): the main window's title bar, after MuseScore 4's
// (docs/research/musescore-shell.md §1 and §7, SH1): the application icon
// (the window menu), the menus, the layout switcher, the window's title
// and the window buttons, in one 32-pixel row above the Document tabs. Its
// free parts move the window, a double-click maximises it, and a right
// click opens the window system's window menu (MainWindowFrame, which also
// makes the window frameless). Where the system's title bar is kept
// (MainWindowFrame.custom false: the "Use system title bar" setting) only
// the menus and the switcher stay, in the window's first row.
//
// Keyboard (MS1 §1 "Menu bar"): Alt alone or F10 (Main.qml) puts the
// keyboard on the first menu, ringed, with the letters underlined;
// Left and Right walk the menus and the switcher, Down, Return or Space
// opens one, a menu's letter opens it, Esc or Alt again gives the keyboard
// back. Narrow windows elide the title, then drop it, then fold the last
// menus into More (⋯).
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

    // The title clears the menus and the buttons by this much, or moves.
    readonly property int titleGap: 32
    // Under this much room the title is not shown.
    readonly property int titleMinimum: 80

    // Keyboard mode: the title the keyboard is on (a menu title, More or
    // the switcher), and the item that had the keyboard before.
    readonly property bool keyboardMode: keyboardTarget !== null
    property Item keyboardTarget: null
    property Item returnFocus: null

    implicitHeight: custom ? 32 : 27
    color: Theme.panel
    Accessible.role: Accessible.TitleBar
    Accessible.name: custom ? title : qsTr("Menus")

    // Press, a shade beyond hover (the mockup's: hover is `raised`).
    readonly property color pressColor: Theme.highContrast
        ? Theme.select : Qt.tint(Theme.raised, Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.06))

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

    // The leading and trailing edges in reading order: in a right-to-left
    // layout the row runs from the right.
    readonly property bool mirrored: LayoutMirroring.enabled
    function leadingOf(x: real, w: real): real { return mirrored ? width - x - w : x }
    readonly property real leadStart: leadingOf(lead.x, lead.width)
    readonly property real buttonsStart: custom ? leadingOf(buttons.x, buttons.width) : width

    // Between the menus and the switcher (Main.qml's Layout.leftMargin).
    readonly property int switcherGap: 8
    // Menus from this index on are folded into More.
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

    // The keyboard's stops, in order: the shown menus, More, the switcher.
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
    // Opens a stop's menu; the menu takes the keyboard from there and
    // gives it back to where it was when it closes.
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
    // A folded menu opens under More; back under its title once closed.
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
    // The letter's menu, if a shown or folded menu has it.
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
    // The keyboard is on the bar: the shell's key router (key_router.cpp)
    // leaves its keys to it, as to any control the keyboard moved to.
    readonly property bool visualFocus: keyboardMode && activeFocus
    // The keys the bar answers in keyboard mode are its own before any
    // shortcut's (Left and Right step frames, Space plays).
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
        // Alt alone: into keyboard mode, or out of it; an open menu closes.
        function onAltTapped() {
            if (bar.keyboardMode)
                bar.leaveKeyboard(true)
            else if (!bar.closeMenus())
                bar.enterKeyboard()
        }
    }
    // Closes an open menu of the bar's; whether one was open.
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

    // The free parts of the bar: moving, maximising, the window menu. On
    // Windows the window system takes the pointer here (WM_NCHITTEST says
    // HTCAPTION, MainWindowFrame), so it never reaches this area.
    MouseArea {
        id: moveArea
        objectName: "titleBarMoveArea"
        anchors.fill: parent
        enabled: bar.custom
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        property point pressedAt
        property point windowAt
        property bool moving: false
        property bool following: false
        function follow(at: point) {
            bar.window.x = Math.round(windowAt.x + at.x - pressedAt.x)
            bar.window.y = Math.round(windowAt.y + at.y - pressedAt.y)
        }
        onPressed: mouse => {
            moving = false
            following = false
            pressedAt = mapToGlobal(mouse.x, mouse.y)
            if (mouse.button === Qt.RightButton)
                bar.frame.showWindowMenu(mapToItem(null, mouse.x, mouse.y))
        }
        onPositionChanged: mouse => {
            if (!(pressedButtons & Qt.LeftButton))
                return
            const at = mapToGlobal(mouse.x, mouse.y)
            if (following) {
                follow(at)
                return
            }
            if (moving || Math.abs(at.x - pressedAt.x) + Math.abs(at.y - pressedAt.y) < Application.styleHints.startDragDistance)
                return
            moving = true
            // Where the platform declines the move (X11 with no window
            // manager), the window follows the pointer; never a maximised one.
            if (!Docking.startSystemMove(moveArea) && !bar.maximized) {
                windowAt = Qt.point(bar.window.x, bar.window.y)
                following = true
                follow(at)
            }
        }
        onDoubleClicked: mouse => {
            if (mouse.button === Qt.LeftButton)
                bar.frame.toggleMaximized()
        }
        onReleased: { moving = false; following = false }
        onCanceled: { moving = false; following = false }
    }

    // The application icon: the window menu (MuseScore and Windows put it
    // there; on Windows WM_NCHITTEST's HTSYSMENU does it natively).
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

    // The menus, More and the switcher (placed here in Component.onCompleted).
    RowLayout {
        id: lead
        objectName: "titleBarLead"
        spacing: 0
        anchors.left: appIcon.right
        anchors.leftMargin: bar.custom ? 0 : 2
        anchors.verticalCenter: parent.verticalCenter
    }
    // More (⋯): the menus a narrow window has no room for.
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

    // The window's title: centred on the window where it clears the menus
    // and the buttons, else centred in the room between them, elided; not
    // shown under titleMinimum.
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

    // The window buttons. On Windows the maximise button is the frame's
    // (WM_NCHITTEST's HTMAXBUTTON, for Windows 11's snap layouts): the
    // window system tracks its pointer (MainWindowFrame.maximizeHovered,
    // maximizePressed) and maximises.
    component WindowButton: AbstractButton {
        id: windowButton
        required property string iconRole
        property bool shownHover: hovered
        property bool shownDown: down
        implicitWidth: 46
        implicitHeight: 32
        focusPolicy: Qt.NoFocus
        hoverEnabled: true
        contentItem: Item {
            Icon {
                anchors.centerIn: parent
                iconRole: windowButton.iconRole
                color: bar.active ? IconTheme.normal : IconTheme.disabled
            }
        }
        background: Rectangle {
            color: windowButton.shownDown ? bar.pressColor : windowButton.shownHover ? Theme.raised : "transparent"
            border.width: Theme.highContrast && (windowButton.shownDown || windowButton.shownHover) ? 1 : 0
            border.color: Theme.line
        }
        Accessible.role: Accessible.Button
        Accessible.name: text
        ToolTip.visible: shownHover && !shownDown
        ToolTip.text: text
        ToolTip.delay: Application.styleHints.mousePressAndHoldInterval
    }
    Row {
        id: buttons
        objectName: "titleBarButtons"
        visible: bar.custom
        anchors.right: parent.right
        anchors.top: parent.top
        WindowButton {
            objectName: "titleBarMinimize"
            iconRole: "window-minimize"
            text: qsTr("Minimise")
            onClicked: bar.window.showMinimized()
        }
        WindowButton {
            id: maximizeButton
            objectName: "titleBarMaximize"
            iconRole: bar.maximized ? "window-restore" : "window-maximize"
            text: bar.maximized ? qsTr("Restore") : qsTr("Maximise")
            shownHover: hovered || bar.frame.maximizeHovered
            shownDown: down || bar.frame.maximizePressed
            onClicked: bar.frame.toggleMaximized()
        }
        WindowButton {
            objectName: "titleBarClose"
            iconRole: "window-close"
            text: qsTr("Close")
            onClicked: bar.window.close()
        }
    }

    // High contrast: the title bar's edge against the tabs below.
    Rectangle {
        visible: Theme.highContrast
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.line
    }
}
