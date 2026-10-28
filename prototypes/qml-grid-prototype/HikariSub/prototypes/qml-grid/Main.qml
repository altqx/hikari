import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HikariPrototype 1.0

ApplicationWindow {
    id: root
    objectName: "gridWindow"
    visible: true
    width: 1420
    height: 860
    minimumWidth: 1060
    minimumHeight: 600
    color: "#111923"
    title: "HikariSub · 50k grid prototype · #27"
    property int variant: 0
    property var widths: [72, 112, 112, 112, 110, Math.max(300, width - 554)]
    palette.windowText: "#e3e9f1"
    palette.text: "#e3e9f1"
    palette.base: "#1b2938"
    palette.button: "#24374a"
    palette.buttonText: "#e3e9f1"
    palette.highlight: "#3986a4"
    Rectangle { anchors.fill: parent; color: "#111923" }

    function reveal(row) {
        if (variant === 0) table.positionViewAtRow(row, TableView.Contain)
        else paintedScroll.contentY = Math.max(0, Math.min(row * 30, paintedScroll.contentHeight - paintedScroll.height))
    }
    Connections {
        target: gridModel
        function onVisible(row) { root.reveal(row) }
    }
    Connections {
        target: probe
        function onScroll(y) {
            if (root.variant === 0) table.contentY = y
            else paintedScroll.contentY = y
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 82
            color: "#1b2938"
            Column {
                anchors.left: parent.left; anchors.leftMargin: 18
                anchors.verticalCenter: parent.verticalCenter
                spacing: 5
                Label { text: gridModel.total.toLocaleString() + " lines. One grid decision."; font.pixelSize: 25; font.bold: true }
                Label { text: "THROWAWAY #27 · Synthetic ASS · TableView / painted viewport · Human review pending"; color: "#a8b9ca" }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            ComboBox {
                id: variants
                model: ["A · TableView", "B · Painted viewport"]
                currentIndex: root.variant
                enabled: !probe.busy
                onActivated: { root.variant = currentIndex; gridFocus.forceActiveFocus() }
                Accessible.name: "Grid renderer"
                Layout.preferredWidth: 190
            }
            CheckBox { text: "Hide ASS tags"; enabled: !probe.busy; onToggled: gridModel.hideTags(checked) }
            TextField {
                id: search
                placeholderText: "Filter text, style or state (try RTL)"
                Layout.fillWidth: true
                enabled: !probe.busy
                onTextChanged: filterDelay.restart()
                Accessible.name: "Filter subtitle rows"
                Timer { id: filterDelay; interval: 180; onTriggered: gridModel.arrange(search.text, sorter.currentIndex) }
            }
            ComboBox {
                id: sorter
                model: ["Source order", "Reverse order", "Group by style"]
                enabled: !probe.busy
                onActivated: gridModel.arrange(search.text, currentIndex)
                Accessible.name: "Sort rows"
            }
            Button { text: "Run 5 s scroll probe"; enabled: !probe.busy; onClicked: probe.start(root.variant === 0 ? "TableView" : "Painted") }
        }
        Label { Layout.leftMargin: 16; Layout.bottomMargin: 8; text: gridModel.summary; color: "#b2d4e1"; Accessible.name: text }
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 30; color: "#334255"
            Row {
                Repeater {
                    model: ["ID", "Start", "End", "Style", "State", "Subtitle text"]
                    Label { required property int index; required property string modelData; width: root.widths[index]; height: 30; leftPadding: 9; verticalAlignment: Text.AlignVCenter; text: modelData; font.bold: true }
                }
            }
        }
        FocusScope {
            id: gridFocus
            objectName: "gridFocus"
            Layout.fillWidth: true; Layout.fillHeight: true
            focus: true
            Keys.onPressed: function(event) {
                var step = event.key === Qt.Key_Down ? 1 : event.key === Qt.Key_Up ? -1 : event.key === Qt.Key_PageDown ? 20 : event.key === Qt.Key_PageUp ? -20 : 0
                if (step !== 0) { gridModel.move(step, event.modifiers); event.accepted = true }
                else if (event.key === Qt.Key_Home) { gridModel.boundary(false, event.modifiers); event.accepted = true }
                else if (event.key === Qt.Key_End) { gridModel.boundary(true, event.modifiers); event.accepted = true }
            }
            TableView {
                id: table
                anchors.fill: parent
                visible: root.variant === 0
                clip: true
                model: gridModel
                reuseItems: true
                columnWidthProvider: function(c) { return root.widths[c] }
                rowHeightProvider: function(r) { return 30 }
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                Accessible.role: Accessible.Table
                Accessible.name: "Subtitle TableView. Arrow keys select rows; Shift extends selection."
                delegate: Rectangle {
                    id: cell
                    required property int row
                    required property int column
                    required property string display
                    required property bool chosen
                    required property bool currentLine
                    required property string rowColor
                    required property int sourceId
                    implicitWidth: root.widths[column]; implicitHeight: 30
                    color: chosen ? "#245571" : rowColor
                    border.color: currentLine ? "#8ed4eb" : "#35414d"
                    border.width: 1
                    Component.onCompleted: probe.delegate(1)
                    Component.onDestruction: probe.delegate(-1)
                    TableView.onReused: probe.delegate(0)
                    Accessible.role: Accessible.Cell
                    Accessible.name: "Line " + sourceId + ", " + ["ID", "start", "end", "style", "state", "text"][column] + ": " + display
                    Accessible.selectable: true
                    Accessible.selected: chosen
                    Accessible.focusable: true
                    Accessible.focused: currentLine && column === 5 && gridFocus.activeFocus
                    Text { anchors.fill: parent; anchors.leftMargin: 9; anchors.rightMargin: 9; verticalAlignment: Text.AlignVCenter; text: cell.display; color: "#e3e9f1"; font.family: "Segoe UI"; font.pixelSize: 13; elide: Text.ElideRight; textFormat: Text.PlainText; Accessible.ignored: true }
                    MouseArea { anchors.fill: parent; onClicked: function(mouse) { gridFocus.forceActiveFocus(); gridModel.select(cell.row, mouse.modifiers) } }
                }
            }
            Flickable {
                id: paintedScroll
                anchors.fill: parent
                visible: root.variant === 1
                clip: true
                contentWidth: width
                contentHeight: gridModel.count * 30
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                PaintedGrid { width: paintedScroll.width; height: paintedScroll.height; y: paintedScroll.contentY; offset: paintedScroll.contentY }
                MouseArea {
                    width: paintedScroll.width; height: paintedScroll.height; y: paintedScroll.contentY
                    onClicked: function(mouse) { gridFocus.forceActiveFocus(); gridModel.select(Math.floor((mouse.y + paintedScroll.contentY) / 30), mouse.modifiers) }
                    onWheel: function(wheel) { paintedScroll.contentY = Math.max(0, Math.min(paintedScroll.contentY - wheel.angleDelta.y, paintedScroll.contentHeight - paintedScroll.height)) }
                }
                Accessible.role: Accessible.Pane
                Accessible.name: "Painted grid prototype. Rows lack a native accessible table adapter. Use the current-line inspector below."
            }
        }
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 115; color: "#1b2938"
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 12; spacing: 6
                Label { text: "Current line · original ASS source · selected IDs survive sorting and hidden filters"; color: "#8ed4eb" }
                TextArea { Layout.fillWidth: true; Layout.fillHeight: true; readOnly: true; text: gridModel.currentText; wrapMode: TextEdit.NoWrap; selectByMouse: true; Accessible.name: "Current subtitle source" }
            }
        }
        Label { Layout.fillWidth: true; Layout.margins: 12; text: probe.status; wrapMode: Text.WordWrap; color: "#d7c695" }
        Label { Layout.fillWidth: true; Layout.leftMargin: 12; Layout.bottomMargin: 10; text: "Click / Ctrl-click / Shift-click · ↑ ↓ / PageUp PageDown · Home / End · Colors also have text labels · Screen-reader behavior unverified"; color: "#98aabd"; font.pixelSize: 12 }
    }
}
