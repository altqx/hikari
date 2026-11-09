pragma Translator: "HikariSub.Review"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

FocusScope {
    id: panel
    required property QtObject review
    required property string targetTitle
    signal goToTab(var document)
    signal restoreRequested(var turn)
    readonly property var turns: review ? review.turns : []
    readonly property var here: turns.map(t => Object.assign({}, t, { changes: t.changes.filter(c => c.target) }))
                                     .filter(t => t.changes.length > 0)
    readonly property var elsewhere: {
        const byDocument = new Map()
        for (const t of turns)
            for (const c of t.changes) {
                if (c.target || c.waiting === 0)
                    continue
                const row = byDocument.get(c.document) ?? { document: c.document, title: c.title, waiting: 0 }
                row.waiting += c.waiting
                byDocument.set(c.document, row)
            }
        return Array.from(byDocument.values())
    }
    property var confirming: null
    objectName: "reviewPanel"

    function undo(change, item) {
        if (review.editedSince(change, item) > 0) {
            confirming = { change: change, item: item }
            return
        }
        review.undo(change, item, false)
    }

    component RowButtons: RowLayout {
        id: buttons
        signal undo()
        signal keep()
        spacing: 2
        IconToolButton {
            objectName: "reviewUndo"
            iconRole: "undo"
            text: qsTr("Undo")
            onClicked: buttons.undo()
        }
        IconToolButton {
            objectName: "reviewKeep"
            iconRole: "review-keep"
            text: qsTr("Keep")
            onClicked: buttons.keep()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 8
            spacing: 0
            Label {
                Layout.fillWidth: true
                elide: Text.ElideRight
                font.bold: true
                text: panel.targetTitle
            }
            Label {
                objectName: "reviewWaiting"
                Layout.fillWidth: true
                elide: Text.ElideRight
                color: Theme.muted
                text: panel.review.waiting > 0 ? qsTr("%n change(s) to review", "", panel.review.waiting)
                                               : qsTr("Nothing to review")
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Layout.topMargin: 6
                spacing: 8
                IconTextButton {
                    objectName: "reviewUndoAll"
                    iconRole: "undo"
                    text: qsTr("Undo all")
                    enabled: panel.review.waiting > 0
                    onClicked: panel.undo(0, -1)
                }
                IconTextButton {
                    objectName: "reviewKeepAll"
                    iconRole: "review-keep"
                    text: qsTr("Keep all")
                    highlighted: true
                    enabled: panel.review.waiting > 0
                    onClicked: panel.review.keep(0, -1)
                }
            }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.line }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            ColumnLayout {
                width: parent.width
                spacing: 4
                Repeater {
                    model: panel.here
                    delegate: ColumnLayout {
                        id: turnBlock
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.leftMargin: 8
                        Layout.rightMargin: 8
                        Layout.topMargin: 6
                        spacing: 2
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Icon {
                                iconRole: "checkpoint"
                                size: 16
                                color: IconTheme.normal
                                accentColor: IconTheme.accent
                            }
                            Label {
                                font.bold: true
                                text: qsTr("Turn %1").arg(turnBlock.modelData.number)
                            }
                            Label {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                textFormat: Text.PlainText
                                text: turnBlock.modelData.label
                            }
                            IconTextButton {
                                objectName: "reviewRestore"
                                iconRole: "checkpoint"
                                text: qsTr("Restore checkpoint")
                                enabled: turnBlock.modelData.restorable
                                onClicked: panel.restoreRequested(turnBlock.modelData.turn)
                            }
                        }
                        Repeater {
                            model: turnBlock.modelData.changes
                            delegate: ColumnLayout {
                                id: changeBlock
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 0
                                RowLayout {
                                    Layout.fillWidth: true
                                    Label {
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                        font.bold: true
                                        textFormat: Text.PlainText
                                        text: changeBlock.modelData.name
                                        ToolTip.visible: changeHover.hovered
                                        ToolTip.text: qsTr("By %1").arg(changeBlock.modelData.caller)
                                        HoverHandler { id: changeHover }
                                    }
                                    RowButtons {
                                        visible: changeBlock.modelData.waiting > 0
                                        onUndo: panel.undo(changeBlock.modelData.change, -1)
                                        onKeep: panel.review.keep(changeBlock.modelData.change, -1)
                                    }
                                }
                                Label {
                                    visible: changeBlock.modelData.waiting === 0
                                    color: Theme.muted
                                    text: qsTr("All kept or undone")
                                }
                                Repeater {
                                    model: changeBlock.modelData.items
                                    delegate: ColumnLayout {
                                        id: itemBlock
                                        required property var modelData
                                        readonly property bool asking: panel.confirming !== null
                                                                       && panel.confirming.change === changeBlock.modelData.change
                                                                       && panel.confirming.item === itemBlock.modelData.item
                                        Layout.fillWidth: true
                                        spacing: 0
                                        ItemDelegate {
                                            objectName: "reviewItem"
                                            Layout.fillWidth: true
                                            leftPadding: 10
                                            topPadding: 2
                                            bottomPadding: 2
                                            onClicked: panel.review.show(changeBlock.modelData.change, itemBlock.modelData.item)
                                            Rectangle {
                                                width: 3
                                                height: parent.height
                                                color: itemBlock.modelData.editedSince ? Theme.warning : Theme.accent
                                            }
                                            contentItem: RowLayout {
                                                spacing: 8
                                                Label {
                                                    text: itemBlock.modelData.title
                                                    color: Theme.muted
                                                }
                                                Label {
                                                    Layout.fillWidth: true
                                                    elide: Text.ElideRight
                                                    text: itemBlock.modelData.detail
                                                }
                                                Label {
                                                    visible: itemBlock.modelData.editedSince
                                                    color: Theme.warning
                                                    text: qsTr("you edited it since")
                                                }
                                                RowButtons {
                                                    onUndo: panel.undo(changeBlock.modelData.change, itemBlock.modelData.item)
                                                    onKeep: panel.review.keep(changeBlock.modelData.change, itemBlock.modelData.item)
                                                }
                                            }
                                        }
                                        Pane {
                                            objectName: "reviewConfirm"
                                            visible: itemBlock.asking
                                            Layout.fillWidth: true
                                            padding: 6
                                            background: Rectangle {
                                                color: Theme.raised
                                                border.color: Theme.warning
                                                radius: 4
                                            }
                                            ColumnLayout {
                                                width: parent.width
                                                Label {
                                                    Layout.fillWidth: true
                                                    wrapMode: Text.WordWrap
                                                    text: qsTr("You changed it after this change. Undo puts it back as it was before the AI, which drops your edit.")
                                                }
                                                RowLayout {
                                                    Button {
                                                        objectName: "reviewUndoAnyway"
                                                        text: qsTr("Undo anyway")
                                                        onClicked: {
                                                            const target = panel.confirming
                                                            panel.confirming = null
                                                            panel.review.undo(target.change, target.item, true)
                                                        }
                                                    }
                                                    Button {
                                                        text: qsTr("Cancel")
                                                        onClicked: panel.confirming = null
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                Pane {
                    objectName: "reviewConfirmAll"
                    visible: panel.confirming !== null && panel.confirming.change === 0
                    Layout.fillWidth: true
                    Layout.margins: 8
                    padding: 6
                    background: Rectangle {
                        color: Theme.raised
                        border.color: Theme.warning
                        radius: 4
                    }
                    ColumnLayout {
                        width: parent.width
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: qsTr("You edited some of these lines after the AI did. Undo all leaves them as they are unless you undo them anyway.")
                        }
                        RowLayout {
                            Button {
                                text: qsTr("Undo all anyway")
                                onClicked: {
                                    panel.confirming = null
                                    panel.review.undo(0, -1, true)
                                }
                            }
                            Button {
                                text: qsTr("Leave my edits")
                                onClicked: {
                                    panel.confirming = null
                                    panel.review.undo(0, -1, false)
                                }
                            }
                            Button {
                                text: qsTr("Cancel")
                                onClicked: panel.confirming = null
                            }
                        }
                    }
                }
                Label {
                    visible: panel.here.length === 0
                    Layout.margins: 8
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.muted
                    text: qsTr("Changes the AI makes to this tab show here until you keep or undo them.")
                }
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.line; visible: panel.elsewhere.length > 0 }
        ColumnLayout {
            visible: panel.elsewhere.length > 0
            Layout.fillWidth: true
            Layout.margins: 8
            spacing: 2
            Label {
                color: Theme.muted
                text: qsTr("Also to review")
            }
            Repeater {
                model: panel.elsewhere
                delegate: RowLayout {
                    id: otherRow
                    required property var modelData
                    Layout.fillWidth: true
                    Label {
                        Layout.maximumWidth: implicitWidth
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        textFormat: Text.PlainText
                        font.bold: true
                        text: otherRow.modelData.title
                    }
                    Label {
                        Layout.fillWidth: true
                        color: Theme.muted
                        text: qsTr("%n change(s)", "", otherRow.modelData.waiting)
                    }
                    IconTextButton {
                        objectName: "reviewGoToTab"
                        iconRole: "tab-switch"
                        text: qsTr("Go to tab")
                        onClicked: panel.goToTab(otherRow.modelData.document)
                    }
                }
            }
        }
    }
}
