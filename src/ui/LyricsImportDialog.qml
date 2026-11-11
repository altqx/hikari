pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Documents"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: dialog
    objectName: "lyricsImportDialog"
    dialogId: "lyricsImport"
    required property var controller
    signal chooseAnother()
    title: qsTr("Import lyrics")
    modal: true
    defaultButton: importButton

    readonly property var formatNames: [qsTr("LRC"), qsTr("TTML"), qsTr("Kugou KRC, decoded"), qsTr("QQ Music QRC, decoded")]
    readonly property bool readable: !controller.encrypted

    function formatText() {
        const c = dialog.controller
        const timing = c.wordTimed ? qsTr("word-timed") : qsTr("line-timed")
        return [formatNames[c.format] ?? "", timing, qsTr("%n lines", "", c.lineCount)].join(" · ")
    }
    function songText() {
        const c = dialog.controller
        const parts = [c.title, c.artist].filter(p => p.length > 0)
        if (c.offset !== 0)
            parts.push(qsTr("offset %1 s applied").arg((c.offset > 0 ? "+" : "") + (c.offset / 1000).toFixed(3)))
        return parts.join(" · ")
    }

    component Seconds: SpinBox {
        from: 0
        to: 360000
        stepSize: 10
        editable: true
        textFromValue: (value, locale) => Number(value / 100).toLocaleString(locale, "f", 2)
        valueFromText: (text, locale) => Math.round(Number.fromLocaleString(locale, text) * 100)
        Layout.preferredWidth: 120
    }

    GridLayout {
        columns: 2
        columnSpacing: 12
        rowSpacing: 4
        Layout.fillWidth: true
        Layout.preferredWidth: 600
        Label { text: qsTr("File") }
        Label {
            objectName: "lyricsImportFile"
            text: dialog.controller.fileName
            elide: Text.ElideMiddle
            Layout.fillWidth: true
        }
        Label {
            text: qsTr("Format")
            visible: dialog.controller.canImport
        }
        Label {
            objectName: "lyricsImportFormat"
            visible: dialog.controller.canImport
            text: dialog.controller.canImport ? dialog.formatText() : ""
            color: Theme.muted
            Layout.fillWidth: true
        }
        Label {
            text: qsTr("Song")
            visible: songLabel.visible
        }
        Label {
            id: songLabel
            objectName: "lyricsImportSong"
            visible: text.length > 0
            text: dialog.controller.canImport ? dialog.songText() : ""
            color: Theme.muted
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
        Label {
            text: qsTr("Encoding")
            visible: dialog.readable
        }
        ComboBox {
            objectName: "lyricsImportEncoding"
            visible: dialog.readable
            Layout.preferredWidth: 260
            model: dialog.controller.encodings.map((name, i) => i === dialog.controller.detectedEncoding
                                                                 ? qsTr("%1 · detected").arg(name) : name)
            currentIndex: dialog.controller.encoding
            onActivated: index => dialog.controller.encoding = index
        }
    }

    Rectangle {
        objectName: "lyricsImportEncrypted"
        visible: dialog.controller.encrypted
        color: Theme.raised
        border.color: Theme.line
        implicitHeight: encryptedRow.implicitHeight + 20
        Layout.fillWidth: true
        RowLayout {
            id: encryptedRow
            anchors.fill: parent
            anchors.margins: 10
            spacing: 10
            Icon {
                iconRole: "notice-warning"
                color: Theme.warning
                accentColor: Theme.warning
                Layout.alignment: Qt.AlignTop
            }
            ColumnLayout {
                spacing: 6
                Layout.fillWidth: true
                Label {
                    text: dialog.controller.format === 2
                          ? qsTr("This Kugou lyrics file is encrypted, so HikariSub can’t read it.")
                          : qsTr("This QQ Music lyrics file is encrypted, so HikariSub can’t read it.")
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                Label {
                    text: qsTr("Decode it to text with your own tool first, then import the decoded file. HikariSub doesn’t decrypt lyric files.")
                    color: Theme.muted
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }
        }
    }

    FieldError {
        objectName: "lyricsImportProblem"
        text: dialog.controller.problem
        Layout.fillWidth: true
    }

    MenuHeading {
        text: qsTr("Preview")
        visible: dialog.controller.canImport
        leftPadding: 0
    }
    Repeater {
        model: dialog.controller.preview
        delegate: RowLayout {
            id: previewRow
            required property var modelData
            objectName: "lyricsImportPreviewRow"
            spacing: 12
            Layout.fillWidth: true
            Label {
                text: previewRow.modelData.start
                font.family: "monospace"
            }
            Label {
                text: previewRow.modelData.end
                font.family: "monospace"
            }
            Label {
                text: previewRow.modelData.text
                textFormat: Text.PlainText
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }
    }
    Label {
        objectName: "lyricsImportMore"
        visible: dialog.controller.canImport && dialog.controller.lineCount > dialog.controller.preview.length
        text: qsTr("and %n more lines", "", dialog.controller.lineCount - dialog.controller.preview.length)
        color: Theme.muted
    }

    MenuHeading {
        text: qsTr("Add the lines to")
        visible: dialog.readable
        leftPadding: 0
    }
    RadioButton {
        objectName: "lyricsImportIntoOpen"
        visible: dialog.readable
        text: qsTr("After the last line of the open file")
        enabled: dialog.controller.openFileAvailable
        checked: dialog.controller.intoOpenFile
        onToggled: dialog.controller.intoOpenFile = checked
    }
    RadioButton {
        objectName: "lyricsImportIntoNew"
        visible: dialog.readable
        text: qsTr("A new file")
        checked: !dialog.controller.intoOpenFile
        onToggled: dialog.controller.intoOpenFile = !checked
    }
    RowLayout {
        visible: dialog.readable && dialog.controller.intoOpenFile
        spacing: 8
        Layout.leftMargin: 24
        Label { text: qsTr("Style") }
        ComboBox {
            objectName: "lyricsImportStyle"
            model: dialog.controller.styles
            currentIndex: dialog.controller.styles.indexOf(dialog.controller.style)
            onActivated: index => dialog.controller.style = dialog.controller.styles[index]
            Layout.preferredWidth: 220
        }
    }

    MenuHeading {
        text: qsTr("End times")
        visible: dialog.readable
        leftPadding: 0
    }
    RadioButton {
        objectName: "lyricsImportEndAtNext"
        visible: dialog.readable
        text: qsTr("Each line ends when the next one starts")
        checked: !dialog.controller.gapOn
        onToggled: dialog.controller.gapOn = !checked
    }
    RadioButton {
        objectName: "lyricsImportEndWithGap"
        visible: dialog.readable
        text: qsTr("Leave a gap before the next line")
        checked: dialog.controller.gapOn
        onToggled: dialog.controller.gapOn = checked
    }
    GridLayout {
        visible: dialog.readable
        columns: 2
        columnSpacing: 8
        rowSpacing: 4
        Label {
            text: qsTr("Gap (s)")
            Layout.leftMargin: 24
        }
        Seconds {
            objectName: "lyricsImportGap"
            enabled: dialog.controller.gapOn
            value: dialog.controller.gap / 10
            onValueModified: dialog.controller.gap = value * 10
        }
        Label { text: qsTr("Last line lasts (s)") }
        Seconds {
            objectName: "lyricsImportLastLine"
            value: dialog.controller.lastLine / 10
            onValueModified: dialog.controller.lastLine = value * 10
        }
    }

    leadingActions: [
        Button {
            objectName: "lyricsImportChooseAnother"
            text: qsTr("Choose another file…")
            onClicked: {
                dialog.reject()
                dialog.chooseAnother()
            }
        }
    ]
    buttons: [
        PrimaryButton {
            id: importButton
            objectName: "lyricsImportImport"
            text: qsTr("Import")
            enabled: dialog.controller.canImport
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: if (dialog.controller.import()) dialog.accept()
        },
        Button {
            objectName: "lyricsImportCancel"
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: dialog.reject()
        }
    ]
}
