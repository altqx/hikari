pragma Translator: "HikariSub.Karaoke"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import Hikari.Ui

DialogWindow {
    id: dialog
    objectName: "kanjiTimerDialog"
    required property var timer
    title: qsTr("Kanji timer")
    dialogId: "kanjiTimer"
    resizable: true
    initialFocus: strip
    readonly property var pair: timer.pair
    readonly property bool hasPair: pair.groups !== undefined
    readonly property int kanjiHeight: timer.furigana ? 58 : 44
    property bool overRest: false
    property bool closeAnswered: false

    function openDialog() {
        open()
        timer.open()
        strip.forceActiveFocus()
    }
    function apply() {
        if (timer.apply())
            return true
        messages.ask({ title: dialog.title, severity: "error",
                       text: qsTr("The kanji timing could not be applied"),
                       detail: qsTr("The subtitles are read-only.") })
        return false
    }
    function askClose() {
        messages.ask({
            title: dialog.title,
            severity: "warning",
            text: qsTr("Apply the kanji timing of %n lines?", "", timer.pending),
            detail: qsTr("They are done but not in the subtitles yet."),
            choices: [
                { id: "apply", text: qsTr("Apply"), role: "accept", isDefault: true },
                { id: "discard", text: qsTr("Discard"), role: "destructive" },
                { id: "cancel", text: qsTr("Cancel"), role: "reject" }
            ]
        }, id => {
            if (id === "apply" && !dialog.apply())
                return
            if (id === "discard")
                timer.discard()
            if (id === "apply" || id === "discard") {
                dialog.closeAnswered = true
                dialog.close()
            }
        })
    }
    onClosing: close => {
        if (timer.pending > 0 && !closeAnswered) {
            close.accepted = false
            askClose()
            return
        }
        closeAnswered = false
        timer.close()
    }

    function statusText(row) {
        switch (row.status) {
        case "suggested": return qsTr("Suggested")
        case "check": return qsTr("Check")
        case "done": return qsTr("Done")
        case "applied": return qsTr("Applied")
        case "changed": return qsTr("Changed")
        case "noKanji": return qsTr("No kanji line")
        case "noRomaji": return qsTr("No romaji line")
        }
        return qsTr("To do")
    }

    MessageHost {
        id: messages
        objectName: "kanjiTimerMessages"
        settings: dialog.settings
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Label { text: qsTr("Romaji") }
        ComboBox {
            objectName: "kanjiRomajiStyle"
            Layout.preferredWidth: 180
            model: dialog.timer.styles
            currentIndex: dialog.timer.styles.indexOf(dialog.timer.romajiStyle)
            Accessible.name: qsTr("Romaji style")
            onActivated: index => dialog.timer.setStyles(dialog.timer.styles[index], dialog.timer.kanjiStyle)
        }
        Label { text: qsTr("Kanji"); Layout.leftMargin: 12 }
        ComboBox {
            objectName: "kanjiKanjiStyle"
            Layout.preferredWidth: 180
            model: dialog.timer.styles
            currentIndex: dialog.timer.styles.indexOf(dialog.timer.kanjiStyle)
            Accessible.name: qsTr("Kanji style")
            onActivated: index => dialog.timer.setStyles(dialog.timer.romajiStyle, dialog.timer.styles[index])
        }
        Item { Layout.fillWidth: true }
        CheckBox {
            objectName: "kanjiSuggest"
            text: qsTr("Suggest groupings")
            checked: dialog.timer.suggest
            onToggled: dialog.timer.suggest = checked
        }
        CheckBox {
            objectName: "kanjiMulti"
            text: qsTr("Multi-highlight (#)")
            checked: dialog.timer.multi
            onToggled: dialog.timer.multi = checked
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Write each further syllable of a group as a # syllable, for the templater's multi effects")
        }
        CheckBox {
            objectName: "kanjiFurigana"
            text: qsTr("Furigana")
            checked: dialog.timer.furigana
            onToggled: dialog.timer.furigana = checked
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Write each kanji's reading after it (漢|かん), as the romaji sings it")
        }
    }
    RowLayout {
        objectName: "kanjiNoDictionary"
        visible: dialog.timer.noDictionary
        Layout.alignment: Qt.AlignRight
        spacing: 6
        Icon { iconRole: "dictionary"; color: Theme.muted; size: 14 }
        Label {
            color: Theme.muted
            textFormat: Text.PlainText
            text: qsTr("No reading dictionary: readings come from the romaji only.")
        }
    }
    RowLayout {
        objectName: "kanjiSameStyle"
        visible: dialog.timer.problem === "same"
        spacing: 6
        Icon { iconRole: "notice-error"; color: Theme.danger }
        Label { text: qsTr("Choose two different styles."); color: Theme.danger }
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        spacing: 16

        Frame {
            Layout.preferredWidth: 300
            Layout.fillHeight: true
            Layout.preferredHeight: 380
            padding: 1
            background: Rectangle { color: Theme.field; border.color: Theme.line }
            ColumnLayout {
                anchors.centerIn: parent
                width: parent.width - 32
                visible: pairList.count === 0
                spacing: 4
                Label {
                    objectName: "kanjiPairsEmpty"
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    color: Theme.muted
                    textFormat: Text.PlainText
                    text: dialog.timer.problem === "noKaraoke"
                          ? qsTr("No karaoke lines in “%1”").arg(dialog.timer.romajiStyle)
                          : dialog.timer.problem === "noStyles" ? qsTr("The subtitles have no styles")
                                                                : qsTr("No lines to pair")
                }
                Label {
                    visible: dialog.timer.problem === "noKaraoke"
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    color: Theme.muted
                    font: dialog.fontAt(12)
                    text: qsTr("The romaji style needs lines timed with %1 tags.").arg("\\k")
                }
            }
            ListView {
                id: pairList
                objectName: "kanjiPairs"
                anchors.fill: parent
                clip: true
                model: dialog.timer.pairs
                currentIndex: dialog.timer.current
                highlightMoveDuration: 0
                focusPolicy: Qt.NoFocus
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Line pairs")
                ScrollBar.vertical: ScrollBar {}
                delegate: ItemDelegate {
                    id: row
                    required property var modelData
                    required property int index
                    readonly property bool isCurrent: index === dialog.timer.current
                    readonly property bool working: isCurrent && modelData.groups !== undefined
                                                    && modelData.status !== "done" && modelData.status !== "applied"
                    width: ListView.view.width
                    focusPolicy: Qt.NoFocus
                    leftPadding: 10
                    rightPadding: 12
                    topPadding: 5
                    bottomPadding: 5
                    Accessible.name: qsTr("Line pair %1: %2, %3. %4").arg(modelData.number,
                                          modelData.kanji || qsTr("no kanji line"),
                                          modelData.romaji || qsTr("no romaji line"), status.text)
                    background: Rectangle {
                        color: row.isCurrent ? Theme.select : row.hovered ? Theme.raised : "transparent"
                        Rectangle { visible: row.isCurrent; width: 3; height: parent.height; color: Theme.accent }
                        Rectangle { visible: row.index > 0; width: parent.width; height: 1; color: Theme.line; opacity: 0.5 }
                    }
                    contentItem: RowLayout {
                        spacing: 10
                        Label {
                            text: row.modelData.number
                            color: Theme.muted
                            horizontalAlignment: Text.AlignRight
                            Layout.preferredWidth: 22
                            font: dialog.fontAt(12)
                        }
                        ColumnLayout {
                            spacing: 1
                            Layout.fillWidth: true
                            Label {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                textFormat: Text.PlainText
                                text: row.modelData.kanji || "—"
                                color: row.modelData.kanji ? Theme.text : Theme.muted
                                font: dialog.fontAt(14)
                            }
                            Label {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                textFormat: Text.PlainText
                                text: row.modelData.romaji || "—"
                                color: Theme.muted
                                font: dialog.fontAt(12)
                            }
                        }
                        RowLayout {
                            spacing: 4
                            readonly property string kind: row.modelData.status
                            Icon {
                                visible: iconRole.length > 0
                                size: 14
                                iconRole: row.working ? ""
                                        : parent.kind === "done" || parent.kind === "applied" ? "commit"
                                        : parent.kind === "check" ? "notice-warning"
                                        : parent.kind === "changed" ? "refresh" : ""
                                color: parent.kind === "done" ? Theme.success
                                     : parent.kind === "check" || parent.kind === "changed" ? Theme.warning
                                     : IconTheme.normal
                            }
                            Label {
                                id: status
                                objectName: "kanjiPairStatus"
                                font: dialog.fontAt(12)
                                color: row.working ? Theme.text
                                     : parent.kind === "done" ? Theme.success
                                     : parent.kind === "check" || parent.kind === "changed" ? Theme.warning
                                     : Theme.muted
                                text: row.working
                                      ? qsTr("%1 of %2 groups").arg(Math.min(row.modelData.linked + 1, row.modelData.groups),
                                                                    row.modelData.groups)
                                      : dialog.statusText(row.modelData)
                            }
                        }
                    }
                    onClicked: {
                        strip.forceActiveFocus()
                        dialog.timer.choose(index)
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 420
            spacing: 10

            Frame {
                visible: !dialog.hasPair
                Layout.fillWidth: true
                Layout.fillHeight: true
                background: Rectangle { color: Theme.field; border.color: Theme.line }
                Label {
                    objectName: "kanjiNoPair"
                    anchors.centerIn: parent
                    color: Theme.muted
                    text: dialog.timer.problem !== "" ? qsTr("Choose the romaji and kanji styles")
                                                      : qsTr("No pair with both lines")
                }
            }

            RowLayout {
                visible: dialog.hasPair
                spacing: 10
                Label {
                    objectName: "kanjiPairTimes"
                    text: dialog.pair.times || ""
                    font.family: "monospace"
                }
                Label {
                    color: Theme.muted
                    font: dialog.fontAt(12)
                    text: qsTr("%1 s").arg(Number((dialog.pair.duration || 0) / 100).toLocaleString(Qt.locale(), "f", 2))
                }
            }

            FocusScope {
                id: strip
                objectName: "kanjiStrip"
                visible: dialog.hasPair
                Layout.fillWidth: true
                implicitHeight: groupsRow.implicitHeight + 24
                activeFocusOnTab: true
                Accessible.role: Accessible.Grouping
                Accessible.name: qsTr("Groups")
                Accessible.description: qsTr("Up and Down: syllables. Right and Left: kanji. Enter links, Backspace unlinks.")
                Rectangle {
                    anchors.fill: parent
                    color: Theme.field
                    border.color: Theme.control
                    radius: 1
                }
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -2
                    color: "transparent"
                    border.color: Theme.focus
                    visible: strip.activeFocus
                }
                Flickable {
                    id: flick
                    anchors.fill: parent
                    anchors.margins: 12
                    clip: true
                    contentWidth: groupsRow.implicitWidth
                    contentHeight: height
                    flickableDirection: Flickable.HorizontalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    function reveal() {
                        for (let i = 0; i < groupsRow.children.length; ++i) {
                            const item = groupsRow.children[i]
                            if (item.kind === "cur" || (item.kind === "rest" && !dialog.pair.groups.some(g => g.kind === "cur"))) {
                                if (item.x < contentX)
                                    contentX = item.x
                                else if (item.x + item.width > contentX + width)
                                    contentX = Math.min(item.x + item.width - width, Math.max(0, contentWidth - width))
                                return
                            }
                        }
                    }
                    Row {
                        id: groupsRow
                        spacing: 6
                        onImplicitWidthChanged: Qt.callLater(flick.reveal)
                        Repeater {
                            model: dialog.pair.groups || []
                            delegate: Column {
                                id: group
                                required property var modelData
                                required property int index
                                readonly property string kind: modelData.kind
                                objectName: "kanjiGroup" + index
                                spacing: 4
                                Row {
                                    id: syllableRow
                                    spacing: 2
                                    Repeater {
                                        model: group.modelData.syllables
                                        delegate: Cell {
                                            required property var modelData
                                            kind: group.kind
                                            width: Math.max(32, syllableText.implicitWidth + 12)
                                            height: 40
                                            Column {
                                                anchors.centerIn: parent
                                                Label {
                                                    id: syllableText
                                                    anchors.horizontalCenter: parent.horizontalCenter
                                                    textFormat: Text.PlainText
                                                    text: modelData.text
                                                }
                                                Label {
                                                    anchors.horizontalCenter: parent.horizontalCenter
                                                    text: modelData.cs
                                                    color: group.kind === "cur" ? Theme.text : Theme.muted
                                                    font.family: "monospace"
                                                    font.pixelSize: 10
                                                }
                                            }
                                            MouseArea {
                                                anchors.fill: parent
                                                onClicked: {
                                                    strip.forceActiveFocus()
                                                    dialog.timer.clickSyllable(modelData.index)
                                                }
                                            }
                                        }
                                    }
                                }
                                Cell {
                                    id: kanjiCell
                                    kind: group.kind
                                    blank: group.modelData.blank
                                    width: Math.max(syllableRow.implicitWidth, readingText.implicitWidth + 12)
                                    height: dialog.kanjiHeight
                                    readonly property var furigana: dialog.timer.furigana ? group.modelData.furigana : undefined
                                    Column {
                                        anchors.centerIn: parent
                                        Label {
                                            id: readingText
                                            objectName: "kanjiGroupReading"
                                            visible: dialog.timer.furigana
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            height: 14
                                            textFormat: Text.PlainText
                                            text: kanjiCell.furigana ? kanjiCell.furigana.reading : ""
                                            color: kanjiCell.furigana && (kanjiCell.furigana.typed || group.kind === "cur") ? Theme.text : Theme.muted
                                            font: dialog.fontAt(11, kanjiCell.furigana && kanjiCell.furigana.typed && group.kind === "cur" ? Font.Bold : Font.Normal)
                                        }
                                        Label {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            textFormat: Text.PlainText
                                            text: group.modelData.kanji
                                            font: dialog.fontAt(22)
                                        }
                                    }
                                    Rectangle {
                                        objectName: "kanjiGroupDiffers"
                                        visible: kanjiCell.furigana !== undefined && kanjiCell.furigana.differs
                                        anchors.top: parent.top
                                        anchors.right: parent.right
                                        anchors.margins: 3
                                        width: 5
                                        height: 5
                                        radius: 2.5
                                        color: Theme.warning
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: {
                                            strip.forceActiveFocus()
                                            if (!group.modelData.blank)
                                                dialog.timer.clickCharacter(group.modelData.first + group.modelData.characters - 1)
                                        }
                                    }
                                }
                            }
                        }
                        Column {
                            readonly property string kind: "rest"
                            visible: (dialog.pair.later || 0) === 0 && (restSyllables.count > 0 || restKanji.count > 0)
                            spacing: 4
                            HoverHandler { onHoveredChanged: dialog.overRest = hovered }
                            Row {
                                spacing: 2
                                height: 40
                                Repeater {
                                    id: restSyllables
                                    model: dialog.pair.restSyllables || []
                                    delegate: Label {
                                        required property var modelData
                                        objectName: "kanjiRestSyllable"
                                        height: 40
                                        verticalAlignment: Text.AlignVCenter
                                        leftPadding: 6
                                        rightPadding: 6
                                        color: Theme.muted
                                        textFormat: Text.PlainText
                                        text: modelData.text
                                        TapHandler {
                                            onTapped: {
                                                strip.forceActiveFocus()
                                                dialog.timer.clickSyllable(modelData.index)
                                            }
                                        }
                                    }
                                }
                            }
                            Row {
                                spacing: 2
                                height: dialog.kanjiHeight
                                Repeater {
                                    id: restKanji
                                    model: dialog.pair.restKanji || []
                                    delegate: Rectangle {
                                        id: restCell
                                        required property var modelData
                                        objectName: "kanjiRestCharacter"
                                        width: restChar.implicitWidth + 6
                                        height: dialog.kanjiHeight
                                        radius: 1
                                        color: restHover.hovered ? Theme.raised : "transparent"
                                        border.color: restHover.hovered ? Theme.accent : "transparent"
                                        Label {
                                            id: restChar
                                            anchors.centerIn: parent
                                            textFormat: Text.PlainText
                                            text: restCell.modelData.text
                                            color: restHover.hovered ? Theme.text : Theme.muted
                                            font: dialog.fontAt(22)
                                        }
                                        HoverHandler { id: restHover }
                                        TapHandler {
                                            onTapped: {
                                                strip.forceActiveFocus()
                                                dialog.timer.clickCharacter(restCell.modelData.index)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                Icon {
                    objectName: "kanjiEarlierGroups"
                    visible: flick.contentX > 0 || (dialog.pair.earlier || 0) > 0
                    anchors.left: parent.left
                    anchors.leftMargin: 1
                    anchors.verticalCenter: parent.verticalCenter
                    iconRole: "chevron-left"
                    size: 14
                    color: Theme.muted
                }
                Icon {
                    objectName: "kanjiLaterGroups"
                    visible: (dialog.pair.later || 0) > 0 || flick.contentX + flick.width < flick.contentWidth - 1
                    anchors.right: parent.right
                    anchors.rightMargin: 1
                    anchors.verticalCenter: parent.verticalCenter
                    iconRole: "chevron-right"
                    size: 14
                    color: Theme.muted
                }
                Keys.onUpPressed: dialog.timer.growSyllables()
                Keys.onDownPressed: dialog.timer.shrinkSyllables()
                Keys.onRightPressed: dialog.timer.growCharacters()
                Keys.onLeftPressed: dialog.timer.shrinkCharacters()
                Keys.onPressed: event => {
                    const ctrl = event.modifiers & Qt.ControlModifier
                    if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                        if (ctrl)
                            dialog.timer.acceptLine()
                        else
                            dialog.timer.enter()
                    } else if (event.key === Qt.Key_Backspace) {
                        dialog.timer.unlink()
                    } else if (event.matches(StandardKey.Undo)) {
                        dialog.timer.undo()
                    } else if (event.matches(StandardKey.Redo)) {
                        dialog.timer.redo()
                    } else {
                        return
                    }
                    event.accepted = true
                }
            }

            RowLayout {
                objectName: "kanjiNoKanjiLeft"
                visible: dialog.hasPair && (dialog.pair.noKanjiLeft || "") !== ""
                Layout.fillWidth: true
                spacing: 8
                Icon { iconRole: "notice-warning"; color: Theme.warning; Layout.alignment: Qt.AlignTop }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    textFormat: Text.PlainText
                    text: qsTr("“%1” has no kanji left. Enter links it on its own (a blank group); Backspace reopens the group before, where ↑ takes it in.")
                          .arg(dialog.pair.noKanjiLeft || "")
                }
            }
            RowLayout {
                visible: dialog.hasPair && (dialog.pair.noKanjiLeft || "") === ""
                Layout.fillWidth: true
                spacing: 8
                readonly property var detail: dialog.pair.detail || {}
                readonly property var furigana: dialog.timer.furigana && !dialog.overRest ? detail.furigana : undefined
                Label {
                    objectName: "kanjiDetail"
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    color: Theme.muted
                    textFormat: Text.PlainText
                    readonly property var detail: parent.detail
                    text: dialog.overRest ? qsTr("Click a kanji: the current group ends there. Click a syllable: its romaji does.")
                        : detail.romaji === undefined ? ""
                        : detail.blank ? qsTr("blank ← %1 · %2 cs").arg(detail.romaji, detail.cs)
                        : detail.reading ? qsTr("%1 ← %2 (%3) · %4 cs").arg(detail.kanji, detail.romaji, detail.reading, detail.cs)
                        : qsTr("%1 ← %2 · %3 cs").arg(detail.kanji, detail.romaji, detail.cs)
                }
                RowLayout {
                    objectName: "kanjiReadingHint"
                    readonly property string text: !parent.furigana ? ""
                        : parent.furigana.dictionary ? qsTr("Dictionary: %1").arg(parent.furigana.dictionary)
                        : parent.furigana.notSung ? qsTr("Not sung in kana") : ""
                    visible: text.length > 0
                    spacing: 5
                    Icon { iconRole: "dictionary"; color: Theme.muted; size: 14 }
                    Label { color: Theme.muted; font: dialog.fontAt(12); textFormat: Text.PlainText; text: parent.text }
                }
                Label {
                    visible: parent.furigana !== undefined
                    text: qsTr("Reading")
                }
                TextField {
                    id: readingField
                    objectName: "kanjiReading"
                    visible: parent.furigana !== undefined
                    Layout.preferredWidth: 120
                    readonly property string shown: parent.furigana ? parent.furigana.reading : ""
                    text: shown
                    onShownChanged: if (!activeFocus) text = shown
                    placeholderText: qsTr("No furigana")
                    Accessible.name: qsTr("Reading")
                    // MUST KILL: every command focuses the strip first so a reading being typed is set here before it runs
                    onEditingFinished: {
                        if (text !== shown)
                            dialog.timer.setReading(text)
                        text = shown
                    }
                    Keys.onReturnPressed: strip.forceActiveFocus()
                    Keys.onEnterPressed: strip.forceActiveFocus()
                }
                IconButton {
                    objectName: "kanjiUseSuggestion"
                    visible: parent.furigana !== undefined
                    iconRole: "restore-default"
                    text: qsTr("Use the suggestion")
                    flat: true
                    enabled: parent.furigana !== undefined && parent.furigana.typed
                    focusPolicy: Qt.TabFocus
                    onClicked: {
                        dialog.timer.useSuggestion()
                        readingField.text = readingField.shown
                        strip.forceActiveFocus()
                    }
                }
            }

            RowLayout {
                visible: dialog.hasPair
                Layout.fillWidth: true
                spacing: 6
                Button {
                    objectName: "kanjiLink"
                    text: qsTr("Link")
                    focusPolicy: Qt.TabFocus
                    onClicked: { strip.forceActiveFocus(); dialog.timer.link() }
                }
                Button {
                    objectName: "kanjiUnlink"
                    text: qsTr("Unlink")
                    enabled: dialog.pair.canUnlink === true
                    focusPolicy: Qt.TabFocus
                    onClicked: { strip.forceActiveFocus(); dialog.timer.unlink() }
                }
                Rectangle { implicitWidth: 1; implicitHeight: 20; color: Theme.line; Layout.leftMargin: 4; Layout.rightMargin: 4 }
                Button {
                    objectName: "kanjiSkipRomaji"
                    text: qsTr("Skip romaji line")
                    focusPolicy: Qt.TabFocus
                    onClicked: { strip.forceActiveFocus(); dialog.timer.skipRomaji() }
                }
                Button {
                    objectName: "kanjiSkipKanji"
                    text: qsTr("Skip kanji line")
                    focusPolicy: Qt.TabFocus
                    onClicked: { strip.forceActiveFocus(); dialog.timer.skipKanji() }
                }
                Item { Layout.fillWidth: true }
                IconTextButton {
                    objectName: "kanjiPrevious"
                    text: qsTr("Previous")
                    iconRole: "chevron-left"
                    enabled: dialog.pair.canPrevious === true
                    focusPolicy: Qt.TabFocus
                    onClicked: { strip.forceActiveFocus(); dialog.timer.previous() }
                }
                Button {
                    objectName: "kanjiAcceptLine"
                    text: qsTr("Accept line")
                    enabled: dialog.pair.canAccept === true
                    focusPolicy: Qt.TabFocus
                    onClicked: { strip.forceActiveFocus(); dialog.timer.acceptLine() }
                }
            }

            RowLayout {
                visible: dialog.hasPair
                Layout.fillWidth: true
                spacing: 8
                Label { text: qsTr("Result") }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 30
                    color: Theme.field
                    border.color: Theme.line
                    radius: 1
                    clip: true
                    Accessible.role: Accessible.StaticText
                    Accessible.name: qsTr("Result: %1").arg(resultText.text)
                    Text {
                        id: resultText
                        objectName: "kanjiResult"
                        anchors.verticalCenter: parent.verticalCenter
                        x: 8
                        textFormat: Text.StyledText
                        color: Theme.text
                        font.family: "monospace"
                        readonly property string plain: (dialog.pair.result || []).map(p => p.text).join("")
                        text: (dialog.pair.result || []).map(p => {
                            const escaped = Qt.escapeHtml(p.text)
                            return p.tag ? "<font color=\"" + Theme.muted + "\">" + escaped + "</font>" : escaped
                        }).join("")
                    }
                }
                Label {
                    color: Theme.muted
                    font: dialog.fontAt(12)
                    text: qsTr("%1 cs").arg(dialog.pair.total || 0)
                }
            }

            Flow {
                visible: dialog.hasPair
                Layout.fillWidth: true
                spacing: 5
                Keycap { text: "↑" }
                Keycap { text: "↓" }
                Hint { text: qsTr("romaji") }
                Keycap { text: "←" }
                Keycap { text: "→" }
                Hint { text: qsTr("kanji") }
                Keycap { text: qsTr("Enter") }
                Hint { text: qsTr("link") }
                Keycap { text: qsTr("Backspace") }
                Hint { text: qsTr("unlink") }
                Keycap { text: qsTr("Ctrl+Enter") }
                Hint { text: qsTr("accept line") }
                Keycap { text: qsTr("Ctrl+Z") }
                Hint { text: qsTr("undo") }
            }
            Item { Layout.fillHeight: true; visible: dialog.hasPair }
        }
    }

    component Hint: Label { color: Theme.muted; font: dialog.fontAt(12); rightPadding: 6 }
    component Cell: Item {
        property string kind: "done"
        property bool blank: false
        readonly property color fill: kind === "cur" ? Theme.select : kind === "sug" ? "transparent" : Theme.panel
        readonly property color edge: kind === "cur" ? Theme.accent : kind === "sug" ? Theme.control : Theme.line
        Rectangle {
            anchors.fill: parent
            visible: parent.kind !== "sug"
            color: parent.fill
            border.color: parent.edge
            radius: 1
        }
        Shape {
            anchors.fill: parent
            visible: parent.kind === "sug"
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: Theme.control
                strokeWidth: 1
                strokeStyle: ShapePath.DashLine
                dashPattern: [3, 2]
                fillColor: "transparent"
                startX: 0.5; startY: 0.5
                PathLine { x: parent.width - 0.5; y: 0.5 }
                PathLine { x: parent.width - 0.5; y: parent.height - 0.5 }
                PathLine { x: 0.5; y: parent.height - 0.5 }
                PathLine { x: 0.5; y: 0.5 }
            }
        }
        Canvas {
            id: hatch
            anchors.fill: parent
            anchors.margins: 1
            visible: parent.blank
            property color ink: parent.kind === "cur" ? Theme.accent : Theme.line
            onInkChanged: requestPaint()
            onWidthChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.strokeStyle = ink
                ctx.lineWidth = 1
                for (let x = -height; x < width; x += 6) {
                    ctx.beginPath()
                    ctx.moveTo(x, height)
                    ctx.lineTo(x + height, 0)
                    ctx.stroke()
                }
            }
        }
    }

    leadingActions: Button {
        objectName: "kanjiAcceptAll"
        text: qsTr("Accept all suggestions")
        enabled: dialog.timer.problem === "" && dialog.timer.pairs.some(p => p.status === "suggested")
        onClicked: { strip.forceActiveFocus(); dialog.timer.acceptAll() }
    }
    buttons: [
        PrimaryButton {
            objectName: "kanjiApply"
            text: qsTr("Apply")
            enabled: dialog.timer.pending > 0
            DialogButtonBox.buttonRole: DialogButtonBox.ApplyRole
            onClicked: dialog.apply()
        },
        Button {
            objectName: "kanjiClose"
            text: qsTr("Close")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: dialog.close()
        }
    ]
}
