pragma Translator: "HikariSub.Search"
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs as Dialogs
import QtQuick.Layouts
import Hikari.Ui

Item {
    id: search
    objectName: "searchTool"
    required property var app
    readonly property alias question: question
    property int tab: 0
    property var finds: []
    property var replacements: []
    property var filterList: []
    property var paths: []
    property var rows: []
    property bool opened: false
    property bool resultsShown: false
    property bool showResults: false
    // MUST KILL: skipActivation makes the next activated() skip taking the editor's selection
    property bool skipActivation: false
    readonly property bool busy: app.findBusy
    readonly property size minimumSize: Qt.size(220 + 6 + 200, 160)

    function showTab(which) {
        if (!opened) {
            apply(app.openFindReplace(which))
            opened = true
        } else if (which !== tab) {
            apply(app.switchFindReplaceTab(settings(), which))
        }
        tab = which
        modes.currentIndex = which
    }
    function focusFind() {
        findText.forceActiveFocus()
    }
    function activated() {
        if (skipActivation) {
            skipActivation = false
            return
        }
        if (opened)
            findText.editText = app.findReplaceActivated(findText.editText)
    }
    function save() {
        if (opened)
            app.saveFindReplaceSettings(settings())
    }
    function destroyTool() {
        save()
        opened = false
        showResults = false
        resultsReplace.initialized = false
        refreshResults()
    }
    function apply(s) {
        finds = s.finds
        replacements = s.replacements
        filterList = s.filterList
        paths = s.paths
        findText.editText = s.find
        replaceText.editText = s.replace
        filtersText.editText = s.filters
        folderText.editText = s.folder
        styles.text = s.styles
        fields.itemAt(s.field).checked = true
        for (let i = 0; i < lineButtons.count; ++i)
            lineButtons.itemAt(i).checked = i === s.lines
        matchCase.checked = s.matchCase
        regex.checked = s.regex
        startOfText.checked = s.startOfText
        endOfText.checked = s.endOfText
        includeComments.checked = s.includeComments
        skipTags.checked = s.skipTags
        skipText.checked = s.skipText
        if (!opened) {
            subfolders.checked = s.subfolders
            hiddenFolders.checked = s.hiddenFolders
        }
    }
    function settings() {
        let field = 0, lines = 3
        for (let i = 0; i < fields.count; ++i) if (fields.itemAt(i).checked) field = i
        for (let i = 0; i < lineButtons.count; ++i) if (lineButtons.itemAt(i).checked) lines = i
        return {tab: tab, find: findText.editText, replace: replaceText.editText, styles: styles.text,
                filters: filtersText.editText, folder: folderText.editText, field: field, lines: lines,
                matchCase: matchCase.checked, regex: regex.checked, startOfText: startOfText.checked,
                endOfText: endOfText.checked, includeComments: includeComments.checked,
                skipTags: skipTags.checked, skipText: skipText.checked, subfolders: subfolders.checked,
                hiddenFolders: hiddenFolders.checked}
    }
    function run(action) {
        if (busy)
            return
        if (action === "findAllCurrent" || action === "findAllTabs" || action === "findInFiles")
            showResults = true
        app.saveFindReplaceSettings(settings())
        app.runFindReplace(action, settings())
    }
    function confirm() {
        if (tab !== 0 && replaceText.activeFocus)
            run(tab === 2 ? "replaceInFiles" : "replace")
        else
            run(tab === 2 ? "findInFiles" : "find")
    }
    function refreshResults() {
        rows = app.findResults()
        resultsShown = app.findResultsShown()
        replaceChecked.enabled = app.canReplaceFindResults()
    }

    Connections {
        target: search.app
        function onFindFinished(s) {
            if (!search.opened)
                return
            search.finds = s.finds
            search.replacements = s.replacements
            search.filterList = s.filterList
            search.paths = s.paths
            findText.editText = s.find
            replaceText.editText = s.replace
            filtersText.editText = s.filters
            folderText.editText = s.folder
            styles.text = s.styles
            if (search.showResults && !resultsReplace.initialized) {
                resultsReplace.editText = s.replacements.length > 0 ? s.replacements[0] : ""
                resultsReplace.initialized = true
            }
            search.showResults = false
        }
        function onFindResultsChanged() { search.refreshResults() }
        function onFindQuestion(id, kind, shown) { question.push(id, kind, shown) }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 6

        ColumnLayout {
            Layout.preferredWidth: 360
            Layout.minimumWidth: 220
            Layout.fillWidth: false
            Layout.fillHeight: true
            spacing: 4
            ScrollView {
                id: rail
                objectName: "searchRail"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth - (ScrollBar.vertical.visible ? ScrollBar.vertical.width + 2 : 0)
                ScrollBar.vertical.policy: contentHeight > availableHeight ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded

                ColumnLayout {
                    width: rail.availableWidth - (rail.ScrollBar.vertical.visible ? rail.ScrollBar.vertical.width + 2 : 0)
                    spacing: 4
                    TabBar {
                        id: modes
                        objectName: "findTabs"
                        Layout.fillWidth: true
                        IconTabButton {
                            objectName: "findTab"
                            iconRole: "search"
                            text: qsTr("Find")
                        }
                        IconTabButton {
                            objectName: "replaceTab"
                            iconRole: "find-replace"
                            text: qsTr("Replace")
                            Accessible.name: qsTr("Find and replace")
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Find and replace")
                        }
                        IconTabButton {
                            objectName: "findInFilesTab"
                            iconRole: "find-in-files"
                            text: qsTr("In files")
                            Accessible.name: qsTr("Find in subtitles")
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Find in subtitles")
                        }
                        onCurrentIndexChanged: {
                            if (search.opened && currentIndex !== search.tab) {
                                search.apply(search.app.switchFindReplaceTab(search.settings(), currentIndex))
                                search.tab = currentIndex
                            }
                        }
                    }
                    GridLayout {
                        columns: 2
                        Layout.fillWidth: true
                        Label { text: qsTr("Search text:") }
                        ComboBox {
                            id: findText
                            objectName: "findText"
                            editable: true
                            model: search.finds
                            Layout.fillWidth: true
                            Accessible.name: qsTr("Search text:")
                            Keys.onReturnPressed: search.confirm()
                            Keys.onEnterPressed: search.confirm()
                        }
                        Label { text: qsTr("Replace with:"); visible: search.tab !== 0 }
                        ComboBox {
                            id: replaceText
                            objectName: "findReplaceText"
                            editable: true
                            visible: search.tab !== 0
                            model: search.replacements
                            Layout.fillWidth: true
                            Accessible.name: qsTr("Replace with:")
                            Keys.onReturnPressed: search.confirm()
                            Keys.onEnterPressed: search.confirm()
                        }
                        Label { text: qsTr("Filters:"); visible: search.tab === 2 }
                        ComboBox {
                            id: filtersText
                            objectName: "findFilters"
                            editable: true
                            visible: search.tab === 2
                            model: search.filterList
                            Layout.fillWidth: true
                            ToolTip.text: qsTr("Windows search filters separated by semicolons, e.g. \"*.ass; *.srt\".")
                            ToolTip.visible: hovered
                            Accessible.name: qsTr("Filters:")
                            Keys.onReturnPressed: search.confirm()
                        }
                        Label { text: qsTr("Catalog:"); visible: search.tab === 2 }
                        RowLayout {
                            visible: search.tab === 2
                            Layout.fillWidth: true
                            ComboBox {
                                id: folderText
                                objectName: "findFolder"
                                editable: true
                                model: search.paths
                                Layout.fillWidth: true
                                Accessible.name: qsTr("Subtitle search folder:")
                                Keys.onReturnPressed: search.confirm()
                            }
                            IconToolButton {
                                objectName: "findChooseFolder"
                                iconRole: "folder-open"
                                text: qsTr("Choose subtitle search folder")
                                onClicked: {
                                    search.skipActivation = true
                                    folderDialog.open()
                                }
                            }
                        }
                    }
                    GridLayout {
                        columns: 2
                        Layout.fillWidth: true
                        CheckBox { id: matchCase; objectName: "findMatchCase"; text: qsTr("Match case") }
                        CheckBox { id: includeComments; objectName: "findIncludeComments"; text: qsTr("Include comments") }
                        CheckBox { id: regex; objectName: "findRegex"; text: qsTr("Regular expressions") }
                        CheckBox {
                            id: skipTags
                            objectName: "findSkipTags"
                            text: qsTr("Skip tags")
                            onToggled: if (checked) skipText.checked = false
                        }
                        CheckBox {
                            id: startOfText
                            objectName: "findStartOfText"
                            text: qsTr("Beginning of text")
                            onToggled: if (checked) endOfText.checked = false
                        }
                        CheckBox {
                            id: skipText
                            objectName: "findSkipText"
                            text: qsTr("Skip text")
                            onToggled: if (checked) skipTags.checked = false
                        }
                        CheckBox {
                            id: endOfText
                            objectName: "findEndOfText"
                            text: qsTr("End of text")
                            onToggled: if (checked) startOfText.checked = false
                        }
                    }
                    GroupBox {
                        title: qsTr("In field")
                        Layout.fillWidth: true
                        RowLayout {
                            ButtonGroup { id: fieldGroup }
                            Repeater {
                                id: fields
                                model: [qsTr("Text"), qsTr("Styles"), qsTr("Actor"), qsTr("Effect")]
                                RadioButton { text: modelData; ButtonGroup.group: fieldGroup }
                            }
                        }
                    }
                    GroupBox {
                        title: qsTr("Lines")
                        visible: search.tab !== 2
                        Layout.fillWidth: true
                        ColumnLayout {
                            anchors.fill: parent
                            RowLayout {
                                ButtonGroup { id: linesGroup }
                                Repeater {
                                    id: lineButtons
                                    model: [qsTr("All lines"), qsTr("Selected lines"), qsTr("From selected")]
                                    RadioButton { text: modelData; ButtonGroup.group: linesGroup; onClicked: search.app.resetFindReplace() }
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                IconToolButton {
                                    id: findChooseStylesButton
                                    objectName: "findChooseStyles"
                                    iconRole: "styles"
                                    text: qsTr("Choose styles")
                                    onClicked: {
                                        search.skipActivation = true
                                        stylesChooser.before = styles.text
                                        stylesChooser.openFor(findChooseStylesButton, search.app.styleNames(),
                                                              styles.text.split(",").map(n => n.trim()),
                                                              { counts: search.app.styleLineCounts() })
                                    }
                                }
                                TextField {
                                    id: styles
                                    objectName: "findStyles"
                                    Layout.fillWidth: true
                                    placeholderText: qsTr("Styles")
                                    Accessible.name: qsTr("Styles")
                                }
                            }
                        }
                    }
                    RowLayout {
                        visible: search.tab === 2
                        CheckBox { id: subfolders; objectName: "findSubfolders"; text: qsTr("Search in subfolders") }
                        CheckBox { id: hiddenFolders; objectName: "findHiddenFolders"; text: qsTr("Search in hidden folders") }
                    }
                }
            }
            GridLayout {
                columns: 2
                Layout.fillWidth: true
                enabled: !search.busy
                Button {
                    objectName: "findButton"
                    text: qsTr("Find")
                    visible: search.tab !== 2
                    Layout.fillWidth: true
                    onClicked: search.run("find")
                }
                Button {
                    objectName: "findAllTabsButton"
                    text: qsTr("Find in all open subtitles")
                    visible: search.tab === 0
                    Layout.fillWidth: true
                    onClicked: search.run("findAllTabs")
                }
                Button {
                    objectName: "findAllCurrentButton"
                    text: qsTr("Find all in current subtitles")
                    visible: search.tab === 0
                    Layout.fillWidth: true
                    onClicked: search.run("findAllCurrent")
                }
                Button {
                    objectName: "replaceNextButton"
                    text: qsTr("Replace next")
                    visible: search.tab === 1
                    Layout.fillWidth: true
                    onClicked: search.run("replace")
                }
                Button {
                    objectName: "replaceAllButton"
                    text: qsTr("Replace all")
                    visible: search.tab === 1
                    Layout.fillWidth: true
                    onClicked: search.run("replaceAll")
                }
                Button {
                    objectName: "replaceAllTabsButton"
                    text: qsTr("Replace in all open subtitles")
                    visible: search.tab === 1
                    Layout.fillWidth: true
                    onClicked: search.run("replaceAllTabs")
                }
                Button {
                    objectName: "findInFilesButton"
                    text: qsTr("Find in subtitles")
                    visible: search.tab === 2
                    Layout.fillWidth: true
                    onClicked: search.run("findInFiles")
                }
                Button {
                    objectName: "replaceInFilesButton"
                    text: qsTr("Replace in subtitles")
                    visible: search.tab === 2
                    Layout.fillWidth: true
                    onClicked: search.run("replaceInFiles")
                }
            }
        }

        ToolSeparator {
            orientation: Qt.Vertical
            Layout.fillHeight: true
        }

        ColumnLayout {
            objectName: "findResultsPane"
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 4
            Accessible.role: Accessible.Pane
            Accessible.name: qsTr("Search results")
            Label {
                objectName: "findResultsEmpty"
                visible: !search.resultsShown
                Layout.fillWidth: true
                Layout.fillHeight: true
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                wrapMode: Text.WordWrap
                color: Theme.muted
                text: qsTr("No results yet")
                ToolTip.visible: emptyHover.hovered
                ToolTip.text: qsTr("Find all, Find in all open subtitles and Find in subtitles list their matches here; " +
                                   "check the ones to change and replace them.")
                HoverHandler { id: emptyHover }
            }
            ListView {
                id: resultsList
                objectName: "findResultsList"
                visible: search.resultsShown
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: search.rows
                ScrollBar.vertical: ScrollBar {}
                delegate: RowLayout {
                    id: resultRow
                    required property var modelData
                    required property int index
                    width: resultsList.width
                    visible: modelData.header || modelData.visible
                    height: visible ? implicitHeight : 0
                    CheckBox {
                        checked: resultRow.modelData.checked
                        Accessible.name: resultRow.modelData.header ? resultRow.modelData.text
                                                                    : (resultRow.modelData.line || "") + resultRow.modelData.text
                        onClicked: search.app.toggleFindResult(resultRow.index)
                    }
                    Label {
                        visible: resultRow.modelData.header
                        text: resultRow.modelData.text
                        font.bold: true
                        Layout.fillWidth: true
                        elide: Text.ElideMiddle
                        TapHandler { onTapped: search.app.toggleFindGroup(resultRow.index) }
                    }
                    Row {
                        visible: !resultRow.modelData.header
                        Layout.fillWidth: true
                        clip: true
                        Label { text: (resultRow.modelData.line || "") + (resultRow.modelData.before || "") }
                        Label {
                            id: matchLabel
                            text: resultRow.modelData.match || ""
                            background: Rectangle { color: matchLabel.palette.highlight }
                            color: matchLabel.palette.highlightedText
                        }
                        Label { text: resultRow.modelData.after || "" }
                        TapHandler { onDoubleTapped: search.app.showFindResult(resultRow.index) }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                visible: search.resultsShown
                enabled: search.resultsShown && !search.busy
                IconTextButton { iconRole: "select-all"; text: qsTr("Check all"); onClicked: search.app.checkFindResults(true) }
                IconTextButton { iconRole: "clear"; text: qsTr("Uncheck all"); onClicked: search.app.checkFindResults(false) }
                ComboBox {
                    id: resultsReplace
                    objectName: "findResultsReplace"
                    property bool initialized: false
                    editable: true
                    model: search.replacements
                    Layout.fillWidth: true
                    Accessible.name: qsTr("Replace with:")
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Replace with:")
                }
                Button {
                    id: replaceChecked
                    objectName: "replaceCheckedButton"
                    text: qsTr("Replace")
                    onClicked: search.app.replaceFindResults(resultsReplace.editText)
                }
            }
        }
    }

    Dialogs.FolderDialog {
        id: folderDialog
        objectName: "findFolderDialog"
        title: qsTr("Choose subtitle search folder")
        onAccepted: folderText.editText = search.app.localPath(selectedFolder)
    }

    ChooseStyles {
        id: stylesChooser
        objectName: "findStylesChooser"
        property string before: ""
        onChanged: (names, reverted) => styles.text = reverted ? before : names.join(",")
    }

    Item {
        id: question
        objectName: "findQuestion"
        visible: false
        property var queue: []
        property int questionId: 0
        property int kind: 0
        property string text: ""
        readonly property bool opened: boxLoader.item !== null && boxLoader.item.visible
        readonly property MessageBox box: boxLoader.item
        // MUST KILL: question kinds are magic ints (1 end reached, 2 some styles missing, 3 no chosen style, else replace in files), answer codes too (0 Ok, 1 Yes, 2 No, 3 Cancel)
        function choicesFor(k) {
            switch (k) {
            case 1:
                return [{ id: "fromStart", text: qsTr("Search from the start"), role: "accept", isDefault: true, code: 1 },
                        { id: "stop", text: qsTr("Stop"), role: "reject", code: 2 }]
            case 2:
                return [{ id: "dropMissing", text: qsTr("Drop the missing styles"), role: "accept", isDefault: true, code: 0 },
                        { id: "everyStyle", text: qsTr("Search every style"), role: "action", code: 1 },
                        { id: "keepStyles", text: qsTr("Keep them"), role: "action", code: 2 },
                        { id: "cancel", text: qsTr("Cancel"), role: "reject", code: 3 }]
            case 3:
                return [{ id: "everyStyle", text: qsTr("Search every style"), role: "accept", isDefault: true, code: 1 },
                        { id: "cancel", text: qsTr("Cancel"), role: "reject", code: 3 }]
            default:
                return [{ id: "replaceInFiles", text: qsTr("Replace in the files"), role: "destructive", code: 1 },
                        { id: "cancel", text: qsTr("Cancel"), role: "reject", isDefault: true, code: 2 }]
            }
        }
        function push(id, k, shown) {
            queue.push({id: id, kind: k, shown: shown})
            // MUST KILL: deferred so the box that asked again has closed before next() opens it
            Qt.callLater(next)
        }
        function next() {
            if (opened || queue.length === 0)
                return
            const q = queue.shift()
            questionId = q.id
            kind = q.kind
            text = q.shown.text
            search.skipActivation = true
            boxLoader.active = true
            const choices = choicesFor(q.kind)
            const id = q.id
            box.ask({ title: q.shown.title, text: q.shown.text, detail: q.shown.detail, severity: q.shown.severity,
                      choices: choices }, answer => {
                const choice = choices.find(c => c.id === answer)
                search.app.answerFindQuestion(id, choice ? choice.code : choices.find(c => c.role === "reject").code)
                Qt.callLater(question.next)
            })
        }
        Loader {
            id: boxLoader
            active: false
            sourceComponent: MessageBox {
                objectName: "findQuestionBox"
                settings: search.app.settings
            }
        }
    }
}
