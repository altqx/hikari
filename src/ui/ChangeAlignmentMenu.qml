pragma Translator: "HikariSub.Position"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

ShellMenu {
    id: menu
    required property var shellRoot
    required property string prefix
    readonly property var places: [
        { an: 7, icon: "alignment-7", text: qsTr("Top left (%1)") }, { an: 8, icon: "alignment-8", text: qsTr("Top centre (%1)") }, { an: 9, icon: "alignment-9", text: qsTr("Top right (%1)") },
        { an: 4, icon: "alignment-4", text: qsTr("Middle left (%1)") }, { an: 5, icon: "alignment-5", text: qsTr("Middle centre (%1)") }, { an: 6, icon: "alignment-6", text: qsTr("Middle right (%1)") },
        { an: 1, icon: "alignment-1", text: qsTr("Bottom left (%1)") }, { an: 2, icon: "alignment-2", text: qsTr("Bottom centre (%1)") }, { an: 3, icon: "alignment-3", text: qsTr("Bottom right (%1)") }
    ]
    objectName: prefix + "ChangeAlignmentMenu"
    iconRole: "change-alignment"
    title: qsTr("Change alignment, keep position")
    Instantiator {
        model: menu.places
        delegate: ShellMenuItem {
            required property var modelData
            command: "GLOBAL_ALIGNMENT_KEEP_POSITION_AN" + modelData.an
            objectName: menu.prefix + "ChangeAlignment" + modelData.an
            iconRole: modelData.icon
            text: modelData.text.arg("\\an" + modelData.an)
            onTriggered: if (!menu.shellRoot.hotkeyGesture(command)) menu.shellRoot.app.changeAlignment(modelData.an)
        }
        onObjectAdded: (index, object) => menu.insertItem(index, object)
        onObjectRemoved: (index, object) => menu.removeItem(object)
    }
}
