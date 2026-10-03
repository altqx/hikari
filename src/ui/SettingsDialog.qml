// O1: legacy OptionsDialog "Options" (GLOBAL_SETTINGS, File > Settings) over
// the settings registry: the page tree Editor (Conversion, Advanced), Video,
// Audio (Advanced) and Subtitle properties, with OK, Apply, Cancel and Set
// default. Values are staged here and written by OK/Apply when they differ
// (legacy SetOptions). Themes are excluded by the accepted settings decision;
// Hotkeys belong to the shortcut editor (O2); Associations are Windows only
// and wait for their platform action.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Hikari.Ui

Dialog {
    id: dialog
    objectName: "settingsDialog"
    required property var app
    title: qsTr("Options")
    modal: true
    width: 640
    height: 600
    property var values: ({})
    property var languages: []
    property var dictionaries: []
    signal reloaded()

    function openDialog() {
        load()
        pageList.currentIndex = 0 // legacy ChangeSelection(0)
        open()
    }
    function load() {
        values = app.settingsDialogValues()
        languages = app.settingsLanguages()
        dictionaries = app.settingsDictionaries()
        reloaded()
    }
    function put(setting, value) {
        const v = Object.assign({}, values)
        v[setting] = value
        values = v
    }
    function apply() {
        app.applySettings(values)
        load()
    }

    footer: DialogButtonBox {
        Button { objectName: "settingsOk"; text: "OK"; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
        Button { objectName: "settingsApply"; text: qsTr("Apply"); DialogButtonBox.buttonRole: DialogButtonBox.ApplyRole }
        Button { objectName: "settingsCancel"; text: qsTr("Cancel"); DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        Button {
            objectName: "settingsDefault"
            text: qsTr("Set default")
            DialogButtonBox.buttonRole: DialogButtonBox.ResetRole
        }
        onApplied: dialog.apply()
        onReset: {
            dialog.app.resetSettings()
            dialog.load()
        }
    }
    onAccepted: app.applySettings(values)

    // A check box bound to a boolean setting.
    component SettingCheck: CheckBox {
        id: check
        required property string setting
        objectName: "setting_" + setting
        Accessible.name: text
        function refresh() { checked = dialog.values[setting] === true }
        Component.onCompleted: refresh()
        Connections { target: dialog; function onReloaded() { check.refresh() } }
        onToggled: dialog.put(setting, checked)
    }
    // A NumCtrl: a label and a number within the legacy range.
    component SettingNumber: RowLayout {
        id: numberRow
        required property string setting
        property alias text: numberLabel.text
        property string tip
        property int from: 0
        property int to: 100000
        Layout.fillWidth: true
        Label { id: numberLabel; Layout.fillWidth: true; wrapMode: Text.Wrap }
        SpinBox {
            id: spin
            objectName: "setting_" + numberRow.setting
            from: numberRow.from
            to: numberRow.to
            editable: true
            Accessible.name: numberLabel.text
            Layout.preferredWidth: 150
            ToolTip.text: numberRow.tip
            ToolTip.visible: hovered && numberRow.tip !== ""
            function refresh() { value = dialog.values[numberRow.setting] ?? 0 }
            Component.onCompleted: refresh()
            Connections { target: dialog; function onReloaded() { spin.refresh() } }
            onValueModified: dialog.put(numberRow.setting, value)
        }
    }
    // A text field bound to a string setting.
    component SettingText: TextField {
        id: field
        required property string setting
        objectName: "setting_" + setting
        Layout.fillWidth: true
        function refresh() { text = dialog.values[setting] ?? "" }
        Component.onCompleted: refresh()
        Connections { target: dialog; function onReloaded() { field.refresh() } }
        onTextEdited: dialog.put(setting, text)
    }
    // A choice whose index is the setting's integer (legacy ID_HIKARI_CHOICE).
    component SettingChoice: ComboBox {
        id: choice
        required property string setting
        objectName: "setting_" + setting
        Layout.fillWidth: true
        function refresh() {
            const v = dialog.values[setting] ?? 0
            currentIndex = v >= 0 && v < count ? v : -1
        }
        Component.onCompleted: refresh()
        onModelChanged: refresh()
        Connections { target: dialog; function onReloaded() { choice.refresh() } }
        onActivated: dialog.put(setting, currentIndex)
    }
    // FontPickerButton: the family and point size of a font setting.
    component SettingFont: RowLayout {
        id: fontRow
        required property string setting
        required property string sizeSetting
        property alias text: fontLabel.text
        Layout.fillWidth: true
        Label { id: fontLabel; Layout.fillWidth: true }
        ComboBox {
            id: family
            objectName: "setting_" + fontRow.setting
            editable: true
            model: Qt.fontFamilies()
            Accessible.name: fontLabel.text
            Layout.preferredWidth: 200
            function refresh() { editText = dialog.values[fontRow.setting] ?? "" }
            Component.onCompleted: refresh()
            Connections { target: dialog; function onReloaded() { family.refresh() } }
            onActivated: dialog.put(fontRow.setting, currentText)
            onAccepted: dialog.put(fontRow.setting, editText)
        }
        SpinBox {
            id: size
            objectName: "setting_" + fontRow.sizeSetting
            from: 1
            to: 200
            editable: true
            Accessible.name: qsTr("Font size")
            function refresh() { value = dialog.values[fontRow.sizeSetting] ?? 10 }
            Component.onCompleted: refresh()
            Connections { target: dialog; function onReloaded() { size.refresh() } }
            onValueModified: dialog.put(fontRow.sizeSetting, value)
        }
    }

    RowLayout {
        anchors.fill: parent
        ListView {
            id: pageList
            objectName: "settingsPages"
            Layout.preferredWidth: 150
            Layout.fillHeight: true
            clip: true
            // Legacy AddPage / AddSubPage order.
            model: [
                {name: qsTr("Editor"), depth: 0},
                {name: qsTr("Conversion"), depth: 1},
                {name: qsTr("Advanced"), depth: 1},
                {name: qsTr("Video"), depth: 0},
                {name: qsTr("Audio"), depth: 0},
                {name: qsTr("Advanced"), depth: 1},
                {name: qsTr("Subtitle properties"), depth: 0}
            ]
            delegate: ItemDelegate {
                required property var modelData
                required property int index
                width: ListView.view.width
                text: modelData.name
                leftPadding: 8 + modelData.depth * 16
                highlighted: ListView.isCurrentItem
                onClicked: pageList.currentIndex = index
            }
            Accessible.name: qsTr("Options")
        }
        StackLayout {
            id: pages
            currentIndex: pageList.currentIndex
            Layout.fillWidth: true
            Layout.fillHeight: true

            // Editor (legacy GLOBAL_EDITOR page).
            ScrollView {
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    GroupBox {
                        title: qsTr("Language (program restart required)")
                        Layout.fillWidth: true
                        ComboBox {
                            id: languageBox
                            objectName: "setting_program.language"
                            anchors.left: parent.left
                            anchors.right: parent.right
                            model: dialog.languages.map(l => l.name)
                            Accessible.name: qsTr("Language (program restart required)")
                            function refresh() {
                                const i = dialog.languages.findIndex(l => l.name === dialog.languageName(dialog.values["program.language"]))
                                currentIndex = i < 0 ? 0 : i
                                // Legacy writes the chosen tag when it differs, "en" included.
                                if (dialog.languages.length > 0)
                                    dialog.values["program.language"] = dialog.languages[currentIndex].tag
                            }
                            Connections { target: dialog; function onReloaded() { languageBox.refresh() } }
                            onActivated: dialog.put("program.language", dialog.languages[currentIndex].tag)
                        }
                    }
                    GroupBox {
                        title: qsTr("Spell checker language (\"Dictionary\" folder)")
                        Layout.fillWidth: true
                        ComboBox {
                            id: dictionaryBox
                            objectName: "setting_editor.dictionaryLanguage"
                            anchors.left: parent.left
                            anchors.right: parent.right
                            model: dialog.dictionaries.length > 0 ? dialog.dictionaries.map(d => d.name)
                                                                  : [qsTr("Put files .dic and .aff to \"Dictionary\" folder")]
                            Accessible.name: qsTr("Spell checker language (\"Dictionary\" folder)")
                            function refresh() {
                                currentIndex = dialog.dictionaries.findIndex(d => d.name === dialog.languageName(dialog.values["editor.dictionaryLanguage"]))
                            }
                            Connections { target: dialog; function onReloaded() { dictionaryBox.refresh() } }
                            onActivated: {
                                if (currentIndex >= 0 && currentIndex < dialog.dictionaries.length)
                                    dialog.put("editor.dictionaryLanguage", dialog.dictionaries[currentIndex].tag)
                            }
                        }
                    }
                    SettingCheck { setting: "grid.loadSortedSubs"; text: qsTr("Open sorted subtitles") }
                    SettingCheck { setting: "editor.spellchecker"; text: qsTr("Turn spell checking") }
                    SettingCheck { setting: "grid.autoSelectLinesFromLastTab"; text: qsTr("Select the line with the time line\nof the previous active tab") }
                    SettingCheck { setting: "editor.suggestionsOnDoubleClick"; text: qsTr("Show suggestions by double-clicking on misspell") }
                    SettingCheck { setting: "subtitles.openInNewTab"; text: qsTr("Always open subtitles in a new tab") }
                    SettingCheck { setting: "editor.dontGoToNextLineOnTimesEdit"; text: qsTr("Stay on selected line when editing times") }
                    SettingCheck { setting: "video.disableLiveEditing"; text: qsTr("Turn off edits preview on video\n(re-opening tab is required)") }
                    SettingCheck { setting: "grid.setVisibleLineAfterFullScreen"; text: qsTr("Turn searching of visible line\nafter switching from full screen") }
                    SettingCheck { setting: "shiftTimes.changeValuesWithTab"; text: qsTr("Synchronize time shifting window in all tabs") }
                    SettingCheck { setting: "grid.changeActiveOnSelection"; text: qsTr("Change active line after add to selection") }
                    SettingCheck { setting: "translation.showOriginal"; text: qsTr("Show original in translator mode") }
                    SettingCheck { setting: "translation.hideOriginalOnVideo"; text: qsTr("Hide original on video in translator mode") }
                    SettingCheck { setting: "grid.duplicationDontChangeSelection"; text: qsTr("Do not change selections when duplicating dialogue lines") }
                    SettingCheck { setting: "grid.dontCenterActiveLine"; text: qsTr("Do not vertically center the active line in the subtitle grid") }
                    SettingCheck { setting: "editor.allowNumpadHotkeys"; text: qsTr("Use numpad shortcuts in text fields") }
                    SettingCheck { setting: "video.visualWarningsOff"; text: qsTr("Turn off visual tools warning") }
                    SettingCheck { setting: "video.dontAskForBadResolution"; text: qsTr("Do not warn about resolution mismatch") }
                    SettingCheck { setting: "automation.oldScriptsCompatibility"; text: qsTr("Compatibility with older HikariSub scripts") }
                }
            }

            // Conversion (legacy ConvOpt page).
            ScrollView {
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    GroupBox {
                        title: qsTr("Choose catalog")
                        Layout.fillWidth: true
                        SettingText { setting: "convert.styleCatalog"; anchors.left: parent.left; anchors.right: parent.right }
                    }
                    GroupBox {
                        title: qsTr("Choose style")
                        Layout.fillWidth: true
                        SettingText { setting: "convert.style"; anchors.left: parent.left; anchors.right: parent.right }
                    }
                    GroupBox {
                        title: qsTr("Choose FPS")
                        Layout.fillWidth: true
                        ComboBox {
                            id: fpsBox
                            objectName: "setting_convert.fps"
                            anchors.left: parent.left
                            anchors.right: parent.right
                            editable: true
                            model: ["23.976", "24", "25", "29.97", "30", "60"]
                            Accessible.name: qsTr("Choose FPS")
                            function refresh() { editText = dialog.values["convert.fps"] ?? "" }
                            Component.onCompleted: refresh()
                            Connections { target: dialog; function onReloaded() { fpsBox.refresh() } }
                            onActivated: dialog.put("convert.fps", currentText)
                            onEditTextChanged: if (activeFocus) dialog.put("convert.fps", editText)
                        }
                    }
                    SettingCheck { setting: "convert.fpsFromVideo"; text: qsTr("FPS from video") }
                    SettingCheck { setting: "convert.newEndTimes"; text: qsTr("New end times") }
                    SettingCheck { setting: "convert.showSettings"; text: qsTr("Show window before conversion") }
                    GroupBox {
                        title: qsTr("Time for one letter in milliseconds")
                        Layout.fillWidth: true
                        SettingNumber { setting: "convert.timePerCharacter"; from: 30; to: 1000; anchors.left: parent.left; anchors.right: parent.right }
                    }
                    GroupBox {
                        title: qsTr("Resolution when converting to ASS")
                        Layout.fillWidth: true
                        RowLayout {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            SettingNumber { setting: "convert.resolutionWidth"; from: 1; to: 3000 }
                            Label { text: " X " }
                            SettingNumber { setting: "convert.resolutionHeight"; from: 1; to: 3000 }
                        }
                    }
                    GroupBox {
                        title: qsTr("Tags to paste at the beginning of every ASS line")
                        Layout.fillWidth: true
                        SettingText { setting: "convert.assTagsToInsertInLine"; anchors.left: parent.left; anchors.right: parent.right }
                    }
                }
            }

            // Advanced (legacy EditorAdvanced page).
            ScrollView {
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    SettingCheck { setting: "grid.calcSpacesAndPunctuationForWraps"; text: qsTr("Calculate spaces and punctation characters for wraps") }
                    SettingCheck { setting: "grid.calcSpacesAndPunctuationForCps"; text: qsTr("Calculate spaces and punctation characters for CPS") }
                    SettingNumber {
                        setting: "editor.saveAfterCharacterCount"; from: 0; to: 10000; text: qsTr("Number of edits to save")
                        tip: qsTr("0 turns off saving while editing")
                    }
                    SettingNumber {
                        setting: "autosave.maxFiles"; from: 2; to: 1000000; text: qsTr("Maximum number of autosave files")
                        tip: qsTr("Number of autosaves can be set from 2 to 1000000")
                    }
                    SettingNumber { setting: "grid.insertStartOffset"; from: -100000; to: 100000; text: qsTr("Start frame offset in ms:") }
                    SettingNumber { setting: "grid.insertEndOffset"; from: -100000; to: 100000; text: qsTr("End frame offset in ms:") }
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: qsTr("ASS tag replacement:"); Layout.fillWidth: true }
                        SettingText { setting: "grid.tagsSwapCharacter"; Layout.fillWidth: false; Layout.preferredWidth: 150 }
                    }
                    SettingNumber {
                        setting: "program.tabTextMaxChars"; from: 20; to: 150; text: qsTr("Number of tab name characters")
                        tip: qsTr("Number of tab name characters. Range from 20 to 150")
                    }
                    SettingNumber { setting: "automation.traceLevel"; from: 0; to: 5; text: qsTr("LUA scripts tracking level") }
                    SettingFont { setting: "grid.font"; sizeSetting: "grid.fontSize"; text: qsTr("Subtitle grid font:") }
                    SettingFont { setting: "program.font"; sizeSetting: "program.fontSize"; text: qsTr("Program font:") }
                    GroupBox {
                        title: qsTr("Autoload loading method")
                        Layout.fillWidth: true
                        SettingChoice {
                            setting: "automation.loadingMethod"
                            anchors.left: parent.left
                            anchors.right: parent.right
                            model: [qsTr("After program start asynchronously"), qsTr("After program start"),
                                    qsTr("After open menu asynchronously"), qsTr("After open menu")]
                        }
                    }
                    GroupBox {
                        title: qsTr("Folder with external fonts")
                        Layout.fillWidth: true
                        RowLayout {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            SettingText { id: fontsFolder; setting: "fonts.externalDirectory" }
                            Button {
                                text: qsTr("Choose")
                                onClicked: fontsFolderDialog.open()
                            }
                        }
                    }
                }
            }

            // Video (legacy video page).
            ScrollView {
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    SettingCheck { setting: "video.fullScreenOnStart"; text: qsTr("Open video from context menu on full screen") }
                    SettingCheck { setting: "video.pauseOnClick"; text: qsTr("Left mouse button pauses video") }
                    SettingCheck { setting: "video.openAtActiveLine"; text: qsTr("Open video with time of active line") }
                    SettingCheck { setting: "video.gpuConversion"; text: qsTr("Convert video colours on the GPU (requires reloading)") }
                    GroupBox {
                        title: qsTr("Preferred audio (separated by semicolons)")
                        Layout.fillWidth: true
                        SettingText { setting: "video.acceptedAudioStream"; anchors.left: parent.left; anchors.right: parent.right }
                    }
                    GroupBox {
                        title: qsTr("FFMS2 video seeking method (requires reloading)")
                        Layout.fillWidth: true
                        SettingChoice {
                            setting: "video.ffms2Seeking"
                            anchors.left: parent.left
                            anchors.right: parent.right
                            model: [qsTr("Linear"), qsTr("Normal"), qsTr("Unsafe (always fast)"), qsTr("Aggressive (fast in rewind)")]
                        }
                    }
                    GroupBox {
                        title: qsTr("Start video zoom in percent.")
                        Layout.fillWidth: true
                        SettingNumber { setting: "video.zoomPercent"; from: 100; to: 1100; anchors.left: parent.left; anchors.right: parent.right }
                    }
                }
            }

            // Audio (legacy AudioMain page).
            ScrollView {
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    SettingCheck { setting: "audio.drawTimeCursor"; text: qsTr("Show time next to cursor") }
                    SettingCheck { setting: "audio.drawSecondaryLines"; text: qsTr("Show seconds markers") }
                    SettingCheck { setting: "audio.drawSelectionBackground"; text: qsTr("Show background selection") }
                    SettingCheck { setting: "audio.drawVideoPosition"; text: qsTr("Show video position") }
                    SettingCheck { setting: "audio.drawKeyframes"; text: qsTr("Show keyframes") }
                    SettingCheck { setting: "audio.lockScrollOnCursor"; text: qsTr("Follow audio during playback") }
                    SettingCheck { setting: "audio.autoFocus"; text: qsTr("Activate the audio when hover") }
                    SettingCheck { setting: "audio.snapToKeyframes"; text: qsTr("Snap to keyframe") }
                    SettingCheck { setting: "audio.snapToOtherLines"; text: qsTr("Snap to other lines") }
                    SettingCheck { setting: "audio.dontPlayWhenLineChanges"; text: qsTr("Do not play audio after changing the line") }
                    SettingCheck { setting: "audio.mergeEveryNWithSyllable"; text: qsTr("Merge all the \"n\" with the previous syllable") }
                    SettingCheck { setting: "audio.karaokeMoveOnClick"; text: qsTr("Move syllable line after click") }
                    SettingCheck { setting: "audio.ramCache"; text: qsTr("Load audio into RAM") }
                }
            }

            // Audio > Advanced (legacy AudioSecond page).
            ScrollView {
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    GroupBox {
                        title: qsTr("Audio delay in milliseconds")
                        Layout.fillWidth: true
                        SettingNumber { setting: "audio.delay"; from: -50000000; to: 50000000; anchors.left: parent.left; anchors.right: parent.right }
                    }
                    GroupBox {
                        title: qsTr("Audio to play before and after the marker in milliseconds")
                        Layout.fillWidth: true
                        SettingNumber { setting: "audio.markPlayTime"; from: 400; to: 5000; anchors.left: parent.left; anchors.right: parent.right }
                    }
                    GroupBox {
                        title: qsTr("Lead-in") + " / " + qsTr("Lead-out")
                        Layout.fillWidth: true
                        RowLayout {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            SettingNumber { setting: "audio.leadInValue"; from: 0; to: 10000; text: qsTr("Lead-in") }
                            SettingNumber { setting: "audio.leadOutValue"; from: 0; to: 10000; text: qsTr("Lead-out") }
                        }
                    }
                    GroupBox {
                        title: qsTr("Line boundaries thickness")
                        Layout.fillWidth: true
                        SettingNumber { setting: "audio.lineBoundariesThickness"; from: 1; to: 5; anchors.left: parent.left; anchors.right: parent.right }
                    }
                    GroupBox {
                        title: qsTr("Audio cache files limit")
                        Layout.fillWidth: true
                        SettingNumber {
                            setting: "audio.cacheFilesLimit"; from: 0; to: 10000; anchors.left: parent.left; anchors.right: parent.right
                            tip: qsTr("Range from 0 to 10000, where 0 turns off\nremoving audio cache files.")
                        }
                    }
                    GroupBox {
                        title: qsTr("The way to display inactive lines")
                        Layout.fillWidth: true
                        SettingChoice {
                            setting: "audio.inactiveLinesDisplayMode"
                            anchors.left: parent.left
                            anchors.right: parent.right
                            model: [qsTr("None"), qsTr("Before and after the active"), qsTr("All visible")]
                        }
                    }
                }
            }

            // Subtitle properties (legacy SubtitlesProperties page): the
            // labels pair with the ASS_PROPERTIES_* options as legacy pairs them.
            ScrollView {
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    GroupBox {
                        title: qsTr("Subtitle information")
                        Layout.fillWidth: true
                        GridLayout {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            columns: 3
                            Label { text: qsTr("Title") }
                            SettingText { setting: "scriptProperties.title"; Accessible.name: qsTr("Title") }
                            SettingCheck { setting: "scriptProperties.titleOn"; Accessible.name: qsTr("Title") }
                            Label { text: qsTr("Author") }
                            SettingText { setting: "scriptProperties.script"; Accessible.name: qsTr("Author") }
                            SettingCheck { setting: "scriptProperties.scriptOn"; Accessible.name: qsTr("Author") }
                            Label { text: qsTr("Translator") }
                            SettingText { setting: "scriptProperties.translation"; Accessible.name: qsTr("Translator") }
                            SettingCheck { setting: "scriptProperties.translationOn"; Accessible.name: qsTr("Translator") }
                            Label { text: qsTr("Proofreading") }
                            SettingText { setting: "scriptProperties.editing"; Accessible.name: qsTr("Proofreading") }
                            SettingCheck { setting: "scriptProperties.editingOn"; Accessible.name: qsTr("Proofreading") }
                            Label { text: qsTr("Timer") }
                            SettingText { setting: "scriptProperties.timing"; Accessible.name: qsTr("Timer") }
                            SettingCheck { setting: "scriptProperties.timingOn"; Accessible.name: qsTr("Timer") }
                            Label { text: qsTr("Editing") }
                            SettingText { setting: "scriptProperties.update"; Accessible.name: qsTr("Editing") }
                            SettingCheck { setting: "scriptProperties.updateOn"; Accessible.name: qsTr("Editing") }
                        }
                    }
                    SettingCheck { setting: "scriptProperties.askForChange"; text: qsTr("Always ask before changing subtitle information") }
                }
            }
        }
    }

    FolderDialog {
        id: fontsFolderDialog
        title: qsTr("Choose choose external font folder")
        onAccepted: {
            const path = dialog.app.localPath(selectedFolder)
            fontsFolder.text = path
            dialog.put("fonts.externalDirectory", path)
        }
    }

    // The name legacy FindLanguage gives a tag, to find it in a choice.
    function languageName(tag) {
        for (const l of languages) if (l.tag === tag) return l.name
        for (const d of dictionaries) if (d.tag === tag) return d.name
        return tag
    }
}
