pragma Translator: "HikariSub.VisualTools"
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Popover {
    id: guide
    objectName: "visualToolGuide"
    required property VisualToolsController tools
    property var keyOf: symbol => ""
    readonly property int family: tools.activeFamily
    readonly property var info: tools.families[family] ?? ({})
    readonly property VisualToolTexts texts: VisualToolTexts {}
    readonly property string code: texts.writesCode(family)
    readonly property string note: texts.writesNote(family)
    title: info.name ?? ""
    showTitle: true
    initialFocus: more

    function openBeside(button, side) {
        if (!side) {
            openAt(button)
            return
        }
        const left = button.mapFromItem(side, 0, 0).x
        openAt(button, null, Qt.rect(left - implicitWidth - 8, 0, 1, button.height))
    }

    ColumnLayout {
        Layout.preferredWidth: 300
        spacing: 4
        Label {
            text: qsTr("Writes")
            color: Theme.muted
            font.pixelSize: 12
        }
        Label {
            objectName: "visualToolGuideWrites"
            Layout.fillWidth: true
            visible: guide.code.length > 0
            text: guide.code
            textFormat: Text.PlainText
            font.family: "monospace"
            wrapMode: Text.WrapAnywhere
        }
        Label {
            objectName: "visualToolGuideNote"
            Layout.fillWidth: true
            visible: guide.note.length > 0
            text: guide.note
            wrapMode: Text.Wrap
        }
    }
    ColumnLayout {
        objectName: "visualToolGuideLines"
        Layout.preferredWidth: 300
        Layout.topMargin: 4
        spacing: 4
        Repeater {
            model: guide.texts.guideLines(guide.family)
            delegate: Label {
                required property string modelData
                Layout.fillWidth: true
                text: modelData
                wrapMode: Text.Wrap
                leftPadding: 12
                Label {
                    text: "•"
                    color: Theme.muted
                }
            }
        }
    }
    GridLayout {
        objectName: "visualToolGuideKeys"
        Layout.preferredWidth: 300
        Layout.topMargin: 4
        columns: 2
        columnSpacing: 12
        rowSpacing: 4
        Repeater {
            model: guide.texts.guideKeys(guide.family, guide.keyOf(guide.info.hotkey ?? ""))
            delegate: Label {
                required property var modelData
                required property int index
                Layout.column: 0
                Layout.row: index
                Layout.alignment: Qt.AlignTop
                text: modelData.keys
                color: Theme.muted
                font.features: { "tnum": 1 }
            }
        }
        Repeater {
            model: guide.texts.guideKeys(guide.family, guide.keyOf(guide.info.hotkey ?? ""))
            delegate: Label {
                required property var modelData
                required property int index
                Layout.column: 1
                Layout.row: index
                Layout.fillWidth: true
                text: modelData.text
                wrapMode: Text.Wrap
            }
        }
    }
    LinkButton {
        id: more
        objectName: "visualToolGuideMore"
        Layout.topMargin: 4
        visible: UserGuide.hasPage("panel", guide.texts.guideId(guide.family))
        iconRole: "help"
        text: qsTr("More in the guide")
        onClicked: {
            UserGuide.open("panel", guide.texts.guideId(guide.family))
            guide.keep()
        }
    }
}
