pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.NodeEffects"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

ShellMenu {
    id: libraryMenu
    objectName: "nodeLibraryMenu"
    required property NodeEffectsController controller
    title: qsTr("From the library")
    iconRole: "node-library"
    enabled: controller.library.length > 0 || controller.pluginLibrary.length > 0
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
    MenuSeparator {
        visible: libraryMenu.controller.library.length > 0 && libraryMenu.controller.pluginLibrary.length > 0
        height: visible ? implicitHeight : 0
    }
    Instantiator {
        model: libraryMenu.controller.pluginLibrary
        delegate: ShellMenu {
            id: pluginMenu
            required property var modelData
            required property int index
            objectName: "nodePluginLibrary_" + index
            title: modelData.name
            iconRole: "plugins"
            Instantiator {
                model: pluginMenu.modelData.graphs
                delegate: ShellMenuItem {
                    required property var modelData
                    required property int index
                    objectName: "nodePluginLibrary_" + pluginMenu.index + "_" + index
                    iconRole: "node-library"
                    text: modelData.title
                    onTriggered: libraryMenu.controller.addFromPlugin(modelData.file)
                }
                onObjectAdded: (index, object) => pluginMenu.insertItem(index, object)
                onObjectRemoved: (index, object) => pluginMenu.removeItem(object)
            }
        }
        onObjectAdded: (index, object) => libraryMenu.addMenu(object)
        onObjectRemoved: (index, object) => libraryMenu.removeMenu(object)
    }
}
