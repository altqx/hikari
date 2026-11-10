pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.NodeEffects"
import QtQuick
import Hikari.Ui

ShellMenu {
    id: libraryMenu
    objectName: "nodeLibraryMenu"
    required property NodeEffectsController controller
    title: qsTr("From the library…")
    iconRole: "node-library"
    enabled: controller.library.length > 0
    Instantiator {
        model: libraryMenu.controller.library
        delegate: ShellMenuItem {
            required property string modelData
            required property int index
            objectName: "nodeLibrary_" + index
            iconRole: "node-library"
            text: modelData
            onTriggered: libraryMenu.controller.addFromLibrary(modelData)
        }
        onObjectAdded: (index, object) => libraryMenu.insertItem(index, object)
        onObjectRemoved: (index, object) => libraryMenu.removeItem(object)
    }
}
