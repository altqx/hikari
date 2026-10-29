import QtQuick
import Hikari.Ui

QtObject {
    id: question
    property MessageHost messages: null
    property string questionId: ""
    property var pending: []

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
            question.pending = question.pending.filter(q => q !== asked)
            if (again && chosen && id === chosen.id)
                hide()
            if (done)
                done(id)
        })
        pending = pending.concat([asked])
    }
    function withdraw() {
        for (const asked of pending.slice())
            messages.withdraw(asked)
    }
    function hide() {
        const settings = messages.settings
        const list = Array.from(settings.value("notices.hiddenQuestions") || [])
        if (list.indexOf(questionId) < 0)
            settings.setValue("notices.hiddenQuestions", list.concat([questionId]))
    }
}
