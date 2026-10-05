// K1: the Themes page's icon colours (docs/qt/ux/icons.md): per appearance
// (light, dark, high contrast) the icon, its accent layer, hover / pressed
// and disabled. A double click picks a colour; "Reset icon colours" puts the
// theme defaults back. Like the page's other colours they are staged in the
// Options dialog's values and saved by OK / Apply, and the icons repaint at
// once. Each appearance shows a sample of the set in its staged colours.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

ColumnLayout {
    id: editor
    required property var dialog // the Options dialog: values, put(), pickThemeColour()

    readonly property var appearances: [
        {id: "light", name: qsTr("Light"), background: "#E5E9EC"},
        {id: "dark", name: qsTr("Dark"), background: "#171B20"},
        {id: "highContrast", name: qsTr("High contrast"), background: "#000000"}
    ]
    readonly property var slots: [
        {id: "normal", name: qsTr("icon")},
        {id: "accent", name: qsTr("accent")},
        {id: "active", name: qsTr("hover / pressed")},
        {id: "disabled", name: qsTr("disabled")}
    ]
    readonly property var rows: {
        const out = []
        for (const a of appearances)
            for (const s of slots)
                out.push({setting: "icons." + a.id + "." + s.id, name: qsTr("%1 theme icons: %2").arg(a.name).arg(s.name)})
        return out
    }
    function staged(setting) { return editor.dialog.values[setting] ?? "" }

    RowLayout {
        Layout.fillWidth: true
        Label { text: qsTr("Icon colours"); font.bold: true; Layout.fillWidth: true }
        Button {
            objectName: "resetIconColours"
            text: qsTr("Reset icon colours")
            onClicked: {
                for (const id of IconTheme.settingIds())
                    editor.dialog.put(id, IconTheme.defaultColour(id))
            }
        }
    }
    // A sample per appearance in the staged colours.
    RowLayout {
        objectName: "iconColourSamples"
        Layout.fillWidth: true
        Repeater {
            model: editor.appearances
            delegate: Rectangle {
                id: sample
                required property var modelData
                Layout.fillWidth: true
                implicitHeight: 28
                color: modelData.background
                border.color: palette.mid
                Accessible.ignored: true
                Row {
                    anchors.centerIn: parent
                    spacing: 6
                    Repeater {
                        model: ["media-play", "save-as", "tag-underline", "undo"]
                        delegate: TintedSvg {
                            id: sampleIcon
                            required property string modelData
                            required property int index
                            width: 16
                            height: 16
                            iconRole: modelData
                            // the last sample shows the disabled colour, the third hover / pressed
                            color: editor.staged("icons." + sample.modelData.id + "." + (sampleIcon.index === 3 ? "disabled" : sampleIcon.index === 2 ? "active" : "normal"))
                            accentColor: editor.staged("icons." + sample.modelData.id + "." + (sampleIcon.index === 3 ? "disabled" : sampleIcon.index === 2 ? "active" : "accent"))
                        }
                    }
                }
            }
        }
    }
    ListView {
        id: list
        objectName: "iconColours"
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: 120
        clip: true
        model: editor.rows
        delegate: ItemDelegate {
            id: row
            required property var modelData
            required property int index
            objectName: "iconColour_" + row.modelData.setting
            width: ListView.view.width
            highlighted: ListView.isCurrentItem
            readonly property string colour: editor.staged(row.modelData.setting)
            contentItem: RowLayout {
                Label { text: row.modelData.name; Layout.fillWidth: true }
                Rectangle {
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                    color: row.colour.length ? row.colour : "transparent"
                    border.color: palette.mid
                }
                Label { text: row.colour; Layout.preferredWidth: 100 }
            }
            Accessible.name: row.modelData.name + " " + row.colour
            onClicked: list.currentIndex = row.index
            onDoubleClicked: {
                list.currentIndex = row.index
                editor.dialog.pickThemeColour(row.modelData.setting, row.colour)
            }
        }
        Accessible.name: qsTr("Icon colours")
    }
}
