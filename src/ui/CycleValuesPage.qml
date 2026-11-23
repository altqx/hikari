pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Options"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Item {
    id: page
    objectName: "settingsPageEditing"
    required property var dialog
    implicitHeight: cycleColumn.implicitHeight
    Layout.fillWidth: true

    ColumnLayout {
        id: cycleColumn
        width: page.width
        spacing: 24

        SettingsSection {
            objectName: "editingCycleValues"
            host: page.dialog
            key: "editing.cycleValues"
            ruled: false
            Label {
                text: qsTr("Each Cycle command steps the tag in the start block of the selected lines to the next value. Cycle back steps the other way.")
                color: Theme.muted
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Repeater {
                model: ["blur", "bord", "shad", "alpha", "an", "fsp"]
                delegate: SettingsRow {
                    id: row
                    required property string modelData
                    readonly property string key: "editing.cycleValues." + modelData
                    iconRole: "cycle-values"
                    text: "\\" + modelData
                    TextField {
                        id: values
                        objectName: "setting_" + row.key
                        readonly property string docsLabel: row.text
                        font.family: "monospace"
                        Layout.fillWidth: true
                        Accessible.name: qsTr("Values for %1, in order").arg(row.text)
                        function refresh() { text = page.dialog.values[row.key] ?? "" }
                        Component.onCompleted: refresh()
                        Connections { target: page.dialog; function onReloaded() { values.refresh() } }
                        onTextEdited: page.dialog.put(row.key, text)
                    }
                }
            }
        }
    }
}
