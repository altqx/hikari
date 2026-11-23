pragma Translator: "HikariSub.Styles"
import QtQuick

QtObject {
    id: stepper
    required property var app
    required property var editor
    property bool on: false

    function canApply(name) {
        return name.length > 0 && editor.editable && editor.assFields
    }
    function apply(name) {
        if (!canApply(name))
            return false
        const step = on ? app.styleStep(1) : null
        if (step && step.from !== undefined && app.selectedLineCount() > 1)
            app.selectLine(step.from)
        if (!editor.chooseStyle(name))
            return false
        if (step && step.id !== undefined)
            app.selectLine(step.id)
        return true
    }
    function back() {
        const step = app.styleStep(-1)
        if (step.id !== undefined)
            app.selectLine(step.id)
    }
    function foot(name, activeNumber) {
        const step = activeNumber > 0 && name.length > 0 ? app.styleStep(1) : ({})
        if (step.fromNumber === undefined)
            return ""
        //: The style step's foot line: %1 the highlighted style, %2 the active Line's number, %3 the next Line's
        return step.id !== undefined ? qsTr("Enter applies “%1” to line %2 and goes to %3.").arg(name, step.fromNumber, step.number)
            //: The style step's foot line at the last Line: %1 the highlighted style, %2 the active Line's number
            : qsTr("Enter applies “%1” to line %2.").arg(name, step.fromNumber)
    }
}
