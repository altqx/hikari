// TM1 (#232) tags from the original: the Original text's runs under the
// Translated text, one chip each (LineEditorController::tagRuns). A placed
// run's chip is filled, an unplaced one only outlined. A click, its Alt+n
// (EDITBOX_PLACE_RUN_n) or Ctrl+B/I/U for its style places it on the
// Translated text's selection; on the same selection again it comes off.
// The chips take no focus, so the selection stays.
pragma Translator: "HikariSub.Translation"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Flow {
    id: strip
    required property var editor
    // Main's root: hotkeyGesture (Shift+click maps) and boundKeys.
    required property var shellRoot
    // The Translated text field, whose selection a chip places a run on.
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
            readonly property string symbol: "EDITBOX_PLACE_RUN_" + modelData.number
            objectName: "runChip" + modelData.number
            focusPolicy: Qt.NoFocus
            enabled: strip.editor.editable
            hoverEnabled: true
            padding: 3
            leftPadding: 4
            rightPadding: 6
            Accessible.name: qsTr("Run %1: %2, %3").arg(modelData.number).arg(modelData.text).arg(modelData.kind)
            Accessible.description: modelData.placed ? qsTr("Placed") : qsTr("Not placed")
            ToolTip.visible: hovered
            ToolTip.delay: 500
            ToolTip.text: {
                const keys = strip.shellRoot.boundKeys(symbol, 2)
                const how = keys.length ? qsTr("Select text in the translation, then press %1 or click.").arg(keys)
                                        : qsTr("Select text in the translation, then click.")
                return qsTr("Run %1: %2 (%3)").arg(modelData.number).arg(modelData.text).arg(modelData.kind) + "\n" + how
            }
            onClicked: {
                if (strip.shellRoot.hotkeyGesture(symbol, 2, true))
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
                    text: chip.modelData.text
                    elide: Text.ElideRight
                    Layout.maximumWidth: 180
                    font.italic: chip.modelData.kind.indexOf(qsTr("Italic")) >= 0
                    font.bold: chip.modelData.kind.indexOf(qsTr("Bold")) >= 0
                    font.underline: chip.modelData.kind.indexOf(qsTr("Underline")) >= 0
                    font.strikeout: chip.modelData.kind.indexOf(qsTr("Strikeout")) >= 0
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
