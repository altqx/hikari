pragma Translator: "HikariSub.Video"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

ShellMenu {
    id: barMenu
    required property VideoFullscreenController controller
    property string itemPrefix: "fullscreenBar"
    readonly property var names: [qsTr("Show when the pointer moves"), qsTr("Always show"), qsTr("Never show")]
    Instantiator {
        model: [qsTr("Show on pointer movement, hide when idle"), qsTr("Keep the bar; the picture ends above it"),
                qsTr("Picture only")]
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
        onObjectAdded: (index, object) => barMenu.insertItem(index, object)
        onObjectRemoved: (index, object) => barMenu.removeItem(object)
    }
}
