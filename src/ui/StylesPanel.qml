// TP1 (#274): the Styles panel, the editing target's Styles beside the video
// (owner-approved mockup 2026-10-22). Each row draws a sample in the Style's
// font and colours (PV1 #398: the preview text, styles.previewText), with
// its name, font, size and how many Lines use it; the
// current Line's Style is marked. Enter or a double click applies the chosen
// Style to the selected Lines, as the Line editor's Style choice does (one
// undo step). Typing filters. The header's toolbar (Docking.setPanelHeader)
// holds Apply, New, Edit, Duplicate, Delete and Style manager; the row menu
// adds Select lines with this style and Copy to the catalog. Catalogs and the
// Style editor stay in the Style manager window, which New, Edit, Duplicate
// and Copy to the catalog open.
pragma Translator: "HikariSub.Styles"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Item {
    id: panel
    required property var styles  // StyleManagerController
    required property var editor   // LineEditorController: the current line's style, chooseStyle
    required property var app      // selectLinesWithStyle
    required property var manager  // the Style manager window (StyleManager.qml)
    required property var catalogs // FontCatalogsController: the samples' text (previewText)
    // The header's toolbar; its dock reads the rows only while it is open.
    readonly property Item toolbar: tools
    property bool live: false

    // Each row carries `row`, its place in the Document: a file may hold two
    // Styles of one name, and the commands act on the one chosen.
    property var rows: []
    readonly property var shownRows: {
        const text = filterField.text.trim().toLocaleLowerCase()
        return text.length === 0 ? rows : rows.filter(r => r.name.toLocaleLowerCase().includes(text))
    }
    // The chosen Style, kept by name (and row) while the list is read again;
    // the commands act on it only while the filter shows it.
    property string chosen: ""
    property int chosenAt: -1
    readonly property var chosenRow: shownRows.find(r => r.row === chosenAt) ?? null
    onChosenChanged: {
        if (rows[chosenAt]?.name !== chosen)
            chosenAt = rows.findIndex(r => r.name === chosen)
    }
    // A Style named "" is listed but not applied (the Line editor's Style
    // choice refuses it too).
    readonly property bool canApply: chosenRow !== null && chosen.length > 0 && editor.editable && editor.assFields
    // While the Style manager holds an edit not yet applied, nothing here
    // starts another or changes the list under it.
    readonly property bool canChange: !manager.editing
    readonly property bool canChangeChosen: chosenRow !== null && canChange
    readonly property int smallSize: Math.max(9, Math.round(Qt.application.font.pointSize * 0.9))

    // The list moves its current row while its model is replaced (to the
    // first row); the choice is made from the new rows before that, and the
    // move is not taken as one.
    property bool reloading: false
    // `retargeted`: another Document's rows; in the same one a chosen row
    // whose name went (renamed in the Style manager) stays chosen.
    function reload(retargeted) {
        if (!live)
            return
        const next = styles.panelRows().map((r, i) => Object.assign({row: i}, r))
        let name = chosen
        let at = chosenAt
        if (!retargeted && !next.some(r => r.name === name) && at >= 0 && at < next.length
                && next.length === rows.length) {
            name = next[at].name
        } else if (!next.some(r => r.name === name)) {
            name = next.some(r => r.name === editor.style) ? editor.style : (next.length > 0 ? next[0].name : "")
            at = -1
        }
        if (next[at]?.name !== name)
            at = next.findIndex(r => r.name === name)
        reloading = true
        rows = next
        chosenAt = at
        chosen = name
        reloading = false
        list.currentIndex = shownRows.findIndex(r => r.row === chosenAt)
    }
    onLiveChanged: reload(true)
    Connections {
        target: panel.styles
        function onDocumentChanged(retargeted) { panel.reload(retargeted) }
        function onChanged() { panel.reload(false) }
    }
    onShownRowsChanged: list.currentIndex = shownRows.findIndex(r => r.row === chosenAt)

    function apply(name) {
        if (name.length > 0 && editor.editable && editor.assFields)
            editor.chooseStyle(name)
    }
    function remove(row) {
        if (row >= 0 && row < rows.length)
            styles.removeStyles(false, [row])
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 4
        TextField {
            id: filterField
            objectName: "stylesFilter"
            Layout.fillWidth: true
            placeholderText: qsTr("Filter")
            Accessible.name: qsTr("Filter styles")
            Keys.onDownPressed: list.forceActiveFocus(Qt.TabFocusReason)
            Keys.onEscapePressed: event => {
                if (text.length === 0) {
                    event.accepted = false
                    return
                }
                clear()
            }
        }
        RowLayout {
            visible: panel.rows.length > 0
            Layout.fillWidth: true
            Layout.leftMargin: 6
            Layout.rightMargin: 10
            Label { text: qsTr("Style"); color: Theme.muted; font.pointSize: panel.smallSize; Layout.fillWidth: true }
            Label { text: qsTr("Lines"); color: Theme.muted; font.pointSize: panel.smallSize }
        }
        Rectangle { visible: panel.rows.length > 0; Layout.fillWidth: true; implicitHeight: 1; color: Theme.highContrast ? Theme.line : Theme.raised }
        Label {
            objectName: "stylesEmpty"
            visible: panel.shownRows.length === 0
            Layout.margins: 6
            color: Theme.muted
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            text: !panel.styles.available ? qsTr("No styles: the file has no ASS styles")
                : panel.rows.length === 0 ? qsTr("No styles") : qsTr("No matching styles")
        }
        ListView {
            id: list
            objectName: "stylesList"
            visible: count > 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            activeFocusOnTab: true
            boundsBehavior: Flickable.StopAtBounds
            keyNavigationEnabled: true
            highlightMoveDuration: 0
            model: panel.shownRows
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Styles")
            ScrollBar.vertical: ScrollBar { policy: list.contentHeight > list.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }
            // The ring shows while the keyboard has the list (the Grid's row rule).
            property bool keyboardFocus: false
            onActiveFocusChanged: {
                if (!activeFocus)
                    keyboardFocus = false
                else if (currentIndex < 0 && count > 0)
                    currentIndex = 0
            }
            onCurrentIndexChanged: {
                if (!panel.reloading && currentIndex >= 0 && currentIndex < panel.shownRows.length) {
                    panel.chosenAt = panel.shownRows[currentIndex].row
                    panel.chosen = panel.shownRows[currentIndex].name
                }
            }
            Keys.onPressed: event => {
                const row = panel.shownRows[currentIndex]
                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    if (row)
                        panel.apply(row.name)
                    event.accepted = true
                } else if (event.key === Qt.Key_Delete) {
                    if (row)
                        if (panel.canChange)
                            panel.remove(row.row)
                    event.accepted = true
                } else if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && event.modifiers & Qt.ShiftModifier)) {
                    if (row && currentItem)
                        rowMenu.show(row, currentItem, 12, currentItem.height)
                    event.accepted = true
                } else if (event.key === Qt.Key_Home) {
                    currentIndex = 0
                    event.accepted = true
                } else if (event.key === Qt.Key_End) {
                    currentIndex = count - 1
                    event.accepted = true
                } else if (event.text.length > 0 && event.text.trim().length > 0
                           && !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))) {
                    // Typing filters.
                    filterField.text += event.text
                    filterField.forceActiveFocus(Qt.ShortcutFocusReason)
                    event.accepted = true
                }
                if (event.key !== Qt.Key_Tab && event.key !== Qt.Key_Backtab)
                    keyboardFocus = true
            }
            delegate: Item {
                id: row
                required property var modelData
                required property int index
                readonly property bool current: ListView.isCurrentItem
                readonly property bool lineStyle: modelData.name === panel.editor.style
                objectName: "stylesRow" + index
                width: ListView.view.width
                implicitHeight: Math.max(sample.height, rowText.implicitHeight) + 12
                Accessible.role: Accessible.ListItem
                Accessible.name: modelData.name + ", " + modelData.font + " " + modelData.size + ", "
                                 + qsTr("Lines: %1").arg(modelData.lines)
                                 + (lineStyle ? ", " + qsTr("the current line's style") : "")
                Accessible.selected: current

                Rectangle {
                    anchors.fill: parent
                    color: row.current ? Theme.select : hover.hovered ? Theme.raised : "transparent"
                }
                Rectangle { // the chosen row's leading marker
                    visible: row.current
                    width: 3
                    height: parent.height
                    color: Theme.accent
                }
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: Theme.highContrast ? Theme.line : Theme.raised
                }
                Rectangle { // keyboard focus: 1 wide, inside the row, in the text colour
                    anchors.fill: parent
                    color: "transparent"
                    border.width: 1
                    border.color: Theme.focus
                    visible: row.current && list.activeFocus && list.keyboardFocus
                }
                HoverHandler { id: hover }
                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    onTapped: {
                        list.currentIndex = row.index
                        list.forceActiveFocus(Qt.MouseFocusReason)
                    }
                    onDoubleTapped: panel.apply(row.modelData.name)
                }
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: eventPoint => {
                        list.currentIndex = row.index
                        list.forceActiveFocus(Qt.MouseFocusReason)
                        rowMenu.show(row.modelData, row, eventPoint.position.x, eventPoint.position.y)
                    }
                }
                RowLayout {
                    id: rowContent
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 6
                    anchors.rightMargin: 10
                    spacing: 8
                    // The Style's look: the preview text in its font, primary
                    // and outline colours, over a checkered ground (as the
                    // Style editor's preview), so light and dark Styles both
                    // show; a long text shrinks to the swatch.
                    Rectangle {
                        id: sample
                        objectName: "stylesSample" + row.index
                        implicitWidth: 92
                        implicitHeight: 30
                        border.width: 1
                        border.color: Theme.line
                        color: "#6b6b6b"
                        clip: true
                        Grid {
                            anchors.fill: parent
                            anchors.margins: 1
                            columns: 10
                            Repeater {
                                model: 30
                                Rectangle {
                                    required property int index
                                    width: 9
                                    height: 10
                                    color: (index + Math.floor(index / 10)) % 2 === 0 ? "#6b6b6b" : "#8a8a8a"
                                }
                            }
                        }
                        Text {
                            objectName: "stylesSampleText" + row.index
                            anchors.fill: parent
                            anchors.margins: 3
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            textFormat: Text.PlainText
                            text: panel.catalogs.samplePlain(panel.catalogs.previewText, true)
                            fontSizeMode: Text.HorizontalFit
                            minimumPixelSize: 9
                            font.family: row.modelData.font
                            font.pixelSize: 17
                            font.bold: row.modelData.bold
                            font.italic: row.modelData.italic
                            color: row.modelData.primary
                            style: Text.Outline
                            styleColor: row.modelData.outline
                            Accessible.ignored: true
                        }
                    }
                    ColumnLayout {
                        id: rowText
                        spacing: 0
                        Layout.fillWidth: true
                        // The file's names, never markup (no <img> fetched).
                        Label {
                            objectName: "stylesName" + row.index
                            text: row.modelData.name
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Label {
                            objectName: "stylesFont" + row.index
                            text: row.modelData.font + " · " + row.modelData.size
                            textFormat: Text.PlainText
                            color: Theme.muted
                            font.pointSize: panel.smallSize
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                    Icon {
                        objectName: "stylesLineMark" + row.index
                        visible: row.lineStyle
                        iconRole: "edit"
                        ToolTip.visible: markHover.hovered
                        ToolTip.text: qsTr("The current line's style")
                        ToolTip.delay: 500
                        HoverHandler { id: markHover }
                    }
                    Label {
                        text: row.modelData.lines
                        textFormat: Text.PlainText
                        color: Theme.muted
                        font.pointSize: panel.smallSize
                        horizontalAlignment: Text.AlignRight
                        Layout.minimumWidth: 26
                    }
                }
            }
        }
    }

    // The header's toolbar (MuseScore's toolbar slot, as the Original text
    // panel's): icon-only, their names on hover, taking no focus. A header
    // narrower than the buttons shows one "More" button whose menu holds
    // them in the same order. Until the header takes it, it stays out of sight.
    Item {
        id: tools
        objectName: "stylesTools"
        visible: parent !== null && parent.objectName === "dockToolbarSlot"
        implicitWidth: buttons.implicitWidth
        implicitHeight: buttons.implicitHeight
        readonly property bool folded: width + 0.5 < buttons.implicitWidth
        RowLayout {
            id: buttons
            objectName: "stylesButtons"
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            visible: !tools.folded
            spacing: 2
            IconToolButton {
                objectName: "stylesApply"
                iconRole: "style-apply"
                text: qsTr("Apply to the selected lines")
                tip: qsTr("Apply to the selected lines: sets their style (Enter, or double-click a style)")
                focusPolicy: Qt.NoFocus
                enabled: panel.canApply
                onClicked: panel.apply(panel.chosen)
            }
            ToolSeparator { Layout.preferredHeight: 18 }
            IconToolButton {
                objectName: "stylesNew"
                iconRole: "add"
                text: qsTr("New style")
                focusPolicy: Qt.NoFocus
                enabled: panel.styles.available && panel.canChange
                onClicked: panel.manager.newDocumentStyle()
            }
            IconToolButton {
                objectName: "stylesEdit"
                iconRole: "edit"
                text: qsTr("Edit style")
                focusPolicy: Qt.NoFocus
                enabled: panel.canChangeChosen
                onClicked: panel.manager.editStyleAt(panel.chosenAt)
            }
            IconToolButton {
                objectName: "stylesDuplicate"
                iconRole: "duplicate"
                text: qsTr("Duplicate")
                focusPolicy: Qt.NoFocus
                enabled: panel.canChangeChosen
                onClicked: panel.manager.duplicateDocumentStyle(panel.chosenAt)
            }
            IconToolButton {
                objectName: "stylesDelete"
                iconRole: "delete"
                text: qsTr("Delete")
                focusPolicy: Qt.NoFocus
                enabled: panel.canChangeChosen
                onClicked: panel.remove(panel.chosenAt)
            }
            ToolSeparator { Layout.preferredHeight: 18 }
            IconToolButton {
                objectName: "stylesManager"
                iconRole: "styles"
                text: qsTr("Style manager")
                focusPolicy: Qt.NoFocus
                onClicked: panel.manager.showAt(panel.chosenRow ? panel.chosenAt : -1)
            }
        }
        IconToolButton {
            id: more
            objectName: "stylesMore"
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            visible: tools.folded
            iconRole: "menu-more"
            text: qsTr("More")
            focusPolicy: Qt.NoFocus
            onClicked: moreMenu.popup(more, 0, more.height)
        }
    }
    ShellMenu {
        id: moreMenu
        objectName: "stylesMoreMenu"
        ShellMenuItem {
            iconRole: "style-apply"
            text: qsTr("Apply to the selected lines")
            enabled: panel.canApply
            onTriggered: panel.apply(panel.chosen)
        }
        MenuSeparator {}
        ShellMenuItem {
            iconRole: "add"
            text: qsTr("New style")
            enabled: panel.styles.available && panel.canChange
            onTriggered: panel.manager.newDocumentStyle()
        }
        ShellMenuItem {
            iconRole: "edit"
            text: qsTr("Edit style")
            enabled: panel.canChangeChosen
            onTriggered: panel.manager.editStyleAt(panel.chosenAt)
        }
        ShellMenuItem {
            iconRole: "duplicate"
            text: qsTr("Duplicate")
            enabled: panel.canChangeChosen
            onTriggered: panel.manager.duplicateDocumentStyle(panel.chosenAt)
        }
        ShellMenuItem {
            iconRole: "delete"
            text: qsTr("Delete")
            enabled: panel.canChangeChosen
            onTriggered: panel.remove(panel.chosenAt)
        }
        MenuSeparator {}
        ShellMenuItem {
            iconRole: "styles"
            text: qsTr("Style manager")
            onTriggered: panel.manager.showAt(panel.chosenRow ? panel.chosenAt : -1)
        }
    }

    // The row menu (right click, the Menu key, Shift+F10).
    ShellMenu {
        id: rowMenu
        objectName: "stylesRowMenu"
        property string name: ""
        property int row: -1 // its place in the Document
        function show(entry, item, x, y) {
            rowMenu.name = entry.name
            rowMenu.row = entry.row
            rowMenu.popup(item, x, y)
        }
        ShellMenuItem {
            objectName: "stylesMenuApply"
            iconRole: "style-apply"
            text: qsTr("Apply to the selected lines") + "\tEnter"
            enabled: rowMenu.name.length > 0 && panel.editor.editable && panel.editor.assFields
            onTriggered: panel.apply(rowMenu.name)
        }
        MenuSeparator {}
        ShellMenuItem {
            objectName: "stylesMenuEdit"
            enabled: panel.canChange
            iconRole: "edit"
            text: qsTr("Edit…")
            onTriggered: panel.manager.editStyleAt(rowMenu.row)
        }
        ShellMenuItem {
            objectName: "stylesMenuDuplicate"
            enabled: panel.canChange
            iconRole: "duplicate"
            text: qsTr("Duplicate")
            onTriggered: panel.manager.duplicateDocumentStyle(rowMenu.row)
        }
        ShellMenuItem {
            objectName: "stylesMenuDelete"
            enabled: panel.canChange
            iconRole: "delete"
            text: qsTr("Delete") + "\tDel"
            onTriggered: panel.remove(rowMenu.row)
        }
        MenuSeparator {}
        ShellMenuItem {
            objectName: "stylesMenuSelectLines"
            iconRole: "select-lines"
            text: qsTr("Select lines with this style")
            onTriggered: panel.app.selectLinesWithStyle(rowMenu.name)
        }
        ShellMenuItem {
            objectName: "stylesMenuCopyToCatalog"
            enabled: panel.canChange
            iconRole: "copy-to-storage"
            text: qsTr("Copy to the catalog…")
            onTriggered: panel.manager.copyStyleToCatalog(rowMenu.row)
        }
        MenuSeparator {}
        ShellMenuItem {
            objectName: "stylesMenuManager"
            iconRole: "styles"
            text: qsTr("Open in Style manager")
            onTriggered: panel.manager.showAt(rowMenu.row)
        }
    }
}
