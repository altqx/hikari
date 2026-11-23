pragma Translator: "HikariSub.Video"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

ShellMenu {
    id: barMenu
    required property VideoFullscreenController controller
    property string itemPrefix: "fullscreenBar"
    readonly property var names: [qsTr("Auto-hide"), qsTr("Always"), qsTr("Never")]
    signal settingsRequested()

    ShellMenu {
        objectName: barMenu.itemPrefix + "Position"
        iconRole: "align-bottom"
        title: qsTr("Bar position")
        MenuRadioItem {
            objectName: barMenu.itemPrefix + "PositionBelow"
            text: qsTr("Below the picture (letterbox)")
            checked: barMenu.controller.barPosition === VideoFullscreenController.BarPosition.Below
            onTriggered: {
                barMenu.controller.barPosition = VideoFullscreenController.BarPosition.Below
                checked = Qt.binding(() => barMenu.controller.barPosition === VideoFullscreenController.BarPosition.Below)
            }
        }
        MenuRadioItem {
            objectName: barMenu.itemPrefix + "PositionOver"
            text: qsTr("Over the picture")
            checked: barMenu.controller.barPosition === VideoFullscreenController.BarPosition.Over
            onTriggered: {
                barMenu.controller.barPosition = VideoFullscreenController.BarPosition.Over
                checked = Qt.binding(() => barMenu.controller.barPosition === VideoFullscreenController.BarPosition.Over)
            }
        }
    }
    ShellMenu {
        id: showMenu
        objectName: barMenu.itemPrefix + "Show"
        iconRole: "show"
        title: qsTr("Show bar")
        Instantiator {
            model: [qsTr("Show it when the pointer moves or a key is pressed; hide it 2 seconds later while playing"),
                    qsTr("Keep the bar shown"), qsTr("Picture only")]
            delegate: MenuRadioItem {
                required property string modelData
                required property int index
                objectName: barMenu.itemPrefix + ["Auto", "Always", "Never"][index]
                text: barMenu.names[index]
                help: modelData
                checked: barMenu.controller.barMode === index
                onTriggered: {
                    barMenu.controller.barMode = index
                    checked = Qt.binding(() => barMenu.controller.barMode === index)
                }
            }
            onObjectAdded: (index, object) => showMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => showMenu.removeItem(object)
        }
    }
    ShellMenuItem {
        objectName: barMenu.itemPrefix + "Settings"
        iconRole: "settings-video"
        text: qsTr("Full-screen settings…")
        onTriggered: barMenu.settingsRequested()
    }
}
