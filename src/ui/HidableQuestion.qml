// PU4 (#306): a question "Don't ask again" can hide (PK1, visual-language.md
// "Popups"), asked in the application's message box (NT1's MessageHost),
// unless `questionId` is in notices.hiddenQuestions, when the default choice
// is the answer at once. Ticked with the default choice, "Don't ask again"
// hides the question (Settings > Notices > Ask again brings it back); with
// any other answer it hides nothing, so a hidden question never answers what
// was not chosen.
//
// askWith(options, done): MessageBox.ask's options (the default choice's
// isDefault offers "Don't ask again"; `from`, the window it opens over) and
// done(id). withdraw() takes back the question still waiting for its
// answer, as its reject choice: the caller's, when what it asks about
// changed (a question over the fullscreen video window leaves the main
// window in use).
import QtQuick
import Hikari.Ui

QtObject {
    id: question
    property MessageHost messages: null
    property string questionId: ""
    // The question waiting for its answer (MessageHost.ask's), or null.
    property var pending: null

    function hidden() {
        const settings = messages ? messages.settings : null
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
        const asked = messages.ask(Object.assign({}, options, { dontAsk: chosen !== undefined }), (id, again) => {
            if (question.pending === asked)
                question.pending = null
            if (again && chosen && id === chosen.id)
                hide()
            if (done)
                done(id)
        })
        pending = asked
    }
    function withdraw() {
        if (pending)
            messages.withdraw(pending)
    }
    function hide() {
        const settings = messages.settings
        const list = Array.from(settings.value("notices.hiddenQuestions") || [])
        if (list.indexOf(questionId) < 0)
            settings.setValue("notices.hiddenQuestions", list.concat([questionId]))
    }
}
