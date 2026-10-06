// K2: the Options dialog's Appearance page (in place of legacy's Themes page;
// theme files stay excluded), after MuseScore 4's appearance preferences
// (docs/research/musescore-appearance.md): the four themes as sample cards,
// "Follow system theme" (Light or Dark from the platform's colour scheme;
// choosing a theme by hand turns it off), the current mode's seven accent
// swatches in Light and Dark, and in high contrast the accent, text-and-icons
// and border colour pickers with their reset. Every choice is staged in the
// dialog's values and previewed live (Theme.preview); OK or Apply saves it,
// Cancel takes the preview back.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

ScrollView {
    id: page
    objectName: "settingsPageAppearance"
    required property var dialog // the Options dialog: values, put(), pickThemeColour()
    contentWidth: availableWidth

    function staged(setting) { return page.dialog.values[setting] }
    // The theme shown while previewing the staged values.
    readonly property string shownTheme: Theme.code
    readonly property string accentSetting: Theme.dark ? "appearance.darkAccent" : "appearance.lightAccent"

    ColumnLayout {
        width: page.availableWidth
        spacing: 8

        GroupBox {
            title: qsTr("Theme")
            Layout.fillWidth: true
            ColumnLayout {
                anchors.left: parent.left
                anchors.right: parent.right
                CheckBox {
                    id: follow
                    objectName: "appearanceFollowSystem"
                    text: qsTr("Follow system theme")
                    checked: page.staged("appearance.followSystem") === true
                    onToggled: page.dialog.put("appearance.followSystem", checked)
                    Accessible.name: text
                    ToolTip.text: qsTr("Light or Dark as the system's colour scheme sets it; high contrast stays as chosen")
                    ToolTip.visible: hovered
                }
                GridLayout {
                    objectName: "appearanceThemes"
                    columns: 2
                    columnSpacing: 8
                    rowSpacing: 8
                    Layout.fillWidth: true
                    Repeater {
                        model: Theme.themes()
                        delegate: AbstractButton {
                            id: card
                            required property var modelData
                            objectName: "appearanceTheme_" + modelData.code
                            readonly property var roles: Theme.rolesOf(modelData.code, page.dialog.values)
                            Layout.fillWidth: true
                            implicitHeight: 64
                            checkable: false // checked follows the staged value
                            checked: page.shownTheme === modelData.code
                            text: modelData.name
                            Accessible.role: Accessible.RadioButton
                            Accessible.name: text
                            Accessible.checked: checked
                            // Choosing a theme by hand turns following off.
                            onClicked: {
                                page.dialog.put("appearance.theme", modelData.code)
                                if (page.staged("appearance.followSystem") === true)
                                    page.dialog.put("appearance.followSystem", false)
                            }
                            background: Rectangle {
                                radius: 1
                                color: card.roles.background
                                border.color: card.checked ? Theme.accent : Theme.line
                                border.width: card.checked ? 2 : 1
                                FocusRing { control: card; radius: 1 }
                            }
                            contentItem: Item {
                                implicitWidth: 160
                                // A sample of the theme: a panel with text, a
                                // secondary line and an accent button.
                                Rectangle {
                                    anchors.fill: parent
                                    anchors.margins: 6
                                    radius: 1
                                    color: card.roles.panel
                                    border.color: card.roles.line
                                    Column {
                                        x: 6
                                        y: 4
                                        spacing: 2
                                        Text { text: card.text; color: card.roles.text; font.bold: true }
                                        Text { text: qsTr("Secondary"); color: card.roles.muted; font.pixelSize: 10 }
                                    }
                                    Rectangle {
                                        anchors.right: parent.right
                                        anchors.bottom: parent.bottom
                                        anchors.margins: 6
                                        width: 28
                                        height: 14
                                        radius: 1
                                        color: card.roles.accent
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // Light and Dark: the mode's seven accents.
        GroupBox {
            objectName: "appearanceAccents"
            title: qsTr("Accent color")
            visible: !Theme.highContrast
            Layout.fillWidth: true
            Row {
                spacing: 8
                Repeater {
                    model: Theme.accents(Theme.dark)
                    delegate: AbstractButton {
                        id: swatch
                        required property var modelData
                        objectName: "accent_" + modelData.key
                        width: 28
                        height: 28
                        checkable: false // checked follows the staged value
                        checked: page.staged(page.accentSetting) === modelData.key
                        text: modelData.name
                        Accessible.role: Accessible.RadioButton
                        Accessible.name: text
                        Accessible.checked: checked
                        ToolTip.text: qsTr("%1 (contrast %2:1)").arg(modelData.name).arg(modelData.contrast.toFixed(1))
                        ToolTip.visible: hovered
                        onClicked: page.dialog.put(page.accentSetting, modelData.key)
                        background: Rectangle {
                            radius: width / 2
                            color: swatch.modelData.accent
                            border.color: swatch.checked ? Theme.text : Theme.line
                            border.width: swatch.checked ? 2 : 1
                            FocusRing { control: swatch; radius: width / 2 }
                        }
                        contentItem: Item {}
                    }
                }
            }
        }

        // High contrast: the pickers of the theme shown.
        GroupBox {
            objectName: "appearanceHighContrast"
            title: qsTr("UI colors")
            visible: Theme.highContrast
            Layout.fillWidth: true
            ColumnLayout {
                anchors.left: parent.left
                anchors.right: parent.right
                Repeater {
                    model: Theme.picks(page.shownTheme)
                    delegate: RowLayout {
                        id: pick
                        required property var modelData
                        objectName: "highContrastPick_" + modelData.setting
                        readonly property string colour: page.staged(modelData.setting) ?? ""
                        Layout.fillWidth: true
                        Label { text: pick.modelData.name; Layout.fillWidth: true }
                        Rectangle {
                            Layout.preferredWidth: 20
                            Layout.preferredHeight: 20
                            color: pick.colour.length ? pick.colour : "transparent"
                            border.color: Theme.line
                            Accessible.ignored: true
                        }
                        Label { text: pick.colour; Layout.preferredWidth: 80 }
                        Button {
                            objectName: "highContrastChoose_" + pick.modelData.setting
                            text: qsTr("Choose...")
                            Accessible.name: qsTr("Choose %1").arg(pick.modelData.name)
                            onClicked: page.dialog.pickThemeColour(pick.modelData.setting, pick.colour)
                        }
                    }
                }
                Button {
                    objectName: "resetHighContrast"
                    text: qsTr("Reset to default")
                    onClicked: {
                        for (const p of Theme.picks(page.shownTheme))
                            page.dialog.put(p.setting, p.defaultColour)
                    }
                }
            }
        }
    }
}
