pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Interpolate"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: dialog
    objectName: "interpolateLinesDialog"
    required property var app
    required property var shell
    title: qsTr("Interpolate across lines")
    dialogId: "interpolateLines"
    defaultButton: applyButton
    property var document: 0
    property var plan: ({})
    property var excluded: []
    property string failure: ""
    readonly property var changes: plan.changes ?? []
    readonly property bool canApply: (plan.problem ?? "").length === 0 && changes.some(c => c.skip.length === 0 && !excluded.includes(c.id))

    function options() {
        return { accel: Number(accel.text) || 1, shortestRotation: shortest.checked, cascade: cascade.checked, excluded: excluded }
    }
    function replan() {
        if (visible)
            plan = app.interpolationPlan(options())
    }
    function openDialog() {
        excluded = []
        failure = ""
        accel.text = "1"
        shortest.checked = false
        cascade.checked = false
        document = app.editingDocument()
        open()
        replan()
    }
    function apply() {
        if (!canApply || app.editingDocument() !== document)
            return
        failure = app.applyInterpolation(options())
        if (failure.length === 0)
            close()
    }
    function toggle(id, on) {
        const kept = excluded.filter(e => e !== id)
        if (!on)
            kept.push(id)
        excluded = kept
        replan()
    }

    onVisibleChanged: if (!visible) app.clearInterpolationPreview()
    Connections {
        target: dialog.app
        function onTabsChanged() {
            if (dialog.visible && dialog.app.editingDocument() !== dialog.document)
                dialog.close()
        }
    }
    Connections {
        target: dialog.shell
        function onActiveLineChanged() { dialog.replan() }
        function onTargetsChanged() { dialog.replan() }
    }

    ColumnLayout {
        spacing: 10
        Layout.preferredWidth: 560

        Label {
            objectName: "interpolateLinesRange"
            text: (dialog.plan.count ?? 0) > 0
                  ? qsTr("%n line(s), from line %1 to line %2. The video shows the result while this is open.", "", dialog.plan.count)
                        .arg(dialog.plan.firstRow).arg(dialog.plan.lastRow)
                  : ""
            visible: text.length > 0
            color: Theme.muted
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        RowLayout {
            objectName: "interpolateLinesNotice"
            readonly property string text: dialog.failure.length > 0 ? dialog.failure
                                          : (dialog.plan.problem ?? "").length > 0 ? dialog.plan.problem
                                          : dialog.plan.startBlockOnly ? qsTr("The first and last lines have different text, so only the start tags are interpolated.")
                                          : ""
            readonly property bool error: dialog.failure.length > 0 || (dialog.plan.problem ?? "").length > 0
            visible: text.length > 0
            spacing: 6
            Layout.fillWidth: true
            Icon {
                iconRole: parent.error ? "notice-error" : "notice-warning"
                size: 14
                color: parent.error ? Theme.danger : Theme.warning
                accentColor: color
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 2
            }
            Label {
                objectName: "interpolateLinesNoticeText"
                text: parent.text
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }

        Rectangle {
            visible: dialog.changes.length > 0
            color: Theme.field
            border.color: Theme.line
            implicitHeight: tags.implicitHeight + 12
            Layout.fillWidth: true
            GridLayout {
                id: tags
                objectName: "interpolateLinesTags"
                x: 8; y: 6
                width: parent.width - 16
                columns: 4
                columnSpacing: 12
                rowSpacing: 4
                Item { implicitWidth: 18 }
                Label { text: qsTr("Tag"); color: Theme.muted; font: dialog.fontAt(12, Font.DemiBold) }
                Label { text: qsTr("First line"); color: Theme.muted; font: dialog.fontAt(12, Font.DemiBold); Layout.fillWidth: true }
                Label { text: qsTr("Last line"); color: Theme.muted; font: dialog.fontAt(12, Font.DemiBold); Layout.fillWidth: true }
                Repeater {
                    model: dialog.changes
                    delegate: CheckBox {
                        required property var modelData
                        objectName: "interpolateTag_" + modelData.id
                        enabled: modelData.skip.length === 0
                        checked: enabled && !dialog.excluded.includes(modelData.id)
                        Accessible.name: modelData.tag
                        onToggled: dialog.toggle(modelData.id, checked)
                        Layout.row: index + 1
                        Layout.column: 0
                        required property int index
                    }
                }
                Repeater {
                    model: dialog.changes
                    delegate: Label {
                        required property var modelData
                        required property int index
                        text: modelData.before.length > 0 ? qsTr("%1 before “%2”").arg(modelData.tag, modelData.before) : modelData.tag
                        font.family: "monospace"
                        color: modelData.skip.length > 0 ? Theme.muted : Theme.text
                        Layout.row: index + 1
                        Layout.column: 1
                    }
                }
                Repeater {
                    model: dialog.changes
                    delegate: ColumnLayout {
                        required property var modelData
                        required property int index
                        spacing: 0
                        Layout.row: index + 1
                        Layout.column: 2
                        Layout.columnSpan: modelData.skip.length > 0 ? 2 : 1
                        Layout.fillWidth: true
                        ValueLabel {
                            value: modelData.skip.length > 0 ? qsTr("%1 → %2").arg(modelData.from || qsTr("none"), modelData.to || qsTr("none"))
                                                              : modelData.from
                            swatch: modelData.skip.length > 0 ? "" : modelData.fromColour
                            muted: modelData.skip.length > 0
                        }
                        Label {
                            visible: modelData.skip.length > 0
                            text: modelData.skip
                            color: Theme.muted
                            font: dialog.fontAt(12)
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                    }
                }
                Repeater {
                    model: dialog.changes
                    delegate: ValueLabel {
                        required property var modelData
                        required property int index
                        visible: modelData.skip.length === 0
                        value: modelData.to
                        swatch: modelData.toColour
                        Layout.row: index + 1
                        Layout.column: 3
                    }
                }
            }
        }

        RowLayout {
            spacing: 10
            Label { text: qsTr("Acceleration") }
            TextField {
                id: accel
                objectName: "interpolateLinesAccel"
                validator: DoubleValidator { bottom: 0.01; top: 10; notation: DoubleValidator.StandardNotation }
                Layout.preferredWidth: 80
                Accessible.name: qsTr("Acceleration")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("1 is even. Above 1 starts slow and ends fast; below 1 starts fast and ends slow.")
                onTextEdited: dialog.replan()
            }
        }
        CheckBox {
            id: shortest
            objectName: "interpolateLinesShortest"
            text: qsTr("Rotate the shortest way")
            onToggled: dialog.replan()
        }
        CheckBox {
            id: cascade
            objectName: "interpolateLinesCascade"
            text: qsTr("Cascade transforms")
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Each line’s %1 tags run one after another over the line").arg("\\t")
            onToggled: dialog.replan()
        }

        ColumnLayout {
            visible: (dialog.plan.results ?? []).length > 0
            spacing: 4
            Layout.fillWidth: true
            Label { text: qsTr("Result"); font: dialog.fontAt(12, Font.DemiBold) }
            Rectangle {
                color: Theme.field
                border.color: Theme.line
                implicitHeight: 120
                Layout.fillWidth: true
                ListView {
                    objectName: "interpolateLinesResults"
                    anchors.fill: parent
                    anchors.margins: 1
                    clip: true
                    model: dialog.plan.results ?? []
                    ScrollBar.vertical: ScrollBar {}
                    delegate: RowLayout {
                        required property var modelData
                        width: ListView.view.width
                        spacing: 10
                        Label {
                            text: modelData.row
                            color: Theme.muted
                            horizontalAlignment: Text.AlignRight
                            Layout.preferredWidth: 32
                        }
                        Label {
                            text: modelData.text
                            font.family: "monospace"
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                }
            }
        }
    }

    component ValueLabel: RowLayout {
        property string value
        property string swatch
        property bool muted: false
        spacing: 4
        Rectangle {
            visible: parent.swatch.length > 0
            color: parent.swatch.length > 0 ? parent.swatch : "transparent"
            border.color: Theme.line
            implicitWidth: 12
            implicitHeight: 12
        }
        Label {
            text: parent.value
            font.family: "monospace"
            color: parent.muted ? Theme.muted : Theme.text
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
    }

    buttons: [
        PrimaryButton {
            id: applyButton
            objectName: "interpolateLinesApply"
            text: qsTr("Apply")
            enabled: dialog.canApply
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: dialog.apply()
        },
        Button {
            objectName: "interpolateLinesCancel"
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: dialog.close()
        }
    ]
}
