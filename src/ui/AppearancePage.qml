pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Options"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Item {
    id: page
    objectName: "settingsPageAppearance"
    required property var dialog
    implicitHeight: appearanceColumn.implicitHeight
    Layout.fillWidth: true

    function staged(setting) { return page.dialog.values[setting] }
    readonly property string shownTheme: Theme.code
    readonly property var pairSettings: ({light: "appearance.lightTheme", dark: "appearance.darkTheme",
                                          highContrastLight: "appearance.highContrastLightTheme",
                                          highContrastDark: "appearance.highContrastDarkTheme"})
    function stagedList(setting) { return page.staged(setting) ?? [] }

    ColumnLayout {
        id: appearanceColumn
        width: page.width
        spacing: 24

        SettingsSection {
            objectName: "appearanceTheme"
            host: page.dialog
            key: "appearance.theme"
            ruled: false
            SettingsRow {
                iconRole: "theme"
                text: qsTr("Interface theme")
                GridLayout {
                    objectName: "appearanceThemes"
                    // MUST KILL: the docs* properties look unused; only docs_export.cpp reads them
                    readonly property var docsEntries: [{
                        setting: "appearance.theme", control: "choice", label: "Interface theme",
                        values: Theme.themes().map(t => ({value: t.code, label: t.name}))
                    }].concat(Object.keys(page.pairSettings).map(type => ({
                        setting: page.pairSettings[type], control: "choice",
                        label: "Interface theme (" + type + ", following the system)",
                        values: Theme.themes().filter(t => t.type === type).map(t => ({value: t.code, label: t.name}))
                    })))
                    columns: 2
                    columnSpacing: 8
                    rowSpacing: 8
                    Layout.fillWidth: true
                    Repeater {
                        model: Theme.themeList
                        delegate: AbstractButton {
                            id: card
                            required property var modelData
                            objectName: "appearanceTheme_" + modelData.code
                            readonly property var roles: Theme.rolesOf(modelData.code, page.dialog.values)
                            Layout.fillWidth: true
                            implicitHeight: 64
                            checkable: false
                            checked: page.shownTheme === modelData.code
                            text: modelData.name
                            Accessible.role: Accessible.RadioButton
                            Accessible.name: text
                            Accessible.checked: checked
                            onClicked: {
                                page.dialog.put("appearance.theme", modelData.code)
                                page.dialog.put(page.pairSettings[modelData.type], modelData.code)
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
                leftPadding: 1
                spacing: 9
                checked: page.staged("appearance.followSystem") === true
                onToggled: page.dialog.put("appearance.followSystem", checked)
                Accessible.name: text
                Accessible.description: followHint.text
            }
            Label {
                id: followHint
                text: qsTr("Light or Dark as the system’s colour scheme sets it; high contrast stays as chosen")
                color: Theme.muted
                wrapMode: Text.Wrap
                Layout.leftMargin: 24
                Layout.topMargin: -5
                Layout.fillWidth: true
                Accessible.ignored: true
            }
        }

        SettingsSection {
            objectName: "appearanceAccents"
            host: page.dialog
            key: "appearance.accentColour"
            visible: Theme.accentsOf(page.shownTheme).length > 0
            SettingsRow {
                iconRole: "accent-colour"
                text: qsTr("Accent")
                Row {
                    spacing: 8
                    readonly property var docsEntries: [{
                        setting: "appearance.accents",
                        control: "choice",
                        label: "Accent",
                        values: Theme.accents(true).map(a => ({value: a.key, label: a.name}))
                    }]
                    Repeater {
                        model: Theme.accentsOf(page.shownTheme)
                        delegate: AbstractButton {
                            id: swatch
                            required property var modelData
                            objectName: "accent_" + modelData.key
                            width: 24
                            height: 24
                            checkable: false
                            checked: Theme.accentChoice(page.stagedList("appearance.accents"), page.shownTheme) === modelData.key
                            text: modelData.name
                            Accessible.role: Accessible.RadioButton
                            Accessible.name: text
                            Accessible.checked: checked
                            ToolTip.text: qsTr("%1 (contrast %2:1)").arg(modelData.name, modelData.contrast.toFixed(1))
                            ToolTip.visible: hovered
                            onClicked: page.dialog.put("appearance.accents",
                                Theme.withAccentChoice(page.stagedList("appearance.accents"), page.shownTheme, modelData.key))
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

        SettingsSection {
            objectName: "appearanceHighContrast"
            host: page.dialog
            key: "appearance.uiColours"
            visible: Theme.picks(page.shownTheme).length > 0
            Repeater {
                readonly property var docsEntries: [{setting: "appearance.colours", control: "colour", label: "UI colours"}]
                model: Theme.picks(page.shownTheme)
                delegate: SettingsRow {
                    id: pick
                    required property var modelData
                    required property int index
                    objectName: "highContrastPick_" + modelData.role
                    readonly property string colour: Theme.userColour(page.stagedList("appearance.colours"), page.shownTheme, modelData.role)
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
                        objectName: "highContrastChoose_" + pick.modelData.role
                        text: qsTr("Choose…")
                        Accessible.name: qsTr("Choose %1").arg(pick.modelData.name)
                        onClicked: page.dialog.pickThemeColour(page.shownTheme, pick.modelData.role, pick.colour, this,
                                                               pick.modelData.name)
                    }
                }
            }
            SettingsRow {
                IconTextButton {
                    objectName: "resetHighContrast"
                    iconRole: "restore-default"
                    text: qsTr("Reset to default")
                    onClicked: page.dialog.put("appearance.colours",
                                               Theme.withoutUserColours(page.stagedList("appearance.colours"), page.shownTheme))
                }
            }
        }

        SettingsSection {
            objectName: "appearanceFont"
            host: page.dialog
            key: "appearance.font"
            SettingsRow {
                iconRole: "ui-font"
                text: qsTr("Program font")
                FontFamilyBox {
                    id: family
                    objectName: "setting_program.font"
                    families: Qt.fontFamilies()
                    Accessible.name: qsTr("Program font")
                    Layout.preferredWidth: 200
                    function refresh() { text = page.dialog.values["program.font"] ?? "" }
                    Component.onCompleted: refresh()
                    Connections { target: page.dialog; function onReloaded() { family.refresh() } }
                    onPicked: name => page.dialog.put("program.font", name)
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
            SettingsRow {
                iconRole: "preview-text"
                text: qsTr("Preview text")
                note: qsTr("The sample text of the style and font previews.")
                TextField {
                    id: previewText
                    objectName: "setting_styles.previewText"
                    Accessible.name: qsTr("Preview text")
                    Layout.preferredWidth: 260
                    function refresh() { text = page.dialog.values["styles.previewText"] ?? "" }
                    Component.onCompleted: refresh()
                    Connections { target: page.dialog; function onReloaded() { previewText.refresh() } }
                    onTextEdited: page.dialog.put("styles.previewText", text)
                }
            }
        }

        SettingsSection {
            objectName: "appearanceWindow"
            host: page.dialog
            key: "appearance.window"
            Layout.bottomMargin: 24
            CheckBox {
                objectName: "appearanceSystemTitleBar"
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
            SettingsRow {
                text: qsTr("AI side bar")
                note: qsTr("The other side bar holds the tools. Swap side bars in the View menu switches them.")
                labelWidth: 150
                ComboBox {
                    objectName: "appearanceAiSideBar"
                    readonly property string docsSetting: "appearance.aiSideBar"
                    readonly property string docsControl: "choice"
                    readonly property var docsValues: [
                        {value: "start", label: "Left"},
                        {value: "end", label: "Right"}
                    ]
                    readonly property var edges: Qt.application.layoutDirection === Qt.RightToLeft ? ["end", "start"] : ["start", "end"]
                    model: [qsTr("Left"), qsTr("Right")]
                    currentIndex: edges.indexOf(page.staged("appearance.aiSideBar") || "start")
                    onActivated: index => page.dialog.put("appearance.aiSideBar", edges[index])
                    Accessible.name: qsTr("AI side bar")
                }
            }
        }
    }
}
