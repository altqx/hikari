pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Options"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

ScrollView {
    id: page
    objectName: "settingsPageEditing"
    required property var dialog
    contentWidth: availableWidth

    ColumnLayout {
        width: page.availableWidth
        spacing: 16

        SettingsSection {
            objectName: "editingCycleValues"
            title: qsTr("Cycle values")
            ruled: false
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.topMargin: 20
            Label {
                text: qsTr("Each Cycle command steps the tag in the selected lines' start block to the next value. Cycle back steps the other way.")
                color: Theme.muted
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Repeater {
                model: ["blur", "bord", "shad", "alpha", "an", "fsp"]
                delegate: SettingsRow {
                    id: row
                    required property string modelData
                    readonly property string setting: "editing.cycleValues." + modelData
                    iconRole: "cycle-values"
                    text: "\\" + modelData
                    TextField {
                        id: values
                        objectName: "setting_" + row.setting
                        font.family: "monospace"
                        Layout.fillWidth: true
                        Accessible.name: qsTr("Values for %1, in order").arg(row.text)
                        function refresh() { text = page.dialog.values[row.setting] ?? "" }
                        Component.onCompleted: refresh()
                        Connections { target: page.dialog; function onReloaded() { values.refresh() } }
                        onTextEdited: page.dialog.put(row.setting, text)
                    }
                }
            }
        }
    }
}
