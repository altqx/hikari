pragma Translator: "HikariSub.VisualTools"
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import QtQuick.Layouts
import Hikari.Ui

// HY1 (#271): Hydra's tag list in the inspector bar (legacy AllTagsItem's
// HikariChoice, VideoToolbar.cpp:737-799). The button shows the chosen
// definition and its tag; its list opens with a search field on top and the
// definitions under their sections (Common, More folded with its count, the
// user's own), each with its tag beside it, and "Edit tags…" at the end (the
// Edit button's "Tag editing" dialog). Typing filters every definition at
// once by its name or its tag; Up and Down move through the matches, Enter
// takes the highlighted one (the first), Esc closes the list.
AbstractButton {
    id: picker
    required property var modelData
    required property var live
    required property VisualToolsController tools
    property bool measuring: false
    // VB1: named, unless unseen (measured, or folded off the bar's line).
    property bool named: !measuring
    readonly property var model: live.choices
    readonly property var notes: live.notes ?? []
    readonly property var sections: live.sections ?? []
    readonly property var sectionNames: live.sectionNames ?? []
    readonly property int currentIndex: live.index
    readonly property string current: currentIndex >= 0 && currentIndex < model.length ? model[currentIndex] : ""
    readonly property string currentNote: currentIndex >= 0 && currentIndex < notes.length ? notes[currentIndex] : ""

    objectName: named ? "visualOption_" + modelData.name : ""
    text: modelData.tooltip.split("\n")[0]
    Accessible.role: Accessible.ComboBox
    Accessible.name: text
    Accessible.description: current
    down: pressed || list.visible
    enabled: live.enabled && tools.railEnabled
    focusPolicy: Qt.NoFocus
    hoverEnabled: true
    padding: 0
    implicitHeight: 24
    implicitWidth: row.implicitWidth + 14
    ToolTip.visible: hovered && !list.visible
    ToolTip.text: modelData.tooltip
    Layout.alignment: Qt.AlignVCenter

    // The section titles the tool gives (marked: the bar's words).
    readonly property var sectionTitles: ({
        "Common": qsTr("Common"),
        "More": qsTr("More"),
        "Your tags": qsTr("Your tags")
    })

    // The longest name and tag, so the bar does not move as the tag changes.
    FontMetrics {
        id: metrics
        font: picker.font
    }
    readonly property real widestName: {
        let w = 0
        for (const n of model)
            w = Math.max(w, metrics.advanceWidth(n))
        return Math.ceil(Math.min(w, 140))
    }
    readonly property real widestNote: {
        let w = 0
        for (const n of notes)
            w = Math.max(w, noteMetrics.advanceWidth(n))
        return Math.ceil(Math.min(w, 70))
    }
    FontMetrics {
        id: noteMetrics
        font: noteLabel.font
    }

    contentItem: RowLayout {
        id: row
        spacing: 6
        Icon {
            Layout.leftMargin: 6
            Layout.alignment: Qt.AlignVCenter
            iconRole: "search"
        }
        Label {
            objectName: picker.objectName.length > 0 ? picker.objectName + "_text" : ""
            Layout.preferredWidth: picker.widestName
            text: picker.current
            elide: Text.ElideRight
            color: picker.enabled ? Theme.text : Theme.disabled
        }
        Label {
            id: noteLabel
            Layout.preferredWidth: picker.widestNote
            text: picker.currentNote
            font.family: "monospace"
            font.pixelSize: Math.round(picker.font.pixelSize > 0 ? picker.font.pixelSize * 0.92 : 11)
            color: Theme.muted
            elide: Text.ElideRight
        }
        ColorImage {
            Layout.rightMargin: 6
            source: "qrc:/qt-project.org/imports/QtQuick/Controls/Fusion/images/arrow.png"
            color: picker.enabled ? Theme.text : Theme.disabled
            width: 10
            height: 10
            fillMode: Image.PreserveAspectFit
        }
    }
    background: Rectangle {
        implicitHeight: 24
        radius: 2
        color: picker.down ? Qt.darker(Theme.raised, 1.08) : Theme.raised
        border.color: list.visible ? Theme.accent : picker.enabled ? Theme.control : Theme.line
        border.width: 1
    }
    onClicked: list.visible ? list.keep() : list.openAt(picker, null)

    // The kit's popover (PK1): Esc, a press outside or the button again
    // closes it with the tag as it was; a row or Enter takes one.
    Popover {
        id: list
        objectName: picker.objectName.length > 0 ? picker.objectName + "_menu" : ""
        title: picker.text
        padding: 4
        initialFocus: search
        property bool moreOpen: false
        property int highlighted: -1
        // The rows: section titles and definitions (their index), filtered
        // by the search, then the action.
        readonly property var rows: {
            const q = search.text.trim().replace(/^\\/, "").toLowerCase() // \blur as shown
            const out = []
            if (q.length > 0) {
                for (let i = 0; i < picker.model.length; ++i) {
                    const tag = (picker.notes[i] ?? "").replace(/^\\/, "").toLowerCase()
                    if (picker.model[i].toLowerCase().includes(q) || tag.includes(q))
                        out.push({ kind: "entry", index: i })
                }
                return out
            }
            for (let s = 0; s < picker.sectionNames.length; ++s) {
                const members = []
                for (let i = 0; i < picker.model.length; ++i)
                    if ((picker.sections[i] ?? 0) === s)
                        members.push(i)
                if (members.length === 0)
                    continue
                // More folds unless the chosen tag is in it.
                const folds = picker.sectionNames[s] === "More"
                const open = !folds || moreOpen || members.includes(picker.currentIndex)
                out.push({ kind: "section", section: s, open: open, count: members.length, folds: folds })
                if (open)
                    for (const i of members)
                        out.push({ kind: "entry", index: i })
            }
            return out
        }
        readonly property var entries: rows.filter(r => r.kind === "entry").map(r => r.index)
        readonly property int entryCount: entries.length
        // The choice is taken after the click: another tag can change the
        // bar's options, which makes this picker (and its list) again.
        function choose(index) {
            keep()
            if (index < 0)
                return
            const tools = picker.tools
            const name = picker.modelData.name
            Qt.callLater(() => tools.setOption(name, index))
        }
        function move(by) {
            if (entries.length === 0)
                return
            const at = entries.indexOf(highlighted)
            highlighted = entries[Math.max(0, Math.min(entries.length - 1, (at < 0 ? (by > 0 ? -1 : 0) : at) + by))]
            // The highlighted row kept in view.
            const y = rows.findIndex(r => r.kind === "entry" && r.index === highlighted) * 24
            if (y < rowList.contentY)
                rowList.contentY = y
            else if (y + 24 > rowList.contentY + rowList.height)
                rowList.contentY = y + 24 - rowList.height
        }
        onAboutToShow: {
            search.text = ""
            moreOpen = false
            highlighted = picker.currentIndex
        }
        ColumnLayout {
            spacing: 0
            Layout.preferredWidth: Math.max(picker.width, 260)
            TextField {
                id: search
                objectName: list.objectName.length > 0 ? list.objectName + "_search" : ""
                Layout.fillWidth: true
                Layout.margins: 5
                placeholderText: qsTr("Search tags")
                Accessible.name: qsTr("Search tags")
                onTextChanged: list.highlighted = text.length > 0 ? (list.entries[0] ?? -1) : picker.currentIndex
                Keys.onUpPressed: list.move(-1)
                Keys.onDownPressed: list.move(1)
                function take() {
                    list.choose(list.entries.includes(list.highlighted) ? list.highlighted : (list.entries[0] ?? -1))
                }
                Keys.onReturnPressed: take()
                Keys.onEnterPressed: take()
                Keys.onShortcutOverride: event => event.accepted = event.key !== Qt.Key_Escape
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.line
            }
            // A few dozen rows at most: all made at once (a ListView makes
            // them as it lays out).
            Flickable {
                id: rowList
                objectName: list.objectName.length > 0 ? list.objectName + "_rows" : ""
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(list.rows.length * 24, 24 * 14)
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                contentHeight: rowColumn.height
                Accessible.role: Accessible.List
                Accessible.name: picker.text
                ScrollBar.vertical: ScrollBar {}
                Column {
                    id: rowColumn
                    width: rowList.width
                    Repeater {
                        model: list.rows
                        delegate: Item {
                            id: entry
                            required property var modelData
                            required property int index
                            readonly property bool isSection: modelData.kind === "section"
                            readonly property bool chosen: !isSection && modelData.index === picker.currentIndex
                            readonly property bool lit: !isSection && modelData.index === list.highlighted
                            width: rowColumn.width
                            height: 24
                            objectName: isSection ? list.objectName + "_section" + modelData.section
                                                  : list.objectName + "_" + modelData.index
                            Accessible.role: isSection ? Accessible.Heading : Accessible.ListItem
                            Accessible.name: isSection ? (picker.sectionTitles[picker.sectionNames[modelData.section]]
                                                          ?? picker.sectionNames[modelData.section])
                                                       : picker.model[modelData.index]
                            Accessible.selected: chosen
                            Rectangle {
                                anchors.fill: parent
                                anchors.leftMargin: 2
                                anchors.rightMargin: 2
                                visible: entry.lit || hover.containsMouse && !entry.isSection
                                color: Theme.select
                                border.color: entry.lit ? Theme.accent : "transparent"
                                radius: 2
                            }
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 10
                                spacing: 6
                                Label {
                                    Layout.preferredWidth: 14
                                    text: entry.isSection ? (entry.modelData.open ? "▾" : "▸")
                                                          : entry.chosen ? "✓" : ""
                                    color: entry.isSection ? Theme.muted : Theme.text
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: entry.isSection ? (picker.sectionTitles[picker.sectionNames[entry.modelData.section]]
                                                             ?? picker.sectionNames[entry.modelData.section])
                                                          : picker.model[entry.modelData.index]
                                    font.bold: entry.isSection
                                    color: entry.isSection ? Theme.muted : Theme.text
                                    elide: Text.ElideRight
                                }
                                Label {
                                    text: entry.isSection ? (entry.modelData.open ? "" : String(entry.modelData.count))
                                                          : (picker.notes[entry.modelData.index] ?? "")
                                    font.family: entry.isSection ? picker.font.family : "monospace"
                                    color: Theme.muted
                                }
                            }
                            MouseArea {
                                id: hover
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    if (!entry.isSection)
                                        list.choose(entry.modelData.index)
                                    else if (entry.modelData.folds)
                                        list.moreOpen = !list.moreOpen
                                }
                            }
                        }
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.line
            }
            // The Edit button's dialog, as the list's last row.
            AbstractButton {
                id: editTags
                objectName: list.objectName.length > 0 ? list.objectName + "_edit" : ""
                Layout.fillWidth: true
                Layout.topMargin: 2
                Layout.bottomMargin: 2
                implicitHeight: 24
                hoverEnabled: true
                focusPolicy: Qt.NoFocus
                text: qsTr("Edit tags…")
                Accessible.role: Accessible.MenuItem
                Accessible.name: text
                contentItem: RowLayout {
                    spacing: 6
                    Icon {
                        Layout.leftMargin: 6
                        Layout.preferredWidth: 16
                        iconRole: "tag-edit"
                    }
                    Label {
                        Layout.fillWidth: true
                        text: editTags.text
                        color: Theme.text
                    }
                }
                background: Rectangle {
                    anchors.fill: parent
                    anchors.leftMargin: 2
                    anchors.rightMargin: 2
                    radius: 2
                    color: editTags.hovered ? Theme.select : "transparent"
                }
                onClicked: {
                    list.keep()
                    const tools = picker.tools
                    Qt.callLater(() => tools.setOption("edit", 1))
                }
            }
        }
    }
}
