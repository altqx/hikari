// Y1/Y2: legacy StyleStore "Style manager" with its StyleChange editor:
// the catalog ("Styles stored:") above, the Document's Styles ("Styles in
// ASS file:") below, the transfers between them, and the Style editor with
// its preview at the side.
pragma Translator: "HikariSub.Styles"
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Hikari.Ui

Window {
    color: Theme.panel // K2: a Window draws white unless told
    id: manager
    objectName: "styleManager"
    // K1: the set's styles icon as the window's (legacy stylestore SetIcon).
    Component.onCompleted: IconTheme.setWindowIcon(manager, "styles")
    required property var styles   // StyleManagerController
    required property var catalogs // Y6: FontCatalogsController
    required property ColourPickerController colourPicker
    required property var app
    title: qsTr("Style manager")
    // Tall enough for the editor and its preview above the buttons, within
    // the screen's free area (1280 x 800 at 150 % leaves 853 x 533); a
    // shorter window scrolls the editor.
    width: Math.min(1000, Screen.width - 24, Screen.desktopAvailableWidth - 24)
    height: Math.min(810, Screen.height - 48, Screen.desktopAvailableHeight - 48)
    flags: Qt.Dialog

    property var storeSelected: []
    property var assSelected: []
    property bool editingStore: false
    property bool editing: false
    property var values: ({})

    function showFor(styleName) {
        styles.refreshLists()
        const row = styles.documentStyles.indexOf(styleName)
        assSelected = [Math.max(0, row)]
        storeSelected = styles.storeStyles.length > 0 ? [0] : []
        show()
        raise()
        requestActivate()
    }
    // LE6 (#231): the Line editor's Edit style: the editor on the Line's
    // Style, its name field focused, whatever the window showed; a Style
    // the Document lacks is offered for creation, from its Default.
    function editStyle(styleName) {
        styles.refreshLists()
        const row = styles.documentStyles.indexOf(styleName)
        // An open manager keeps its selection until a Style is created.
        if (row >= 0 || !visible) {
            showFor(styleName)
        } else {
            raise()
            requestActivate()
        }
        if (row >= 0) {
            editDocumentStyle(row)
        } else if (styleName.length > 0 && styles.available) {
            createQuestion.name = styleName
            createQuestion.open()
        }
    }
    function editDocumentStyle(row) {
        assSelected = [row]
        beginEditing(styles.beginEdit(false, row), false)
        nameField.forceActiveFocus()
        nameField.selectAll()
    }
    function createStyle(styleName) {
        const v = styles.beginNamed(styleName)
        if (v.name === undefined)
            return
        const problem = styles.commitEdit(v, [], false, false)
        if (problem !== "") {
            styles.endEdit()
            message.text = problem
            message.open()
            return
        }
        editDocumentStyle(styles.documentStyles.indexOf(styleName))
    }
    Dialog {
        id: createQuestion
        objectName: "styleCreateQuestion"
        property string name: ""
        title: qsTr("Missing style")
        modal: true
        anchors.centerIn: parent
        Label { text: qsTr("The document has no style named \"%1\". Create it from Default?").arg(createQuestion.name) }
        footer: DialogButtonBox {
            Button { objectName: "styleCreateYes"; text: qsTr("Create"); DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
            Button { objectName: "styleCreateCancel"; text: qsTr("Cancel"); DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        }
        onAccepted: manager.createStyle(name)
    }
    function closeManager() {
        styles.saveCatalog() // OnClose saves the store
        endEditing()
        close()
    }
    function sorted(list) { return list.slice().sort((a, b) => a - b) }

    // --- Conflict questions, asked one after another --------------------
    property var pendingNames: []
    property var pendingAnswers: []
    property var pendingDone: null
    function askReplace(names, done) {
        pendingNames = names
        pendingAnswers = []
        pendingDone = done
        nextQuestion()
    }
    function nextQuestion() {
        const last = pendingAnswers.length > 0 ? pendingAnswers[pendingAnswers.length - 1] : ""
        if (pendingAnswers.length >= pendingNames.length || last === "yesToAll" || last === "cancel") {
            const done = pendingDone
            pendingDone = null
            done(pendingAnswers)
            return
        }
        replaceQuestion.name = pendingNames[pendingAnswers.length]
        replaceQuestion.open()
    }
    function answer(a) {
        replaceQuestion.close()
        pendingAnswers = pendingAnswers.concat([a])
        // After the click: the question's button box closes the dialog
        // too (each answer has a role), which would close the next one.
        Qt.callLater(nextQuestion)
    }
    Dialog {
        id: replaceQuestion
        objectName: "styleReplaceQuestion"
        property string name: ""
        title: qsTr("Confirmation")
        modal: true
        anchors.centerIn: parent
        Label { text: qsTr("Style named \"%1\" already exists. Replace?").arg(replaceQuestion.name) }
        footer: DialogButtonBox {
            Button { objectName: "replaceYesToAll"; text: qsTr("Yes to all"); DialogButtonBox.buttonRole: DialogButtonBox.YesRole; onClicked: manager.answer("yesToAll") }
            Button { objectName: "replaceYes"; text: qsTr("Yes"); DialogButtonBox.buttonRole: DialogButtonBox.YesRole; onClicked: manager.answer("yes") }
            Button { objectName: "replaceNo"; text: qsTr("No"); DialogButtonBox.buttonRole: DialogButtonBox.NoRole; onClicked: manager.answer("no") }
            Button { text: qsTr("Cancel"); DialogButtonBox.buttonRole: DialogButtonBox.RejectRole; onClicked: manager.answer("cancel") }
        }
    }
    Dialog {
        id: message
        objectName: "styleMessage"
        property alias text: messageLabel.text
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok
        Label { id: messageLabel }
    }

    // --- Editor ------------------------------------------------------------
    function beginEditing(map, store) {
        if (map.name === undefined)
            return
        editingStore = store
        values = map
        // Y6: StyleChange makes its font list once (ChangeCatalog in its constructor).
        if (!fontCatalogBar.made) {
            fontCatalogBar.made = true
            fontCatalogBar.reset()
        }
        editor.load(map)
        editing = true
        updatePreview()
    }
    function endEditing() {
        editing = false
        styles.endEdit()
    }
    // SY3: the sample line follows every field at once; while the font list
    // is open it shows the family under the keyboard or the pointer.
    function updatePreview() {
        if (!editing)
            return
        const v = editor.values()
        const browsed = fontField.browsedFont()
        if (browsed.length)
            v.fontname = browsed
        styles.renderPreview(v, Math.max(1, preview.width), Math.max(1, preview.height), previewText.text)
    }
    // CommitChange: the multi-edit question, then ChangeStyle and the rename question.
    function commit(close) {
        const v = editor.values()
        const q = styles.commitQuestions(v)
        const selected = editingStore ? storeSelected : assSelected
        const finish = function(applyAll) {
            const done = function(rename) {
                const problem = styles.commitEdit(v, selected, applyAll, rename)
                if (problem !== "") {
                    message.text = problem
                    message.open()
                    return
                }
                if (close)
                    endEditing()
            }
            if (q.rename) {
                renameQuestion.done = done
                renameQuestion.open()
            } else {
                done(false)
            }
        }
        if (q.fields && selected.length > 1) {
            multiQuestion.done = finish
            multiQuestion.open()
        } else {
            finish(false)
        }
    }
    Dialog {
        id: multiQuestion
        objectName: "styleMultiQuestion"
        property var done: null
        title: qsTr("Prompt")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Yes | Dialog.No
        Label { text: qsTr("Change all selected styles?") }
        onAccepted: done(true)
        onRejected: done(false)
    }
    Dialog {
        id: renameQuestion
        objectName: "styleRenameQuestion"
        property var done: null
        title: qsTr("Confirmation")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Yes | Dialog.No
        Label { text: qsTr("Style name modified. Do you want to change all instances to the new name?") }
        onAccepted: done(true)
        onRejected: done(false)
    }

    // A field of the Style editor's geometry grid, as wide as its column
    // and no narrower than 40 pixels.
    component StyleField: TextField {
        Layout.fillWidth: true
        Layout.minimumWidth: 40
    }
    // A list's commands beside it: an icon-only column of 22-pixel buttons
    // (legacy's text buttons and arrow bitmaps), the name in the tooltip and
    // as the accessible name, the move arrows under a separator. A list
    // shorter than that column (a small screen) puts the arrows in a column
    // of their own, the separator between the two groups; the commands
    // take a second column only when they alone are taller than the list.
    component ListCommands: Item {
        id: strip
        default property alias commands: commandColumn.data
        property alias moves: moveColumn.data
        readonly property real unit: commandColumn.children.length > 0 ? commandColumn.children[0].implicitHeight : 22
        readonly property int rows: Math.max(1, Math.floor(height / unit))
        readonly property bool split: (commandColumn.children.length + moveColumn.children.length) * unit
                                      + across.implicitHeight > height
        // As few rows as the columns a group needs take, evenly filled.
        function balanced(count) { return Math.ceil(count / Math.ceil(count / rows)) }
        implicitWidth: split ? commandColumn.width + down.width + moveColumn.width
                             : Math.max(commandColumn.width, moveColumn.width)
        Grid {
            id: commandColumn
            flow: Grid.TopToBottom
            rows: strip.split ? strip.balanced(children.length) : children.length
        }
        ToolSeparator {
            id: across
            visible: !strip.split
            orientation: Qt.Horizontal
            padding: 0
            y: commandColumn.height
            width: commandColumn.width
        }
        ToolSeparator {
            id: down
            visible: strip.split
            orientation: Qt.Vertical
            x: commandColumn.width
            height: Math.max(commandColumn.height, moveColumn.height)
        }
        Grid {
            id: moveColumn
            flow: Grid.TopToBottom
            rows: strip.split ? strip.balanced(children.length) : children.length
            x: strip.split ? down.x + down.width : 0
            y: strip.split ? 0 : across.y + across.height
        }
    }
    component StyleList: ListView {
        id: list
        required property var names
        required property bool store
        property var selected: []
        signal picked(var rows)
        signal edit()
        clip: true
        model: names
        Accessible.role: Accessible.List
        Accessible.name: store ? qsTr("Styles stored:") : qsTr("Styles in ASS file:")
        keyNavigationEnabled: true
        delegate: Rectangle {
            required property int index
            required property string modelData
            width: list.width
            height: label.implicitHeight + 6
            color: list.selected.indexOf(index) >= 0 ? palette.highlight : "transparent"
            Accessible.role: Accessible.ListItem
            Accessible.name: modelData
            Accessible.selected: list.selected.indexOf(index) >= 0
            Label {
                id: label
                anchors.verticalCenter: parent.verticalCenter
                x: 4
                text: modelData
                color: list.selected.indexOf(index) >= 0 ? palette.highlightedText : palette.text
            }
            MouseArea {
                anchors.fill: parent
                onClicked: (mouse) => {
                    let rows = list.selected.slice()
                    if (mouse.modifiers & Qt.ControlModifier) {
                        const at = rows.indexOf(index)
                        if (at >= 0) rows.splice(at, 1); else rows.push(index)
                    } else if ((mouse.modifiers & Qt.ShiftModifier) && rows.length > 0) {
                        const from = rows[0]
                        rows = []
                        for (let i = Math.min(from, index); i <= Math.max(from, index); ++i) rows.push(i)
                    } else {
                        rows = [index]
                    }
                    list.picked(rows.sort((a, b) => a - b))
                }
                onDoubleClicked: { list.picked([index]); list.edit() }
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 6
        anchors.bottomMargin: 6 + managerFooter.height + 6
        ColumnLayout {
            // As wide as its transfer row (Add to all open ASS files): the
            // editor beside it takes the rest.
            Layout.preferredWidth: 400
            Layout.minimumWidth: 400
            Layout.maximumWidth: manager.editing ? 400 : Number.POSITIVE_INFINITY
            Layout.fillWidth: !manager.editing
            Layout.fillHeight: true
            GroupBox {
                title: qsTr("Catalog:")
                Layout.fillWidth: true
                RowLayout {
                    anchors.fill: parent
                    ComboBox {
                        id: catalogBox
                        objectName: "styleCatalog"
                        Layout.fillWidth: true
                        model: manager.styles.catalogs
                        currentIndex: manager.styles.catalogs.indexOf(manager.styles.catalog)
                        ToolTip.text: qsTr("Style catalog")
                        // Named as its tooltip, not by the catalog it shows.
                        Accessible.name: qsTr("Style catalog")
                        onActivated: (i) => { manager.styles.chooseCatalog(model[i]); manager.storeSelected = [0] }
                    }
                    IconToolButton { objectName: "newCatalog"; iconRole: "add"; text: qsTr("New"); tip: qsTr("New style catalog"); onClicked: newCatalog.open() }
                    IconToolButton {
                        objectName: "deleteCatalog"
                        iconRole: "delete"
                        text: qsTr("Delete")
                        tip: qsTr("Delete selected style catalog")
                        enabled: manager.styles.catalog !== "Default"
                        onClicked: deleteCatalogQuestion.open()
                    }
                }
            }
            GroupBox {
                title: qsTr("Styles stored:")
                Layout.fillWidth: true
                Layout.fillHeight: true
                // The two lists share the height evenly, whatever their
                // commands' columns take.
                Layout.preferredHeight: 0
                RowLayout {
                    anchors.fill: parent
                    StyleList {
                        id: storeList
                        objectName: "storeStyles"
                        names: manager.styles.storeStyles
                        store: true
                        selected: manager.storeSelected
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        onPicked: (rows) => manager.storeSelected = rows
                        onEdit: manager.beginEditing(manager.styles.beginEdit(true, manager.storeSelected[0]), true)
                    }
                    ListCommands {
                        objectName: "storeCommands"
                        Layout.fillHeight: true
                        IconToolButton { padding: 3; objectName: "storeNew"; iconRole: "add"; text: qsTr("New"); tip: qsTr("Create new style in storage"); onClicked: manager.beginEditing(manager.styles.beginNew(true), true) }
                        IconToolButton { padding: 3; objectName: "storeCopy"; iconRole: "duplicate"; text: qsTr("Copy"); tip: qsTr("Copy the selected style"); enabled: manager.storeSelected.length > 0; onClicked: manager.beginEditing(manager.styles.beginCopy(true, manager.storeSelected[0]), true) }
                        IconToolButton { padding: 3; objectName: "storeEdit"; iconRole: "edit"; text: qsTr("Edit"); tip: qsTr("Edit the selected style"); enabled: manager.storeSelected.length > 0; onClicked: manager.beginEditing(manager.styles.beginEdit(true, manager.storeSelected[0]), true) }
                        IconToolButton { padding: 3; iconRole: "import"; text: qsTr("Load"); tip: qsTr("Load style from external ASS file to storage"); onClicked: { loadDialog.toStore = true; loadDialog.open() } }
                        IconToolButton { padding: 3; objectName: "storeDelete"; iconRole: "delete"; text: qsTr("Delete"); tip: qsTr("Delete the selected styles"); enabled: manager.storeSelected.length > 0; onClicked: manager.storeSelected = manager.styles.removeStyles(true, manager.storeSelected) }
                        IconToolButton { padding: 3; iconRole: "sort"; text: qsTr("Sort"); tip: qsTr("Sort the styles by name"); onClicked: { manager.styles.sortStyles(true); manager.storeSelected = [0] } }
                        moves: [
                            IconToolButton { padding: 3; iconRole: "move-to-top"; text: qsTr("Move selected styles to beginning"); onClicked: manager.storeSelected = manager.styles.moveStyles(true, manager.storeSelected, 0) },
                            IconToolButton { padding: 3; iconRole: "move-up"; text: qsTr("Move selected styles up"); onClicked: manager.storeSelected = manager.styles.moveStyles(true, manager.storeSelected, 1) },
                            IconToolButton { padding: 3; iconRole: "move-down"; text: qsTr("Move selected styles down"); onClicked: manager.storeSelected = manager.styles.moveStyles(true, manager.storeSelected, 2) },
                            IconToolButton { padding: 3; iconRole: "move-to-bottom"; text: qsTr("Move selected styles to end"); onClicked: manager.storeSelected = manager.styles.moveStyles(true, manager.storeSelected, 3) }
                        ]
                    }
                }
            }
            RowLayout {
                IconTextButton {
                    objectName: "addToStore"
                    iconRole: "copy-to-storage"
                    text: qsTr("Add to storage")
                    tip: qsTr("Copy style from ASS to storage")
                    enabled: manager.assSelected.length > 0
                    onClicked: {
                        const rows = manager.assSelected
                        manager.askReplace(manager.styles.transferConflicts(true, rows), function(answers) {
                            manager.storeSelected = manager.styles.addToStore(rows, answers)
                        })
                    }
                }
                IconTextButton {
                    objectName: "addToAss"
                    iconRole: "copy-to-ass"
                    text: qsTr("Add to ASS")
                    tip: qsTr("Copy style from storage to ASS")
                    enabled: manager.storeSelected.length > 0 && manager.styles.available
                    onClicked: {
                        const rows = manager.storeSelected
                        manager.askReplace(manager.styles.transferConflicts(false, rows), function(answers) {
                            manager.assSelected = manager.styles.addToDocument(rows, answers)
                        })
                    }
                }
                IconTextButton {
                    objectName: "addToAllAss"
                    iconRole: "copy-to-all-ass"
                    text: qsTr("Add to all open ASS files")
                    tip: qsTr("Copy style from storage to every open ASS file")
                    enabled: manager.storeSelected.length > 0
                    onClicked: {
                        const rows = manager.storeSelected
                        // Legacy asks once per conflicting name in each Document; the
                        // active Document's questions stand for all (its answers are reused).
                        manager.askReplace(manager.styles.transferConflicts(false, rows), function(answers) {
                            manager.styles.addToAllDocuments(rows, answers)
                        })
                    }
                }
            }
            GroupBox {
                title: qsTr("Styles in ASS file:")
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: 0 // as the stored styles'
                enabled: manager.styles.available
                RowLayout {
                    anchors.fill: parent
                    StyleList {
                        id: assList
                        objectName: "assStyles"
                        names: manager.styles.documentStyles
                        store: false
                        selected: manager.assSelected
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        onPicked: (rows) => manager.assSelected = rows
                        onEdit: manager.beginEditing(manager.styles.beginEdit(false, manager.assSelected[0]), false)
                    }
                    ListCommands { // as the stored styles'
                        objectName: "assCommands"
                        Layout.fillHeight: true
                        IconToolButton { padding: 3; objectName: "assNew"; iconRole: "add"; text: qsTr("New"); tip: qsTr("Create new ASS style"); onClicked: manager.beginEditing(manager.styles.beginNew(false), false) }
                        IconToolButton { padding: 3; objectName: "assCopy"; iconRole: "duplicate"; text: qsTr("Copy"); tip: qsTr("Copy the selected style"); enabled: manager.assSelected.length > 0; onClicked: manager.beginEditing(manager.styles.beginCopy(false, manager.assSelected[0]), false) }
                        IconToolButton { padding: 3; objectName: "assEdit"; iconRole: "edit"; text: qsTr("Edit"); tip: qsTr("Edit the selected style"); enabled: manager.assSelected.length > 0; onClicked: manager.beginEditing(manager.styles.beginEdit(false, manager.assSelected[0]), false) }
                        IconToolButton { padding: 3; iconRole: "import"; text: qsTr("Load"); tip: qsTr("Load style from external ASS file"); onClicked: { loadDialog.toStore = false; loadDialog.open() } }
                        IconToolButton { padding: 3; objectName: "assDelete"; iconRole: "delete"; text: qsTr("Delete"); tip: qsTr("Delete the selected styles"); enabled: manager.assSelected.length > 0; onClicked: manager.assSelected = manager.styles.removeStyles(false, manager.assSelected) }
                        IconToolButton { padding: 3; iconRole: "sort"; text: qsTr("Sort"); tip: qsTr("Sort the styles by name"); onClicked: { manager.styles.sortStyles(false); manager.assSelected = [0] } }
                        IconToolButton {
                            padding: 3
                            objectName: "assClean"
                            iconRole: "clear"
                            text: qsTr("Clear")
                            tip: qsTr("Delete unused ASS styles")
                            onClicked: { message.title = qsTr("Status of deleted styles"); message.text = manager.styles.cleanStyles(); message.open() }
                        }
                        moves: [
                            IconToolButton { padding: 3; iconRole: "move-to-top"; text: qsTr("Move selected styles to beginning"); onClicked: manager.assSelected = manager.styles.moveStyles(false, manager.assSelected, 0) },
                            IconToolButton { padding: 3; iconRole: "move-up"; text: qsTr("Move selected styles up"); onClicked: manager.assSelected = manager.styles.moveStyles(false, manager.assSelected, 1) },
                            IconToolButton { padding: 3; iconRole: "move-down"; text: qsTr("Move selected styles down"); onClicked: manager.assSelected = manager.styles.moveStyles(false, manager.assSelected, 2) },
                            IconToolButton { padding: 3; iconRole: "move-to-bottom"; text: qsTr("Move selected styles to end"); onClicked: manager.assSelected = manager.styles.moveStyles(false, manager.assSelected, 3) }
                        ]
                    }
                }
            }
        }

        // StyleChange: the editor, shown while a Style is edited. Its
        // buttons stay in view under it; a window too short for the editor
        // scrolls it, with a visible scroll bar.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: manager.editing
            ScrollView {
                id: editorScroll
                objectName: "styleEditorScroll"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredWidth: 0
                Layout.fillHeight: true
                // As wide as the view, or the geometry grid's narrowest
                // (a screen narrower than 1280 x 800 at 150 % scrolls it).
                // The grid reports it once measured (a layout measures
                // itself while laid out: read in this binding, it loops).
                property real narrowest: 0
                contentWidth: Math.max(narrowest, availableWidth - (ScrollBar.vertical.visible ? ScrollBar.vertical.width + 2 : 0))
                // Its width is the layout's, never its content's: a view sized
                // by what it scrolls would loop through contentWidth.
                implicitWidth: 0
                ScrollBar.vertical.policy: contentHeight > availableHeight ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
                ColumnLayout {
                    id: editor
                    objectName: "styleEditor"
                    width: editorScroll.contentWidth
                    function load(m) {
                        nameField.text = m.name
                        fontField.editText = m.fontname
                        sizeField.text = m.fontsize
                        boldBox.checked = m.bold
                        italicBox.checked = m.italic
                        underlineBox.checked = m.underline
                        strikeBox.checked = m.strikeOut
                        colours.itemAt(0).setColour(m.primary)
                        colours.itemAt(1).setColour(m.secondary)
                        colours.itemAt(2).setColour(m.outline)
                        colours.itemAt(3).setColour(m.back)
                        outlineField.text = m.outlineWidth
                        shadowField.text = m.shadow
                        scaleXField.text = m.scaleX
                        scaleYField.text = m.scaleY
                        angleField.text = m.angle
                        spacingField.text = m.spacing
                        borderBox.checked = m.borderStyle
                        alignments.itemAt([7, 8, 9, 4, 5, 6, 1, 2, 3].indexOf(Number(m.alignment))).checked = true
                        marginLeft.text = m.marginLeft
                        marginRight.text = m.marginRight
                        marginVertical.text = m.marginVertical
                        // An unknown encoding shows "1 - Default" (legacy).
                        let enc = encodings.model.findIndex(e => e.startsWith(m.encoding + " "))
                        encodings.currentIndex = enc < 0 ? 1 : enc
                    }
                    function values() {
                        let alignment = "2"
                        for (let i = 0; i < 9; ++i)
                            if (alignments.itemAt(i).checked)
                                alignment = String([7, 8, 9, 4, 5, 6, 1, 2, 3][i])
                        return {
                            name: nameField.text, fontname: fontField.editText, fontsize: sizeField.text,
                            primary: colours.itemAt(0).colour, secondary: colours.itemAt(1).colour,
                            outline: colours.itemAt(2).colour, back: colours.itemAt(3).colour,
                            bold: boldBox.checked, italic: italicBox.checked, underline: underlineBox.checked,
                            strikeOut: strikeBox.checked, outlineWidth: outlineField.text, shadow: shadowField.text,
                            scaleX: scaleXField.text, scaleY: scaleYField.text, angle: angleField.text,
                            spacing: spacingField.text, borderStyle: borderBox.checked, alignment: alignment,
                            marginLeft: marginLeft.text, marginRight: marginRight.text, marginVertical: marginVertical.text,
                            encoding: encodings.currentText.split(" ")[0]
                        }
                    }
                    GroupBox {
                        title: qsTr("Style name:")
                        Layout.fillWidth: true
                        TextField {
                            id: nameField
                            objectName: "styleName"
                            width: parent.width
                            Accessible.name: qsTr("Style name:")
                            validator: RegularExpressionValidator { regularExpression: /[^,]*/ } // legacy excludes ","
                            onTextEdited: manager.updatePreview()
                        }
                    }
                    GroupBox {
                        title: qsTr("Font and size:")
                        Layout.fillWidth: true
                        ColumnLayout {
                            width: parent.width
                            RowLayout {
                                ComboBox {
                                    id: fontField
                                    objectName: "styleFont"
                                    editable: true
                                    Layout.fillWidth: true
                                    Accessible.name: qsTr("Font")
                                    onEditTextChanged: manager.updatePreview()
                                    onHighlightedIndexChanged: if (popup.visible) manager.updatePreview()
                                    // The family highlighted in the open list, or "".
                                    function browsedFont() {
                                        return popup.visible && highlightedIndex >= 0 && model && highlightedIndex < model.length
                                               ? model[highlightedIndex] : ""
                                    }
                                    Connections {
                                        target: fontField.popup
                                        function onVisibleChanged() { manager.updatePreview() }
                                    }
                                    // A refresh of the fonts: each row's face asked again.
                                    Connections {
                                        target: manager.catalogs
                                        function onFontsChanged() { FontFaces.forget() }
                                    }
                                    // SY3: each family in its own face. The list makes
                                    // rows only for what it shows, so a large font set
                                    // costs no more than a screenful of faces.
                                    delegate: FontFamilyRow {
                                        required property string modelData
                                        required property int index
                                        objectName: "styleFontRow"
                                        family: modelData
                                        width: ListView.view ? ListView.view.width : implicitWidth
                                        highlighted: fontField.highlightedIndex === index
                                        hoverEnabled: fontField.hoverEnabled
                                    }
                                }
                                TextField { id: sizeField; objectName: "styleSize"; Layout.preferredWidth: 66; Accessible.name: qsTr("Size"); onTextEdited: manager.updatePreview() }
                            }
                            // Y6: the font catalogs and the Filter (StyleChange's filtersizer).
                            FontCatalogBar {
                                id: fontCatalogBar
                                property bool made: false
                                Layout.fillWidth: true
                                catalogs: manager.catalogs
                                fontName: fontField.editText
                                // HikariChoice::PutArray keeps the typed font.
                                onListMade: {
                                    const text = fontField.editText
                                    fontField.model = fonts
                                    fontField.editText = text
                                }
                            }
                            RowLayout {
                                CheckBox { id: boldBox; objectName: "styleBold"; text: qsTr("Bold"); onToggled: manager.updatePreview() }
                                CheckBox { id: italicBox; text: qsTr("Italic"); onToggled: manager.updatePreview() }
                                CheckBox { id: underlineBox; text: qsTr("Underline"); onToggled: manager.updatePreview() }
                                CheckBox { id: strikeBox; text: qsTr("Strikethrough"); onToggled: manager.updatePreview() }
                            }
                        }
                    }
                    // Legacy's kolgrid (StyleChange.cpp:146-171): each colour's
                    // name, button and transparency in a column, the columns as
                    // wide as the geometry's below so the two boxes line up.
                    GroupBox {
                        title: qsTr("Colors and transparency:")
                        Layout.fillWidth: true
                        GridLayout {
                            columns: 4
                            columnSpacing: geometry.columnSpacing
                            Repeater {
                                id: colours
                                model: [qsTr("First"), qsTr("Second"), qsTr("Border"), qsTr("Shadow")]
                                ColumnLayout {
                                    required property string modelData
                                    required property int index
                                    property var colour: ({ r: 0, g: 0, b: 0, a: 0 })
                                    property Item swatch: swatchButton
                                    function setColour(c) {
                                        colour = c
                                    }
                                    Layout.preferredWidth: geometry.columnWidth(index)
                                    // The colour's name above its swatch (a label drawn on the
                                    // swatch was unreadable on light or dark colours).
                                    Label { text: modelData }
                                    Button {
                                        id: swatchButton
                                        objectName: "styleColour" + (index + 1)
                                        Accessible.name: modelData
                                        Layout.fillWidth: true
                                        implicitHeight: 24
                                        ToolTip.visible: hovered
                                        ToolTip.text: modelData
                                        background: Rectangle {
                                            color: Qt.rgba(colour.r / 255, colour.g / 255, colour.b / 255, 1)
                                            border.color: Theme.control
                                            radius: 2
                                        }
                                        onClicked: manager.colourClick(index + 1, true)
                                        // Y7: the right click (wxEVT_RIGHT_UP, StyleChange.cpp:144-151).
                                        TapHandler {
                                            acceptedButtons: Qt.RightButton
                                            onTapped: manager.colourClick(index + 1, false)
                                        }
                                    }
                                    SpinBox {
                                        objectName: "styleAlpha" + (index + 1)
                                        Layout.fillWidth: true
                                        Layout.preferredWidth: 0
                                        from: 0; to: 255; editable: true
                                        value: colour.a
                                        Accessible.name: modelData + " " + qsTr("transparency")
                                        onValueModified: { colour = { r: colour.r, g: colour.g, b: colour.b, a: value }; manager.updatePreview() }
                                    }
                                }
                            }
                        }
                    }
                    // Legacy's labelled boxes and Alignment (StyleChange.cpp:173-252)
                    // as one grid: each field under its label, four columns
                    // (Border, Shadow, Scale X, Scale Y; Angle, Spacing, Border
                    // type; the margins), Alignment in the fourth beside Angle
                    // and the margins. The columns share the width evenly and
                    // narrow together, down to what the box and the radios take.
                    GroupBox {
                        id: geometryBox
                        Layout.fillWidth: true
                        GridLayout {
                            id: geometry
                            objectName: "styleGeometry"
                            width: parent.width
                            // An even share of the width for each column, while
                            // their labels, the box and the radios fit in it.
                            readonly property real cellWidth: (width - 3 * columnSpacing) / 4
                            // After the layout pass that measured it.
                            onImplicitWidthChanged: Qt.callLater(() => editorScroll.narrowest = Layout.minimumWidth + geometryBox.leftPadding + geometryBox.rightPadding)
                            columns: 4
                            columnSpacing: 8
                            rowSpacing: 2
                            // Column i's width, from its first row's field.
                            function columnWidth(i) {
                                return [outlineField, shadowField, scaleXField, scaleYField][i].width
                            }
                            Label { Layout.row: 0; Layout.column: 0; text: qsTr("Border:") }
                            Label { Layout.row: 0; Layout.column: 1; text: qsTr("Shadow:") }
                            Label { Layout.row: 0; Layout.column: 2; text: qsTr("Scale X:") }
                            Label { Layout.row: 0; Layout.column: 3; text: qsTr("Scale Y:") }
                            StyleField { id: outlineField; objectName: "styleOutline"; Layout.preferredWidth: geometry.cellWidth; Layout.row: 1; Layout.column: 0; Accessible.name: qsTr("Border:"); onTextEdited: manager.updatePreview() }
                            StyleField { id: shadowField; objectName: "styleShadow"; Layout.preferredWidth: geometry.cellWidth; Layout.row: 1; Layout.column: 1; Accessible.name: qsTr("Shadow:"); onTextEdited: manager.updatePreview() }
                            StyleField { id: scaleXField; objectName: "styleScaleX"; Layout.preferredWidth: geometry.cellWidth; Layout.row: 1; Layout.column: 2; Accessible.name: qsTr("Scale X:"); onTextEdited: manager.updatePreview() }
                            StyleField { id: scaleYField; objectName: "styleScaleY"; Layout.preferredWidth: geometry.cellWidth; Layout.row: 1; Layout.column: 3; Accessible.name: qsTr("Scale Y:"); onTextEdited: manager.updatePreview() }
                            Label { Layout.row: 2; Layout.column: 0; Layout.topMargin: 6; text: qsTr("Angle:") }
                            Label { Layout.row: 2; Layout.column: 1; Layout.topMargin: 6; text: qsTr("Spacing:") }
                            Label { Layout.row: 2; Layout.column: 2; Layout.topMargin: 6; text: qsTr("Border type:") }
                            Label { Layout.row: 2; Layout.column: 3; Layout.topMargin: 6; text: qsTr("Alignment:") }
                            StyleField { id: angleField; objectName: "styleAngle"; Layout.preferredWidth: geometry.cellWidth; Layout.row: 3; Layout.column: 0; Accessible.name: qsTr("Angle:"); onTextEdited: manager.updatePreview() }
                            StyleField { id: spacingField; objectName: "styleSpacing"; Layout.preferredWidth: geometry.cellWidth; Layout.row: 3; Layout.column: 1; Accessible.name: qsTr("Spacing:"); onTextEdited: manager.updatePreview() }
                            CheckBox {
                                id: borderBox
                                objectName: "styleOpaqueBox"
                                Layout.row: 3; Layout.column: 2
                                Layout.minimumWidth: implicitWidth
                                Layout.fillWidth: true
                                Layout.preferredWidth: geometry.cellWidth
                                text: qsTr("Opaque box")
                                onToggled: manager.updatePreview()
                            }
                            GridLayout {
                                objectName: "styleAlignment"
                                Layout.row: 3; Layout.column: 3; Layout.rowSpan: 3
                                Layout.alignment: Qt.AlignLeft | Qt.AlignTop
                                Layout.minimumWidth: implicitWidth
                                columns: 3
                                columnSpacing: 0
                                rowSpacing: 0
                                ButtonGroup { id: alignmentGroup }
                                Repeater {
                                    id: alignments
                                    model: ["7", "8", "9", "4", "5", "6", "1", "2", "3"]
                                    RadioButton {
                                        required property string modelData
                                        // Legacy's 2-pixel border (angrid): the three
                                        // fit a narrow column.
                                        leftPadding: 2
                                        rightPadding: 6
                                        text: modelData
                                        ButtonGroup.group: alignmentGroup
                                        onToggled: manager.updatePreview()
                                    }
                                }
                            }
                            Label { Layout.row: 4; Layout.column: 0; Layout.topMargin: 6; text: qsTr("Left margin:") }
                            Label { Layout.row: 4; Layout.column: 1; Layout.topMargin: 6; text: qsTr("Right:") }
                            Label { Layout.row: 4; Layout.column: 2; Layout.topMargin: 6; text: qsTr("Vertical:") }
                            StyleField { id: marginLeft; objectName: "styleMarginLeft"; Layout.preferredWidth: geometry.cellWidth; Layout.row: 5; Layout.column: 0; Accessible.name: qsTr("Left margin:"); onTextEdited: manager.updatePreview() }
                            StyleField { id: marginRight; objectName: "styleMarginRight"; Layout.preferredWidth: geometry.cellWidth; Layout.row: 5; Layout.column: 1; Accessible.name: qsTr("Right:"); onTextEdited: manager.updatePreview() }
                            StyleField { id: marginVertical; objectName: "styleMarginVertical"; Layout.preferredWidth: geometry.cellWidth; Layout.row: 5; Layout.column: 2; Accessible.name: qsTr("Vertical:"); onTextEdited: manager.updatePreview() }
                        }
                    }
                    GroupBox {
                        title: qsTr("Text encoding:")
                        Layout.fillWidth: true
                        ComboBox {
                            id: encodings
                            objectName: "styleEncoding"
                            width: parent.width
                            Accessible.name: qsTr("Text encoding:")
                            onActivated: manager.updatePreview()
                            model: [qsTr("0 - ANSI"), qsTr("1 - Default"), qsTr("2 - Symbol"), qsTr("77 - Mac"), qsTr("128 - Japanese"),
                                    qsTr("129 - Hangul"), qsTr("130 - Johab"), qsTr("134 - Chinese GB2312"), qsTr("135 - Chinese BIG5"),
                                    qsTr("161 - Greek"), qsTr("162 - Turkish"), qsTr("163 - Vietnamese"), qsTr("177 - Hebrew"),
                                    qsTr("178 - Arabic"), qsTr("186 - Baltic"), qsTr("204 - Russian"), qsTr("222 - Thai"),
                                    qsTr("238 - Central European (Polish)"), qsTr("255 - OEM")]
                        }
                    }
                    GroupBox {
                        title: qsTr("Style preview:")
                        Layout.fillWidth: true
                        ColumnLayout {
                            width: parent.width
                            Image {
                                id: preview
                                objectName: "stylePreview"
                                Layout.fillWidth: true
                                Layout.preferredHeight: 100
                                cache: false
                                source: manager.editing ? "image://stylepreview/" + manager.styles.previewKey : ""
                                onWidthChanged: manager.updatePreview()
                            }
                            // Legacy STYLE_PREVIEW_TEXT, edited by clicking the preview.
                            // SY3: kept when edited, as legacy's StylePreview kept it
                            // (StylePreview.cpp:136-141).
                            TextField {
                                id: previewText
                                objectName: "stylePreviewText"
                                text: manager.catalogs.previewText
                                Accessible.name: qsTr("Preview text")
                                Layout.fillWidth: true
                                onTextEdited: manager.updatePreview()
                                onEditingFinished: manager.catalogs.setPreviewText(text)
                            }
                        }
                    }
                }
            }
        }
    }

    // One closing row under the manager and the editor beside it: the
    // editor's OK, Apply and Cancel while a Style is edited (StyleChange's
    // buttons, which close the editor; the manager's OK did the editor's),
    // the manager's OK and Close (StyleStore's Close) otherwise, so one
    // button rejects at a time.
    WindowFooter {
        id: managerFooter
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 6
        FooterButton {
            objectName: "styleEditorOk"
            text: qsTr("OK")
            visible: manager.editing
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: manager.commit(true)
        }
        FooterButton {
            objectName: "styleManagerOk"
            text: qsTr("OK")
            visible: !manager.editing
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: manager.closeManager()
        }
        FooterButton {
            objectName: "styleEditorApply"
            text: qsTr("Apply")
            visible: manager.editing
            DialogButtonBox.buttonRole: DialogButtonBox.ApplyRole
            onClicked: manager.commit(false)
        }
        FooterButton {
            objectName: "styleEditorCancel"
            text: qsTr("Cancel")
            visible: manager.editing
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: manager.endEditing()
        }
        FooterButton {
            objectName: "styleManagerClose"
            text: qsTr("Close")
            visible: !manager.editing
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: manager.closeManager()
        }
    }

    // Y7: StyleChange::OnAllCols (StyleChange.cpp:335-399). SY2: a left
    // click opens the colour popover at the button's colour and
    // transparency, a right click opens it with the eyedropper started;
    // COLORPICKER_SWITCH_CLICKS swaps them.
    function colourClick(number, leftClick) {
        if (colourPicker.switchClicks)
            leftClick = !leftClick
        const column = colours.itemAt(number - 1)
        stylePopover.openFor(column.swatch, styleColours, () => {
            colourPicker.opened("styleEditor") // DialogColorPicker::Get(this, ...)
            return styleColours.begin(number)
        }, { title: column.modelData, dropper: !leftClick, focus: column.swatch })
    }
    // The popover's target: one of the four colours of the edited Style.
    QtObject {
        id: styleColours
        property int number: 1
        // lastColor: what Cancel puts back.
        property var lastColour: ({ r: 0, g: 0, b: 0, a: 0 })
        function begin(n) {
            number = n
            const c = colours.itemAt(n - 1).colour
            lastColour = { r: c.r, g: c.g, b: c.b, a: c.a }
            return lastColour
        }
        // COLOR_CHANGED: UpdateColor (737-746) sets the button, the
        // transparency and the preview.
        function changeColour(c) {
            colours.itemAt(number - 1).setColour({ r: c.r, g: c.g, b: c.b, a: c.a })
            manager.updatePreview()
            return true
        }
        // Cancel: UpdateColor(lastColor) (368-371, 395-398).
        function endDialog(accepted) {
            if (!accepted)
                changeColour(lastColour)
        }
    }
    ColourPopover {
        id: stylePopover
        objectName: "styleColourPopover"
        picker: manager.colourPicker
    }
    Dialog {
        id: newCatalog
        objectName: "newCatalogDialog"
        title: qsTr("New catalog")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        TextField { id: catalogName; objectName: "newCatalogName"; Accessible.name: qsTr("Catalog name") }
        onAccepted: { manager.styles.createCatalog(catalogName.text); catalogName.text = ""; manager.storeSelected = [] }
    }
    Dialog {
        id: deleteCatalogQuestion
        title: qsTr("Prompt")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Yes | Dialog.No
        Label { text: qsTr("Do you really want to delete the catalog named \"%1\"?").arg(manager.styles.catalog) }
        onAccepted: manager.styles.deleteCatalog(manager.styles.catalog)
    }
    // LoadStylesS: "Choose ASS file", then the Styles to load.
    FileDialog {
        id: loadDialog
        property bool toStore: false
        title: qsTr("Choose ASS file")
        nameFilters: [qsTr("ASS subtitle files(*.ass)") + " (*.ass)"]
        onAccepted: { chooseLoaded.file = selectedFile; chooseLoaded.names = manager.styles.fileStyles(selectedFile); chooseLoaded.checkedNames = []; chooseLoaded.open() }
    }
    Dialog {
        id: chooseLoaded
        objectName: "chooseLoadedStyles"
        property url file
        property var names: []
        property var checkedNames: []
        title: qsTr("Choose styles")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        ColumnLayout {
            Repeater {
                model: chooseLoaded.names
                CheckBox {
                    required property string modelData
                    text: modelData
                    onToggled: chooseLoaded.checkedNames = checked ? chooseLoaded.checkedNames.concat([modelData])
                                                                   : chooseLoaded.checkedNames.filter(n => n !== modelData)
                }
            }
        }
        onAccepted: {
            const names = chooseLoaded.names.filter(n => chooseLoaded.checkedNames.indexOf(n) >= 0)
            const toStore = loadDialog.toStore
            manager.askReplace(manager.styles.fileConflicts(toStore, chooseLoaded.file, names), function(answers) {
                manager.styles.loadFromFile(toStore, chooseLoaded.file, names, answers)
            })
        }
    }
}
