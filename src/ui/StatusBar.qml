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
    property string reviewText
    property var pluginRuntime: null
    readonly property var workingPlugins: pluginRuntime ? pluginRuntime.working.filter(w => w.background) : []
    onWorkingPluginsChanged: if (workingPlugins.length === 0) workingPopover.revert()
    signal reviewClicked()
    readonly property color warningColour: Theme.warning
    default property alias trailing: trailingItems.data

    // Qt.application.font is read once; a Label's font follows the program font live.
    readonly property font statusFont: TypeRoles.sized(fontSource.font, 11, Font.Normal)

    implicitHeight: Math.max(Theme.statusBarHeight, row.implicitHeight)
    color: Theme.background
    Accessible.role: Accessible.StatusBar
    Accessible.name: qsTr("Status bar")

    Label {
        id: fontSource
        visible: false
    }

    Rectangle {
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 1
        color: Theme.line
    }

    component Field: Label {
        id: field
        property string tip
        property int stretch: 0
        property bool warning: false
        visible: stretch > 0 || text.length > 0
        font: bar.statusFont
        color: warning ? bar.warningColour : Theme.muted
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
        spacing: 16

        ToolButton {
            id: pluginsWorking
            objectName: "statusPluginsWorking"
            visible: bar.workingPlugins.length > 0
            focusPolicy: Qt.NoFocus
            padding: 0
            Layout.fillHeight: true
            //: The status bar's busy icon: how many background plugins are working
            text: qsTr("%n plugin(s) working", "", bar.workingPlugins.length)
            contentItem: Row {
                spacing: 4
                Icon {
                    iconRole: "run-busy"
                    accentColor: IconTheme.accent
                    anchors.verticalCenter: parent.verticalCenter
                }
                Label {
                    text: bar.workingPlugins.length
                    font: bar.statusFont
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
            Accessible.name: text
            ToolTip.visible: hovered
            ToolTip.text: text
            onClicked: workingPopover.openAt(pluginsWorking)
        }
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
        Field {
            objectName: "statusReview"
            text: bar.reviewText
            tip: qsTranslate("HikariSub.Review", "Show the Review panel")
            TapHandler { onTapped: bar.reviewClicked() }
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

    Popover {
        id: workingPopover
        objectName: "pluginsWorkingPopover"
        title: qsTr("Plugins working")
        showTitle: true
        Repeater {
            model: bar.workingPlugins
            RowLayout {
                id: workingRow
                required property var modelData
                objectName: "pluginWorking_" + modelData.plugin
                spacing: 12
                Layout.fillWidth: true
                Layout.minimumWidth: 280
                ColumnLayout {
                    spacing: 2
                    Layout.fillWidth: true
                    Label {
                        text: workingRow.modelData.name
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Label {
                        text: workingRow.modelData.what
                        textFormat: Text.PlainText
                        color: Theme.muted
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
                DangerButton {
                    objectName: "pluginWorkingStop"
                    text: qsTr("Stop")
                    Accessible.description: workingRow.modelData.name
                    onClicked: bar.pluginRuntime.stopWorking(workingRow.modelData.plugin)
                }
            }
        }
    }
}
