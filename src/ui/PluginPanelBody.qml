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
    readonly property bool qmlPanel: panelState ? panelState.qml : false
    readonly property bool held: qmlPanel && panelState.held
    readonly property bool qmlTroubled: status === "running" && qmlPanel && (held || panelState.loadError.length > 0)
    readonly property Item qmlItem: panelState ? panelState.item : null
    readonly property size minimumBodySize: {
        const item = body.qmlItem
        if (!item)
            return Qt.size(160, 120)
        const width = item.Layout.minimumWidth > 0 ? item.Layout.minimumWidth : item.implicitWidth
        const height = item.Layout.minimumHeight > 0 ? item.Layout.minimumHeight : item.implicitHeight
        return Qt.size(Math.min(Math.max(width, 160), 320), Math.min(Math.max(height, 80), 120))
    }
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
    signal copy(string text)

    onShownChanged: ui.setShown(dock, shown)
    Component.onCompleted: {
        lastPluginName = livePluginName
        lastTitle = liveTitle
        ui.setQmlHost(dock, qmlHost)
        ui.setShown(dock, shown)
    }
    Component.onDestruction: {
        ui.setQmlHost(dock, null)
        ui.setShown(dock, false)
    }

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

    Item {
        id: qmlHost
        objectName: "pluginQmlHost"
        anchors.fill: parent
        visible: body.status === "running" && body.qmlPanel && !body.qmlTroubled
    }

    ColumnLayout {
        objectName: "pluginQmlTrouble"
        visible: body.qmlTroubled
        anchors.centerIn: parent
        width: Math.min(parent.width - 24, 420)
        spacing: 8
        Icon {
            Layout.alignment: Qt.AlignHCenter
            iconRole: "notice-warning"
            size: 32
            color: Theme.warning
        }
        Label {
            objectName: "pluginQmlTroubleText"
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
            text: body.held ? qsTr("HikariSub closed while %1’s panel was open.").arg(body.pluginName)
                            : qsTr("The panel couldn’t load.")
        }
        Label {
            objectName: "pluginQmlTroubleDetail"
            Layout.fillWidth: true
            horizontalAlignment: body.held ? Text.AlignHCenter : Text.AlignLeft
            wrapMode: body.held ? Text.Wrap : Text.WrapAnywhere
            textFormat: Text.PlainText
            font.family: body.held ? Qt.application.font.family : "monospace"
            color: Theme.muted
            text: body.held ? qsTr("Its panels stay closed until you choose Load again.")
                            : body.qmlTroubled ? body.panelState.loadError : ""
        }
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 12
            IconTextButton {
                objectName: "pluginQmlLoadAgain"
                iconRole: "restart"
                text: qsTr("Load again")
                onClicked: body.ui.loadAgain(body.dock)
            }
            IconTextButton {
                objectName: "pluginQmlCopy"
                visible: !body.held
                iconRole: "edit-copy"
                text: qsTr("Copy")
                onClicked: body.copy(body.panelState.loadError)
            }
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
