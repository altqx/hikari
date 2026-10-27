// K2: the Settings window's Appearance page (in place of legacy's Themes page;
// theme files stay excluded), after MuseScore 4's appearance preferences
// (docs/research/musescore-appearance.md): the four themes as sample cards,
// "Follow system theme" (Light or Dark from the platform's colour scheme;
// choosing a theme by hand turns it off), the current mode's seven accent
// swatches in Light and Dark, and in high contrast the accent, text-and-icons
// and border colour pickers with their reset. Every choice is staged in the
// dialog's values and previewed live (Theme.preview); OK or Apply saves it,
// Cancel takes the preview back. SH1 (#220): "Use system title bar", which
// OK or Apply applies. PF1 (#225): in the Settings window's section form,
// with the program font (from legacy Editor > Advanced).
pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Options"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

ScrollView {
    id: page
    objectName: "settingsPageAppearance"
    required property var dialog // the Settings window: values, put(), pickThemeColour(), reloaded
    // #386: as wide as the view, or the rows' narrowest, so a narrow window
    // scrolls the page sideways (as the Style manager's editor). The column
    // reports it once measured (read in this binding, it loops).
    property real narrowest: 0
    contentWidth: Math.max(narrowest, availableWidth)
    ScrollBar.horizontal.policy: contentWidth > availableWidth + 0.5 ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded

    function staged(setting) { return page.dialog.values[setting] }
    // The theme shown while previewing the staged values.
    readonly property string shownTheme: Theme.code
    readonly property string accentSetting: Theme.dark ? "appearance.darkAccent" : "appearance.lightAccent"

    ColumnLayout {
        width: page.contentWidth
        spacing: 16
        onImplicitWidthChanged: Qt.callLater(() => page.narrowest = Layout.minimumWidth)

        SettingsSection {
            objectName: "appearanceTheme"
            title: qsTr("Theme")
            ruled: false
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.topMargin: 20
            SettingsRow {
                iconRole: "theme"
                text: qsTr("Interface theme")
                GridLayout {
                    objectName: "appearanceThemes"
                    // DX1 (#367): what the docs export reads (docs_export.cpp).
                    readonly property string docsSetting: "appearance.theme"
                    readonly property string docsControl: "choice"
                    readonly property var docsValues: Theme.themes().map(t => ({value: t.code, label: t.name}))
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
                                FocusRing { control: card; radius: 2 }
                            }
                            contentItem: Item {
                                implicitWidth: 140
                                // A sample of the theme: a panel with its name,
                                // secondary lines and an accent button.
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
                                        // a line of secondary text, drawn as a bar (a
                                        // sample word read as a label)
                                        Rectangle { width: 48; height: 3; radius: 1; color: card.roles.muted }
                                        Rectangle { width: 32; height: 3; radius: 1; color: card.roles.muted }
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
            CheckBox {
                id: follow
                objectName: "appearanceFollowSystem"
                readonly property string docsSetting: "appearance.followSystem"
                text: qsTr("Follow system theme")
                leftPadding: 1 // the box in the rows' icon slot
                spacing: 9
                checked: page.staged("appearance.followSystem") === true
                onToggled: page.dialog.put("appearance.followSystem", checked)
                Accessible.name: text
                Accessible.description: followHint.text
            }
            Label {
                id: followHint
                text: qsTr("Light or Dark as the system's colour scheme sets it; high contrast stays as chosen")
                color: Theme.muted
                wrapMode: Text.Wrap
                Layout.leftMargin: 24
                Layout.topMargin: -5
                Layout.fillWidth: true
                Accessible.ignored: true
            }
        }

        // Light and Dark: the mode's seven accents.
        SettingsSection {
            objectName: "appearanceAccents"
            title: qsTr("Accent color")
            visible: !Theme.highContrast
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            SettingsRow {
                iconRole: "accent-colour"
                text: qsTr("Accent")
                Row {
                    spacing: 8
                    // DX1: Light's and Dark's accents, whichever is shown.
                    readonly property var docsEntries: Theme.themes().filter(t => !t.highContrast).map(t => ({
                        setting: t.dark ? "appearance.darkAccent" : "appearance.lightAccent",
                        control: "choice",
                        label: "Accent (" + t.name + ")",
                        values: Theme.accents(t.dark).map(a => ({value: a.key, label: a.name}))
                    }))
                    Repeater {
                        model: Theme.accents(Theme.dark)
                        delegate: AbstractButton {
                            id: swatch
                            required property var modelData
                            objectName: "accent_" + modelData.key
                            width: 24
                            height: 24
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
        }

        // High contrast: the pickers of the theme shown.
        SettingsSection {
            objectName: "appearanceHighContrast"
            title: qsTr("UI colors")
            visible: Theme.highContrast
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Repeater {
                // DX1: both high contrast themes' colors, whichever is shown.
                readonly property var docsEntries: [].concat(...Theme.themes().filter(t => t.highContrast).map(
                    t => Theme.picks(t.code).map(p => ({setting: p.setting, control: "colour", label: p.name + " (" + t.name + ")"}))))
                model: Theme.picks(page.shownTheme)
                delegate: SettingsRow {
                    id: pick
                    required property var modelData
                    required property int index
                    objectName: "highContrastPick_" + modelData.setting
                    readonly property string colour: page.staged(modelData.setting) ?? ""
                    iconRole: index === 0 ? "ui-colours" : ""
                    text: modelData.name
                    Rectangle {
                        Layout.preferredWidth: 24
                        Layout.preferredHeight: 24
                        color: pick.colour.length ? pick.colour : "transparent"
                        border.color: Theme.line
                        Accessible.ignored: true
                    }
                    Label { text: pick.colour; Layout.preferredWidth: 80 }
                    Button {
                        objectName: "highContrastChoose_" + pick.modelData.setting
                        text: qsTr("Choose…")
                        Accessible.name: qsTr("Choose %1").arg(pick.modelData.name)
                        onClicked: page.dialog.pickThemeColour(pick.modelData.setting, pick.colour, this, pick.modelData.name)
                    }
                }
            }
            SettingsRow {
                IconTextButton {
                    objectName: "resetHighContrast"
                    iconRole: "restore-default"
                    text: qsTr("Reset to default")
                    onClicked: {
                        for (const p of Theme.picks(page.shownTheme))
                            page.dialog.put(p.setting, p.defaultColour)
                    }
                }
            }
        }

        // The program font (legacy Editor > Advanced's FontPickerButton).
        SettingsSection {
            objectName: "appearanceFont"
            title: qsTr("Font")
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            SettingsRow {
                iconRole: "ui-font"
                text: qsTr("Program font")
                ComboBox {
                    id: family
                    objectName: "setting_program.font"
                    editable: true
                    model: Qt.fontFamilies()
                    Accessible.name: qsTr("Program font")
                    Layout.preferredWidth: 200
                    function refresh() { editText = page.dialog.values["program.font"] ?? "" }
                    Component.onCompleted: refresh()
                    Connections { target: page.dialog; function onReloaded() { family.refresh() } }
                    onActivated: page.dialog.put("program.font", currentText)
                    onAccepted: page.dialog.put("program.font", editText)
                }
                SpinBox {
                    id: size
                    objectName: "setting_program.fontSize"
                    from: 1
                    to: 200
                    editable: true
                    Accessible.name: qsTr("Font size")
                    function refresh() { value = page.dialog.values["program.fontSize"] ?? 10 }
                    Component.onCompleted: refresh()
                    Connections { target: page.dialog; function onReloaded() { size.refresh() } }
                    onValueModified: page.dialog.put("program.fontSize", value)
                }
            }
        }

        // SH1 (#220): the window's title bar, HikariSub's own or the
        // system's. Choosing the desktop's default stores "auto", so the
        // choice follows the desktop again.
        SettingsSection {
            objectName: "appearanceWindow"
            title: qsTr("Window")
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.bottomMargin: 24
            CheckBox {
                objectName: "appearanceSystemTitleBar"
                // (The docs export's entries are in English: it runs in English.)
                readonly property string docsSetting: "appearance.titleBar"
                readonly property string docsControl: "choice"
                readonly property var docsValues: [
                    {value: "auto", label: "The desktop's default"},
                    {value: "system", label: "The system's title bar"},
                    {value: "custom", label: "HikariSub's title bar"}
                ]
                text: qsTr("Use system title bar")
                leftPadding: 1
                spacing: 9
                // Where the window cannot draw its own (WindowFrame.customAvailable), it is the system's.
                enabled: WindowFrame.customAvailable
                checked: !WindowFrame.customAvailable
                         || WindowFrame.usesSystemTitleBar(page.staged("appearance.titleBar") ?? "auto")
                onToggled: page.dialog.put("appearance.titleBar",
                                           checked === WindowFrame.systemTitleBarByDefault ? "auto"
                                               : checked ? "system" : "custom")
                Accessible.name: text
                Accessible.description: systemTitleBarHint.text
            }
            Label {
                id: systemTitleBarHint
                text: qsTr("Off by default on Windows, KDE Plasma and GNOME; on by default on tiling window managers.")
                color: Theme.muted
                wrapMode: Text.WordWrap
                Layout.leftMargin: 24
                Layout.topMargin: -5
                Layout.fillWidth: true
                Accessible.ignored: true
            }
        }
    }
}
