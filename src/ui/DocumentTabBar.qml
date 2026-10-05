import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

// P6: legacy Notebook's tab bar below the workspace. One tab per Document
// (the protected reference has its own tray). A click shows a tab; the
// active tab's close mark or a middle click closes a tab through the close
// review; "+" and a double click on the empty bar add a tab (both write the
// session, as legacy AddPage(true, true) did). K1: the close mark, the new tab
// button and the modified mark (legacy's "*" between the history step and the
// name) are the set's icons.
//
// P9: the tab menu's Save, Save all and Close all tabs go to the window
// (saveRequested, saveAllRequested, closeAllRequested). A tab dragged over
// another trades places with it (legacy Notebook::OnMouseEvent swaps the two
// Pages while the left button is down, Notebook.cpp:537-556, and writes the
// session when the button comes up after a swap, 397-400); a drag shows the
// tab it started on. The bar scrolls with the wheel; dragging never scrolls it.
Item {
    id: bar
    objectName: "documentTabBar"
    required property var app
    signal closeRequested(int index)
    signal saveRequested(var id)
    signal saveAllRequested()
    signal closeAllRequested()

    implicitHeight: row.implicitHeight
    Accessible.role: Accessible.PageTabList
    Accessible.name: qsTr("Open documents")

    TapHandler {
        // A double click where there is no tab (legacy i == -1).
        onDoubleTapped: bar.app.addPage(true)
    }
    TapHandler {
        // A right click where there is no tab: the tab menu for none (-1).
        acceptedButtons: Qt.RightButton
        onTapped: eventPoint => bar.openTabMenu(-1, bar, eventPoint.position.x, eventPoint.position.y)
    }

    // Legacy Notebook::ContextMenu on tab `index` (-1: none), at (x, y) in
    // `item`; its items are in DocumentTabMenu.qml.
    function openTabMenu(index, item, x, y) {
        tabMenu.openOn(index, item, x, y, firstVisibleTab())
    }
    DocumentTabMenu {
        id: tabMenu
        app: bar.app
        onSaveRequested: id => bar.saveRequested(id)
        onSaveAllRequested: bar.saveAllRequested()
        onCloseAllRequested: bar.closeAllRequested()
    }

    // The tabs as a model that moves its rows rather than making them anew,
    // so a reorder during a drag keeps the dragged pointer's grab.
    ListModel {
        id: tabModel
    }
    function tabEntry(row) {
        return { tabId: String(row.id), tabLabel: row.label, tabTitle: row.title, tabModified: row.modified,
                 tabCurrent: row.current, tabTip: row.tip, tabReference: !!row.reference } // R2
    }
    function syncTabs() {
        const rows = bar.app.tabs
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
    Component.onCompleted: syncTabs()

    // Legacy firstVisibleTab: the first tab the scrolled bar shows.
    function firstVisibleTab() {
        for (let i = 0; i < tabRepeater.count; ++i) {
            const tab = tabRepeater.itemAt(i)
            if (tab && tab.x + tab.width > flick.contentX)
                return i
        }
        return 0
    }
    // The tab at `x` in the row (-1: none), from the tabs' widths in their
    // order now (legacy FindTab over tabSizes), so a swap the layout has not
    // placed yet is already seen.
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

    // P9: a tab dragged over another trades places with it (the handler is
    // the bar's own, so it outlives the tab items the new order rebuilds).
    DragHandler {
        id: tabDrag
        objectName: "tabDragHandler"
        target: null
        yAxis.enabled: false
        acceptedButtons: Qt.LeftButton
        property int dragged: -1
        function tabUnder(x) {
            return bar.tabAt(row.mapFromItem(bar, x, 0).x)
        }
        onActiveChanged: {
            if (active) {
                // The press showed the tab in legacy (ChangePage on LeftDown).
                dragged = tabUnder(centroid.pressPosition.x)
                if (dragged >= 0)
                    bar.app.selectTab(dragged)
            } else {
                dragged = -1
                bar.app.endTabDrag()
            }
        }
        onCentroidChanged: {
            if (!active || dragged < 0)
                return
            const over = tabUnder(centroid.position.x)
            if (over >= 0 && over !== dragged && bar.app.dragTab(dragged, over))
                dragged = over
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: row.implicitWidth
        clip: true
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        interactive: false // a drag reorders tabs (P9); the wheel scrolls

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
                    objectName: "documentTab" + index
                    checkable: false
                    checked: tabCurrent
                    focusPolicy: Qt.NoFocus
                    text: tabLabel
                    Accessible.role: Accessible.PageTab
                    Accessible.name: tabTitle
                    Accessible.checked: tabCurrent
                    // R2: the tab shown as the protected reference says so.
                    Accessible.description: [tabModified ? qsTr("Modified") : "",
                                             tabReference ? qsTr("Protected reference") : ""].filter(s => s.length > 0).join(", ")
                    // "<history step>*<name>" while modified: the modified
                    // mark in place of the "*", then the name; the step (a
                    // bare number on the tab read as a counter) is in the
                    // tooltip. Every tab keeps the close button's slot, so
                    // a tab's width does not change as it becomes current.
                    readonly property int mark: tabModified ? tabLabel.indexOf("*") : -1
                    readonly property string stepText: mark >= 0 ? tabLabel.slice(0, mark) : ""
                    implicitHeight: label.implicitHeight + 10
                    implicitWidth: label.implicitWidth + 12 + closeMark.implicitWidth
                                   + (mark >= 0 ? modifiedMark.width + 4 : 0)
                    ToolTip.visible: hovered
                    ToolTip.delay: 600
                    ToolTip.text: stepText.length > 0
                                  ? tabTip + "\n" + qsTr("Modified (history step %1)").arg(stepText) : tabTip
                    background: Rectangle {
                        color: tab.checked ? palette.base : (tab.hovered ? palette.midlight : palette.button)
                        border.color: tab.checked ? palette.highlight : palette.mid
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
                            font.bold: tab.checked
                            leftPadding: tab.mark >= 0 ? 0 : 4
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
                    TapHandler {
                        acceptedButtons: Qt.MiddleButton
                        onTapped: bar.closeRequested(tab.index)
                    }
                    TapHandler {
                        // The tab's own: the bar's handler does not see it.
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
