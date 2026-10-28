import QtQuick
import Hikari.Ui

// D3: the drop feedback while a panel is dragged, MuseScore's highlight
// rectangle (docs/research/musescore-docking.md §4, its DockFrame.qml): a
// 1-pixel accent border over an accent fill at 30 %, covering the area the
// panel would take (half the group for a side, all of it for a tab, a strip
// along the window's edge). The only place the dock chrome uses the accent.
// The adapter's DropHighlight (ui/docking.cpp) loads it as the engine's
// rubber band, which the engine sizes, shows and hides.
Rectangle {
    objectName: "dockDropHighlight"
    anchors.fill: parent
    color: Qt.alpha(Theme.accent, 0.3)
    border.width: 1
    border.color: Theme.accent
}
