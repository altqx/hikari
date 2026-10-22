pragma Translator: "HikariSub.VisualTools"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

// HY1 (#271): Hydra's last change card, over the video's top-right corner.
// After each change the tool applied it keeps that change's values: a value
// typed here (Enter) undoes the change's step and does the change again
// with it, on the same Lines, so the history still has one step for it.
// Another edit of the Document, an undo, or another tool, tag or change
// option closes it, as does ✕.
Rectangle {
    id: card
    objectName: "visualLastChange"
    required property VisualToolsController tools
    readonly property var last: tools.lastChange
    readonly property bool hasChange: (last.values ?? []).length > 0
    visible: hasChange
    implicitWidth: Math.max(268, content.implicitWidth + 20)
    implicitHeight: content.implicitHeight + 16
    color: Theme.panel
    border.color: Theme.line
    radius: 4

    readonly property var changeNames: [qsTr("Set"), qsTr("Add"), qsTr("Multiply"), qsTr("Gradient")]
    readonly property var labelNames: ({
        "Value": qsTr("Value"),
        "By": qsTr("By"),
        "Start": qsTr("Start"),
        "Per line": qsTr("Per line"),
        "From": qsTr("From"),
        "To": qsTr("To"),
        "In": qsTr("In"),
        "Out": qsTr("Out"),
        "End": qsTr("End")
    })
    function nameOf(text) {
        return labelNames[text] ?? text
    }
    // The fields are made again only when the values' names change, not on
    // each refresh (a pointer move): typed text stays until Enter or Esc.
    readonly property string shape: (last.values ?? []).map(v => v.name).join("\u0001")
    readonly property var names: shape.length > 0 ? shape.split("\u0001") : []
    function valueNamed(name) {
        for (const v of last.values ?? [])
            if (v.name === name)
                return v
        return { name: name, label: "", group: "", text: "", swatch: "" }
    }

    // The card takes its own clicks: none reaches the video below.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: wheel => wheel.accepted = true
    }

    ColumnLayout {
        id: content
        anchors.fill: parent
        anchors.margins: 8
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 6
        RowLayout {
            spacing: 8
            Label {
                text: qsTr("Last change")
                font.bold: true
            }
            Label {
                objectName: "visualLastChangeSummary"
                Layout.fillWidth: true
                text: [card.last.tag ?? "", card.changeNames[card.last.change ?? 0] ?? "",
                       (card.last.lines ?? 1) > 1 ? qsTr("%n lines", "", card.last.lines) : qsTr("1 line")].join(" · ")
                color: Theme.muted
                elide: Text.ElideRight
            }
            IconToolButton {
                objectName: "visualLastChangeClose"
                iconRole: "clear"
                text: qsTr("Close")
                focusPolicy: Qt.NoFocus
                onClicked: card.tools.dismissLastChange()
            }
        }
        Flow {
            Layout.fillWidth: true
            spacing: 8
            Repeater {
                model: card.names
                delegate: RowLayout {
                    id: entry
                    required property string modelData
                    readonly property var value: card.valueNamed(modelData)
                    spacing: 4
                    Rectangle {
                        visible: (entry.value.swatch ?? "").length > 0
                        Layout.preferredWidth: 22
                        Layout.preferredHeight: 22
                        radius: 2
                        color: visible ? entry.value.swatch : "transparent"
                        border.color: Theme.control
                    }
                    Label {
                        text: [entry.value.group, entry.value.label].filter(s => s.length > 0)
                                  .map(s => card.nameOf(s)).join(" ")
                        color: Theme.muted
                    }
                    TextField {
                        id: input
                        objectName: "visualLastChange_" + entry.value.name
                        property bool editing: false
                        implicitHeight: 24
                        implicitWidth: 64
                        topPadding: 0
                        bottomPadding: 0
                        selectByMouse: true
                        horizontalAlignment: TextInput.AlignRight
                        font.features: { "tnum": 1 }
                        enabled: card.tools.railEnabled && !card.tools.gestureActive
                        Accessible.name: qsTr("Last change") + " " + card.nameOf(entry.value.label)
                        Component.onCompleted: text = entry.value.text
                        Connections {
                            target: entry
                            function onValueChanged() {
                                if (!input.editing)
                                    input.text = entry.value.text
                            }
                        }
                        onTextEdited: editing = true
                        onAccepted: {
                            editing = false
                            card.tools.editLastChange(entry.value.name, text)
                            text = entry.value.text
                        }
                        Keys.onShortcutOverride: event => {
                            const plain = (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)) === 0
                            event.accepted = event.key === Qt.Key_Escape ? editing
                                : plain && ([Qt.Key_Left, Qt.Key_Right, Qt.Key_Home, Qt.Key_End, Qt.Key_Backspace,
                                             Qt.Key_Delete, Qt.Key_Return, Qt.Key_Enter].includes(event.key)
                                            || (event.text.length > 0 && event.text >= " "))
                        }
                        Keys.onEscapePressed: {
                            editing = false
                            text = entry.value.text
                        }
                        onActiveFocusChanged: if (!activeFocus && editing) {
                            editing = false
                            text = entry.value.text
                        }
                    }
                }
            }
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("Change a value to redo it in place of that step")
            color: Theme.muted
            font.pixelSize: Math.max(10, Qt.application.font.pixelSize - 1)
            wrapMode: Text.WordWrap
        }
    }
}
