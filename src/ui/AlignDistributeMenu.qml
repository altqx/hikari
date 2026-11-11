pragma Translator: "HikariSub.Position"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

ShellMenu {
    id: menu
    required property var shellRoot
    required property string prefix
    property bool toVideoFrame: false
    objectName: prefix + "AlignDistributeMenu"
    iconRole: "align-center"
    title: qsTr("Align and distribute")
    onAboutToShow: toVideoFrame = shellRoot.app.settings.value("grid.alignToVideoFrame")
    function run(symbol, action) {
        if (!menu.shellRoot.hotkeyGesture(symbol))
            action()
    }
    function relativeTo(video) {
        menu.shellRoot.app.settings.setValue("grid.alignToVideoFrame", video)
        menu.toVideoFrame = video
    }
    MenuHeading { text: qsTr("Relative to") }
    MenuRadioItem {
        objectName: menu.prefix + "AlignToActiveLine"
        text: qsTr("The active line")
        checked: !menu.toVideoFrame
        onTriggered: menu.relativeTo(false)
    }
    MenuRadioItem {
        objectName: menu.prefix + "AlignToVideoFrame"
        text: qsTr("The video frame")
        checked: menu.toVideoFrame
        onTriggered: menu.relativeTo(true)
    }
    MenuSeparator {}
    ShellMenuItem {
        command: "GLOBAL_ALIGN_LEFT_EDGES"
        iconRole: "align-left"
        objectName: menu.prefix + "AlignLeftEdges"
        text: qsTr("Align left edges")
        onTriggered: menu.run(command, () => menu.shellRoot.app.alignLines(0))
    }
    ShellMenuItem {
        command: "GLOBAL_ALIGN_CENTRES"
        iconRole: "align-center"
        objectName: menu.prefix + "AlignCentres"
        text: qsTr("Align centres")
        onTriggered: menu.run(command, () => menu.shellRoot.app.alignLines(1))
    }
    ShellMenuItem {
        command: "GLOBAL_ALIGN_RIGHT_EDGES"
        iconRole: "align-right"
        objectName: menu.prefix + "AlignRightEdges"
        text: qsTr("Align right edges")
        onTriggered: menu.run(command, () => menu.shellRoot.app.alignLines(2))
    }
    MenuSeparator {}
    ShellMenuItem {
        command: "GLOBAL_ALIGN_TOP_EDGES"
        iconRole: "align-top"
        objectName: menu.prefix + "AlignTopEdges"
        text: qsTr("Align top edges")
        onTriggered: menu.run(command, () => menu.shellRoot.app.alignLines(3))
    }
    ShellMenuItem {
        command: "GLOBAL_ALIGN_MIDDLES"
        iconRole: "align-middle"
        objectName: menu.prefix + "AlignMiddles"
        text: qsTr("Align middles")
        onTriggered: menu.run(command, () => menu.shellRoot.app.alignLines(4))
    }
    ShellMenuItem {
        command: "GLOBAL_ALIGN_BOTTOM_EDGES"
        iconRole: "align-bottom"
        objectName: menu.prefix + "AlignBottomEdges"
        text: qsTr("Align bottom edges")
        onTriggered: menu.run(command, () => menu.shellRoot.app.alignLines(5))
    }
    MenuSeparator {}
    ShellMenuItem {
        command: "GLOBAL_DISTRIBUTE_HORIZONTALLY"
        iconRole: "distribute-horizontal"
        objectName: menu.prefix + "DistributeHorizontally"
        text: qsTr("Distribute horizontally")
        onTriggered: menu.run(command, () => menu.shellRoot.app.distributeLines(false))
    }
    ShellMenuItem {
        command: "GLOBAL_DISTRIBUTE_VERTICALLY"
        iconRole: "distribute-vertical"
        objectName: menu.prefix + "DistributeVertically"
        text: qsTr("Distribute vertically")
        onTriggered: menu.run(command, () => menu.shellRoot.app.distributeLines(true))
    }
}
