pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.NodeEffects"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Rectangle {
    id: root
    objectName: "nodeModeBar"
    required property NodeEffectsController controller
    signal leave()
    signal problemChosen(string node)
    implicitHeight: column.implicitHeight
    color: Theme.panel
    readonly property bool editable: controller.editable && !controller.newerFormat

    function milliseconds(ms) {
        return qsTr("%1 ms").arg(ms < 10 ? ms.toFixed(1) : Math.round(ms))
    }
    function karaokeLines(n) {
        return n === 1 ? qsTr("1 karaoke line") : qsTr("%n karaoke lines", "", n)
    }

    ColumnLayout {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.topMargin: 4
            Layout.bottomMargin: 4
            spacing: 6

            Icon {
                iconRole: "node-editor"
            }
            Label {
                text: qsTr("Node effects")
                font.bold: true
            }
            ComboBox {
                id: graphBox
                objectName: "nodeGraphBox"
                Layout.preferredWidth: 180
                visible: root.controller.graphs.length > 0
                model: root.controller.graphs.map(g => g.name)
                currentIndex: root.controller.graphs.findIndex(g => g.line === root.controller.current)
                displayText: root.controller.name
                onActivated: index => root.controller.current = root.controller.graphs[index].line
                Accessible.name: qsTr("Graph")
            }
            Label {
                visible: root.controller.graphs.length === 0
                text: qsTr("No graph")
                color: Theme.muted
            }
            IconToolButton {
                id: graphMenuButton
                objectName: "nodeGraphMenuButton"
                iconRole: "menu-more"
                text: qsTr("Graph menu")
                enabled: root.controller.available
                down: graphMenu.visible
                onClicked: graphMenu.popup(graphMenuButton, 0, graphMenuButton.height)
                Accessible.role: Accessible.ButtonMenu
            }

            Rectangle {
                visible: root.controller.hasGraph
                implicitWidth: 1
                implicitHeight: 20
                color: Theme.line
            }
            Label {
                visible: root.controller.hasGraph
                text: qsTr("Style")
            }
            ComboBox {
                id: styleBox
                objectName: "nodeStyleBox"
                visible: root.controller.hasGraph
                Layout.preferredWidth: 130
                enabled: root.editable
                readonly property string allStyles: qsTr("All styles")
                model: [allStyles].concat(root.controller.styles)
                currentIndex: root.controller.allStyles ? 0 : root.controller.styles.indexOf(root.controller.style) + 1
                displayText: root.controller.allStyles ? allStyles : root.controller.style
                onActivated: index => {
                    if (index === 0) {
                        root.controller.setAllStyles(true)
                    } else {
                        root.controller.setAllStyles(false)
                        root.controller.setStyle(root.controller.styles[index - 1])
                    }
                }
                Accessible.name: qsTr("Style")
            }
            Label {
                visible: root.controller.hasGraph
                text: qsTr("Seed")
            }
            TextField {
                id: seedField
                objectName: "nodeSeed"
                visible: root.controller.hasGraph
                Layout.preferredWidth: 80
                enabled: root.editable
                text: root.controller.seed
                validator: IntValidator { bottom: 0 }
                Accessible.name: qsTr("Seed")
                onEditingFinished: {
                    if (text !== String(root.controller.seed))
                        root.controller.setSeed(Number(text))
                    root.controller.commit()
                }
            }
            IconToolButton {
                objectName: "nodeNewGraphSeed"
                visible: root.controller.hasGraph
                iconRole: "refresh"
                text: qsTr("New seed")
                enabled: root.editable
                onClicked: {
                    root.controller.newSeed()
                    root.controller.commit()
                }
            }

            Item {
                Layout.fillWidth: true
            }

            Button {
                id: problemsButton
                objectName: "nodeProblems"
                visible: root.controller.hasGraph
                flat: true
                enabled: root.controller.problems.length > 0
                icon.source: root.controller.problems.length > 0 ? IconTheme.image("problem", Theme.danger, Theme.danger, false) : ""
                icon.color: "transparent"
                text: root.controller.problems.length === 0 ? qsTr("No problems")
                      : root.controller.problems.length === 1 ? qsTr("1 problem")
                      : qsTr("%n problems", "", root.controller.problems.length)
                down: problemsPopup.visible
                onClicked: problemsPopup.open()
                Accessible.role: Accessible.ButtonDropDown
            }
            Label {
                objectName: "nodeLastApply"
                visible: text !== ""
                color: root.controller.lastApply.failed ? Theme.danger : Theme.muted
                elide: Text.ElideRight
                Layout.maximumWidth: 320
                text: {
                    const a = root.controller.lastApply
                    if (a.failed)
                        return qsTr("Apply stopped with a problem; nothing changed.")
                    if (a.lines === undefined)
                        return ""
                    //: After Apply: %n the Lines written, %1 the karaoke Lines they came from, %2 the run's time
                    return qsTr("Applied: %n lines from %1 in %2", "", a.lines).arg(root.karaokeLines(a.sources), root.milliseconds(a.ms))
                }
            }
            RowLayout {
                spacing: 0
                PrimaryButton {
                    objectName: "nodeApply"
                    text: qsTr("Apply")
                    enabled: root.controller.canApply
                    onClicked: root.controller.apply(false)
                    Accessible.description: qsTr("Apply to every karaoke line")
                }
                PrimaryButton {
                    id: applyArrow
                    objectName: "nodeApplyArrow"
                    implicitWidth: 24
                    leftPadding: 4
                    rightPadding: 4
                    text: "⌄"
                    enabled: root.controller.canApply
                    onClicked: applyMenu.popup(applyArrow, applyArrow.width - applyMenu.width, applyArrow.height)
                    Accessible.name: qsTr("Apply options")
                    Accessible.role: Accessible.ButtonMenu
                }
            }
            IconToolButton {
                objectName: "nodeLeave"
                iconRole: "exit"
                text: qsTr("Leave node effects")
                display: AbstractButton.TextBesideIcon
                onClicked: root.leave()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.line
        }

        RowLayout {
            objectName: "nodeTrustNotice"
            visible: root.controller.hasGraph && root.controller.hasLua && !root.controller.trusted
            Layout.fillWidth: true
            Layout.margins: 8
            spacing: 8
            Icon {
                iconRole: "notice-warning"
                color: Theme.warning
                accentColor: Theme.warning
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("This file’s node effects run Lua code with your rights. Preview them if you trust the file.")
            }
            Button {
                objectName: "nodeTrustButton"
                text: qsTr("Preview")
                icon.source: IconTheme.image("play-line", IconTheme.normal, IconTheme.accent, false)
                icon.color: "transparent"
                onClicked: root.controller.trustDocument()
            }
        }
        RowLayout {
            objectName: "nodeNewerNotice"
            visible: root.controller.newerFormat
            Layout.fillWidth: true
            Layout.margins: 8
            spacing: 8
            Icon {
                iconRole: "notice-info"
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("This graph was saved by a newer HikariSub, so it opens read-only. Update HikariSub to edit it.")
            }
        }
    }

    ShellMenu {
        id: graphMenu
        objectName: "nodeGraphMenu"
        ShellMenuItem {
            objectName: "nodeMenuNew"
            iconRole: "add"
            text: qsTr("New graph")
            enabled: root.controller.editable
            onTriggered: root.controller.newGraph()
        }
        NodePresetMenu {
            controller: root.controller
            enabled: root.controller.editable
        }
        NodeLibraryMenu {
            controller: root.controller
            enabled: root.controller.editable && root.controller.library.length > 0
        }
        MenuSeparator {}
        ShellMenuItem {
            objectName: "nodeMenuRename"
            iconRole: "edit"
            text: qsTr("Rename…")
            enabled: root.controller.current !== 0 && root.editable
            onTriggered: renamePopup.openFor(root.controller.name)
        }
        ShellMenuItem {
            objectName: "nodeMenuDuplicate"
            iconRole: "duplicate"
            text: qsTr("Duplicate")
            enabled: root.controller.hasGraph && root.controller.editable
            onTriggered: root.controller.duplicateGraph()
        }
        ShellMenuItem {
            objectName: "nodeMenuSave"
            iconRole: "save-as"
            text: qsTr("Save to the library")
            help: qsTr("Keeps a copy of this graph in your user folder, to add to other files.")
            enabled: root.controller.hasGraph
            onTriggered: root.controller.saveToLibrary()
        }
        ShellMenuItem {
            objectName: "nodeMenuExport"
            iconRole: "export"
            text: qsTr("Export as template lines")
            help: qsTr("Adds template lines after the graph line that make the same lines in Aegisub's karaoke templater.")
            enabled: root.controller.hasGraph && root.controller.editable
            onTriggered: {
                const blockers = root.controller.exportAsTemplate()
                if (blockers.length > 0)
                    exportPopup.openWith(blockers)
            }
        }
        MenuSeparator {}
        ShellMenuItem {
            objectName: "nodeMenuDelete"
            iconRole: "delete"
            text: qsTr("Delete graph")
            help: qsTr("Removes the graph line and the lines it wrote. Undo brings them back.")
            enabled: root.controller.current !== 0 && root.controller.editable
            onTriggered: root.controller.deleteGraph()
        }
    }

    ShellMenu {
        id: applyMenu
        objectName: "nodeApplyMenu"
        ShellMenuItem {
            objectName: "nodeApplyAll"
            iconRole: "karaoke"
            text: qsTr("Apply to every karaoke line")
            onTriggered: root.controller.apply(false)
        }
        ShellMenuItem {
            objectName: "nodeApplySelected"
            iconRole: "select-lines"
            text: qsTr("Apply to the selected lines")
            help: root.controller.canApplySelected
                  ? qsTr("Only the lines selected in “Source lines” are regenerated; the others keep their output.")
                  : qsTr("Select karaoke lines in “Source lines” first.")
            enabled: root.controller.canApplySelected
            onTriggered: root.controller.apply(true)
        }
    }

    Popup {
        id: exportPopup
        objectName: "nodeExportPopup"
        property var blockers: []
        function openWith(rows) {
            blockers = rows
            open()
        }
        parent: graphMenuButton
        y: graphMenuButton.height
        width: 420
        padding: 8
        contentItem: ColumnLayout {
            spacing: 6
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.bold: true
                text: qsTr("These nodes have no form in a karaoke template, so the graph can't be exported:")
            }
            Repeater {
                model: exportPopup.blockers
                delegate: ItemDelegate {
                    id: blocker
                    required property var modelData
                    required property int index
                    objectName: "nodeExportBlocker_" + index
                    Layout.fillWidth: true
                    Accessible.name: blockerLabel.text
                    contentItem: Label {
                        id: blockerLabel
                        wrapMode: Text.Wrap
                        textFormat: Text.StyledText
                        text: "<b>" + Qt.escapeHtml(blocker.modelData.node) + ":</b> " + Qt.escapeHtml(blocker.modelData.message)
                    }
                    onClicked: {
                        exportPopup.close()
                        if (blocker.modelData.viewNode !== "")
                            root.problemChosen(blocker.modelData.viewNode)
                    }
                }
            }
        }
    }

    Popup {
        id: problemsPopup
        objectName: "nodeProblemsPopup"
        parent: problemsButton
        y: problemsButton.height
        x: problemsButton.width - width
        width: 420
        padding: 8
        contentItem: ColumnLayout {
            spacing: 6
            Repeater {
                model: root.controller.problems
                delegate: ItemDelegate {
                    id: problem
                    required property var modelData
                    required property int index
                    objectName: "nodeProblem_" + index
                    Layout.fillWidth: true
                    Accessible.name: label.text
                    contentItem: RowLayout {
                        spacing: 6
                        Icon {
                            Layout.alignment: Qt.AlignTop
                            iconRole: "problem"
                            color: Theme.danger
                            accentColor: Theme.danger
                        }
                        Label {
                            id: label
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            textFormat: Text.StyledText
                            text: "<b>" + Qt.escapeHtml(problem.modelData.node + (problem.modelData.port !== "" ? " › " + problem.modelData.port : ""))
                                  + ":</b> " + Qt.escapeHtml(problem.modelData.message)
                        }
                    }
                    onClicked: {
                        problemsPopup.close()
                        if (problem.modelData.viewNode !== "")
                            root.problemChosen(problem.modelData.viewNode)
                    }
                }
            }
        }
    }

    Popup {
        id: renamePopup
        objectName: "nodeRenamePopup"
        parent: graphMenuButton
        y: graphMenuButton.height
        width: 280
        padding: 10
        modal: true
        function openFor(name) {
            renameField.text = name
            open()
            renameField.forceActiveFocus()
            renameField.selectAll()
        }
        readonly property string problem: renameField.text === root.controller.name ? "" : root.controller.graphNameProblem(renameField.text)
        function accept() {
            if (problem !== "")
                return
            if (renameField.text !== root.controller.name)
                root.controller.renameGraph(renameField.text)
            close()
        }
        contentItem: ColumnLayout {
            spacing: 6
            Label {
                text: qsTr("Graph name")
                font.bold: true
            }
            TextField {
                id: renameField
                objectName: "nodeRenameField"
                Layout.fillWidth: true
                Accessible.name: qsTr("Graph name")
                onAccepted: renamePopup.accept()
            }
            Label {
                objectName: "nodeRenameProblem"
                visible: renamePopup.problem !== ""
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.danger
                text: renamePopup.problem
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Button {
                    text: qsTr("Cancel")
                    onClicked: renamePopup.close()
                }
                PrimaryButton {
                    objectName: "nodeRenameOk"
                    text: qsTr("Rename")
                    enabled: renamePopup.problem === ""
                    onClicked: renamePopup.accept()
                }
            }
        }
    }
}
