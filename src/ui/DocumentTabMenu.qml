import QtQuick
import QtQuick.Controls

// The tab menu: legacy Notebook::ContextMenu (Notebook.cpp, at 20d647c4) on
// tab `index` (-1: none). R1 fills in its "Subtitle comparison"
// (Notebook.cpp:915-956); the rest of the legacy tab menu is P9's, added here
// around it so DocumentTabBar.qml keeps only the right-click handlers.
ShellMenu {
    id: tabMenu
    objectName: "documentTabMenu"
    required property var app

    // Before the menu opens: legacy builds it on every right click.
    function openOn(index, item, x, y) {
        comparisonMenu.prepare(index)
        tabMenu.popup(item, x, y)
    }

    SubtitleComparisonMenu {
        id: comparisonMenu
        app: tabMenu.app
    }
}
