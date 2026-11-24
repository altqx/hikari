pragma Translator: "HikariSub.VisualTools"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

ShellMenu {
    id: segmentMenu
    objectName: "vectorSegmentMenu"
    required property VisualToolsController tools
    property var segment: ({ index: -1, curve: false })
    property point at

    function openFor(found, item, point) {
        segment = found
        at = point
        popup(item, point)
    }
    function edit(kind) {
        segmentMenu.tools.editSegment(segment.index, segment.curve, kind, at.x, at.y)
    }

    ShellMenuItem {
        objectName: "segmentMakeCurve"
        iconRole: "vector-bezier"
        //: The menu of a vector clip's or drawing's segment
        text: qsTr("Make curve")
        enabled: !segmentMenu.segment.curve
        onTriggered: segmentMenu.edit(VisualToolsController.MakeCurve)
    }
    ShellMenuItem {
        objectName: "segmentMakeStraight"
        iconRole: "vector-line"
        text: qsTr("Make straight")
        enabled: segmentMenu.segment.curve
        onTriggered: segmentMenu.edit(VisualToolsController.MakeStraight)
    }
    ShellMenuItem {
        objectName: "segmentInsertPoint"
        iconRole: "add"
        text: qsTr("Insert point here")
        onTriggered: segmentMenu.edit(VisualToolsController.InsertPoint)
    }
    ShellMenuItem {
        objectName: "segmentDelete"
        iconRole: "delete"
        text: qsTr("Delete segment")
        onTriggered: segmentMenu.edit(VisualToolsController.DeleteSegment)
    }
}
