pragma Translator: "HikariSub.VisualTools"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

FocusScope {
    id: host
    objectName: "fullscreenToolDialog"
    required property VisualToolsController tools
    property SettingsStore settings: null
    property QtObject editor: null
    readonly property Item pane: shapes.item ?? tags.item
    Connections {
        target: host.tools
        function onShapeEditorChanged() { host.editor = host.tools.shapeEditor ?? host.tools.tagsEditor }
        function onTagsEditorChanged() { host.editor = host.tools.tagsEditor ?? host.tools.shapeEditor }
    }
    Component.onCompleted: editor = tools.shapeEditor ?? tools.tagsEditor
    readonly property bool shown: editor !== null
    visible: shown
    function focusFirst(reason) {
        if (pane)
            pane.firstFocus.forceActiveFocus(reason)
    }
    property bool hadFocus: false
    signal closed(bool hadFocus)
    onActiveFocusChanged: if (shown) hadFocus = activeFocus
    onShownChanged: {
        if (shown) {
            Qt.callLater(focusFirst, Qt.OtherFocusReason)
        } else {
            closed(hadFocus)
            hadFocus = false
        }
    }
    Accessible.role: Accessible.Pane
    Accessible.name: pane ? pane.title : ""
    Keys.onEscapePressed: event => {
        event.accepted = true
        host.editor.close()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            Layout.bottomMargin: 4
            Label {
                objectName: "fullscreenToolDialogName"
                Layout.fillWidth: true
                text: host.pane ? host.pane.title : ""
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            IconToolButton {
                objectName: "fullscreenToolDialogClose"
                iconRole: "window-close"
                text: qsTr("Close")
                focusPolicy: Qt.TabFocus
                onClicked: host.editor.close()
            }
        }
        ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            Item {
                width: scroll.availableWidth
                implicitHeight: (host.pane ? host.pane.implicitHeight : 0) + 24
                Loader {
                    id: shapes
                    x: 12
                    width: parent.width - 24
                    active: host.shown && host.editor === host.tools.shapeEditor
                    sourceComponent: ShapesEditionPane {
                        column: true
                        editor: host.tools.shapeEditor
                        settings: host.settings
                    }
                }
                Loader {
                    id: tags
                    x: 12
                    width: parent.width - 24
                    active: host.shown && host.editor === host.tools.tagsEditor
                    sourceComponent: AllTagsEditionPane {
                        column: true
                        editor: host.tools.tagsEditor
                        settings: host.settings
                    }
                }
            }
        }
    }
}
