// PU4 (#306): a question "Don't ask again" can hide (PK1, visual-language.md
// "Popups"): PK1's message box, made at the first question, unless
// `questionId` is in notices.hiddenQuestions, when the default choice is the
// answer at once. Ticked with the default choice, "Don't ask again" hides the
// question (Settings > Notices > Ask again brings it back); with any other
// answer it hides nothing, so a hidden question never answers what was not
// chosen.
//
// askWith(options, done): MessageBox.ask's options (the default choice's
// isDefault offers "Don't ask again") and done(id).
import QtQuick
import Hikari.Ui

QtObject {
    id: question
    property SettingsStore settings: null
    property string questionId: ""
    // The box's objectName (tests).
    property string boxName: ""
    // The box once made.
    property MessageBox box: null

    function hidden() {
        const list = settings ? Array.from(settings.value("notices.hiddenQuestions") || []) : []
        return list.indexOf(questionId) >= 0
    }
    function askWith(options, done) {
        const choices = options.choices || []
        const chosen = choices.find(c => c.isDefault === true && c.role !== "destructive")
        if (chosen && hidden()) {
            if (done)
                done(chosen.id)
            return
        }
        if (!box)
            box = boxComponent.createObject(question, { settings: question.settings, objectName: question.boxName })
        box.ask(Object.assign({}, options, { dontAsk: chosen !== undefined }), (id, again) => {
            if (again && chosen && id === chosen.id)
                hide()
            if (done)
                done(id)
        })
    }
    function hide() {
        const list = Array.from(settings.value("notices.hiddenQuestions") || [])
        if (list.indexOf(questionId) < 0)
            settings.setValue("notices.hiddenQuestions", list.concat([questionId]))
    }

    readonly property Component boxComponent: Component { MessageBox {} }
}
