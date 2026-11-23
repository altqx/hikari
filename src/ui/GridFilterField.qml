import QtQuick
import QtQuick.Controls
import Hikari.Ui

Item {
    id: bar
    required property ShellController shell
    signal leave(bool toFirstLine)
    implicitWidth: 240
    implicitHeight: 22

    TextField {
        id: field
        objectName: "gridFilter"
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        height: 22
        topPadding: 0
        bottomPadding: 0
        placeholderText: qsTranslate("HikariSub.Grid", "Filter")
        text: bar.shell.gridFilterText
        selectByMouse: true
        Accessible.name: qsTranslate("HikariSub.Grid", "Filter the Grid")
        onTextEdited: bar.shell.gridFilterText = text
        Keys.onEscapePressed: {
            if (text.length > 0)
                bar.shell.gridFilterText = ""
            else
                bar.leave(false)
        }
        Keys.onReturnPressed: bar.leave(true)
        Keys.onEnterPressed: bar.leave(true)
        Keys.onDownPressed: bar.leave(true)
    }
}
