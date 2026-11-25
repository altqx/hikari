pragma Translator: "HikariSub.Styles"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Item {
    id: row
    required property string name
    required property string family
    required property string size
    property bool bold: false
    property bool italic: false
    property color primary: "white"
    property color outline: "black"
    property string sampleText: "Aa"
    property bool counted: false
    property int lines: 0
    property bool linesTrailing: false
    property bool chosen: false
    property bool marked: false
    property bool focused: false
    property bool pending: false
    property bool fontMissing: false
    property string shownIn: ""
    property string namePrefix: "styleRow"
    property int nameIndex: 0
    property alias trailing: trail.data
    readonly property bool hovered: hover.hovered
    readonly property font smallFont: TypeRoles.sized(fontSource.font, 12, Font.Normal)
    readonly property font countFont: {
        const f = TypeRoles.sized(fontSource.font, 12, Font.Normal)
        f.features = { "tnum": 1 }
        return f
    }
    readonly property font nameFont: {
        const f = TypeRoles.sized(fontSource.font, 13, Font.DemiBold)
        f.italic = pending
        return f
    }

    implicitHeight: Math.max(sample.height, text.implicitHeight) + 12
    Accessible.role: Accessible.ListItem
    Accessible.name: name + ", " + family + " " + size
                     + (counted ? ", " + qsTr("%n line(s)", "", lines) : "")
                     + (marked ? ", " + qsTr("the current line’s style") : "")
                     + (fontMissing ? ", " + missingText.text : "")
    Accessible.selected: chosen

    Label { id: fontSource; visible: false }
    HoverHandler { id: hover }
    Rectangle {
        anchors.fill: parent
        color: row.chosen ? Theme.select : row.hovered ? Theme.hover : "transparent"
    }
    Rectangle {
        visible: row.chosen
        width: 3
        height: parent.height
        color: Theme.accent
    }
    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.line
    }
    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.width: 1
        border.color: Theme.focus
        visible: row.focused
    }
    RowLayout {
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 8
        anchors.rightMargin: 12
        spacing: 8
        Rectangle {
            id: sample
            objectName: row.namePrefix + "Sample" + row.nameIndex
            implicitWidth: 64
            implicitHeight: 34
            radius: Theme.radius
            color: "#6b6b6b"
            clip: true
            Grid {
                anchors.fill: parent
                columns: 8
                Repeater {
                    model: 32
                    Rectangle {
                        required property int index
                        width: 8
                        height: 8.5
                        color: (index + Math.floor(index / 8)) % 2 === 0 ? "#6b6b6b" : "#8a8a8a"
                    }
                }
            }
            Text {
                objectName: row.namePrefix + "SampleText" + row.nameIndex
                anchors.fill: parent
                anchors.margins: 3
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                textFormat: Text.PlainText
                text: row.sampleText
                fontSizeMode: Text.HorizontalFit
                minimumPixelSize: 9
                font.family: row.family
                font.pixelSize: 19
                font.bold: row.bold
                font.italic: row.italic
                color: row.primary
                style: Text.Outline
                styleColor: row.outline
                Accessible.ignored: true
            }
        }
        ColumnLayout {
            id: text
            spacing: 0
            Layout.fillWidth: true
            Item {
                implicitHeight: nameLabel.implicitHeight
                Layout.fillWidth: true
                Label {
                    id: nameLabel
                    objectName: row.namePrefix + "Name" + row.nameIndex
                    width: Math.min(implicitWidth, parent.width - (row.marked ? 10 : 0))
                    text: row.name
                    textFormat: Text.PlainText
                    font: row.nameFont
                    elide: Text.ElideRight
                }
                Rectangle {
                    objectName: row.namePrefix + "LineMark" + row.nameIndex
                    visible: row.marked
                    x: nameLabel.width + 4
                    anchors.verticalCenter: nameLabel.verticalCenter
                    width: 6
                    height: 6
                    radius: 3
                    color: Theme.accent
                }
            }
            Label {
                objectName: row.namePrefix + "Font" + row.nameIndex
                text: row.family + " · " + row.size
                      + (row.counted && !row.linesTrailing ? " · " + qsTr("%n line(s)", "", row.lines) : "")
                textFormat: Text.PlainText
                color: Theme.muted
                font: row.smallFont
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            RowLayout {
                objectName: row.namePrefix + "Missing" + row.nameIndex
                visible: row.fontMissing
                spacing: 4
                Layout.fillWidth: true
                Icon {
                    iconRole: "notice-warning"
                    size: 12
                    color: Theme.warning
                }
                Label {
                    id: missingText
                    text: row.shownIn.length === 0 ? qsTr("Font not installed")
                                                     : qsTr("Font not installed: shown in %1").arg(row.shownIn)
                    textFormat: Text.PlainText
                    color: Theme.warning
                    font: row.smallFont
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    ToolTip.text: text
                    ToolTip.visible: truncated && hover.hovered
                    ToolTip.delay: 600
                }
            }
        }
        RowLayout {
            id: trail
            spacing: 8
        }
        Label {
            objectName: row.namePrefix + "Lines" + row.nameIndex
            visible: row.counted && row.linesTrailing && !row.pending
            text: row.lines
            color: row.lines > 0 ? Theme.muted : Theme.disabled
            font: row.countFont
            horizontalAlignment: Text.AlignRight
        }
    }
}
