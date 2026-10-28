pragma Translator: "HikariSub.Video"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

// V3: the chapter menu (legacy VideoBox::ContextMenu's chapter entries,
// VideoBox.cpp:1010-1016 and 1046-1047): "<name>  [H:MM:SS.CC]", the first
// chapter whose next one starts after the shown time marked (legacy's radio
// dot, Menu.cpp:764-767); choosing one seeks to its start. Its own submenu
// in the Video menu; the video context menu lists the same entries flat.
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
            keepsMenuOpen: false // ITEM_RADIO
            onTriggered: menu.video.seekChapter(index)
        }
        onObjectAdded: (index, object) => menu.insertItem(index, object)
        onObjectRemoved: (index, object) => menu.removeItem(object)
    }
}
