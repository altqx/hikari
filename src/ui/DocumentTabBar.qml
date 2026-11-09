pragma Translator: "HikariSub.Tabs"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Item {
    id: bar
    objectName: "documentTabBar"
    required property var app
    property int shellWindow: 0
    property QtObject review: null
    signal closeRequested(int index)
    signal closeTabsRequested(int index, bool toTheRight)
    signal saveRequested(var id)
    signal saveAllRequested()
    signal closeAllRequested()

    readonly property int inset: 4
    implicitHeight: row.implicitHeight + inset
    readonly property bool shown: {
        bar.app.home
        return !bar.app.windowHome(bar.shellWindow)
    }
    Accessible.role: Accessible.PageTabList
    Accessible.name: qsTr("Open documents")
    Accessible.ignored: !shown

    TapHandler {
        enabled: bar.shown
        onDoubleTapped: bar.app.addPage(true)
    }
    TapHandler {
        enabled: bar.shown
        acceptedButtons: Qt.RightButton
        onTapped: eventPoint => bar.openTabMenu(-1, bar, eventPoint.position.x, eventPoint.position.y)
    }

    function openTabMenu(index, item, x, y) {
        tabMenu.openOn(index, item, x, y, firstVisibleTab())
    }
    DocumentTabMenu {
        id: tabMenu
        app: bar.app
        shellWindow: bar.shellWindow
        onSaveRequested: id => bar.saveRequested(id)
        onSaveAllRequested: bar.saveAllRequested()
        onCloseRequested: index => bar.closeRequested(index)
        onCloseOthersRequested: (index, toTheRight) => bar.closeTabsRequested(index, toTheRight)
        onCloseAllRequested: bar.closeAllRequested()
    }
    MoveTabMenu {
        id: moveToWindowPopup
        objectName: "moveTabToWindowPopup"
        app: bar.app
        shellWindow: bar.shellWindow
    }
    function openMoveToWindowMenu() {
        const index = bar.app.windowCurrentTab(bar.shellWindow)
        const tab = tabRepeater.itemAt(index)
        moveToWindowPopup.prepare(bar.app.tabDocument(index))
        if (tab && moveToWindowPopup.enabled)
            moveToWindowPopup.popup(tab, 0, tab.height)
    }
    function dropIndexAt(x, y, document) {
        if (x < 0 || y < 0 || x > width || y > height)
            return -1
        const inRow = row.mapFromItem(bar, x, 0).x
        let left = 0
        let index = tabRepeater.count
        let from = -1
        for (let i = 0; i < tabRepeater.count; ++i) {
            const width = tabRepeater.itemAt(i)?.implicitWidth ?? 0
            if (index === tabRepeater.count && inRow < left + width / 2)
                index = i
            if (tabModel.get(i).tabId === String(document))
                from = i
            left += width + row.spacing
        }
        return from >= 0 && index > from ? index - 1 : index
    }

    Item {
        id: draggedTab
        property real document: 0
        Drag.dragType: Drag.Automatic
        Drag.supportedActions: Qt.MoveAction
        Drag.mimeData: ({ "application/x-hikari-tab": String(draggedTab.document) })
        Drag.onDragFinished: dropAction => {
            const document = draggedTab.document
            bar.app.tabDragging = false
            if (dropAction === Qt.IgnoreAction)
                Qt.callLater(() => bar.app.moveTabToNewWindow(document, Qt.point(0, 0), false))
        }
    }
    function prepareTabDrag(index, grabbedAt) {
        const tab = tabRepeater.itemAt(index)
        draggedTab.document = bar.app.tabDocument(index)
        draggedTab.Drag.imageSource = ""
        if (!tab)
            return
        draggedTab.Drag.hotSpot = Qt.point(tab.mapFromItem(bar, grabbedAt.x, 0).x, tab.height / 2)
        tab.grabToImage(result => draggedTab.Drag.imageSource = result.url)
    }

    // A ListModel moves rows instead of remaking delegates, so a drag keeps its pointer grab.
    ListModel {
        id: tabModel
    }
    function tabEntry(row) {
        return { tabId: String(row.id), tabLabel: row.label, tabTitle: row.title, tabModified: row.modified,
                 tabCurrent: row.current, tabTip: row.tip, tabReference: !!row.reference, tabReview: row.review ?? 0 }
    }
    function syncTabs() {
        const rows = bar.app.windowTabs(bar.shellWindow)
        let same = rows.length === tabModel.count
        for (let i = 0; same && i < rows.length; ++i) {
            let found = false
            for (let j = 0; j < tabModel.count && !found; ++j)
                found = tabModel.get(j).tabId === String(rows[i].id)
            same = found
        }
        if (!same) {
            tabModel.clear()
            for (const row of rows)
                tabModel.append(tabEntry(row))
            return
        }
        for (let i = 0; i < rows.length; ++i) {
            let j = i
            while (j < tabModel.count && tabModel.get(j).tabId !== String(rows[i].id))
                ++j
            if (j !== i)
                tabModel.move(j, i, 1)
            tabModel.set(i, tabEntry(rows[i]))
        }
    }
    Connections {
        target: bar.app
        function onTabsChanged() { bar.syncTabs() }
    }
    Connections {
        target: bar.review
        function onChanged() { bar.syncTabs() }
    }
    Component.onCompleted: syncTabs()

    function firstVisibleTab() {
        for (let i = 0; i < tabRepeater.count; ++i) {
            const tab = tabRepeater.itemAt(i)
            if (tab && tab.x + tab.width > flick.contentX)
                return i
        }
        return 0
    }
    // Widths in order, not x: the RowLayout places a swap only at its next polish.
    function tabAt(x) {
        let left = 0
        for (let i = 0; i < tabRepeater.count; ++i) {
            const tab = tabRepeater.itemAt(i)
            if (!tab)
                return -1
            if (x >= left && x < left + tab.implicitWidth)
                return i
            left += tab.implicitWidth + row.spacing
        }
        return -1
    }

    DragHandler {
        id: tabDrag
        objectName: "tabDragHandler"
        enabled: bar.shown
        target: null
        acceptedButtons: Qt.LeftButton
        property int dragged: -1
        property bool out: false
        property point releasedAt
        readonly property bool wayland: Qt.platform.pluginName.startsWith("wayland")
        function tabUnder(x) {
            return bar.tabAt(row.mapFromItem(bar, x, 0).x)
        }
        onActiveChanged: {
            if (active) {
                dragged = tabUnder(centroid.pressPosition.x)
                if (dragged >= 0) {
                    bar.app.selectTab(dragged)
                    if (wayland)
                        bar.prepareTabDrag(dragged, centroid.pressPosition)
                }
                return
            }
            const document = out && !wayland && dragged >= 0 ? bar.app.tabDocument(dragged) : 0
            dragged = -1
            out = false
            bar.app.endTabDrag()
            if (document > 0)
                bar.app.dropTab(document, releasedAt)
        }
        onCentroidChanged: {
            if (!active || dragged < 0)
                return
            releasedAt = bar.mapToGlobal(centroid.position.x, centroid.position.y)
            const margin = Application.styleHints.startDragDistance
            out = centroid.position.y < -margin || centroid.position.y > bar.height + margin
            if (out) {
                if (wayland && !draggedTab.Drag.active) {
                    bar.app.tabDragging = true
                    draggedTab.Drag.active = true
                }
                return
            }
            const over = tabUnder(centroid.position.x)
            if (over >= 0 && over !== dragged && bar.app.dragTab(dragged, over))
                dragged = over
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        anchors.topMargin: bar.inset
        anchors.leftMargin: bar.inset
        opacity: bar.shown ? 1 : 0
        enabled: bar.shown
        contentWidth: row.implicitWidth
        clip: true
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        interactive: false

        WheelHandler {
            orientation: Qt.Vertical
            onWheel: event => {
                const max = Math.max(0, flick.contentWidth - flick.width)
                flick.contentX = Math.max(0, Math.min(max, flick.contentX - event.angleDelta.y / 2))
            }
        }

        RowLayout {
            id: row
            spacing: 2
            Repeater {
                id: tabRepeater
                model: tabModel
                delegate: AbstractButton {
                    id: tab
                    required property int index
                    required property string tabLabel
                    required property string tabTitle
                    required property bool tabModified
                    required property bool tabCurrent
                    required property string tabTip
                    required property bool tabReference
                    required property int tabReview
                    objectName: "documentTab" + index
                    checkable: false
                    checked: tabCurrent
                    focusPolicy: Qt.TabFocus
                    activeFocusOnTab: tabCurrent
                    text: tabLabel
                    Accessible.role: Accessible.PageTab
                    Accessible.name: tabTitle
                    Accessible.checked: tabCurrent
                    Accessible.description: [tabModified ? qsTr("Modified") : "",
                                             tabReference ? qsTr("Protected reference") : ""].filter(s => s.length > 0).join(", ")
                    // MUST KILL: tabLabel packs "<history step>*<name>" and is parsed back here
                    readonly property int mark: tabModified ? tabLabel.indexOf("*") : -1
                    readonly property string stepText: mark >= 0 ? tabLabel.slice(0, mark) : ""
                    implicitHeight: label.implicitHeight + 10
                    implicitWidth: label.implicitWidth + 12 + closeMark.implicitWidth
                                   + (mark >= 0 ? modifiedMark.width + 4 : 0)
                                   + (reviewBadge.visible ? reviewBadge.implicitWidth + 2 : 0)
                    ToolTip.visible: hovered
                    ToolTip.text: [tabTip,
                                   stepText.length > 0 ? qsTr("Modified (history step %1)").arg(stepText) : "",
                                   tabReview > 0 ? qsTranslate("HikariSub.Review", "%n AI change(s) to review", "", tabReview) : ""]
                                  .filter(s => s.length > 0).join("\n")
                    background: Rectangle {
                        color: tab.checked ? palette.base : (tab.hovered ? palette.midlight : palette.button)
                        border.color: tab.checked || tab.visualFocus ? palette.highlight : palette.mid
                        border.width: tab.visualFocus ? 2 : 1
                        radius: 2
                    }
                    contentItem: RowLayout {
                        spacing: 2
                        Icon {
                            id: modifiedMark
                            objectName: "documentTabModified" + tab.index
                            visible: tab.mark >= 0
                            iconRole: "document-modified"
                            Layout.leftMargin: 4
                        }
                        Label {
                            id: label
                            text: tab.mark >= 0 ? tab.text.slice(tab.mark + 1) : tab.text
                            leftPadding: tab.mark >= 0 ? 0 : 4
                        }
                        Rectangle {
                            id: reviewBadge
                            objectName: "documentTabReview" + tab.index
                            visible: tab.tabReview > 0
                            implicitWidth: badgeRow.implicitWidth + 8
                            implicitHeight: badgeRow.implicitHeight + 2
                            radius: implicitHeight / 2
                            color: tab.checked ? Theme.accent : "transparent"
                            border.color: Theme.accent
                            Accessible.role: Accessible.StaticText
                            Accessible.name: qsTranslate("HikariSub.Review", "%n AI change(s) to review", "", tab.tabReview)
                            Row {
                                id: badgeRow
                                anchors.centerIn: parent
                                spacing: 2
                                Icon {
                                    iconRole: "review-keep"
                                    size: 12
                                    color: tab.checked ? Theme.onAccent : IconTheme.normal
                                    accentColor: tab.checked ? Theme.onAccent : IconTheme.accent
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Label {
                                    text: tab.tabReview
                                    font.pixelSize: 11
                                    color: tab.checked ? Theme.onAccent : Theme.text
                                }
                            }
                        }
                        IconToolButton {
                            id: closeMark
                            objectName: "documentTabClose" + tab.index
                            visible: true
                            opacity: tab.checked ? 1 : 0
                            enabled: tab.checked
                            Accessible.ignored: !tab.checked
                            iconRole: "tab-close"
                            text: qsTr("Close %1").arg(tab.tabTitle)
                            focusPolicy: Qt.NoFocus
                            padding: 1
                            implicitWidth: 18
                            implicitHeight: 18
                            onClicked: bar.closeRequested(tab.index)
                        }
                    }
                    onClicked: bar.app.selectTab(index)
                    function focusTab(step) {
                        const next = tabRepeater.itemAt(index + step)
                        if (next)
                            next.forceActiveFocus(Qt.TabFocusReason)
                    }
                    Keys.onShortcutOverride: event => event.accepted = (event.modifiers & (Qt.ShiftModifier | Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)) === 0
                                                                 && [Qt.Key_Left, Qt.Key_Right, Qt.Key_Space, Qt.Key_Menu].includes(event.key)
                    Keys.onLeftPressed: focusTab(Qt.application.layoutDirection === Qt.RightToLeft ? 1 : -1)
                    Keys.onRightPressed: focusTab(Qt.application.layoutDirection === Qt.RightToLeft ? -1 : 1)
                    MenuKey.onRequested: bar.openTabMenu(tab.index, tab, 0, tab.height)
                    TapHandler {
                        acceptedButtons: Qt.MiddleButton
                        onTapped: bar.closeRequested(tab.index)
                    }
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        gesturePolicy: TapHandler.WithinBounds
                        onTapped: eventPoint => bar.openTabMenu(tab.index, tab, eventPoint.position.x, eventPoint.position.y)
                    }
                }
            }
            IconToolButton {
                objectName: "newTabButton"
                iconRole: "tab-new"
                text: qsTr("Open new tab")
                focusPolicy: Qt.NoFocus
                onClicked: bar.app.addPage(true)
            }
        }
    }
}
