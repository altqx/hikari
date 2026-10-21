pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.MainWindow"
// PK1 (#301): toasts, the kit's notice that needs no answer (the OK-only
// messages of legacy). They stack at the bottom right of the window they
// are put in, the newest on top, three at most:
// * "info" fades after 5 seconds; the pointer over it holds it;
// * "warning" and "error" stay until their × is pressed;
// * a fourth makes the oldest information toast go, or, with three
//   warnings showing, folds the oldest into a "1 more notice" line that
//   opens the Log (it stays when the toasts showing are dismissed);
// * Details (when the notice has more to say) opens the Log.
// Every notice goes to the Log too (silently: the Log window does not pop
// up). With "Show notices as toasts" off (notices.toasts) they go only to
// the Log.
//
// post(severity, title, text, options): options.details, more lines for
// the Log, which adds Details; options.actions, [{text, run}] buttons
// beside it. Returns the toast's id (0: none shown).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Item {
    id: stack
    objectName: "toastStack"
    property LogController log: null
    property SettingsStore settings: null
    // Opens the whole Log (default: the Log window).
    property var openLog: () => { if (stack.log) stack.log.showAll() }
    // Newest first: {id, severity, title, text, details, actions}.
    property var entries: []
    // Warnings folded into the "more" line.
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

    function post(severity, title, text, options) {
        const o = options || {}
        const logged = [title, text, o.details].filter(t => t && t.length > 0).join("\n")
        if (log)
            log.log(logged, true)
        if (!toasts)
            return 0
        const entry = { id: nextId++, severity: severity || "info", title: title || "", text: text || "",
                        details: o.details || "", actions: o.actions || [] }
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
        list.unshift(entry)
        entries = list
        return entry.id
    }
    // The folded count stays: only the Log (or clear()) lets those go.
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
                        iconRole: ({ error: "notice-error", warning: "notice-warning", info: "about" })[toast.modelData.severity] || "about"
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
                            text: toast.modelData.title
                            font.weight: Font.DemiBold
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                        Label {
                            objectName: "toastText"
                            visible: text.length > 0
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
                            Button {
                                objectName: "toastDetails"
                                visible: toast.modelData.details.length > 0
                                flat: true
                                text: qsTr("Details")
                                onClicked: stack.openLog()
                            }
                            Repeater {
                                model: toast.modelData.actions
                                Button {
                                    required property var modelData
                                    flat: true
                                    text: modelData.text
                                    onClicked: modelData.run()
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
        // The warnings that did not fit.
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
                Button {
                    objectName: "toastMoreOpenLog"
                    flat: true
                    text: qsTr("Open the Log")
                    onClicked: {
                        stack.folded = 0
                        stack.openLog()
                    }
                }
            }
        }
    }
}
