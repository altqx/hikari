pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Conversion"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: dialog
    objectName: "conversionDialog"
    required property var app
    property var catalogs: []
    dialogId: "conversion"
    modal: true
    defaultButton: acceptButton
    property string target: ""
    property var preview: ({})
    property var previous: null
    readonly property bool toAss: target === "ass"
    readonly property bool frameRate: target === "mdvd" || preview.fpsInvalid === true
    readonly property bool inputsValid: (!toAss || (resWidth.acceptableInput && resHeight.acceptableInput))
                                        && (!perLetter.visible || !newEnds.checked || perLetter.acceptableInput)

    function openFor(t, label) {
        target = t
        title = label
        const o = app.conversionOptions()
        fps.text = o.fps
        fpsFromVideo.checked = o.fpsFromVideo
        let shown = catalogs.indexOf(o.styleCatalog)
        if (shown < 0)
            shown = catalogs.findIndex(c => c.toLowerCase() === String(o.styleCatalog).toLowerCase())
        shown = Math.max(0, shown)
        catalog.currentIndex = shown
        // MUST KILL: opening the dialog rewrites the stored styleCatalog when it differs from the shown one
        if (catalogs.length > 0 && catalogs[shown] !== o.styleCatalog)
            app.setConversionOptions({styleCatalog: catalogs[shown]})
        styleBox.editText = o.style
        newEnds.checked = o.newEndTimes
        perLetter.text = o.timePerCharacter
        prefix.text = o.prefix
        resWidth.text = o.resolutionWidth
        resHeight.text = o.resolutionHeight
        preview = ({})
        refresh()
        open()
    }
    function refresh() {
        previous = preview.ok ? preview.losses : null
        preview = app.previewConversion(target)
    }
    function set(key, value) {
        app.setConversionOptions({[key]: value})
        refresh()
    }
    function convert() {
        if (!app.acceptConversion())
            refresh()
        else
            close()
    }

    component Hint: Label {
        Layout.fillWidth: true
        Layout.preferredWidth: 200
        wrapMode: Text.Wrap
        color: Theme.muted
        font: dialog.fontAt(12)
    }
    component NumberBox: TextField {
        id: box
        property bool invalid: false
        Layout.preferredWidth: 72
        horizontalAlignment: TextInput.AlignRight
        selectByMouse: true
        background: Rectangle {
            color: Theme.field
            border.width: box.invalid || box.activeFocus ? 2 : 1
            border.color: box.invalid ? Theme.danger : box.activeFocus ? Theme.focus : Theme.control
        }
    }

    RowLayout {
        spacing: 24
        Layout.fillWidth: true
        Layout.fillHeight: true
        ColumnLayout {
            Layout.alignment: Qt.AlignTop
            Layout.preferredWidth: 330
            spacing: 12
            FormSection {
                objectName: "convStyleSection"
                title: qsTr("Style")
                ruled: false
                visible: dialog.toAss
                GridLayout {
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 6
                    Layout.fillWidth: true
                    Label { text: qsTr("Catalog"); Layout.preferredWidth: 92 }
                    ComboBox {
                        id: catalog
                        objectName: "convCatalog"
                        model: dialog.catalogs
                        Layout.preferredWidth: 200
                        Accessible.name: qsTr("Catalog")
                        onActivated: i => {
                            dialog.set("styleCatalog", dialog.catalogs[i])
                            styleBox.editText = dialog.app.conversionOptions().style
                        }
                    }
                    Label { text: qsTr("Style") }
                    ComboBox {
                        id: styleBox
                        objectName: "convStyle"
                        editable: true
                        model: dialog.toAss && catalog.currentIndex >= 0 ? dialog.app.conversionStyles(dialog.catalogs[catalog.currentIndex] ?? "") : []
                        Layout.preferredWidth: 200
                        Accessible.name: qsTr("Style")
                        onActivated: i => dialog.set("style", model[i])
                        Connections {
                            target: styleBox.contentItem
                            ignoreUnknownSignals: true
                            function onTextEdited() { dialog.set("style", styleBox.editText) }
                        }
                    }
                    Item { implicitWidth: 1 }
                    Hint { text: qsTr("Every line gets this style.") }
                    Label { text: qsTr("Tags at start") }
                    TextField {
                        id: prefix
                        objectName: "convPrefix"
                        font.family: "monospace"
                        Layout.preferredWidth: 200
                        Accessible.name: qsTr("Tags at start")
                        onTextEdited: dialog.set("prefix", text)
                    }
                    Item { implicitWidth: 1 }
                    Hint { text: qsTr("Written at the start of every line.") }
                }
            }
            FormSection {
                objectName: "convResolutionSection"
                title: qsTr("Resolution")
                visible: dialog.toAss
                GridLayout {
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 6
                    Layout.fillWidth: true
                    Item { implicitWidth: 1; Layout.preferredWidth: 92 }
                    RowLayout {
                        spacing: 6
                        NumberBox {
                            id: resWidth
                            objectName: "convWidth"
                            validator: IntValidator { bottom: 1; top: 100000 }
                            Accessible.name: qsTr("Width")
                            invalid: !acceptableInput
                            onTextEdited: if (acceptableInput) dialog.set("resolutionWidth", text)
                        }
                        Label { text: "×" }
                        NumberBox {
                            id: resHeight
                            objectName: "convHeight"
                            validator: IntValidator { bottom: 1; top: 100000 }
                            Accessible.name: qsTr("Height")
                            invalid: !acceptableInput
                            onTextEdited: if (acceptableInput) dialog.set("resolutionHeight", text)
                        }
                    }
                    Item { implicitWidth: 1 }
                    Hint { text: qsTr("The new file’s script resolution.") }
                }
            }
            FormSection {
                objectName: "convFrameRateSection"
                title: qsTr("Frame rate")
                ruled: dialog.toAss
                visible: dialog.frameRate
                GridLayout {
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 6
                    Layout.fillWidth: true
                    Label { text: qsTr("Frames"); Layout.preferredWidth: 92 }
                    RowLayout {
                        spacing: 6
                        NumberBox {
                            id: fps
                            objectName: "convFps"
                            Accessible.name: qsTr("Frame rate")
                            Accessible.description: fpsHint.text
                            invalid: dialog.preview.fpsInvalid === true
                            onTextEdited: dialog.set("fps", text)
                        }
                        Label { text: qsTr("fps"); color: Theme.muted }
                        CheckBox {
                            id: fpsFromVideo
                            objectName: "convFpsFromVideo"
                            text: qsTr("From video")
                            onToggled: dialog.set("fpsFromVideo", checked)
                        }
                    }
                    Item { implicitWidth: 1 }
                    Hint {
                        id: fpsHint
                        objectName: "convFpsHint"
                        color: dialog.preview.fpsInvalid === true ? Theme.danger : Theme.muted
                        text: dialog.preview.fpsInvalid === true ? qsTr("Enter a frame rate from 1 to 1000.")
                            : qsTr("MicroDVD counts frames: times become frames at this rate.")
                    }
                }
            }
            FormSection {
                objectName: "convEndTimesSection"
                title: qsTr("End times")
                ruled: dialog.toAss || dialog.frameRate
                visible: dialog.target !== "tmp"
                GridLayout {
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 6
                    Layout.fillWidth: true
                    Item { implicitWidth: 1; Layout.preferredWidth: 92 }
                    CheckBox {
                        id: newEnds
                        objectName: "convNewEnds"
                        text: qsTr("New end times")
                        onToggled: dialog.set("newEndTimes", checked)
                    }
                    Label { text: qsTr("Per character") }
                    RowLayout {
                        spacing: 6
                        NumberBox {
                            id: perLetter
                            objectName: "convPerLetter"
                            validator: IntValidator { bottom: 30; top: 1000 }
                            Accessible.name: qsTr("Per character")
                            invalid: newEnds.checked && !acceptableInput
                            onTextEdited: if (acceptableInput) dialog.set("timePerCharacter", Number(text))
                        }
                        Label { text: qsTr("ms"); color: Theme.muted }
                    }
                    Item { implicitWidth: 1 }
                    Hint { text: qsTr("A line lasts its length × this, at least 1 s, and ends where the next one starts.") }
                }
            }
        }
        Rectangle {
            Layout.alignment: Qt.AlignTop
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 340
            Layout.minimumHeight: 240
            color: Theme.field
            border.color: Theme.line
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8
                Label {
                    text: qsTr("What changes")
                    font.weight: Font.DemiBold
                }
                Label {
                    objectName: "conversionProblem"
                    visible: dialog.preview.ok !== true
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: Theme.muted
                    text: dialog.preview.fpsInvalid === true ? qsTr("Nothing to show until the frame rate is valid.")
                        : (dialog.preview.problem ?? "")
                }
                ListView {
                    id: losses
                    objectName: "conversionLosses"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 2
                    model: dialog.preview.ok === true ? dialog.preview.losses : []
                    Accessible.role: Accessible.List
                    Accessible.name: qsTr("What changes")
                    delegate: Rectangle {
                        id: row
                        required property string modelData
                        required property int index
                        readonly property bool removes: (dialog.preview.kinds ?? [])[index] === "warning"
                        readonly property bool fresh: dialog.previous !== null && dialog.previous.indexOf(modelData) < 0
                        width: ListView.view.width
                        implicitHeight: text.implicitHeight + 6
                        radius: 3
                        color: "transparent"
                        Accessible.role: Accessible.ListItem
                        Accessible.name: modelData
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 4
                            anchors.rightMargin: 4
                            spacing: 8
                            Icon {
                                iconRole: row.removes ? "notice-warning" : "notice-info"
                                size: 16
                                color: row.removes ? Theme.warning : IconTheme.normal
                                accentColor: row.removes ? Theme.warning : IconTheme.accent
                                Layout.alignment: Qt.AlignTop
                                Layout.topMargin: 3
                            }
                            Label {
                                id: text
                                text: row.modelData
                                textFormat: Text.PlainText
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                                topPadding: 3
                            }
                        }
                        SequentialAnimation on color {
                            running: row.fresh
                            ColorAnimation { to: Theme.select; duration: 120 }
                            PauseAnimation { duration: 600 }
                            ColorAnimation { to: Color.rgba(0, 0, 0, 0); duration: 400 }
                        }
                    }
                }
            }
        }
    }

    buttons: [
        PrimaryButton {
            id: acceptButton
            objectName: "conversionAccept"
            text: qsTr("Convert")
            enabled: dialog.preview.ok === true && dialog.inputsValid
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: dialog.convert()
        },
        Button {
            objectName: "conversionCancel"
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: dialog.close()
        }
    ]
}
