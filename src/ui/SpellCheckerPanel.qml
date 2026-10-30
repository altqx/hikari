pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Spelling"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

FocusScope {
    id: panel
    required property var app
    property ToastStack notices: null
    property string documentName: ""
    property bool ignoreComments: false
    property bool ignoreUpperCase: false
    property bool checking: false
    property var current: ({})
    property var suggestions: []
    property string mode: "spelling"
    readonly property bool grammarMode: mode === "grammar"
    property var grammar: ({})
    property string grammarShown: ""
    readonly property bool found: checking && (grammarMode ? grammar.found === true : current.found === true)
    signal settingsRequested()

    implicitWidth: 760
    implicitHeight: 150
    readonly property size minimumSize: Qt.size(Math.ceil(wordColumn.Layout.minimumWidth + suggestionFrame.Layout.minimumWidth
                                                          + actions.implicitWidth + 2 * columns.spacing) + 16,
                                                Math.ceil(Math.max(actions.implicitHeight, wordColumn.implicitHeight)) + 16)

    // A FocusScope has no font of its own; the hidden fontSource Label gives the body font.
    function fontAt(size, weight) {
        const base = fontSource.font
        const f = Qt.font({family: base.family, weight: weight === undefined ? Font.Normal : weight})
        if (base.pointSize > 0)
            f.pointSize = base.pointSize * size / 13
        else
            f.pixelSize = Math.round(base.pixelSize * size / 13)
        return f
    }
    function options() {
        return {ignoreComments: ignoreComments, ignoreUpperCase: ignoreUpperCase}
    }
    function showGrammar(next, action) {
        grammar = next
        if (next.problem && next.problem.length > 0 && notices)
            notices.post("error", action || qsTr("Grammar check"), next.problem)
        const key = next.found ? [next.index, next.line, next.rule, next.before, next.text].join("\u0001") : ""
        if (key === grammarShown)
            return
        grammarShown = key
        suggestions = next.found ? next.suggestions : []
        suggestionList.currentIndex = suggestions.length > 0 ? 0 : -1
        replacement.text = next.found ? next.replacement : ""
        if (next.found && !replacement.activeFocus && panel.activeFocus)
            replacement.forceActiveFocus()
    }
    function setMode(next) {
        if (next === mode)
            return
        const wasChecking = checking
        stop()
        mode = next
        if (wasChecking)
            start()
    }
    function show(next, action) {
        if (next.unchanged)
            return
        current = next
        if (next.problem && next.problem.length > 0) {
            if (notices)
                notices.post("error", action || qsTr("Replace"), next.problem)
            return
        }
        suggestions = next.suggestions
        suggestionList.currentIndex = suggestions.length > 0 ? 0 : -1
        replacement.text = next.replacement
        if (!next.found && next.available !== false && notices)
            notices.post("info", qsTr("Spellchecker"), qsTr("No spelling errors were found"))
    }
    function start() {
        checking = true
        if (grammarMode) {
            grammarShown = ""
            showGrammar(app.startGrammarWalk())
        } else {
            show(app.openSpellChecker(options()))
        }
        if (found)
            replacement.forceActiveFocus()
        else
            emptyAction.forceActiveFocus()
    }
    function stop() {
        if (!checking)
            return
        checking = false
        current = ({})
        grammar = ({})
        grammarShown = ""
        suggestions = []
        app.closeSpellChecker()
        app.closeGrammarWalk()
    }
    function setIgnoreComments(on) { ignoreComments = on }
    function setIgnoreUpperCase(on) { ignoreUpperCase = on }
    function replace() {
        if (grammarMode)
            showGrammar(app.grammarWalkReplace(replacement.text), qsTr("Replace"))
        else
            show(app.spellCheckerReplace(replacement.text, options()), qsTr("Replace"))
    }
    function replaceAll() { show(app.spellCheckerReplaceAll(current.word, replacement.text, options()), qsTr("Replace all")) }
    function ignore() {
        if (grammarMode)
            showGrammar(app.grammarWalkIgnore())
        else
            show(app.spellCheckerIgnore(options()))
    }
    function ignoreRule() {
        const next = app.grammarWalkIgnoreRule()
        const group = next.ignoredGroup ?? -1
        showGrammar(next)
        if (group < 0 || !notices)
            return
        const groups = app.grammarGroups()
        notices.post("info", qsTr("%1 turned off").arg(next.ignoredGroupName),
                     qsTr("Grammar check no longer uses its %n rules. Turn it back on in Settings › Spelling & grammar.", "",
                          group < groups.length ? groups[group].rules : 0),
                     {actions: [{text: qsTr("Undo"), run: () => panel.app.setGrammarGroup(group, true)},
                                {text: qsTr("Grammar settings…"), run: () => panel.settingsRequested()}]})
    }
    function ignoreAll() { show(app.spellCheckerIgnoreAll(current.word, options())) }
    function addWord() {
        if (grammarMode)
            showGrammar(app.grammarWalkAddWord(), qsTr("Add to dictionary"))
        else
            show(app.spellCheckerAddWord(current.word, options()))
    }
    function openAddedWords() {
        addedWordsLoader.active = true
        addedWordsLoader.item.openWith(app.addedDictionaryWords())
    }

    // MUST KILL: actionPressed scans for a pressed action so focus-in does not swap the word under a click
    function actionPressed() {
        if ([replaceButton, replaceAllButton, ignoreButton, ignoreAllButton, addWordButton, emptyAction].some(b => b.pressed))
            return true
        const delegates = suggestionList.contentItem.children
        for (let i = 0; i < delegates.length; ++i)
            if (delegates[i].pressed === true)
                return true
        return false
    }
    function activate() {
        if (!activeFocus || !checking || grammarMode || actionPressed())
            return
        const next = app.spellCheckerActivated(options())
        if (next.restarted)
            show(next)
    }
    onActiveFocusChanged: if (activeFocus) Qt.callLater(activate)
    // MUST KILL: restarting and restartPending dedupe the restart signals openSpellChecker and restartSpellChecker emit
    property bool restarting: false
    property bool restartPending: false
    function scheduleRestart() {
        if (restartPending)
            return
        restartPending = true
        Qt.callLater(restartCheck)
    }
    function restartCheck() {
        restartPending = false
        if (!checking || grammarMode)
            return
        restarting = true
        show(app.openSpellChecker(options()))
        restarting = false
    }
    Connections {
        target: panel.app
        function onSpellCheckerRestarted() {
            if (panel.checking && !panel.restarting && !panel.grammarMode)
                panel.scheduleRestart()
        }
        function onSpellingChanged() {
            if (panel.checking && !panel.restarting && !panel.grammarMode && panel.current.available === false)
                panel.scheduleRestart()
        }
        function onGrammarWalkChanged() {
            if (panel.checking && panel.grammarMode)
                panel.showGrammar(panel.app.grammarWalkState())
        }
        function onGrammarChanged() {
            if (panel.grammarMode && panel.checking && panel.grammar.on !== panel.app.grammarOn)
                panel.start()
        }
    }
    component ModeSwitch: SegmentedButtons {
        choices: [{text: qsTr("Spelling"), name: "spellModeSpelling"}, {text: qsTr("Grammar"), name: "spellModeGrammar"}]
        currentIndex: panel.grammarMode ? 1 : 0
        onActivated: index => panel.setMode(index === 1 ? "grammar" : "spelling")
        Accessible.name: qsTr("Check")
    }
    component GrammarProgress: RowLayout {
        spacing: 8
        visible: panel.grammarMode && panel.checking && (panel.grammar.total ?? 0) > 0
        ProgressBar {
            visible: panel.grammar.walking === true
            from: 0
            to: Math.max(1, panel.grammar.total ?? 1)
            value: panel.grammar.checked ?? 0
            Layout.preferredWidth: 180
        }
        Label {
            color: Theme.muted
            font: panel.fontAt(12)
            text: panel.grammar.walking === true
                  ? qsTr("Checking line %1 of %2…").arg(panel.grammar.checked ?? 0, panel.grammar.total ?? 0)
                  : panel.grammar.found === true
                  ? qsTr("Finding %1 of %2 · %3").arg(panel.grammar.index, panel.grammar.count,
                                                         qsTr("%n lines checked", "", panel.grammar.checked ?? 0))
                  : ""
        }
        Button {
            objectName: "grammarCancel"
            visible: panel.grammar.walking === true
            text: qsTr("Cancel")
            onClicked: panel.showGrammar(panel.app.cancelGrammarWalk())
        }
    }

    ColumnLayout {
        objectName: "spellEmpty"
        anchors.centerIn: parent
        visible: !panel.found
        spacing: 10
        ModeSwitch {
            objectName: "spellModeEmpty"
            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: 4
        }
        readonly property bool grammarOff: panel.grammarMode && !panel.app.grammarOn
        readonly property bool walking: panel.grammarMode && panel.checking && panel.grammar.walking === true
        GrammarProgress {
            Layout.alignment: Qt.AlignHCenter
            visible: parent.walking
        }
        Label {
            objectName: "spellEmptyText"
            Layout.alignment: Qt.AlignHCenter
            visible: !parent.walking
            color: parent.grammarOff || (panel.checking && panel.grammarMode) ? Theme.text : Theme.muted
            font.bold: parent.grammarOff || (panel.checking && panel.grammarMode)
            text: panel.grammarMode
                ? (parent.grammarOff ? qsTr("Grammar check is off")
                   : !panel.checking ? qsTr("Checks the grammar of the whole file from the active line")
                   : panel.grammar.cancelled === true ? qsTr("Grammar check cancelled")
                   : (panel.grammar.count ?? 0) > 0
                   ? (panel.documentName.length > 0 ? qsTr("No more grammar findings in %1").arg(panel.documentName)
                                                    : qsTr("No more grammar findings"))
                   : panel.documentName.length > 0 ? qsTr("No grammar findings in %1").arg(panel.documentName)
                   : qsTr("No grammar findings"))
                : !panel.checking ? qsTr("Checks the spelling from the active line")
                : panel.current.available === false ? qsTr("No dictionaries found")
                : panel.documentName.length > 0 ? qsTr("No spelling errors in %1").arg(panel.documentName)
                : qsTr("No spelling errors were found")
        }
        Label {
            objectName: "spellEmptyDetail"
            Layout.alignment: Qt.AlignHCenter
            visible: text.length > 0 && !parent.walking
            color: Theme.muted
            text: !panel.grammarMode ? ""
                : parent.grammarOff ? qsTr("Choose an English in Settings to check grammar with Harper.")
                : panel.checking && panel.grammar.done === true ? qsTr("%n lines checked.", "", panel.grammar.checked ?? 0)
                : ""
        }
        Button {
            id: emptyAction
            objectName: "spellEmptyAction"
            Layout.alignment: Qt.AlignHCenter
            visible: !parent.walking
            readonly property bool settings: panel.grammarMode ? parent.grammarOff
                                                               : panel.checking && panel.current.available === false
            text: panel.grammarMode
                ? (settings ? qsTr("Grammar settings…") : panel.checking ? qsTr("Check again") : qsTr("Check grammar"))
                : settings ? qsTr("Spelling settings…") : panel.checking ? qsTr("Check again") : qsTr("Check spelling")
            onClicked: settings ? panel.settingsRequested() : panel.start()
        }
    }

    RowLayout {
        id: columns
        objectName: "spellColumns"
        anchors.fill: parent
        anchors.margins: 8
        spacing: 24
        visible: panel.found
        ColumnLayout {
            id: wordColumn
            Layout.alignment: Qt.AlignTop
            Layout.fillWidth: true
            Layout.minimumWidth: 220
            Layout.preferredWidth: 330
            spacing: 4
            ModeSwitch {
                objectName: "spellMode"
                Layout.bottomMargin: 4
            }
            GrammarProgress {
                objectName: "grammarProgress"
                Layout.fillWidth: true
            }
            Label {
                objectName: "grammarMessage"
                visible: panel.grammarMode
                text: panel.grammar.message ?? ""
                textFormat: Text.PlainText
                font: panel.fontAt(15, Font.DemiBold)
                elide: Text.ElideRight
                Layout.fillWidth: true
                Layout.preferredWidth: 0
                Accessible.name: text
                ToolTip.text: text
                ToolTip.visible: truncated && hover.hovered
                HoverHandler { id: hover }
            }
            Label {
                objectName: "grammarLine"
                visible: panel.grammarMode
                text: qsTr("Line %1 · %2").arg(panel.grammar.line ?? 0, panel.grammar.groupName ?? "")
                textFormat: Text.PlainText
                color: Theme.muted
                font: panel.fontAt(12)
            }
            RowLayout {
                spacing: 10
                visible: !panel.grammarMode
                Layout.fillWidth: true
                Label {
                    id: misspell
                    objectName: "spellMisspell"
                    text: panel.current.word ?? ""
                    font: panel.fontAt(18, Font.DemiBold)
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    Layout.preferredWidth: 0
                    Layout.maximumWidth: Math.ceil(implicitWidth)
                    Accessible.name: qsTr("Misspelled word: %1").arg(text)
                }
                Label {
                    id: lineLabel
                    objectName: "spellLine"
                    visible: (panel.current.line ?? 0) > 0
                    text: qsTr("Line %1").arg(panel.current.line ?? 0)
                    color: Theme.muted
                    font: panel.fontAt(12)
                }
            }
            Label {
                objectName: "spellContext"
                Layout.fillWidth: true
                Layout.preferredWidth: 0
                elide: Text.ElideRight
                color: Theme.muted
                textFormat: Text.StyledText
                readonly property var shown: panel.grammarMode
                    ? {before: panel.grammar.before, word: panel.grammar.text, after: panel.grammar.after, colour: Theme.grammar}
                    : {before: panel.current.before, word: panel.current.word, after: panel.current.after, colour: Theme.text}
                text: Qt.escapeHtml(shown.before ?? "") + "<u><font color=\"" + shown.colour + "\">"
                      + Qt.escapeHtml(shown.word ?? "") + "</font></u>" + Qt.escapeHtml(shown.after ?? "")
                Accessible.name: (shown.before ?? "") + (shown.word ?? "") + (shown.after ?? "")
            }
            RowLayout {
                spacing: 8
                Layout.topMargin: 6
                Layout.fillWidth: true
                Label { text: qsTr("Replace with") }
                TextField {
                    id: replacement
                    objectName: "spellReplacement"
                    Layout.fillWidth: true
                    Layout.maximumWidth: 260
                    Accessible.name: qsTr("Replace with")
                    Keys.onReturnPressed: panel.replace()
                    Keys.onEnterPressed: panel.replace()
                }
            }
        }
        Frame {
            id: suggestionFrame
            Layout.fillHeight: true
            Layout.minimumWidth: 160
            Layout.preferredWidth: 220
            padding: 1
            background: Rectangle { color: Theme.field; border.color: Theme.line }
            Label {
                anchors.centerIn: parent
                visible: suggestionList.count === 0
                color: Theme.muted
                text: qsTr("No suggestions")
            }
            ListView {
                id: suggestionList
                objectName: "spellSuggestions"
                model: panel.suggestions
                clip: true
                anchors.fill: parent
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Suggestions")
                ScrollBar.vertical: ScrollBar {}
                delegate: ItemDelegate {
                    required property string modelData
                    required property int index
                    width: ListView.view.width
                    text: modelData
                    highlighted: ListView.isCurrentItem
                    focusPolicy: Qt.ClickFocus
                    onClicked: {
                        suggestionList.currentIndex = index
                        replacement.text = modelData
                    }
                    onDoubleClicked: {
                        replacement.text = modelData
                        panel.replace()
                    }
                }
            }
        }
        GridLayout {
            id: actions
            Layout.alignment: Qt.AlignTop
            columns: 2
            rowSpacing: 8
            columnSpacing: 8
            PrimaryButton {
                id: replaceButton
                objectName: "spellReplace"
                text: qsTr("Replace")
                Layout.fillWidth: true
                Layout.preferredWidth: 140
                onClicked: panel.replace()
            }
            Button {
                id: replaceAllButton
                objectName: "spellReplaceAll"
                visible: !panel.grammarMode
                text: qsTr("Replace all")
                Layout.fillWidth: true
                Layout.preferredWidth: 140
                onClicked: panel.replaceAll()
            }
            Button {
                id: ignoreButton
                objectName: "spellIgnore"
                text: qsTr("Ignore")
                Layout.fillWidth: true
                onClicked: panel.ignore()
            }
            Button {
                id: ignoreAllButton
                objectName: "spellIgnoreAll"
                visible: !panel.grammarMode
                text: qsTr("Ignore All")
                Layout.fillWidth: true
                onClicked: panel.ignoreAll()
            }
            Button {
                id: ignoreRuleButton
                objectName: "grammarIgnoreRule"
                visible: panel.grammarMode
                text: qsTr("Ignore this rule")
                Layout.fillWidth: true
                Layout.preferredWidth: 140
                onClicked: panel.ignoreRule()
            }
            IconTextButton {
                id: addWordButton
                objectName: "spellAddWord"
                iconRole: "dictionary-add"
                text: qsTr("Add to dictionary")
                enabled: !panel.grammarMode || panel.grammar.word === true
                Layout.columnSpan: panel.grammarMode ? 1 : 2
                Layout.fillWidth: true
                onClicked: panel.addWord()
            }
        }
    }

    Label { id: fontSource; visible: false }
    Loader {
        id: addedWordsLoader
        active: false
        sourceComponent: SpellingAddedWords {
            settings: panel.app.settings
            onRemoveRequested: words => panel.app.removeDictionaryWords(words)
        }
    }
}
