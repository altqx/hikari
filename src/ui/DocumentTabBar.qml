pragma Translator: "HikariSub.Tabs"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Item {
    id: bar
    objectName: "documentTabBar"
    required property var app
    signal closeRequested(int index)
    signal saveRequested(var id)
    signal saveAllRequested()
    signal closeAllRequested()

    readonly property int inset: 4
    implicitHeight: row.implicitHeight + inset
    readonly property bool shown: !bar.app.home
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
        onSaveRequested: id => bar.saveRequested(id)
        onSaveAllRequested: bar.saveAllRequested()
        onCloseAllRequested: bar.closeAllRequested()
    }

    // A ListModel moves rows instead of remaking delegates, so a drag keeps its pointer grab.
    ListModel {
        id: tabModel
    }
    function tabEntry(row) {
        return { tabId: String(row.id), tabLabel: row.label, tabTitle: row.title, tabModified: row.modified,
                 tabCurrent: row.current, tabTip: row.tip, tabReference: !!row.reference }
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
        yAxis.enabled: false
        acceptedButtons: Qt.LeftButton
        property int dragged: -1
        function tabUnder(x) {
            return bar.tabAt(row.mapFromItem(bar, x, 0).x)
        }
        onActiveChanged: {
            if (active) {
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
                    objectName: "documentTab" + index
                    checkable: false
                    checked: tabCurrent
                    focusPolicy: Qt.NoFocus
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
                    ToolTip.visible: hovered
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
