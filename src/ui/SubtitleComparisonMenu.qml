import QtQuick
import QtQuick.Controls

// R1: the tab menu's "Subtitle comparison" (legacy Notebook::ContextMenu,
// Notebook.cpp:915-956, at 20d647c4). Opened on a tab other than the active
// one, it compares the editing target with that tab: by times, visible lines,
// selections, styles and selected styles (the styles both share), then
// Compare; Turn off comparison while one is on. The criteria are
// SUBS_COMPARISON_TYPE and SUBS_COMPARISON_STYLES, saved as they change.
Menu {
    id: comparison
    objectName: "subtitleComparisonMenu"
    title: qsTr("Subtitle comparison")
    required property var app
    // The tab the menu was opened on (-1: not on a tab), and what
    // app.openComparisonMenu reported for it.
    property int tabIndex: -1
    property var menuState: ({})
    enabled: menuState.enabled === true

    // Before the tab menu opens: legacy builds the menu then, adding the
    // checked styles to the chosen ones (Notebook.cpp:917-939).
    function prepare(index) {
        tabIndex = index
        menuState = app.openComparisonMenu(index)
        compareByTimes.checked = menuState.times === true
        compareByVisible.checked = menuState.visible === true
        compareBySelections.checked = menuState.selections === true
        compareByStyles.checked = menuState.styles === true
    }

    // "Compare by selected styles" is a checked item with the styles under
    // it, checked while any style is chosen (Notebook.cpp:936, 113-121).
    delegate: MenuItem {
        id: entry
        readonly property bool stylesEntry: entry.subMenu !== null && entry.subMenu.objectName === "compareStylesMenu"
        objectName: stylesEntry ? "compareByChosenStyles" : ""
        checkable: stylesEntry
        checked: stylesEntry && comparison.menuState.chosenStyles === true
        // A click opens the styles; it checks nothing (legacy sends no event
        // for an item with a submenu, Menu.cpp:647-649).
        onToggled: checked = Qt.binding(() => stylesEntry && comparison.menuState.chosenStyles === true)
        Accessible.checkable: stylesEntry
        Accessible.checked: checked
    }

    MenuItem {
        id: compareByTimes
        objectName: "compareByTimes"
        text: qsTr("Compare by times")
        checkable: true
        enabled: comparison.menuState.canCompare === true
        onTriggered: comparison.app.toggleComparisonBit(1) // COMPARE_BY_TIMES
    }
    MenuItem {
        id: compareByVisible
        objectName: "compareByVisible"
        text: qsTr("Compare by visible lines")
        checkable: true
        enabled: comparison.menuState.canCompare === true
        onTriggered: comparison.app.toggleComparisonBit(8) // COMPARE_BY_VISIBLE
    }
    MenuItem {
        id: compareBySelections
        objectName: "compareBySelections"
        text: qsTr("Compare by selections")
        checkable: true
        // Both files have selections (SelectionsSize() > 0).
        enabled: comparison.menuState.selectionsEnabled === true
        onTriggered: comparison.app.toggleComparisonBit(16) // COMPARE_BY_SELECTIONS
    }
    MenuItem {
        id: compareByStyles
        objectName: "compareByStyles"
        text: qsTr("Compare by styles")
        checkable: true
        enabled: comparison.menuState.canCompare === true
        onTriggered: comparison.app.toggleComparisonBit(2) // COMPARE_BY_STYLES
    }
    Menu {
        id: stylesMenu
        objectName: "compareStylesMenu"
        title: qsTr("Compare by selected styles")
        enabled: comparison.menuState.canCompare === true
        Instantiator {
            model: comparison.menuState.styleItems ?? []
            delegate: MenuItem {
                required property var modelData
                objectName: "compareStyle_" + modelData.name
                text: modelData.name
                checkable: true
                checked: modelData.checked
                onTriggered: {
                    const shown = comparison.app.toggleComparisonStyle(modelData.name, checked)
                    const next = Object.assign({}, comparison.menuState)
                    next.chosenStyles = shown
                    comparison.menuState = next
                }
            }
            onObjectAdded: (index, object) => stylesMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => stylesMenu.removeItem(object)
        }
    }
    MenuItem {
        objectName: "compareSubtitles"
        text: qsTr("Compare")
        enabled: comparison.menuState.canCompare === true
        onTriggered: comparison.app.compareWithTab(comparison.tabIndex)
    }
    MenuItem {
        objectName: "turnOffComparison"
        text: qsTr("Turn off comparison")
        enabled: comparison.menuState.active === true
        onTriggered: comparison.app.turnOffComparison()
    }
}
