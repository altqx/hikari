pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Plugins"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Item {
    id: body
    required property var ui
    required property string dock
    property var saved: ({})
    property bool shown: false
    property ColourPickerController colourPicker: null
    readonly property var panelState: ui.panel(dock)
    readonly property string status: panelState ? panelState.status : "missing"
    readonly property string livePluginName: panelState ? panelState.pluginName : ""
    readonly property string liveTitle: panelState ? panelState.title : ""
    property string lastPluginName: ""
    property string lastTitle: ""
    onLivePluginNameChanged: if (livePluginName.length > 0) lastPluginName = livePluginName
    onLiveTitleChanged: if (liveTitle.length > 0) lastTitle = liveTitle
    readonly property string pluginName: livePluginName || lastPluginName || (saved.pluginName ?? "")
    readonly property string title: liveTitle || lastTitle || (saved.title ?? "")
    readonly property bool troubled: status === "stopped" || status === "failed"
    readonly property bool missing: status === "missing"
    readonly property bool off: status === "off"
    readonly property bool drawn: status === "running" && panelState.tree !== null && panelState.tree !== undefined
    readonly property var host: ({
        send: event => body.ui.send(body.dock, event),
        packageRoot: panelState ? panelState.packageRoot.toString() : "",
        colourPicker: body.colourPicker,
        colourPopover: popover.item
    })
    signal openPlugins(string pluginId)
    signal turnOn(string pluginId)
    signal restart(string pluginId)
    signal details(string pluginId)

    onShownChanged: ui.setShown(dock, shown)
    Component.onCompleted: {
        lastPluginName = livePluginName
        lastTitle = liveTitle
        ui.setShown(dock, shown)
    }
    Component.onDestruction: ui.setShown(dock, false)

    Loader {
        id: popover
        active: body.colourPicker !== null
        sourceComponent: ColourPopover { picker: body.colourPicker }
    }

    ScrollView {
        id: scroll
        objectName: "pluginPanelScroll"
        anchors.fill: parent
        visible: body.drawn
        contentWidth: availableWidth
        PluginWidget {
            objectName: "pluginPanelTree"
            width: scroll.availableWidth
            height: Math.max(implicitHeight, scroll.availableHeight)
            node: body.drawn ? body.panelState.tree : null
            host: body.host
        }
    }

    DropArea {
        id: drops
        objectName: "pluginPanelDropArea"
        anchors.fill: parent
        enabled: body.drawn && body.panelState.tree.drop === true
        onEntered: drag => {
            if (!drag.hasUrls)
                drag.accepted = false
        }
        onDropped: drop => {
            if (drop.hasUrls && body.ui.drop(body.dock, drop.urls))
                drop.accept(Qt.CopyAction)
        }
    }
    Rectangle {
        anchors.fill: parent
        visible: drops.containsDrag
        color: "transparent"
        border.color: Theme.accent
        border.width: 2
    }

    ColumnLayout {
        objectName: "pluginPanelPlaceholder"
        visible: body.status !== "running"
        anchors.centerIn: parent
        width: Math.min(parent.width - 24, 360)
        spacing: 8
        Icon {
            Layout.alignment: Qt.AlignHCenter
            iconRole: body.troubled ? "notice-warning" : "plugins"
            size: 32
            color: body.troubled ? Theme.warning : Theme.muted
        }
        Label {
            objectName: "pluginPanelPlaceholderText"
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
            text: body.status === "missing" ? qsTr("%1 isn’t installed.").arg(body.pluginName || qsTr("Its plugin"))
                : body.status === "off" ? qsTr("%1 is turned off.").arg(body.pluginName)
                : body.status === "failed" ? qsTr("%1 didn’t start.").arg(body.pluginName)
                : body.status === "stopped" ? qsTr("%1 stopped.").arg(body.pluginName) : ""
        }
        Label {
            objectName: "pluginPanelPlaceholderDetail"
            Layout.fillWidth: true
            visible: text.length > 0
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
            color: Theme.muted
            text: body.status === "missing" ? qsTr("The layout keeps this place for its panel “%1”.").arg(body.title)
                : body.troubled ? body.panelState.reason : ""
        }
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 12
            IconTextButton {
                objectName: "pluginPanelAction"
                iconRole: body.missing ? "plugins" : body.off ? "plugin-turn-on" : "restart"
                text: body.status === "missing" ? qsTr("Open Plugins…")
                    : body.status === "off" ? qsTr("Turn on") : qsTr("Start again")
                onClicked: {
                    const plugin = body.panelState.pluginId
                    if (body.status === "missing")
                        body.openPlugins(plugin)
                    else if (body.status === "off")
                        body.turnOn(plugin)
                    else
                        body.restart(plugin)
                }
            }
            AbstractButton {
                objectName: "pluginPanelDetails"
                visible: body.troubled
                focusPolicy: Qt.StrongFocus
                Accessible.role: Accessible.Link
                Accessible.name: detailsText.text
                contentItem: Text {
                    id: detailsText
                    text: qsTr("Details")
                    color: Theme.accent
                    font.underline: true
                }
                background: FocusRing { visible: parent.visualFocus }
                onClicked: body.details(body.panelState.pluginId)
            }
        }
    }
}
