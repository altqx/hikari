import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Throwaway native editing probe, not production UI or a complete ASS parser.
ApplicationWindow {
    id: root
    width: 1180; height: 900
    minimumWidth: 960; minimumHeight: 740
    visible: true
    title: "HikariSub · ASS editor prototype #28"
    color: "#f3f2f7"
    property bool ready: false
    property var probeState: ({})
    property int initialSample: 0
    property int initialUnderline: 0
    property int initialView: 0
    property string currentMode: "Raw + preview"
    function inspect() {
        if (ready)
            probeState = bridge.inspect(raw.text, raw.cursorPosition, raw.selectionStart, raw.selectionEnd,
                           raw.inputMethodComposing, raw.preeditText)
    }
    function commitLine() {
        bridge.commit(raw.text, raw.inputMethodComposing)
        inspect()
    }
    Component.onCompleted: {
        raw.text = bridge.sample(initialSample)
        currentMode = ["Raw + preview", "Translation", "Offset map"][initialView]
        Qt.callLater(function() {
            bridge.attach(raw.textDocument)
            bridge.underline(initialUnderline)
            ready = true
            inspect()
            raw.forceActiveFocus()
        })
    }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 24; spacing: 12
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 2
                Label { text: "ASS EDITOR / NATIVE QT SPIKE"; color: "#715198"; font.pixelSize: 12; font.bold: true }
                Label { text: "Keep the source. Trust native input."; color: "#292238"; font.pixelSize: 27; font.bold: true }
            }
            Item { Layout.fillWidth: true }
            Label { text: "THROWAWAY · IN MEMORY"; color: "#806245"; font.pixelSize: 12 }
        }
        Label {
            Layout.fillWidth: true; wrapMode: Text.WordWrap
            text: "Try highlighting, a spelling replacement, mixed scripts and clean-text selection. Native IME and screen readers still need your hands-on check."
            color: "#625b6d"
        }
        RowLayout {
            Layout.fillWidth: true
            Label { text: "Fixture"; color: "#54475f" }
            ComboBox {
                id: fixture; model: bridge.samples; Layout.preferredWidth: 235
                currentIndex: root.initialSample
                enabled: !raw.inputMethodComposing
                onActivated: { raw.text = bridge.sample(currentIndex); root.inspect(); raw.forceActiveFocus() }
            }
            Label { text: "Underline"; color: "#54475f" }
            ComboBox {
                model: ["Single", "Wave requested", "SpellCheck requested"]; Layout.preferredWidth: 185
                currentIndex: root.initialUnderline
                onActivated: { bridge.underline(currentIndex); raw.forceActiveFocus() }
            }
            Item { Layout.fillWidth: true }
            ComboBox {
                model: ["Raw + preview", "Translation", "Offset map"]; Layout.preferredWidth: 175
                currentIndex: root.initialView
                onActivated: root.currentMode = currentText
            }
        }
        Rectangle {
            visible: root.currentMode === "Translation"
            Layout.fillWidth: true; Layout.preferredHeight: 75
            radius: 7; color: "#e9e6ef"
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 10; spacing: 3
                Label { text: "ORIGINAL / read only"; color: "#71657f"; font.pixelSize: 11 }
                TextArea {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    text: "The night train is leaving. Keep the original independent of the translation draft."
                    readOnly: true; selectByMouse: true; background: null; padding: 0; font.pixelSize: 15
                    Accessible.name: "Original subtitle, read only"
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Label { text: root.currentMode === "Translation" ? "TRANSLATION / authoritative raw ASS" : "RAW ASS / authoritative source"; font.bold: true; color: "#51445f"; font.pixelSize: 12 }
            Item { Layout.fillWidth: true }
            Label { text: raw.inputMethodComposing ? "● IME COMPOSING — commit guarded" : "IME idle"; color: raw.inputMethodComposing ? "#a85320" : "#777080"; font.pixelSize: 12 }
        }
        ScrollView {
            Layout.fillWidth: true; Layout.preferredHeight: 168
            TextArea {
                id: raw; objectName: "rawAssEditor"
                textFormat: TextEdit.PlainText; wrapMode: TextEdit.Wrap
                selectByMouse: true; persistentSelection: true
                font.family: "Segoe UI"; font.pixelSize: 22; color: "#30263b"
                padding: 15
                background: Rectangle { color: "white"; border.color: raw.activeFocus ? "#9674c5" : "#d6cede"; border.width: raw.activeFocus ? 2 : 1; radius: 8 }
                Accessible.name: "Raw ASS subtitle editor"
                Accessible.description: "Source text including override tags. Control Enter commits when not composing."
                onTextChanged: root.inspect()
                onCursorPositionChanged: root.inspect()
                onSelectionStartChanged: root.inspect()
                onSelectionEndChanged: root.inspect()
                onInputMethodComposingChanged: root.inspect()
                onPreeditTextChanged: root.inspect()
                Keys.priority: Keys.BeforeItem
                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                        if (raw.inputMethodComposing) { event.accepted = false; return }
                        if (event.modifiers & Qt.ControlModifier) { root.commitLine(); event.accepted = true }
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Button { text: "Undo draft"; enabled: raw.canUndo && !raw.inputMethodComposing; onClicked: { raw.undo(); raw.forceActiveFocus() } }
            Button { text: "Redo draft"; enabled: raw.canRedo && !raw.inputMethodComposing; onClicked: { raw.redo(); raw.forceActiveFocus() } }
            Button { text: "Commit line  Ctrl+Enter"; enabled: !raw.inputMethodComposing; onClicked: root.commitLine() }
            Button { text: "Undo committed line"; enabled: root.probeState.commits > 0 && !raw.inputMethodComposing; onClicked: { raw.text = bridge.undoCommit(); root.inspect() } }
            Item { Layout.fillWidth: true }
            Label { text: "draft undo ≠ document undo"; color: "#80748b"; font.pixelSize: 11 }
        }
        RowLayout {
            Layout.fillWidth: true
            Label { text: "SPELLING DEMO"; color: "#9c4255"; font.bold: true; font.pixelSize: 11 }
            Button {
                text: "teh → the"
                enabled: raw.text.indexOf("teh") >= 0 && !raw.inputMethodComposing
                onClicked: { bridge.fixWord("teh"); raw.forceActiveFocus(); root.inspect() }
            }
            Button {
                text: "subtitel → subtitle"
                enabled: raw.text.indexOf("subtitel") >= 0 && !raw.inputMethodComposing
                onClicked: { bridge.fixWord("subtitel"); raw.forceActiveFocus(); root.inspect() }
            }
            Label { text: "Two sample words; no Hunspell dictionary loaded."; color: "#80748b"; font.pixelSize: 11 }
            Item { Layout.fillWidth: true }
        }
        RowLayout {
            Layout.fillWidth: true
            Label { text: root.currentMode === "Offset map" ? "SOURCE MAP / UTF-16 offsets" : "CLEAN PROJECTION / read only, tags and drawings hidden"; font.bold: true; color: "#51445f"; font.pixelSize: 12 }
            Item { Layout.fillWidth: true }
            Button {
                text: "Locate preview selection in raw"; visible: root.currentMode !== "Offset map"
                enabled: !raw.inputMethodComposing
                onClicked: { const pos = bridge.locate(clean.selectionStart, clean.selectionEnd); raw.select(pos.start, pos.end); raw.forceActiveFocus() }
            }
        }
        ScrollView {
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 115
            TextArea {
                id: clean; objectName: "cleanProjection"
                text: root.currentMode === "Offset map" ? (root.probeState.mapJson || "") : (root.probeState.clean || "")
                textFormat: TextEdit.PlainText; wrapMode: TextEdit.Wrap
                readOnly: true; selectByMouse: true; persistentSelection: true
                font.family: root.currentMode === "Offset map" ? "Consolas" : "Segoe UI"
                font.pixelSize: root.currentMode === "Offset map" ? 12 : 23; color: "#302b37"; padding: 14
                background: Rectangle { color: "#eae7ef"; radius: 8 }
                Accessible.name: root.currentMode === "Offset map" ? "Source offset map" : "Clean subtitle projection, read only"
            }
        }
        Label {
            Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#9a5629"; font.pixelSize: 12
            text: root.probeState.problemsText || ""
            visible: text.length > 0
        }
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 71; color: "#e2dbe9"; radius: 7
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 10; spacing: 4
                Label {
                    Layout.fillWidth: true; color: "#44364f"; font.pixelSize: 12
                    text: "STATE   UTF-16: " + (root.probeState.utf16Length || 0) + "   Codepoints: " + (root.probeState.codepoints || 0)
                          + "   Caret: " + raw.cursorPosition + "   Selection: " + raw.selectionStart + "–" + raw.selectionEnd
                          + "   Commits: " + (root.probeState.commits || 0) + "   Preedit: “" + raw.preeditText + "”"
                }
                Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: bridge.notice; color: "#685474"; font.pixelSize: 12 }
            }
        }
        Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: "REVIEW  Does raw + clean help? Do tag-adjacent selections make sense? Does one-line commit feel right? Try Japanese / Chinese / Korean IME before trusting Enter handling."; color: "#80718c"; font.pixelSize: 11 }
    }
}
