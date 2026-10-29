pragma Translator: "HikariSub.Video"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

ShellMenu {
    id: menu
    required property VideoController video
    property var rows: []
    iconRole: "chapters"
    title: qsTr("Chapters")
    enabled: video.hasVideo && video.chapterCount > 0
    onAboutToShow: rows = video.chapters()
    Instantiator {
        model: menu.rows
        delegate: ShellMenuItem {
            required property var modelData
            required property int index
            objectName: "videoChapter" + index
            text: modelData.label + "  " + modelData.time
            checkable: true
            checked: modelData.checked
            keepsMenuOpen: false
            onTriggered: menu.video.seekChapter(index)
        }
        onObjectAdded: (index, object) => menu.insertItem(index, object)
        onObjectRemoved: (index, object) => menu.removeItem(object)
    }
}
