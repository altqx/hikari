pragma Translator: "HikariSub.Translation"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Flow {
    id: strip
    required property var editor
    required property var shellRoot
    required property Item translated
    objectName: "runStrip"
    readonly property bool shown: editor.translationMode && editor.tagRuns.length > 0
    visible: shown
    spacing: 4

    Repeater {
        model: strip.shown ? strip.editor.tagRuns : []
        delegate: AbstractButton {
            id: chip
            required property var modelData
            readonly property string symbol: modelData.number <= 9 ? "EDITBOX_PLACE_RUN_" + modelData.number : ""
            objectName: "runChip" + modelData.number
            focusPolicy: Qt.NoFocus
            enabled: strip.editor.editable
            hoverEnabled: true
            padding: 3
            leftPadding: 4
            rightPadding: 6
            Accessible.name: qsTr("Run %1: %2, %3").arg(modelData.number).arg(modelData.text).arg(modelData.kind)
            Accessible.description: modelData.placed ? qsTr("Placed") : qsTr("Not placed")
            ToolTip {
                id: tip
                objectName: "runChipToolTip" + chip.modelData.number
                visible: chip.hovered
                text: {
                    const keys = chip.symbol ? strip.shellRoot.boundKeys(chip.symbol, 2) : ""
                    const how = keys.length ? qsTr("Select text in the translation, then press %1 or click.").arg(keys)
                                            : qsTr("Select text in the translation, then click.")
                    return qsTr("Run %1: %2 (%3)").arg(chip.modelData.number).arg(chip.modelData.text).arg(chip.modelData.kind)
                           + "\n" + how
                }
                // Qt's AutoText would render the subtitle's markup and fetch any <img>.
                contentItem: Label {
                    text: tip.text
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    color: tip.palette.toolTipText
                }
            }
            onClicked: {
                if (symbol && strip.shellRoot.hotkeyGesture(symbol, 2, true))
                    return
                strip.editor.placeRun(modelData.number, strip.translated.selectionStart, strip.translated.selectionEnd)
            }
            background: Rectangle {
                radius: 3
                color: chip.modelData.placed ? Qt.alpha(Theme.accent, chip.hovered ? 0.3 : 0.2)
                                             : (chip.hovered ? Theme.raised : "transparent")
                border.width: 1
                border.color: chip.modelData.placed ? Theme.accent : Theme.line
            }
            contentItem: RowLayout {
                spacing: 4
                Rectangle {
                    objectName: "runChipNumber"
                    implicitWidth: Math.max(number.implicitWidth + 6, height)
                    implicitHeight: number.implicitHeight + 2
                    radius: height / 2
                    color: chip.modelData.placed ? Theme.accent : "transparent"
                    border.width: 1
                    border.color: Theme.accent
                    Label {
                        id: number
                        anchors.centerIn: parent
                        text: chip.modelData.number
                        font.pixelSize: chipText.font.pixelSize - 2
                        color: chip.modelData.placed ? Theme.onAccent : Theme.text
                    }
                }
                Rectangle {
                    visible: chip.modelData.colour.length > 0
                    implicitWidth: 10
                    implicitHeight: 10
                    color: chip.modelData.colour || "transparent"
                    border.width: 1
                    border.color: Theme.line
                }
                Label {
                    id: chipText
                    objectName: "runChipText" + chip.modelData.number
                    text: chip.modelData.text
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    Layout.maximumWidth: 180
                    font.italic: chip.modelData.italic
                    font.bold: chip.modelData.bold
                    font.underline: chip.modelData.underline
                    font.strikeout: chip.modelData.strikeout
                    color: Theme.text
                }
                Label {
                    text: chip.modelData.kind
                    color: Theme.muted
                    font.pixelSize: chipText.font.pixelSize - 1
                }
            }
        }
    }
}
