pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.MainWindow"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

// DG1 (#222): Help › Credits on the dialog frame, redone for HikariSub
// (the owner's decision of 2026-10-21, DG1-credits-rewrite): who made it,
// who translated it, and the components it is built with (vcpkg.json, and
// libass's text stack), each with its copyright holder and licence, in a
// list that scrolls. CSRI and VSFilter show where they are part of the
// build (Application.includesCsri, includesVsfilter). Fixed size, modeless.
DialogWindow {
    id: credits
    objectName: "creditsDialog"
    required property var app // Application: includesCsri, includesVsfilter
    dialogId: "credits"
    title: qsTr("Credits")
    defaultButton: ok

    // Name, copyright holder, licence (proper names, not translated).
    readonly property var components: {
        const all = [
            ["Qt", "The Qt Company Ltd. and other contributors", "LGPL 3"],
            ["KDDockWidgets", "Klarälvdalens Datakonsult AB (KDAB)", "GPL 2 or 3"],
            ["FFmpeg", "The FFmpeg developers", "LGPL 2.1 or later"],
            ["FFMS2", "Fredrik Mellbin and contributors", "MIT"],
            ["libass", "libass contributors", "ISC"],
            ["FreeType", "David Turner, Robert Wilhelm, Werner Lemberg and others", "FreeType License"],
            ["FriBidi", "FriBidi contributors", "LGPL 2.1 or later"],
            ["HarfBuzz", "HarfBuzz contributors", "MIT"],
            ["LuaJIT", "Mike Pall", "MIT"],
            ["Boost", "Boost contributors", "Boost Software License 1.0"],
            ["ICU", "Unicode, Inc. and others", "Unicode License"],
            ["Hunspell", "László Németh and others", "MPL 1.1, GPL 2 or LGPL 2.1"],
            ["PortAudio", "Ross Bencina, Phil Burk and contributors", "MIT"],
            // KA2-slowed-playback: the slowed audio box keeps its pitch
            ["SoundTouch", "Olli Parviainen", "LGPL 2.1 or later"],
            ["curl", "Daniel Stenberg and contributors", "curl License"],
            ["PCRE2", "University of Cambridge and others", "BSD 3-Clause"],
            ["uchardet", "uchardet contributors", "MPL 1.1, GPL 2 or LGPL 2.1"],
            // GR1: harper-ls, the grammar checker (its crates' licences ship beside it)
            ["Harper", "Automattic and Harper contributors", "Apache 2.0"],
            // DK1: the bundled spelling dictionaries (licenses/dictionaries holds their notices)
            ["en_US Hunspell dictionary", "Kevin Atkinson, Geoff Kuenning and others", "LGPL and BSD"],
            ["sjp.pl Polish dictionary", "sjp.pl contributors", "GPL 2, LGPL 2.1, MPL 1.1, Apache 2.0 or CC BY 4.0"],
            ["th_TH Hunspell dictionary", "NECTEC (LEXiTRON) and libthai contributors", "LGPL"],
            ["hunspell-dict-ko", "Changwoo Ryu, hunspell-dict-ko contributors and the National Institute of Korean Language", "GPL 3 or later"],
            ["ThamiZha! Solthiruthi", "Ve. Elanjelian and the ThamiZha! team", "MPL 1.1"],
        ]
        if (app.includesCsri)
            all.push(["CSRI", "David Lamparter", "BSD"])
        if (app.includesVsfilter)
            all.push(["VSFilter", "Gabest and contributors", "GPL 2 or later"])
        return all
    }

    component SectionLabel: Label {
        color: Theme.muted
    }
    component Person: RowLayout {
        id: person
        required property string name
        required property string role
        spacing: 12
        Label { text: person.name; font.weight: Font.DemiBold; Layout.preferredWidth: 110 }
        Label { text: person.role; color: Theme.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
    }

    ColumnLayout {
        spacing: 6
        Layout.preferredWidth: 492
        SectionLabel { text: qsTr("Development"); font: credits.fontAt(11, Font.DemiBold); Layout.topMargin: 6 }
        Person { objectName: "creditsDevelopment"; name: "altqx"; role: qsTr("Design and development"); Layout.fillWidth: true }
        SectionLabel { text: qsTr("Translation"); font: credits.fontAt(11, Font.DemiBold); Layout.topMargin: 12 }
        Person { objectName: "creditsTranslation"; name: "altqx"; role: qsTr("Korean, Polish, Tamil and Thai"); Layout.fillWidth: true }
        SectionLabel { text: qsTr("Built with"); font: credits.fontAt(11, Font.DemiBold); Layout.topMargin: 12 }
        Rectangle {
            color: Theme.field
            border.width: 1
            // The keyboard on the list (Tab, then Up and Down scroll it): the focus colour.
            border.color: list.activeFocus ? Theme.focus : Theme.line
            Layout.fillWidth: true
            Layout.preferredHeight: 250
            ListView {
                id: list
                objectName: "creditsComponents"
                anchors.fill: parent
                anchors.margins: 1
                clip: true
                topMargin: 6
                bottomMargin: 6
                model: credits.components
                activeFocusOnTab: true
                keyNavigationEnabled: true
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Built with")
                ScrollBar.vertical: ScrollBar { policy: list.contentHeight > list.height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded }
                // (In an item: the view places its delegates at its left edge.)
                delegate: Item {
                    id: entry
                    required property var modelData
                    width: ListView.view.width
                    implicitHeight: row.implicitHeight
                    Accessible.role: Accessible.ListItem
                    Accessible.name: entry.modelData[0] + ", © " + entry.modelData[1] + ", " + entry.modelData[2]
                    RowLayout {
                        id: row
                        x: 10
                        width: parent.width - 24
                        spacing: 10
                        Label { text: entry.modelData[0]; Layout.preferredWidth: 104; Layout.alignment: Qt.AlignTop; topPadding: 2 }
                        ColumnLayout {
                            spacing: 0
                            Layout.fillWidth: true
                            Label {
                                text: "© " + entry.modelData[1]
                                color: Theme.muted
                                wrapMode: Text.WordWrap
                                topPadding: 2
                                Layout.fillWidth: true
                            }
                            Label {
                                text: entry.modelData[2]
                                color: Theme.muted
                                font: credits.fontAt(11)
                                bottomPadding: 4
                                Layout.fillWidth: true
                            }
                        }
                    }
                }
            }
        }
    }

    buttons: [
        PrimaryButton {
            id: ok
            objectName: "creditsOk"
            text: qsTr("OK")
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: credits.accept()
        }
    ]
}
