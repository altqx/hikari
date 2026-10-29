pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.MainWindow"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

RowLayout {
    id: picker
    property date date: new Date()
    property string accessibleName: ""
    property var shortcuts: [{ text: qsTr("A week ago"), days: 7 }, { text: qsTr("A month ago"), months: 1 }]
    property date pending: new Date()
    property int shownYear: pending.getFullYear()
    property int shownMonth: pending.getMonth()
    readonly property var locale: Qt.locale()
    // Qt parses a two-digit year as 19xx.
    readonly property string shortFormat: {
        const f = locale.dateFormat(Locale.ShortFormat)
        return f.indexOf("yyyy") >= 0 || f.indexOf("yy") < 0 ? f : f.replace("yy", "yyyy")
    }
    readonly property alias field: dateField
    readonly property alias popover: calendar
    signal edited(date date)
    spacing: 0

    function day(d) { return new Date(d.getFullYear(), d.getMonth(), d.getDate()) }
    function setDate(d) {
        date = day(d)
        dateField.text = format(date)
        edited(date)
    }
    function format(d) { return d.toLocaleDateString(locale, shortFormat) }
    function parse(text) {
        const d = Date.fromLocaleDateString(locale, text.trim(), shortFormat)
        return isNaN(d.getTime()) ? null : d
    }
    function back(s, from) {
        const d = day(from || new Date())
        if (s.months) {
            const dayOf = d.getDate()
            d.setDate(1)
            d.setMonth(d.getMonth() - s.months)
            d.setDate(Math.min(dayOf, new Date(d.getFullYear(), d.getMonth() + 1, 0).getDate()))
        }
        if (s.days)
            d.setDate(d.getDate() - s.days)
        return d
    }
    function show(d) {
        pending = day(d)
        shownYear = pending.getFullYear()
        shownMonth = pending.getMonth()
    }
    function moveDays(n) {
        const d = new Date(pending)
        d.setDate(d.getDate() + n)
        show(d)
    }
    function moveMonths(n) {
        const d = new Date(pending)
        const dayOf = d.getDate()
        d.setDate(1)
        d.setMonth(d.getMonth() + n)
        d.setDate(Math.min(dayOf, new Date(d.getFullYear(), d.getMonth() + 1, 0).getDate()))
        show(d)
    }
    function openCalendar() {
        const typed = parse(dateField.text)
        if (typed && day(typed).getTime() !== date.getTime())
            setDate(typed)
        show(date)
        // MUST KILL: the grid must load before openAt, which measures the popover's content
        gridLoader.active = true
        calendar.openAt(dateField)
        gridLoader.item.forceActiveFocus()
    }
    onDateChanged: if (!dateField.activeFocus) dateField.text = format(date)

    TextField {
        id: dateField
        objectName: "datePickerField"
        text: picker.format(picker.date)
        Accessible.name: picker.accessibleName
        Accessible.description: qsTr("Date, as %1").arg(picker.shortFormat)
        Layout.preferredWidth: Math.max(112, contentWidth + leftPadding + rightPadding + 32)
        rightPadding: 32
        onEditingFinished: {
            const d = picker.parse(text)
            if (d)
                picker.setDate(d)
            else
                text = picker.format(picker.date)
        }
        Keys.onPressed: event => {
            if (event.key === Qt.Key_Down && (event.modifiers & Qt.AltModifier)) {
                event.accepted = true
                picker.openCalendar()
            }
        }
        IconToolButton {
            objectName: "datePickerOpen"
            anchors.right: parent.right
            anchors.rightMargin: 2
            anchors.verticalCenter: parent.verticalCenter
            iconRole: "calendar"
            text: qsTr("Choose a date")
            focusPolicy: Qt.NoFocus
            onClicked: picker.openCalendar()
        }
    }

    Popover {
        id: calendar
        objectName: "datePickerCalendar"
        title: picker.accessibleName.length > 0 ? picker.accessibleName : qsTr("Choose a date")
        RowLayout {
            Layout.fillWidth: true
            IconToolButton {
                objectName: "datePickerPreviousMonth"
                iconRole: "chevron-left"
                text: qsTr("Previous month")
                focusPolicy: Qt.NoFocus
                onClicked: picker.moveMonths(-1)
            }
            Label {
                objectName: "datePickerMonth"
                text: picker.locale.standaloneMonthName(picker.shownMonth, Locale.LongFormat) + " " + picker.shownYear
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                Layout.fillWidth: true
            }
            IconToolButton {
                objectName: "datePickerNextMonth"
                iconRole: "chevron-right"
                text: qsTr("Next month")
                focusPolicy: Qt.NoFocus
                onClicked: picker.moveMonths(1)
            }
        }
        DayOfWeekRow {
            locale: picker.locale
            Layout.fillWidth: true
            delegate: Label {
                required property string shortName
                text: shortName
                color: Theme.muted
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: Math.round(Qt.application.font.pixelSize * 11 / 13)
            }
        }
        Loader {
            id: gridLoader
            active: false
            Layout.fillWidth: true
            Layout.preferredWidth: 7 * 32
            sourceComponent: MonthGrid {
                id: grid
                objectName: "datePickerGrid"
                month: picker.shownMonth
                year: picker.shownYear
                locale: picker.locale
                focus: true
                Accessible.role: Accessible.Table
                Accessible.name: calendar.title
                Keys.onLeftPressed: picker.moveDays(LayoutMirroring.enabled ? 1 : -1)
                Keys.onRightPressed: picker.moveDays(LayoutMirroring.enabled ? -1 : 1)
                Keys.onUpPressed: picker.moveDays(-7)
                Keys.onDownPressed: picker.moveDays(7)
                Keys.onPressed: event => {
                    if (event.key === Qt.Key_PageUp)
                        picker.moveMonths(-1)
                    else if (event.key === Qt.Key_PageDown)
                        picker.moveMonths(1)
                    else
                        return
                    event.accepted = true
                }
                Keys.onReturnPressed: event => { event.accepted = true; picker.take(picker.pending) }
                Keys.onEnterPressed: event => { event.accepted = true; picker.take(picker.pending) }
                onClicked: d => picker.take(d)
                delegate: Label {
                    id: cell
                    required property var model
                    readonly property bool chosen: model.year === picker.pending.getFullYear()
                                                   && model.month === picker.pending.getMonth()
                                                   && model.day === picker.pending.getDate()
                    text: model.day
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    topPadding: 6
                    bottomPadding: 6
                    color: cell.chosen ? Theme.onAccent : model.month === grid.month ? Theme.text : Theme.disabled
                    font.weight: cell.chosen ? Font.DemiBold : Font.Normal
                    Accessible.role: Accessible.Cell
                    Accessible.name: model.date.toLocaleDateString(picker.locale, Locale.LongFormat)
                    Accessible.selected: cell.chosen
                    background: Rectangle {
                        color: cell.chosen ? Theme.accent : "transparent"
                        border.width: model.today ? 1 : 0
                        border.color: Theme.line
                        radius: 1
                        Rectangle {
                            visible: cell.chosen && grid.activeFocus
                            anchors.fill: parent
                            anchors.margins: -2
                            color: "transparent"
                            border.color: Theme.focus
                            radius: 2
                        }
                    }
                }
            }
        }
        RowLayout {
            spacing: 4
            Layout.fillWidth: true
            Repeater {
                model: picker.shortcuts
                Button {
                    required property var modelData
                    required property int index
                    objectName: "datePickerShortcut" + index
                    flat: true
                    text: modelData.text
                    focusPolicy: Qt.NoFocus
                    onClicked: picker.take(picker.back(modelData))
                }
            }
            Item { Layout.fillWidth: true }
            Button {
                objectName: "datePickerToday"
                flat: true
                text: qsTr("Today")
                focusPolicy: Qt.NoFocus
                onClicked: picker.take(new Date())
            }
        }
    }
    function take(d) {
        setDate(d)
        calendar.keep()
    }
}
