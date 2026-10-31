pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.MisspellReplacer"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Item {
    id: tool
    objectName: "misspellTool"
    required property var app
    property var rules: []
    property int selectedRule: -1
    property bool editorOpen: true
    readonly property size minimumSize: Qt.size(320, 420)
    readonly property alias results: results
    readonly property int checkedRules: rules.filter(r => r.checked).length

    function reloadRules() {
        rules = app.misspellRules()
        if (selectedRule >= rules.length)
            selectedRule = -1
    }
    function reset() {
        results.close()
        selectedRule = -1
        rules = []
        description.text = ""
        findPhrase.text = ""
        replacePhrase.text = ""
        matchCase.checked = false
        caseChoice.currentIndex = 0
        whereChoice.currentIndex = 0
        whichLines.currentIndex = 0
        styles.text = ""
        rules = []
    }
    // MUST KILL: rule options are raw rules-file bits (1 match case, 2 lower, 4 upper, 8 unchanged, 16 tags only, 32 text only)
    function caseIndexOf(o) { return (o & 8) ? 1 : (o & 2) ? 2 : (o & 4) ? 3 : 0 }
    function whereIndexOf(o) { return (o & 16) && !(o & 32) ? 1 : (o & 32) && !(o & 16) ? 2 : 0 }
    function options() {
        return (matchCase.checked ? 1 : 0) | [0, 8, 2, 4][caseChoice.currentIndex] | [0, 16, 32][whereChoice.currentIndex]
    }
    function fields() {
        return {description: description.text, find: findPhrase.text, replace: replacePhrase.text, options: options()}
    }
    function chooseRule(index) {
        selectedRule = index
        const r = rules[index]
        description.text = r.description
        findPhrase.text = r.find
        replacePhrase.text = r.replace
        matchCase.checked = (r.options & 1) !== 0
        caseChoice.currentIndex = caseIndexOf(r.options)
        whereChoice.currentIndex = whereIndexOf(r.options)
    }
    function scope() {
        return {lines: whichLines.currentIndex, styles: styles.text}
    }
    function allTabs() { return tabsChoice.currentIndex === 1 }
    function find() {
        results.show(app.findMisspells(scope(), allTabs()))
    }
    function shown(text) { return text.replace(/ /g, "·") }

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        GridLayout {
            columns: 2
            columnSpacing: 8
            rowSpacing: 6
            Layout.fillWidth: true
            Label { text: qsTr("Lines"); Layout.preferredWidth: 56 }
            ComboBox {
                id: whichLines
                objectName: "misspellWhichLines"
                Layout.fillWidth: true
                model: [qsTr("All lines"), qsTr("Selected lines"), qsTr("From the selected line"),
                        qsTr("According to the selected styles")]
                Accessible.name: qsTr("Which lines")
            }
            Item { visible: whichLines.currentIndex === 3; implicitWidth: 1 }
            RowLayout {
                visible: whichLines.currentIndex === 3
                spacing: 6
                Layout.fillWidth: true
                TextField {
                    id: styles
                    objectName: "misspellStyles"
                    Layout.fillWidth: true
                    placeholderText: qsTr("Styles")
                    Accessible.name: qsTr("Styles")
                }
                IconToolButton {
                    id: stylesButton
                    objectName: "misspellChooseStyles"
                    iconRole: "styles"
                    text: qsTr("Choose styles")
                    onClicked: {
                        stylesChooser.before = styles.text
                        const names = styles.text.split(",").map(n => n.trim()).filter(n => n.length > 0)
                        stylesChooser.openFor(stylesButton, tool.app.styleNames(), names, { counts: tool.app.styleLineCounts() })
                    }
                }
            }
        }

        RowLayout {
            spacing: 2
            Layout.fillWidth: true
            Layout.topMargin: 4
            IconToolButton {
                objectName: "misspellAddRule"
                iconRole: "add"
                text: qsTr("Add rule")
                enabled: !results.shown
                onClicked: if (tool.app.addMisspellRule(tool.fields())) tool.reloadRules()
            }
            IconToolButton {
                objectName: "misspellEditRule"
                iconRole: "edit"
                text: qsTr("Update rule")
                enabled: !results.shown && tool.selectedRule >= 0
                onClicked: if (tool.app.editMisspellRule(tool.selectedRule, tool.fields())) tool.reloadRules()
            }
            IconToolButton {
                objectName: "misspellRemoveRule"
                iconRole: "delete"
                text: qsTr("Delete rule")
                enabled: !results.shown && tool.selectedRule >= 0
                onClicked: if (tool.app.removeMisspellRule(tool.selectedRule)) tool.reloadRules()
            }
            Item { Layout.fillWidth: true }
            Label {
                objectName: "misspellRuleCount"
                //: %1 the rules turned on, %n all the rules
                text: qsTr("%1 of %n rules on", "", tool.rules.length).arg(tool.checkedRules)
                color: Theme.muted
            }
        }
        Frame {
            padding: 1
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 120
            Layout.preferredHeight: 240
            ListView {
                id: rulesList
                objectName: "misspellRules"
                anchors.fill: parent
                clip: true
                model: tool.rules
                boundsBehavior: Flickable.StopAtBounds
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Rules")
                ScrollBar.vertical: ScrollBar {}
                headerPositioning: ListView.OverlayHeader
                header: Rectangle {
                    z: 2
                    width: rulesList.width
                    height: 24
                    color: Theme.raised
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 8
                        Item { Layout.preferredWidth: 20 }
                        Label { text: qsTr("Rule"); color: Theme.muted; Layout.preferredWidth: rulesList.width * 0.45 }
                        Label { text: qsTr("Find → replace"); color: Theme.muted; Layout.fillWidth: true }
                    }
                }
                delegate: Rectangle {
                    id: ruleRow
                    required property var modelData
                    required property int index
                    width: rulesList.width
                    height: ruleLayout.implicitHeight
                    color: index === tool.selectedRule ? Theme.select : "transparent"
                    Accessible.role: Accessible.ListItem
                    Accessible.name: modelData.description
                    Accessible.selected: index === tool.selectedRule
                    Rectangle {
                        visible: ruleRow.index === tool.selectedRule
                        width: 2
                        height: parent.height
                        color: Theme.accent
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: tool.chooseRule(ruleRow.index)
                    }
                    RowLayout {
                        id: ruleLayout
                        width: parent.width
                        spacing: 8
                        CheckBox {
                            objectName: "misspellRuleCheck" + ruleRow.index
                            checked: ruleRow.modelData.checked
                            Accessible.name: ruleRow.modelData.description
                            Layout.leftMargin: 4
                            onToggled: {
                                tool.app.checkMisspellRule(ruleRow.index, checked)
                                tool.reloadRules()
                            }
                        }
                        Label {
                            text: ruleRow.modelData.description
                            elide: Text.ElideRight
                            Layout.preferredWidth: rulesList.width * 0.45
                        }
                        Label {
                            objectName: "misspellRulePattern" + ruleRow.index
                            text: tool.shown(ruleRow.modelData.find) + "  →  "
                                  + (ruleRow.modelData.replace.length > 0 ? tool.shown(ruleRow.modelData.replace) : qsTr("(nothing)"))
                            font.family: "monospace"
                            color: Theme.muted
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                            Layout.rightMargin: 8
                        }
                    }
                }
            }
        }

        AbstractButton {
            id: editorToggle
            objectName: "misspellRuleSection"
            focusPolicy: Qt.StrongFocus
            Accessible.role: Accessible.ButtonDropDown
            Accessible.name: qsTr("Rule")
            Accessible.description: tool.editorOpen ? qsTr("Expanded") : qsTr("Collapsed")
            Layout.fillWidth: true
            Layout.topMargin: 4
            onClicked: tool.editorOpen = !tool.editorOpen
            contentItem: RowLayout {
                spacing: 4
                Icon { iconRole: tool.editorOpen ? "chevron-down" : "chevron-right"; size: 14 }
                Label { text: qsTr("Rule"); font.weight: Font.DemiBold }
                Label {
                    visible: !tool.editorOpen && description.text.length > 0
                    text: description.text
                    color: Theme.muted
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    Layout.leftMargin: 6
                }
                Item { Layout.fillWidth: true; visible: tool.editorOpen || description.text.length === 0 }
            }
            background: FocusRing { control: editorToggle }
        }
        GridLayout {
            visible: tool.editorOpen
            columns: 2
            columnSpacing: 8
            rowSpacing: 6
            Layout.fillWidth: true
            Label { text: qsTr("Name"); Layout.preferredWidth: 56 }
            TextField {
                id: description
                objectName: "misspellDescription"
                Layout.fillWidth: true
                Accessible.name: qsTr("Rule description")
            }
            Label { text: qsTr("Find") }
            TextField {
                id: findPhrase
                objectName: "misspellFind"
                font.family: "monospace"
                Layout.fillWidth: true
                placeholderText: qsTr("A regular expression")
                Accessible.name: qsTr("Search phrase (regular expressions)")
            }
            Label { text: qsTr("Replace") }
            TextField {
                id: replacePhrase
                objectName: "misspellReplace"
                font.family: "monospace"
                Layout.fillWidth: true
                Accessible.name: qsTr("Replace phrase")
            }
            Label { text: qsTr("Case") }
            RowLayout {
                spacing: 10
                Layout.fillWidth: true
                ComboBox {
                    id: caseChoice
                    objectName: "misspellCase"
                    model: [qsTr("Like the found text"), qsTr("Unchanged"), qsTr("Lower case"), qsTr("Upper case")]
                    Accessible.name: qsTr("Case of the replacement")
                    Layout.preferredWidth: 168
                }
                CheckBox { id: matchCase; objectName: "misspellMatchCase"; text: qsTr("Match case") }
            }
            Label { text: qsTr("In") }
            ComboBox {
                id: whereChoice
                objectName: "misspellWhere"
                model: [qsTr("Text and tags"), qsTr("Tags only"), qsTr("Text only")]
                Accessible.name: qsTr("Replace in")
                Layout.preferredWidth: 168
            }
        }

        RowLayout {
            spacing: 6
            Layout.fillWidth: true
            Layout.topMargin: 6
            SegmentedButtons {
                id: tabsChoice
                objectName: "misspellTabs"
                choices: [{ text: qsTr("This tab"), name: "misspellThisTab" }, { text: qsTr("All tabs"), name: "misspellAllTabs" }]
                Accessible.name: qsTr("Tabs to search")
                onActivated: index => currentIndex = index
            }
            Item { Layout.fillWidth: true }
            Button {
                objectName: "misspellFindButton"
                text: qsTr("Find")
                highlighted: !results.shown
                onClicked: tool.find()
            }
            Button {
                objectName: "misspellReplaceAll"
                text: qsTr("Replace all")
                onClicked: tool.app.replaceMisspells(tool.scope(), tool.allTabs())
            }
        }

        ColumnLayout {
            id: results
            objectName: "misspellResults"
            visible: shown
            spacing: 6
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 6
            property bool shown: false
            property var rows: []
            property var checks: []
            property var folded: []
            property bool canReplace: true
            readonly property int finds: rows.filter(r => !r.header).length
            readonly property int files: rows.filter(r => r.header).length
            readonly property int checkedFinds: rows.filter((r, i) => !r.header && checks[i] === true).length

            function show(list) {
                rows = list
                checks = list.map(() => true)
                folded = list.map(() => false)
                canReplace = true
                shown = true
                tool.editorOpen = false
            }
            function close() {
                shown = false
                rows = []
            }
            onShownChanged: tool.app.setMisspellResultsShown(shown)
            function setChecks(on) { checks = rows.map(() => on) }
            function headerOf(i) {
                while (i >= 0 && !rows[i].header)
                    --i
                return i
            }
            function toggleRow(i) {
                const next = checks.slice()
                if (rows[i].header) {
                    let all = true
                    for (let j = i + 1; j < rows.length && !rows[j].header; ++j)
                        all = all && next[j]
                    for (let j = i + 1; j < rows.length && !rows[j].header; ++j)
                        next[j] = !all
                    next[i] = !all
                } else {
                    next[i] = !next[i]
                    const h = headerOf(i)
                    let any = false
                    for (let j = h + 1; j < rows.length && !rows[j].header; ++j)
                        any = any || next[j]
                    if (h >= 0)
                        next[h] = any
                }
                checks = next
            }
            function stateOf(i) {
                let on = 0, all = 0
                for (let j = i + 1; j < rows.length && !rows[j].header; ++j) {
                    ++all
                    if (checks[j])
                        ++on
                }
                return on === 0 ? Qt.Unchecked : on === all ? Qt.Checked : Qt.PartiallyChecked
            }
            function toggleFold(i) {
                const next = folded.slice()
                next[i] = !next[i]
                folded = next
            }
            function replaceChecked() {
                const list = []
                for (let i = 0; i < rows.length; ++i)
                    if (!rows[i].header && checks[i])
                        list.push(rows[i].find)
                tool.app.replaceMisspellFinds(list)
                canReplace = false
            }

            Rectangle { height: 1; color: Theme.line; Layout.fillWidth: true }
            RowLayout {
                spacing: 10
                Layout.fillWidth: true
                Label { text: qsTr("Results"); font.weight: Font.DemiBold }
                Label {
                    objectName: "misspellResultCount"
                    text: (results.finds === 1 ? qsTr("1 error") : qsTr("%n errors", "", results.finds)) + " "
                          + (results.files === 1 ? qsTr("in 1 file") : qsTr("in %n files", "", results.files))
                    color: Theme.muted
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                IconTextButton {
                    iconRole: "select-all"
                    objectName: "misspellCheckAll"
                    flat: true
                    text: qsTr("All")
                    onClicked: results.setChecks(true)
                }
                IconTextButton {
                    iconRole: "clear"
                    objectName: "misspellUncheckAll"
                    flat: true
                    text: qsTr("None")
                    onClicked: results.setChecks(false)
                }
                IconToolButton {
                    objectName: "misspellCloseResults"
                    iconRole: "tab-close"
                    text: qsTr("Close the results")
                    onClicked: results.close()
                }
            }
            Frame {
                padding: 1
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 100
                ListView {
                    id: resultsList
                    objectName: "misspellResultsList"
                    anchors.fill: parent
                    clip: true
                    model: results.rows
                    boundsBehavior: Flickable.StopAtBounds
                    Accessible.role: Accessible.List
                    Accessible.name: qsTr("Search results")
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Rectangle {
                        id: resultRow
                        required property var modelData
                        required property int index
                        readonly property bool hidden: !modelData.header && results.folded[results.headerOf(index)] === true
                        width: resultsList.width
                        height: hidden ? 0 : resultLayout.implicitHeight
                        visible: !hidden
                        color: modelData.header ? Theme.raised : "transparent"
                        Accessible.role: Accessible.ListItem
                        Accessible.name: modelData.header ? modelData.text : qsTr("Line %1: ").arg(modelData.line) + modelData.text
                        MouseArea {
                            objectName: "misspellResultRow" + resultRow.index
                            anchors.fill: parent
                            enabled: !resultRow.modelData.header
                            onDoubleClicked: tool.app.showMisspellFind(resultRow.modelData.find)
                        }
                        RowLayout {
                            id: resultLayout
                            width: parent.width
                            spacing: 6
                            CheckBox {
                                objectName: "misspellResultCheck" + resultRow.index
                                tristate: resultRow.modelData.header
                                checkState: resultRow.modelData.header ? results.stateOf(resultRow.index)
                                            : results.checks[resultRow.index] === true ? Qt.Checked : Qt.Unchecked
                                Accessible.name: resultRow.modelData.header ? resultRow.modelData.text
                                                 : qsTr("Line %1").arg(resultRow.modelData.line)
                                Layout.leftMargin: 4
                                nextCheckState: function() { return checkState }
                                onClicked: results.toggleRow(resultRow.index)
                            }
                            IconToolButton {
                                objectName: "misspellResultFold" + resultRow.index
                                visible: resultRow.modelData.header
                                iconRole: results.folded[resultRow.index] === true ? "chevron-right" : "chevron-down"
                                iconSize: 14
                                text: results.folded[resultRow.index] === true ? qsTr("Show the finds") : qsTr("Hide the finds")
                                onClicked: results.toggleFold(resultRow.index)
                            }
                            Label {
                                objectName: "misspellResultHeader" + resultRow.index
                                visible: resultRow.modelData.header
                                text: resultRow.modelData.header ? resultRow.modelData.text : ""
                                font.weight: Font.DemiBold
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }
                            Label {
                                visible: !resultRow.modelData.header
                                text: qsTr("Line %1").arg(resultRow.modelData.line)
                                color: Theme.muted
                                Layout.leftMargin: 18
                                Layout.preferredWidth: 64
                            }
                            RowLayout {
                                visible: !resultRow.modelData.header
                                spacing: 0
                                Layout.fillWidth: true
                                Layout.rightMargin: 8
                                Label {
                                    text: resultRow.modelData.header ? "" : resultRow.modelData.text.substring(0, resultRow.modelData.position)
                                }
                                Label {
                                    text: resultRow.modelData.header ? ""
                                          : tool.shown(resultRow.modelData.text.substr(resultRow.modelData.position, resultRow.modelData.length))
                                    color: Theme.onAccent
                                    leftPadding: 1
                                    rightPadding: 1
                                    background: Rectangle { color: Theme.accent; radius: 2 }
                                }
                                Label {
                                    text: resultRow.modelData.header ? ""
                                          : resultRow.modelData.text.substring(resultRow.modelData.position + resultRow.modelData.length)
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Label {
                    objectName: "misspellCheckedCount"
                    text: qsTr("%n checked", "", results.checkedFinds)
                    color: Theme.muted
                }
                Item { Layout.fillWidth: true }
                PrimaryButton {
                    objectName: "misspellReplaceChecked"
                    text: qsTr("Replace checked")
                    enabled: results.canReplace && results.checkedFinds > 0
                    onClicked: results.replaceChecked()
                }
            }
        }
    }

    ChooseStyles {
        id: stylesChooser
        objectName: "misspellStylesChooser"
        property string before: ""
        onChanged: (names, reverted) => styles.text = reverted ? before : names.join(",")
    }
}
