import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Native throwaway source-map probe. The product's draft/command policy is not settled here.
ApplicationWindow {
    id: root
    width: 1280; height: 940
    minimumWidth: 1000; minimumHeight: 790
    visible: true
    color: "#171b20"
    title: "HikariSub · Editable hidden ASS tags / native prototype"
    property int initialSample: 0
    property bool ready: false
    property var snapshot: bridge.state
    palette.window: "#171b20"
    palette.windowText: "#e8edf2"
    palette.base: "#171d24"
    palette.text: "#e8edf2"
    palette.button: "#29313a"
    palette.buttonText: "#e8edf2"
    palette.highlight: "#43685d"
    palette.highlightedText: "#ffffff"
    font.pixelSize: 12

    function composition() {
        if (ready) bridge.composition(raw.inputMethodComposing || clean.inputMethodComposing,
                                      raw.preeditText || clean.preeditText)
    }
    function keys(event, field) {
        // Candidate confirmation and IME-owned shortcuts stay with the native input item.
        if (field.inputMethodComposing) { event.accepted = false; return }
        if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_Z) {
            if (event.modifiers & Qt.ShiftModifier) bridge.redo(); else bridge.undo()
            event.accepted = true; return
        }
        if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_Y) {
            bridge.redo(); event.accepted = true; return
        }
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            bridge.commitNext(); event.accepted = true
        }
    }
    Component.onCompleted: Qt.callLater(function() {
        bridge.attach(raw.textDocument, clean.textDocument, raw, clean)
        ready = true
        clean.forceActiveFocus()
    })
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 16; spacing: 9
        RowLayout {
            Label { text: "H"; font.bold: true; color: "#9cdbc9"; font.pixelSize: 23 }
            ColumnLayout {
                spacing: 2
                Label { text: "EDITABLE HIDDEN TAGS / NATIVE QML"; color: "#9cdbc9"; font.bold: true; font.pixelSize: 11 }
                Label { text: "Edit visible text. Keep the ASS source."; font.bold: true; font.pixelSize: 23 }
            }
            Item { Layout.fillWidth: true }
            Label { text: "THROWAWAY · MEMORY ONLY"; color: "#a5b1bd"; font.pixelSize: 11 }
        }
        Label {
            Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#bac5cd"
            text: "Raw ASS is authoritative. Both editors use native text input. Hidden-tag edits map selected visible spans back to source; they never strip and regenerate tags. Boundary and cross-tag policies below are proposals."
        }
        RowLayout {
            Label { text: "Fixture" }
            ComboBox {
                id: fixture; model: bridge.samples; currentIndex: root.initialSample
                Layout.preferredWidth: 205; enabled: !root.snapshot.composing && !root.snapshot.pending
                onActivated: { bridge.fixture(currentIndex); clean.forceActiveFocus() }
            }
            Label { text: "Insert at hidden tags" }
            ComboBox {
                model: ["Before tags", "After tags"]; currentIndex: root.snapshot.affinity === "after" ? 1 : 0
                Layout.preferredWidth: 145; enabled: !root.snapshot.composing && !root.snapshot.pending
                onActivated: bridge.policy(currentIndex === 0 ? "before" : "after", root.snapshot.crossing)
            }
            Label { text: "Across hidden tags" }
            ComboBox {
                model: ["Retain exact tags", "Block cross-tag edit"]; currentIndex: root.snapshot.crossing === "block" ? 1 : 0
                Layout.preferredWidth: 180; enabled: !root.snapshot.composing && !root.snapshot.pending
                onActivated: bridge.policy(root.snapshot.affinity, currentIndex === 0 ? "retain" : "block")
            }
            Item { Layout.fillWidth: true }
        }
        RowLayout {
            Layout.fillWidth: true
            Button { text: "Boundary demo: A | B → insert X"; enabled: !root.snapshot.composing && !root.snapshot.pending; onClicked: bridge.boundaryDemo() }
            Button { text: "Cross-tag demo: ABC → X"; enabled: !root.snapshot.composing && !root.snapshot.pending; onClicked: bridge.crossingDemo() }
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#e0c99d"; font.pixelSize: 11; text: "Retain collapses intervening tags after replacement text. Compare source; styling attachment is not settled." }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.preferredHeight: 235
            ColumnLayout {
                Layout.fillWidth: true; Layout.preferredWidth: 1
                RowLayout {
                    Label { text: "RAW ASS / authoritative editable source"; font.bold: true; color: "#c3afd9" }
                    Item { Layout.fillWidth: true }
                    Label { text: root.snapshot.rawUtf16 + " UTF-16"; color: "#a5b1bd"; font.pixelSize: 10 }
                }
                ScrollView {
                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                    TextArea {
                        id: raw; objectName: "rawEditor"
                        textFormat: TextEdit.PlainText; wrapMode: TextEdit.Wrap
                        selectByMouse: true; persistentSelection: true; padding: 12
                        font.family: "Segoe UI"; font.pixelSize: 20; color: "#e8edf2"
                        readOnly: clean.inputMethodComposing || root.snapshot.pending
                        background: Rectangle { color: "#1f242c"; border.color: raw.activeFocus ? "#b9a4eb" : "#414b57"; border.width: raw.activeFocus ? 2 : 1 }
                        Accessible.name: "Raw ASS source, editable"
                        Accessible.description: "Override tags are visible. Enter commits and advances outside composition."
                        onInputMethodComposingChanged: root.composition()
                        onPreeditTextChanged: root.composition()
                        Keys.priority: Keys.BeforeItem
                        Keys.onPressed: function(event) { root.keys(event, raw) }
                    }
                }
                Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#a5b1bd"; font.pixelSize: 11; text: "Raw edits may intentionally change tags. Highlighting is separate from source edits." }
            }
            ColumnLayout {
                Layout.fillWidth: true; Layout.preferredWidth: 1
                RowLayout {
                    Label { text: "HIDDEN TAGS / editable mapped view"; font.bold: true; color: "#9cdbc9" }
                    Item { Layout.fillWidth: true }
                    Label { text: root.snapshot.cleanUtf16 + " UTF-16"; color: "#a5b1bd"; font.pixelSize: 10 }
                }
                ScrollView {
                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                    TextArea {
                        id: clean; objectName: "cleanEditor"
                        textFormat: TextEdit.PlainText; wrapMode: TextEdit.Wrap
                        selectByMouse: true; persistentSelection: true; padding: 12
                        font.family: "Segoe UI"; font.pixelSize: 23; color: "#e8edf2"
                        readOnly: raw.inputMethodComposing || (root.snapshot.pending && !inputMethodComposing)
                        background: Rectangle { color: "#172722"; border.color: clean.activeFocus ? "#9cdbc9" : "#414b57"; border.width: clean.activeFocus ? 2 : 1 }
                        Accessible.name: "Hidden-tag subtitle text, editable"
                        Accessible.description: "Text edits preserve hidden ASS tokens. Object markers protect drawing or malformed source."
                        onInputMethodComposingChanged: root.composition()
                        onPreeditTextChanged: root.composition()
                        Keys.priority: Keys.BeforeItem
                        Keys.onPressed: function(event) { root.keys(event, clean) }
                    }
                }
                Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#a5b1bd"; font.pixelSize: 11; text: "Square □ = protected drawing/malformed source. Soft break is space; hard space remains NBSP." }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Button { text: "Undo  Ctrl+Z"; enabled: root.snapshot.undo > 0 && !root.snapshot.composing && !root.snapshot.pending; onClicked: bridge.undo() }
            Button { text: "Redo  Ctrl+Y"; enabled: root.snapshot.redo > 0 && !root.snapshot.composing && !root.snapshot.pending; onClicked: bridge.redo() }
            Button { text: "Commit + next  Enter"; enabled: !root.snapshot.composing && !root.snapshot.pending; onClicked: bridge.commitNext() }
            Button { text: "Undo committed Line"; enabled: root.snapshot.commits > 0 && !root.snapshot.composing && !root.snapshot.pending; onClicked: bridge.undoCommit() }
            Button { text: "teh → the"; enabled: !root.snapshot.composing && !root.snapshot.pending; onClicked: bridge.spell() }
            Item { Layout.fillWidth: true }
            Label { text: "Line " + root.snapshot.line + " / " + root.snapshot.lineCount; color: "#9cdbc9" }
        }
        RowLayout {
            Layout.fillWidth: true
            Label { text: "Selection paste sample" }
            TextField { id: pasteSample; text: "日本語 + text"; Layout.fillWidth: true; Accessible.name: "Sample text to paste into hidden-tag selection" }
            Button {
                text: "Replace hidden-view selection"; enabled: !root.snapshot.composing && !root.snapshot.pending
                onClicked: bridge.mappedInsert(clean.selectionStart, clean.selectionEnd, pasteSample.text)
            }
            Button {
                text: "Insert hard break"; enabled: !root.snapshot.composing && !root.snapshot.pending
                onClicked: bridge.mappedInsert(clean.selectionStart, clean.selectionEnd, "\n")
            }
        }
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 83; color: "#23362e"; border.color: "#426455"
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 9; spacing: 4
                Label {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#c7e4d8"
                    text: root.snapshot.notice
                }
                Label {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#b1c7be"; font.pixelSize: 11
                    text: "STATE  revision " + root.snapshot.revision + " · undo " + root.snapshot.undo + " · redo " + root.snapshot.redo
                          + " · clean selection " + clean.selectionStart + "–" + clean.selectionEnd
                          + " · raw selection " + raw.selectionStart + "–" + raw.selectionEnd
                          + " · composing " + root.snapshot.composing + " · preedit “" + root.snapshot.preedit + "”"
                }
            }
        }
        Label {
            Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#e6c68d"; font.pixelSize: 11
            text: root.snapshot.warnings || "Ordinary edits preserve untouched tokens, including karaoke timing. Brace/backslash paste and opaque edits are rejected with recovery text."
        }
        TabBar {
            id: detailTabs; Layout.fillWidth: true
            TabButton { text: "UTF-16 source / display map" }
            TabButton { text: "History and document state" }
            TabButton { text: "Retained rejected draft" }
        }
        ScrollView {
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 105; clip: true
            TextArea {
                text: detailTabs.currentIndex === 0 ? root.snapshot.mapJson : detailTabs.currentIndex === 1 ? root.snapshot.summary : (root.snapshot.rejected || "No rejected draft. This area retains attempted text if an ambiguous edit is refused.")
                readOnly: true; selectByMouse: true; textFormat: TextEdit.PlainText; wrapMode: TextEdit.Wrap
                font.family: "Consolas"; font.pixelSize: 11; color: "#bdc8d2"; padding: 8
                background: Rectangle { color: "#12191f"; border.color: "#414b57" }
                Accessible.name: "Mapping and operation evidence, read only"
            }
        }
        Label {
            Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#a5b1bd"; font.pixelSize: 11
            text: "REVIEW  Before or after boundary tags? Retain or block cross-tag replacement? Native platform CJK candidate/Enter, RTL selection, screen readers and production undo/command policy remain unqualified."
        }
    }
}
