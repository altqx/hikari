// F1: find and replace as the persistent Search tool
// (docs/qt/ux/reviewed-surface-layouts.md, Search: "B — Scope rail"): the
// scope and query controls of legacy FindReplaceDialog's Find, Find and
// replace and Find in subtitles tabs on the left, the results of
// FindReplaceResultsDialog and their change review (checks, Replace) on the
// right. The legacy message boxes are a queue of non-blocking dialogs; the
// application resumes the search when one is answered (findQuestion /
// answerFindQuestion) and refuses other find work meanwhile (findBusy).
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
    // Legacy blockTextChange: the activation after a message box closes
    // does not take the editor's selection.
    property bool skipActivation: false
    readonly property bool busy: app.findBusy

    // Ctrl+F / Ctrl+H / Edit menu: the tab to show (legacy ShowDialog).
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
    // The tool gains the focus from elsewhere (legacy OnActivate): the text
    // selected in the Line editor becomes the search text.
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
    // DestroyDialogs: FR->SaveOptions(), then the dialog and its FindReplace
    // go; the next opening reads the options and recent lists again.
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
    // Enter (OnEnterConfirm): Replace from the replacement field, else Find.
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
        // The operation's end (also after its questions): AddRecent selects
        // the first entry of each list again; the styles may have changed.
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
        function onFindQuestion(id, kind, text, title) { question.push(id, kind, text, title) }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 6

        // The scope rail: mode, query and scope, scrolling (with its scroll
        // bar shown while there is more) above the action buttons, which
        // stay in view.
        ColumnLayout {
            Layout.preferredWidth: 360
            Layout.minimumWidth: 220
            Layout.fillWidth: false // the results take the rest
            Layout.fillHeight: true
            spacing: 4
            ScrollView {
                id: rail
                objectName: "searchRail"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth
                ScrollBar.vertical.policy: contentHeight > availableHeight ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded

                ColumnLayout {
                    width: rail.availableWidth
                    spacing: 4
                    TabBar {
                        id: modes
                        objectName: "findTabs"
                        Layout.fillWidth: true
                        // K1: the three modes with the set's icons (legacy's
                        // search and findreplace bitmaps, the Edit menu's and
                        // FindReplaceDialog's; find-in-files for the third,
                        // which legacy drew without one). The tabs carry short
                        // names that fit the rail; the full name is the tooltip
                        // and the accessible name.
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
                                text: qsTr("Choose save folder")
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
                        // OnRecheck: Skip tags and Skip text exclude each other, as do
                        // Beginning and End of text.
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
                                    // TabWindow::Reset: the next search starts over.
                                    RadioButton { text: modelData; ButtonGroup.group: linesGroup; onClicked: search.app.resetFindReplace() }
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                IconToolButton {
                                    objectName: "findChooseStyles"
                                    iconRole: "styles"
                                    text: qsTr("Choose styles")
                                    onClicked: {
                                        search.skipActivation = true
                                        stylesDialog.openWith(search.app.styleNames())
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

        // Results and change review (legacy "Search results").
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 4
            Label {
                objectName: "findResultsTitle"
                font.bold: true
                text: qsTr("Search results")
            }
            // The empty state: one muted line in the middle of the pane, with
            // the explanation in its tooltip.
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
            // The review footer, once there are results to review.
            RowLayout {
                Layout.fillWidth: true
                visible: search.resultsShown
                enabled: search.resultsShown && !search.busy
                Button { text: qsTr("Check all"); onClicked: search.app.checkFindResults(true) }
                Button { text: qsTr("Uncheck all"); onClicked: search.app.checkFindResults(false) }
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
        title: qsTr("Choose save folder")
        onAccepted: folderText.editText = search.app.localPath(selectedFolder)
    }

    // Legacy Stylelistbox: OK lists the checked styles; Cancel empties the
    // field (GetCheckedElements returns nothing, kept legacy quirk).
    Dialog {
        id: stylesDialog
        objectName: "findStylesDialog"
        title: qsTr("Choose styles")
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        property var names: []
        property var checkedNames: []
        function openWith(list) {
            names = list
            checkedNames = []
            open()
        }
        ColumnLayout {
            Repeater {
                model: stylesDialog.names
                CheckBox {
                    text: modelData
                    onToggled: {
                        const list = stylesDialog.checkedNames.filter(n => n !== modelData)
                        if (checked)
                            list.push(modelData)
                        stylesDialog.checkedNames = list
                    }
                }
            }
        }
        onAccepted: styles.text = names.filter(n => checkedNames.indexOf(n) >= 0).join(",")
        onRejected: styles.text = ""
    }

    // HikariMessageBox / HikariMessageDialog, one at a time: the search waits
    // for a question's answer without blocking anything (no nested event loop).
    Dialog {
        id: question
        objectName: "findQuestion"
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        property var queue: []
        property int questionId: 0
        property int kind: 0
        property alias text: questionText.text
        property bool answered: true
        function push(id, k, t, caption) {
            queue.push({id: id, kind: k, text: t, title: caption})
            next()
        }
        function next() {
            if (visible || queue.length === 0)
                return
            const q = queue.shift()
            questionId = q.id
            kind = q.kind
            text = q.text
            title = q.title
            answered = false
            search.skipActivation = true
            open()
        }
        // 0 Ok, 1 Yes, 2 No, 3 Cancel.
        function answer(code) {
            if (answered)
                return
            answered = true
            const id = questionId
            close()
            search.app.answerFindQuestion(id, code)
        }
        Label { id: questionText; objectName: "findQuestionText" }
        footer: DialogButtonBox {
            Button {
                objectName: "findAnswerOk"
                text: question.kind === 2 ? qsTr("Remove nonexistent styles") : qsTr("OK")
                visible: question.kind === 0 || question.kind === 2
                onClicked: question.answer(0)
            }
            Button {
                objectName: "findAnswerYes"
                text: question.kind === 2 || question.kind === 3 ? qsTr("Remove styles") : qsTr("Yes")
                visible: question.kind !== 0
                onClicked: question.answer(1)
            }
            Button {
                objectName: "findAnswerNo"
                text: question.kind === 2 ? qsTr("Ignore") : qsTr("No")
                visible: question.kind === 1 || question.kind === 2 || question.kind === 4
                onClicked: question.answer(2)
            }
            Button {
                objectName: "findAnswerCancel"
                text: qsTr("Cancel")
                visible: question.kind === 2 || question.kind === 3
                onClicked: question.answer(3)
            }
        }
        // Escape or a click outside: Cancel, else No (a message: OK).
        onClosed: {
            if (!answered)
                answer(kind === 0 ? 0 : kind === 2 || kind === 3 ? 3 : 2)
            Qt.callLater(next)
        }
    }
}
