pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Explorer"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

FocusScope {
    id: panel
    objectName: "explorerTool"
    required property ExplorerModel model
    property var messages: null
    signal escaped()
    signal openSubtitles(string path, bool newTab)
    signal openMedia(string path, string kind)
    signal openProjectSettings()
    signal openFolder()
    signal showInFolder(string path)
    signal copyPath(string path)

    readonly property size minimumBodySize: Qt.size(180, 160)
    readonly property Item toolbar: tools
    readonly property bool hasProject: model.folder.length > 0
    readonly property var mediaKinds: ["video", "audio", "keyframes", "timecodes"]
    property string renaming: ""
    property string creatingIn: ""
    property string problem: ""

    function activate(row) {
        const r = model.rowAt(row)
        if (r.kind === undefined)
            return
        if (r.kind === "folder")
            model.toggle(row)
        else if (r.kind === "projectSettings")
            openProjectSettings()
        else if (r.kind === "subtitles")
            openSubtitles(r.path, true)
        else if (mediaKinds.includes(r.kind))
            openMedia(r.path, r.kind)
    }
    function startRename(row) {
        const r = model.rowAt(row)
        if (r.kind === undefined || r.kind === "projectSettings")
            return
        creatingIn = ""
        problem = ""
        model.currentRow = row
        renaming = r.path
    }
    function finishRename(name) {
        const result = model.rename(renaming, name)
        if (result.problem.length > 0) {
            problem = result.problem
            return
        }
        renaming = ""
        problem = ""
        list.forceActiveFocus()
    }
    function startNewFile(folder) {
        renaming = ""
        problem = ""
        creatingIn = folder.length > 0 ? folder : model.folder
        newName.text = qsTr("untitled") + ".ass"
        newName.select(0, newName.text.lastIndexOf("."))
        newName.forceActiveFocus()
    }
    function finishNewFile(name) {
        const result = model.newSubtitleFile(creatingIn, name)
        if (result.problem.length > 0) {
            problem = result.problem
            return
        }
        creatingIn = ""
        problem = ""
        list.forceActiveFocus()
        openSubtitles(result.path, true)
    }
    function cancelEditing() {
        renaming = ""
        creatingIn = ""
        problem = ""
        list.forceActiveFocus()
    }
    function trash(row) {
        const r = model.rowAt(row)
        if (r.kind === undefined || r.kind === "projectSettings")
            return
        const run = () => {
            const failed = model.moveToTrash(r.path)
            problem = failed
        }
        if (!messages) {
            run()
            return
        }
        messages.ask({
            title: qsTr("Move to the trash"),
            text: r.kind === "folder" ? qsTr("Move the folder “%1” and everything in it to the trash?").arg(r.name)
                                      : qsTr("Move “%1” to the trash?").arg(r.name),
            choices: [{ id: "trash", text: qsTr("Move to the trash"), role: "destructive", isDefault: true },
                      { id: "cancel", text: qsTr("Cancel"), role: "reject" }]
        }, id => {
            if (id === "trash")
                run()
        })
    }
    function showMenu(row, item, x, y) {
        model.currentRow = row
        rowMenu.row = model.rowAt(row)
        rowMenu.at = row
        rowMenu.popup(item, x, y)
    }

    Connections {
        target: panel.model
        function onFolderChanged() { panel.cancelEditing() }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            objectName: "explorerNewFileRow"
            visible: panel.creatingIn.length > 0
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            spacing: 6
            Icon { iconRole: "file-subtitles" }
            TextField {
                id: newName
                objectName: "explorerNewFileName"
                Layout.fillWidth: true
                Accessible.name: qsTr("New subtitle file name")
                onTextEdited: panel.problem = panel.model.nameProblem(panel.creatingIn, text)
                onAccepted: panel.finishNewFile(text)
                Keys.onEscapePressed: panel.cancelEditing()
                onActiveFocusChanged: if (!activeFocus && panel.creatingIn.length > 0 && !rowMenu.visible) panel.cancelEditing()
            }
        }

        Label {
            objectName: "explorerProblem"
            visible: panel.problem.length > 0
            text: panel.problem
            color: Theme.danger
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
        }

        ListView {
            id: list
            objectName: "explorerList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            focus: true
            visible: panel.hasProject && panel.model.count > 0
            model: panel.model
            currentIndex: panel.model.currentRow
            keyNavigationEnabled: false
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            Accessible.role: Accessible.Tree
            Accessible.name: qsTr("Explorer")
            onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)
            Keys.onShortcutOverride: event => event.accepted = (event.modifiers === Qt.NoModifier
                && [Qt.Key_Down, Qt.Key_Up, Qt.Key_Home, Qt.Key_End, Qt.Key_Right, Qt.Key_Left, Qt.Key_Return,
                    Qt.Key_Enter, Qt.Key_F2, Qt.Key_Delete, Qt.Key_Menu, Qt.Key_Escape].includes(event.key))
                || (event.key === Qt.Key_F10 && event.modifiers === Qt.ShiftModifier)
            Keys.onPressed: event => {
                const row = panel.model.currentRow
                const r = panel.model.rowAt(row)
                const step = delta => panel.model.currentRow = Math.max(0, Math.min(panel.model.count - 1, (row < 0 ? 0 : row + delta)))
                if (event.key === Qt.Key_Down) {
                    step(1)
                } else if (event.key === Qt.Key_Up) {
                    step(row < 0 ? 0 : -1)
                } else if (event.key === Qt.Key_Home) {
                    panel.model.currentRow = 0
                } else if (event.key === Qt.Key_End) {
                    panel.model.currentRow = panel.model.count - 1
                } else if (event.key === Qt.Key_Right && r.expandable) {
                    if (r.expanded)
                        step(1)
                    else
                        panel.model.setExpanded(row, true)
                } else if (event.key === Qt.Key_Left && row >= 0) {
                    if (r.expandable && r.expanded) {
                        panel.model.setExpanded(row, false)
                    } else {
                        for (let up = row - 1; up >= 0; --up) {
                            if (panel.model.rowAt(up).depth < r.depth) {
                                panel.model.currentRow = up
                                break
                            }
                        }
                    }
                } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && row >= 0) {
                    panel.activate(row)
                } else if (event.key === Qt.Key_F2 && row >= 0) {
                    panel.startRename(row)
                } else if (event.key === Qt.Key_Delete && row >= 0) {
                    panel.trash(row)
                } else if ((event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && event.modifiers & Qt.ShiftModifier))
                           && row >= 0 && list.currentItem) {
                    panel.showMenu(row, list.currentItem, 24, list.currentItem.height)
                } else if (event.key === Qt.Key_Escape) {
                    panel.escaped()
                } else {
                    return
                }
                event.accepted = true
            }
            delegate: Item {
                id: entry
                required property int index
                required property string name
                required property string path
                required property int depth
                required property string kind
                required property bool expandable
                required property bool expanded
                readonly property bool editing: panel.renaming === path
                objectName: "explorerRow_" + name
                width: ListView.view.width
                implicitHeight: Math.max(24, editing ? renameField.implicitHeight + 2 : label.implicitHeight + 6)
                Accessible.role: Accessible.TreeItem
                Accessible.name: name
                Accessible.description: expandable ? (expanded ? qsTr("expanded") : qsTr("collapsed")) : ""
                Rectangle {
                    anchors.fill: parent
                    color: entry.ListView.isCurrentItem ? Theme.select : rowMouse.containsMouse ? Theme.raised : "transparent"
                }
                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onPressed: mouse => {
                        list.forceActiveFocus()
                        panel.model.currentRow = entry.index
                        if (mouse.button === Qt.RightButton)
                            panel.showMenu(entry.index, entry, mouse.x, mouse.y)
                    }
                    onDoubleClicked: mouse => {
                        if (mouse.button === Qt.LeftButton)
                            panel.activate(entry.index)
                    }
                }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 4 + entry.depth * 14
                    anchors.rightMargin: 4
                    spacing: 4
                    Item {
                        implicitWidth: 16
                        implicitHeight: 16
                        Icon {
                            objectName: "explorerChevron"
                            anchors.centerIn: parent
                            visible: entry.expandable
                            iconRole: entry.expanded ? "chevron-down" : "chevron-right"
                        }
                        TapHandler {
                            enabled: entry.expandable
                            onTapped: panel.model.toggle(entry.index)
                        }
                    }
                    Icon { iconRole: ({ folder: "file-folder", projectSettings: "settings", subtitles: "file-subtitles", video: "file-video", audio: "file-audio", keyframes: "file-keyframes", timecodes: "file-timecodes", fonts: "tag-font", chapters: "chapters", other: "file-other" })[entry.kind] }
                    Label {
                        id: label
                        visible: !entry.editing
                        text: entry.name
                        textFormat: Text.PlainText
                        elide: Text.ElideMiddle
                        color: entry.kind === "projectSettings" ? Theme.muted : Theme.text
                        Layout.fillWidth: true
                    }
                    TextField {
                        id: renameField
                        objectName: "explorerRenameField"
                        visible: entry.editing
                        Layout.fillWidth: true
                        Accessible.name: qsTr("New name for %1").arg(entry.name)
                        onVisibleChanged: if (visible) {
                            text = entry.name
                            const dot = entry.kind === "folder" ? -1 : text.lastIndexOf(".")
                            select(0, dot > 0 ? dot : text.length)
                            forceActiveFocus()
                        }
                        onTextEdited: panel.problem = panel.model.nameProblem(entry.path.substring(0, entry.path.lastIndexOf("/")),
                                                                              text, entry.path)
                        onAccepted: panel.finishRename(text)
                        Keys.onEscapePressed: panel.cancelEditing()
                        onActiveFocusChanged: if (!activeFocus && entry.editing) panel.cancelEditing()
                    }
                }
            }
        }

        ColumnLayout {
            objectName: "explorerEmpty"
            visible: !list.visible
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 12
            spacing: 8
            Item { Layout.fillHeight: true }
            Label {
                text: panel.hasProject ? qsTr("No files to show here.") : qsTr("Open a folder as a project to see its files here.")
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                color: Theme.muted
                Layout.fillWidth: true
            }
            IconTextButton {
                objectName: "explorerOpenFolder"
                visible: !panel.hasProject
                iconRole: "open-project"
                text: qsTr("Open folder…")
                Layout.alignment: Qt.AlignHCenter
                onClicked: panel.openFolder()
            }
            IconTextButton {
                objectName: "explorerEmptyAllFiles"
                visible: panel.hasProject && !panel.model.allFiles
                iconRole: "show-all-files"
                text: qsTr("Show all files")
                Layout.alignment: Qt.AlignHCenter
                onClicked: panel.model.allFiles = true
            }
            Item { Layout.fillHeight: true }
        }
    }

    Item {
        id: tools
        objectName: "explorerTools"
        visible: parent !== null && parent.objectName === "dockToolbarSlot" && panel.hasProject
        implicitWidth: buttons.implicitWidth
        implicitHeight: buttons.implicitHeight
        RowLayout {
            id: buttons
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            visible: tools.width + 0.5 >= implicitWidth
            spacing: 2
            IconToolButton {
                objectName: "explorerNewFile"
                iconRole: "new-subtitle-file"
                text: qsTr("New subtitle file")
                focusPolicy: Qt.NoFocus
                onClicked: panel.startNewFile(panel.model.folderAt(panel.model.currentRow))
            }
            IconToolButton {
                objectName: "explorerGroupByName"
                iconRole: "group-by-name"
                text: qsTr("Group by name")
                focusPolicy: Qt.NoFocus
                checkable: true
                checked: panel.model.groupByName
                onToggled: panel.model.groupByName = checked
            }
            IconToolButton {
                objectName: "explorerAllFiles"
                iconRole: "show-all-files"
                text: qsTr("Show all files")
                focusPolicy: Qt.NoFocus
                checkable: true
                checked: panel.model.allFiles
                onToggled: panel.model.allFiles = checked
            }
            IconToolButton {
                objectName: "explorerRefresh"
                iconRole: "refresh"
                text: qsTr("Refresh")
                focusPolicy: Qt.NoFocus
                onClicked: panel.model.refresh()
            }
        }
    }

    ShellMenu {
        id: rowMenu
        objectName: "explorerRowMenu"
        property var row: ({})
        property int at: -1
        readonly property bool file: row.kind !== undefined && row.kind !== "folder" && row.kind !== "projectSettings"
        readonly property bool editable: row.kind !== undefined && row.kind !== "projectSettings"
        onClosed: if (panel.renaming.length === 0 && panel.creatingIn.length === 0) list.forceActiveFocus()
        ShellMenuItem {
            objectName: "explorerMenuOpenNewTab"
            iconRole: "tab-new"
            text: qsTr("Open in a new tab")
            enabled: rowMenu.row.kind === "subtitles"
            onTriggered: panel.openSubtitles(rowMenu.row.path, true)
        }
        ShellMenuItem {
            objectName: "explorerMenuOpenHere"
            iconRole: "open-subtitles"
            text: qsTr("Open in this tab")
            enabled: rowMenu.row.kind === "subtitles" || panel.mediaKinds.includes(rowMenu.row.kind)
            onTriggered: rowMenu.row.kind === "subtitles" ? panel.openSubtitles(rowMenu.row.path, false)
                                                          : panel.openMedia(rowMenu.row.path, rowMenu.row.kind)
        }
        MenuSeparator {}
        ShellMenuItem {
            objectName: "explorerMenuNewFile"
            iconRole: "new-subtitle-file"
            text: qsTr("New subtitle file here")
            onTriggered: panel.startNewFile(panel.model.folderAt(rowMenu.at))
        }
        ShellMenuItem {
            objectName: "explorerMenuRename"
            iconRole: "edit"
            text: qsTr("Rename")
            shortcutText: ShellMenus.keyText("F2")
            enabled: rowMenu.editable
            onTriggered: panel.startRename(rowMenu.at)
        }
        ShellMenuItem {
            objectName: "explorerMenuTrash"
            iconRole: "delete"
            text: qsTr("Move to the trash")
            shortcutText: ShellMenus.keyText("Del")
            enabled: rowMenu.editable
            onTriggered: panel.trash(rowMenu.at)
        }
        MenuSeparator {}
        ShellMenuItem {
            objectName: "explorerMenuShowInFolder"
            iconRole: "show-in-folder"
            text: qsTr("Show in folder")
            onTriggered: panel.showInFolder(panel.model.nativePath(rowMenu.row.path))
        }
        ShellMenuItem {
            objectName: "explorerMenuCopyPath"
            iconRole: "copy-path"
            text: qsTr("Copy path")
            onTriggered: panel.copyPath(panel.model.nativePath(rowMenu.row.path))
        }
    }
}
