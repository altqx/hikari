pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.StatusBar"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Rectangle {
    id: bar
    objectName: "statusBar"
    required property var shell
    required property var fieldsController
    property string selectionText
    property string saveText
    readonly property color warningColour: Theme.warning
    default property alias trailing: trailingItems.data

    // Qt.application.font is read once; a Label's font follows the program font live.
    readonly property font statusFont: {
        const base = fontSource.font
        const f = Qt.font({family: base.family})
        // A QML font's pointSize is QFont's pointSizeF.
        if (base.pointSize > 0)
            f.pointSize = Math.max(base.pointSize * 10 / 13, 8)
        else
            f.pixelSize = Math.max(Math.round(base.pixelSize * 10 / 13), 11)
        return f
    }

    implicitHeight: row.implicitHeight + 14
    color: palette.window
    Accessible.role: Accessible.StatusBar
    Accessible.name: qsTr("Status bar")

    Label {
        id: fontSource
        visible: false
    }

    Rectangle {
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 1
        color: bar.palette.mid
    }

    component Field: Label {
        id: field
        property string tip
        property int stretch: 0
        property bool warning: false
        visible: stretch > 0 || text.length > 0
        font: bar.statusFont
        color: warning ? bar.warningColour : bar.palette.windowText
        elide: Text.ElideRight
        maximumLineCount: 1
        verticalAlignment: Text.AlignVCenter
        Layout.fillHeight: true
        Layout.fillWidth: stretch > 0
        Layout.preferredWidth: stretch > 0 ? 0 : implicitWidth
        Layout.minimumWidth: stretch > 0 ? 0 : implicitWidth
        Accessible.role: Accessible.StaticText
        Accessible.name: text
        Accessible.description: tip
        Accessible.ignored: text.length === 0
        HoverHandler { id: hover }
        ToolTip.visible: hover.hovered && tip.length > 0 && text.length > 0
        ToolTip.text: tip
    }

    RowLayout {
        id: row
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        anchors.topMargin: 7
        anchors.bottomMargin: 7
        spacing: 14

        Field {
            objectName: "statusText"
            text: bar.shell.statusText
            tip: truncated ? text : ""
            stretch: 12
            Layout.horizontalStretchFactor: 12
        }
        Field {
            objectName: "selectionStatus"
            text: bar.selectionText
        }
        Field {
            objectName: "saveStatus"
            text: bar.saveText
        }
        Repeater {
            model: [
                {name: "statusVideoScale", field: 1},
                {name: "statusVideoZoom", field: 2},
                {name: "statusVideoDuration", field: 3},
                {name: "statusFramesPerSecond", field: 4},
                {name: "statusVideoResolution", field: 5},
                {name: "statusAspectRatio", field: 6},
                {name: "statusSubtitlesResolution", field: 7}
            ]
            delegate: Field {
                required property var modelData
                objectName: modelData.name
                text: bar.fieldsController.fields[modelData.field] ?? ""
                tip: bar.fieldsController.tooltips[modelData.field] ?? ""
                warning: (modelData.field === 5 || modelData.field === 7) && bar.fieldsController.resolutionMismatch
            }
        }
        Field {
            objectName: "statusVideoName"
            text: bar.fieldsController.fields[8] ?? ""
            tip: bar.fieldsController.tooltips[8] ?? ""
            stretch: 22
            Layout.horizontalStretchFactor: 22
        }
        Row {
            id: trailingItems
            objectName: "statusTrailing"
            visible: children.length > 0
            Layout.alignment: Qt.AlignVCenter
        }
    }
}
