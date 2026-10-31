pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Session"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Item {
    id: autosaves
    required property var app
    property SettingsStore settings: null
    property var entries: []
    property string range: "any"
    property date before: new Date()
    property string query: ""
    property date now: new Date()
    readonly property DialogWindow window: windowLoader.item as DialogWindow
    readonly property var shown: {
        const words = query.toLocaleLowerCase().split(" ").filter(w => w.length > 0)
        const from = rangeStart()
        return entries.filter(e => {
            const hay = [e.name, e.path].concat(e.versions.map(v => v.file || "")).join("\n").toLocaleLowerCase()
            if (!words.every(w => hay.includes(w)))
                return false
            if (range === "before")
                return e.when < day(before)
            return from === null || e.when >= from
        })
    }
    visible: false

    function day(d) { return new Date(d.getFullYear(), d.getMonth(), d.getDate()) }
    function rangeStart() {
        const today = day(now)
        switch (range) {
        case "today": return today
        case "week": return new Date(today.getFullYear(), today.getMonth(), today.getDate() - 6)
        case "month": return new Date(today.getFullYear(), today.getMonth(), today.getDate() - 29)
        }
        return null
    }
    function whenText(d) {
        if (!d || isNaN(d.getTime()))
            return ""
        const days = Math.round((day(now) - day(d)) / 86400000)
        const time = d.toLocaleTimeString(Qt.locale(), Locale.ShortFormat)
        if (days === 0)
            return qsTr("Today %1").arg(time)
        if (days === 1)
            return qsTr("Yesterday %1").arg(time)
        return d.toLocaleDateString(Qt.locale(), Locale.ShortFormat)
    }
    signal bundlesDeleted()
    function reload() {
        now = new Date()
        const rows = []
        for (const b of app.recoveryBundles())
            rows.push({ kind: "bundle", key: b.key, name: b.title, path: b.original || "",
                        when: new Date(b.written),
                        versions: b.generations.map(g => ({ label: whenText(new Date(g.written)), generation: g.generation })) })
        for (const f of app.legacyAutosaves())
            if (f.versions.length > 0)
                rows.push({ kind: "legacy", key: "", name: f.name, path: "",
                            when: new Date(f.versions[0].time),
                            versions: f.versions.map(v => ({ label: whenText(new Date(v.time)), file: v.file })) })
        rows.sort((a, b) => b.when - a.when)
        entries = rows
    }
    function show(deleting) {
        reload()
        windowLoader.active = true
        window.open()
        if (deleting === true)
            window.openDeleteOlder()
    }
    function openCopy(entry, version) {
        const v = entry.versions[version]
        const ok = entry.kind === "bundle" ? app.recoverBundle(entry.key, v.generation) : app.openLegacyAutosave(v.file)
        if (ok)
            window.accept()
        return ok
    }
    function remove(entry) {
        if (entry.kind !== "bundle")
            return
        app.dismissBundle(entry.key)
        reload()
        autosaves.bundlesDeleted()
    }
    function olderCount(cutoff) {
        const c = day(cutoff)
        return entries.filter(e => e.kind === "bundle" && e.when < c).length
    }
    function deleteOlder(cutoff) {
        const removed = app.removeAutosavesOlderThan(day(cutoff))
        reload()
        autosaves.bundlesDeleted()
        return removed
    }

    Loader {
        id: windowLoader
        active: false
        sourceComponent: DialogWindow {
            id: autosavesWindow
            objectName: "autosavesWindow"
            dialogId: "autosaves"
            settings: autosaves.settings
            resizable: true
            title: qsTr("Autosaves")
            initialFocus: search

            function openDeleteOlder() {
                olderDate.setDate(olderDate.back({ months: 1 }))
                deleteOlder.openAt(deleteOlderButton, null)
            }

            RowLayout {
                spacing: 8
                Layout.topMargin: 4
                TextField {
                    id: search
                    objectName: "autosavesSearch"
                    placeholderText: qsTr("Search autosaves")
                    Accessible.name: qsTr("Search autosaves")
                    Layout.preferredWidth: 300
                    onTextChanged: autosaves.query = text
                }
                Label { text: qsTr("Saved"); Layout.leftMargin: 8 }
                ComboBox {
                    id: rangeChoice
                    objectName: "autosavesRange"
                    readonly property var ranges: ["any", "today", "week", "month", "before"]
                    model: [qsTr("Any time"), qsTr("Today"), qsTr("Last 7 days"), qsTr("Last 30 days"), qsTr("Before a date")]
                    currentIndex: ranges.indexOf(autosaves.range)
                    Accessible.name: qsTr("Saved")
                    Layout.preferredWidth: 150
                    onActivated: index => autosaves.range = ranges[index]
                }
                DatePicker {
                    id: beforeDate
                    objectName: "autosavesBefore"
                    visible: autosaves.range === "before"
                    accessibleName: qsTr("Saved before")
                    onEdited: d => autosaves.before = d
                    Component.onCompleted: {
                        setDate(back({ months: 1 }))
                        autosaves.before = date
                    }
                }
                Item { Layout.fillWidth: true }
            }
            Rectangle {
                color: Theme.field
                border.color: Theme.line
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 720
                Layout.minimumHeight: 160
                Layout.preferredHeight: header.height + 5 * 48 + 2
                Rectangle {
                    id: header
                    x: 1; y: 1
                    width: parent.width - 2
                    height: 26
                    color: Theme.raised
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 14
                        spacing: 12
                        Label { text: qsTr("Document"); color: Theme.muted; font: autosavesWindow.fontAt(12); Layout.fillWidth: true }
                        Label { text: qsTr("Saved"); color: Theme.muted; font: autosavesWindow.fontAt(12); Layout.preferredWidth: 110 }
                        Label { text: qsTr("Version"); color: Theme.muted; font: autosavesWindow.fontAt(12); Layout.preferredWidth: 236 }
                    }
                }
                Label {
                    objectName: "autosavesEmpty"
                    visible: list.count === 0
                    anchors.centerIn: parent
                    color: Theme.muted
                    text: autosaves.entries.length === 0 ? qsTr("There are no autosaves.") : qsTr("No matching autosaves")
                }
                ListView {
                    id: list
                    objectName: "autosavesList"
                    x: 1
                    y: header.height + 1
                    width: parent.width - 2
                    height: parent.height - header.height - 2
                    clip: true
                    model: autosaves.shown
                    activeFocusOnTab: false
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Item {
                        id: row
                        required property var modelData
                        required property int index
                        readonly property bool legacy: modelData.kind === "legacy"
                        objectName: "autosaveRow" + index
                        width: ListView.view.width
                        implicitHeight: 48
                        Rectangle {
                            visible: row.index > 0
                            width: parent.width
                            height: 1
                            color: Theme.line
                        }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            spacing: 12
                            ColumnLayout {
                                spacing: 2
                                Layout.fillWidth: true
                                Item {
                                    Layout.fillWidth: true
                                    implicitHeight: nameLabel.implicitHeight
                                    Label {
                                        id: nameLabel
                                        objectName: "autosaveName" + row.index
                                        text: row.modelData.name
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                        width: Math.min(implicitWidth, parent.width - (tag.visible ? tag.implicitWidth + 8 : 0))
                                    }
                                    Label {
                                        id: tag
                                        visible: row.legacy
                                        x: nameLabel.width + 8
                                        anchors.verticalCenter: nameLabel.verticalCenter
                                        text: qsTr("Previous version")
                                        color: Theme.muted
                                        font: autosavesWindow.fontAt(11)
                                        leftPadding: 5
                                        rightPadding: 5
                                        background: Rectangle { color: "transparent"; border.color: Theme.line; radius: 3 }
                                    }
                                }
                                Label {
                                    text: row.legacy ? qsTr("Autosave of the previous version")
                                        : row.modelData.path.length > 0 ? row.modelData.path : qsTr("Never saved")
                                    font: {
                                        const f = autosavesWindow.fontAt(12)
                                        f.italic = !row.legacy && row.modelData.path.length === 0
                                        return f
                                    }
                                    color: Theme.muted
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }
                            }
                            Label {
                                text: autosaves.whenText(row.modelData.when)
                                Layout.preferredWidth: 110
                            }
                            ComboBox {
                                id: version
                                objectName: "autosaveVersion" + row.index
                                model: row.modelData.versions.map(v => v.label)
                                Accessible.name: qsTr("Version of %1").arg(row.modelData.name)
                                Layout.preferredWidth: 116
                            }
                            Button {
                                objectName: "autosaveOpen" + row.index
                                text: qsTr("Open copy")
                                Accessible.description: qsTr("Opens it as a new unsaved copy")
                                onClicked: autosaves.openCopy(row.modelData, version.currentIndex)
                            }
                            IconToolButton {
                                objectName: "autosaveDelete" + row.index
                                iconRole: "delete"
                                text: row.legacy ? qsTr("Read only") : qsTr("Delete")
                                enabled: !row.legacy
                                onClicked: autosaves.remove(row.modelData)
                            }
                        }
                    }
                }
            }
            Label {
                text: qsTr("Each opens as a new unsaved copy; the original file is not changed. Autosaves of the previous version are read only.")
                color: Theme.muted
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            leadingActions: [
                IconTextButton {
                    iconRole: "delete"
                    id: deleteOlderButton
                    objectName: "autosavesDeleteOlder"
                    text: qsTr("Delete older than…")
                    highlighted: deleteOlder.visible
                    onClicked: autosavesWindow.openDeleteOlder()
                }
            ]
            buttons: [
                PrimaryButton {
                    id: closeButton
                    objectName: "autosavesClose"
                    text: qsTr("Close")
                    DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                    onClicked: autosavesWindow.reject()
                }
            ]
            defaultButton: closeButton

            Popover {
                id: deleteOlder
                objectName: "autosavesDeleteOlderPopover"
                title: qsTr("Delete autosaves saved before")
                showTitle: true
                initialFocus: olderDate.field
                readonly property int count: autosaves.olderCount(olderDate.date)
                DatePicker {
                    id: olderDate
                    objectName: "olderDate"
                    accessibleName: qsTr("Delete autosaves saved before")
                    // MUST KILL: days: -1 (tomorrow) is the "Everything" shortcut
                    shortcuts: [{ text: qsTr("A week ago"), days: 7 }, { text: qsTr("A month ago"), months: 1 },
                                { text: qsTr("Everything"), days: -1 }]
                }
                DangerButton {
                    objectName: "autosavesDeleteOlderNow"
                    enabled: deleteOlder.count > 0
                    // Without an English catalog, qsTr %n never picks a singular.
                    text: deleteOlder.count === 1 ? qsTr("Delete 1 autosave") : qsTr("Delete %n autosaves", "", deleteOlder.count)
                    Layout.alignment: Qt.AlignRight
                    onClicked: {
                        autosaves.deleteOlder(olderDate.date)
                        deleteOlder.keep()
                    }
                }
            }
        }
    }
}
