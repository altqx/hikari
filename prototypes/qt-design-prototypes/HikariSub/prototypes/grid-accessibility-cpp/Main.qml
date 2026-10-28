import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HikariProbe 1.0

ApplicationWindow {
    id: window
    width: 1180; height: 740; visible: true
    title: "THROWAWAY · C++ painted Grid accessibility"
    color: "#181b21"
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 16; spacing: 10
        Label { text: "C++ table and lazy cell interfaces · 50,000 synthetic Lines"; color: "#eef2f8"; font.pixelSize: 21 }
        Label { text: "Can accessible identity survive reorder/filter independently of painting? In-process evidence only; no UIA, NVDA or performance verdict."; color: "#bdc5d3"; wrapMode: Text.Wrap; Layout.fillWidth: true }
        RowLayout {
            Button { text: "Reverse"; onClicked: { grid.reverseRows(); grid.forceActiveFocus() } }
            Button { text: "All Lines"; onClicked: { grid.filterRows("all"); grid.forceActiveFocus() } }
            Button { text: "RTL subset"; onClicked: { grid.filterRows("rtl"); grid.forceActiveFocus() } }
            Button { text: "Empty results"; onClicked: { grid.filterRows("empty"); grid.forceActiveFocus() } }
            Button { text: "Show / hide fixture tags"; onClicked: { grid.toggleTags(); grid.forceActiveFocus() } }
        }
        Label { text: grid.stateText; color: "#acd8fe"; wrapMode: Text.Wrap; Layout.fillWidth: true; Accessible.name: text }
        PaintedGrid { id: grid; objectName: "probeGrid"; Layout.fillWidth: true; Layout.fillHeight: true; focus: true }
        Label { text: "Arrows / Home / End / Page keys: current + selection · Ctrl: current only · Shift: range · Ctrl-click: toggle · Wheel: scroll"; color: "#bdc5d3"; wrapMode: Text.Wrap; Layout.fillWidth: true }
        Label { text: "Read-only sample. Selection policies and announcement wording are proposals. Raw ASS is retained; tag removal is fixture-only."; color: "#e5be7f"; wrapMode: Text.Wrap; Layout.fillWidth: true }
    }
}
