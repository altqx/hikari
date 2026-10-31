pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.MainWindow"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Item {
    id: stack
    objectName: "toastStack"
    property LogController log: null
    property SettingsStore settings: null
    property var openLog: id => { if (stack.log) stack.log.reveal(id || 0) }
    default property alias pinned: pinnedSlot.data
    property var entries: []
    property int folded: 0
    property int nextId: 1
    readonly property int maximum: 3
    property int fadeAfter: 5000
    property bool toasts: settings ? settings.value("notices.toasts") !== false : true
    Connections {
        target: stack.settings
        function onChanged(id) {
            if (id === "notices.toasts")
                stack.toasts = stack.settings.value(id) !== false
        }
    }
    onToastsChanged: if (!toasts) clear()

    implicitWidth: 360
    implicitHeight: column.implicitHeight
    width: implicitWidth
    height: implicitHeight

    readonly property var severities: ["info", "warning", "error"]
    // MUST KILL: post() passes pendingActions and gets lastShown back through the synchronous noticed signal
    property var pendingActions: []
    property int lastShown: 0
    function post(severity, title, text, options) {
        const o = options || {}
        const level = Math.max(0, severities.indexOf(severity || "info"))
        if (!log)
            return show(0, level, title, text, o.details, o.actions)
        pendingActions = o.actions || []
        lastShown = 0
        log.notice(level, title || "", text || "", o.details || "")
        pendingActions = []
        return lastShown
    }
    Connections {
        target: stack.log
        function onNoticed(id, severity, title, text, details) {
            stack.lastShown = stack.show(id, severity, title, text, details, stack.pendingActions)
        }
    }
    Component.onCompleted: if (log) log.replayNotices()
    function show(entry, level, title, text, details, actions) {
        if (!toasts)
            return 0
        const item = { id: nextId++, entry: entry, severity: severities[level] || "info", title: title || "",
                       text: text || "", details: details || "", actions: actions || [] }
        let list = entries.slice()
        if (list.length >= maximum) {
            let oldest = -1
            for (let i = list.length - 1; i >= 0; --i)
                if (list[i].severity === "info") { oldest = i; break }
            if (oldest < 0) {
                oldest = list.length - 1
                folded += 1
            }
            list.splice(oldest, 1)
        }
        list.unshift(item)
        entries = list
        return item.id
    }
    function dismiss(id) {
        entries = entries.filter(e => e.id !== id)
    }
    function clear() {
        entries = []
        folded = 0
    }

    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("Notices")

    ColumnLayout {
        id: column
        width: parent.width
        anchors.bottom: parent.bottom
        spacing: 8
        Repeater {
            model: stack.entries
            delegate: Item {
                id: toast
                required property var modelData
                readonly property bool fades: modelData.severity === "info"
                objectName: "toast_" + modelData.id
                implicitHeight: row.implicitHeight + 20
                Layout.fillWidth: true
                Accessible.role: Accessible.AlertMessage
                Accessible.name: modelData.title
                Accessible.description: modelData.text
                opacity: 1
                PopoverBackground {
                    anchors.fill: parent
                    color: hover.hovered ? Theme.raised : Theme.panel
                }
                HoverHandler {
                    id: hover
                    onHoveredChanged: {
                        if (!toast.fades)
                            return
                        if (hovered) {
                            fade.stop()
                            toast.opacity = 1
                            timer.stop()
                        } else {
                            timer.restart()
                        }
                    }
                }
                Timer {
                    id: timer
                    objectName: "toastTimer"
                    interval: stack.fadeAfter
                    running: toast.fades
                    onTriggered: fade.start()
                }
                NumberAnimation {
                    id: fade
                    target: toast
                    property: "opacity"
                    to: 0
                    duration: 200
                    onFinished: stack.dismiss(toast.modelData.id)
                }
                RowLayout {
                    id: row
                    anchors.fill: parent
                    anchors.topMargin: 10
                    anchors.bottomMargin: 10
                    anchors.leftMargin: 12
                    anchors.rightMargin: 6
                    spacing: 10
                    Icon {
                        iconRole: ({ error: "notice-error", warning: "notice-warning", info: "notice-info" })[toast.modelData.severity] || "notice-info"
                        color: toast.modelData.severity === "error" ? Theme.danger
                             : toast.modelData.severity === "warning" ? Theme.warning : IconTheme.normal
                        accentColor: toast.modelData.severity === "info" ? IconTheme.accent : color
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 1
                    }
                    ColumnLayout {
                        spacing: 3
                        Layout.fillWidth: true
                        Label {
                            objectName: "toastTitle"
                            textFormat: Text.PlainText
                            text: toast.modelData.title
                            font.weight: Font.DemiBold
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                        Label {
                            objectName: "toastText"
                            visible: text.length > 0
                            textFormat: Text.PlainText
                            text: toast.modelData.text
                            color: Theme.muted
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                        RowLayout {
                            visible: toast.modelData.details.length > 0 || toast.modelData.actions.length > 0
                            spacing: 4
                            Layout.leftMargin: -6
                            Layout.topMargin: 4
                            IconTextButton {
                                iconRole: "log"
                                objectName: "toastDetails"
                                visible: toast.modelData.details.length > 0
                                flat: true
                                text: qsTr("Details")
                                onClicked: stack.openLog(toast.modelData.entry)
                            }
                            Repeater {
                                model: toast.modelData.actions
                                Button {
                                    required property var modelData
                                    flat: true
                                    text: modelData.text
                                    onClicked: {
                                        const id = toast.modelData.id
                                        modelData.run()
                                        stack.dismiss(id)
                                    }
                                }
                            }
                        }
                    }
                    IconToolButton {
                        objectName: "toastDismiss"
                        iconRole: "tab-close"
                        iconSize: 14
                        text: qsTr("Dismiss")
                        Layout.alignment: Qt.AlignTop
                        onClicked: stack.dismiss(toast.modelData.id)
                    }
                }
            }
        }
        Rectangle {
            objectName: "toastMore"
            visible: stack.folded > 0
            color: Theme.panel
            border.color: Theme.line
            radius: 4
            implicitHeight: moreRow.implicitHeight + 8
            Layout.fillWidth: true
            RowLayout {
                id: moreRow
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 4
                spacing: 8
                Label {
                    text: qsTr("%n more notice(s)", "", stack.folded)
                    color: Theme.muted
                    Layout.fillWidth: true
                }
                IconTextButton {
                    iconRole: "log"
                    objectName: "toastMoreOpenLog"
                    flat: true
                    text: qsTr("Open the Log")
                    onClicked: {
                        stack.folded = 0
                        stack.openLog(0)
                    }
                }
            }
        }
        // A child's visible goes false with the slot's, so each says it shows by its own `shown`.
        Item {
            id: pinnedSlot
            objectName: "toastPinned"
            Layout.fillWidth: true
            implicitHeight: {
                let h = 0
                for (const child of children)
                    if (child.shown === true)
                        h = Math.max(h, child.implicitHeight)
                return h
            }
            visible: implicitHeight > 0
        }
    }
}
