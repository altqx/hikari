pragma Translator: "HikariSub.Tabs"
import QtQuick
import QtQuick.Controls

ShellMenu {
    id: comparison
    objectName: "subtitleComparisonMenu"
    iconRole: "compare"
    title: qsTr("Subtitle comparison")
    required property var app
    property int tabIndex: -1
    property var menuState: ({})
    enabled: menuState.enabled === true

    function prepare(index) {
        tabIndex = index
        menuState = app.openComparisonMenu(index)
        compareByTimes.checked = menuState.times === true
        compareByVisible.checked = menuState.visible === true
        compareBySelections.checked = menuState.selections === true
        compareByStyles.checked = menuState.styles === true
        // Reset first: an equal model list keeps the old items and their stale checks.
        styleItems.model = []
        styleItems.model = menuState.styleItems ?? []
    }

    delegate: ShellMenuItem {
        id: entry
        readonly property bool stylesEntry: entry.subMenu !== null && entry.subMenu.objectName === "compareStylesMenu"
        objectName: stylesEntry ? "compareByChosenStyles" : ""
        checkable: stylesEntry
        checked: stylesEntry && comparison.menuState.chosenStyles === true
        onToggled: checked = Qt.binding(() => stylesEntry && comparison.menuState.chosenStyles === true)
        Accessible.checkable: stylesEntry
        Accessible.checked: checked
    }

    ShellMenuItem {
        id: compareByTimes
        objectName: "compareByTimes"
        text: qsTr("Compare by times")
        checkable: true
        enabled: comparison.menuState.canCompare === true
        // MUST KILL: toggleComparisonBit takes raw bits (1 times, 2 styles, 8 visible lines, 16 selections)
        onTriggered: comparison.app.toggleComparisonBit(1)
    }
    ShellMenuItem {
        id: compareByVisible
        objectName: "compareByVisible"
        text: qsTr("Compare by visible lines")
        checkable: true
        enabled: comparison.menuState.canCompare === true
        onTriggered: comparison.app.toggleComparisonBit(8)
    }
    ShellMenuItem {
        id: compareBySelections
        objectName: "compareBySelections"
        text: qsTr("Compare by selections")
        checkable: true
        enabled: comparison.menuState.selectionsEnabled === true
        onTriggered: comparison.app.toggleComparisonBit(16)
    }
    ShellMenuItem {
        id: compareByStyles
        objectName: "compareByStyles"
        text: qsTr("Compare by styles")
        checkable: true
        enabled: comparison.menuState.canCompare === true
        onTriggered: comparison.app.toggleComparisonBit(2)
    }
    ShellMenu {
        id: stylesMenu
        iconRole: "styles"
        objectName: "compareStylesMenu"
        title: qsTr("Compare by selected styles")
        enabled: comparison.menuState.canCompare === true
        Instantiator {
            id: styleItems
            model: []
            delegate: ShellMenuItem {
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
    ShellMenuItem {
        iconRole: "compare"
        objectName: "compareSubtitles"
        text: qsTr("Compare")
        enabled: comparison.menuState.canCompare === true
        onTriggered: comparison.app.compareWithTab(comparison.tabIndex)
    }
    ShellMenuItem {
        iconRole: "compare-off"
        objectName: "turnOffComparison"
        text: qsTr("Turn off comparison")
        enabled: comparison.menuState.active === true
        onTriggered: comparison.app.turnOffComparison()
    }
}
