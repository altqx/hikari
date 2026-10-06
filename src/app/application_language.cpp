// O5: the interface language and the program font, applied live
// (docs/qt/localisation.md). Legacy reads both when it starts
// (hikarisubApp.cpp:327-369; the frame's font from Options.GetFont()) and
// applies the program font live (OptionsDialog.cpp:1105-1112); the card asks
// for the language to switch live as well.

#include "hikari/app/application.h"

#include <QGuiApplication>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>

namespace hikari::app {

void Application::startLocalisation()
{
    m_localisation = std::make_unique<Localisation>();
    // Legacy turns the old "0" and "1" values into English (hikarisubApp.cpp:329-334).
    if (const QString stored = m_settings->text("program.language"); stored == u"0" || stored == u"1")
        m_settings->set("program.language", QString());
    m_untitledTitle = tr("Untitled"); // the tabs made so far are named in English
    connect(m_localisation.get(), &Localisation::languageChanged, this, &Application::languageSwitched);
    // A pseudolocale for layout checks (accents, rtl), never stored.
    m_localisation->setPseudo(Localisation::pseudoNamed(qEnvironmentVariable("HIKARI_PSEUDOLOCALE")));
    switchLanguage();
    // aegisub.gettext: answered on this thread, between switches.
    m_automation->setTranslation([localisation = QPointer<Localisation>(m_localisation.get())](const std::string &source) {
        return localisation ? localisation->gettext(source) : source;
    });
    m_startFont = QGuiApplication::font();
    applyProgramFont();
}

void Application::switchLanguage()
{
    if (!m_localisation)
        return;
    // A language without a catalog leaves English (legacy: wxLocale's
    // "Cannot find language" or "Cannot find translation, language change
    // failed" message boxes, hikarisubApp.cpp:347,365; here in the log, on
    // every platform).
    if (!m_localisation->setLanguage(m_settings->text("program.language")))
        m_log->log(tr("Cannot find translation, language change failed"));
}

// Retranslation refreshes what QML binds with qsTr; the strings the
// application and its controllers made with tr() and keep are made again
// here. Messages already shown (the log, a finished operation's result)
// stay as they were, as strings a script already got do.
void Application::languageSwitched()
{
    // Untitled tabs (legacy TabPanel's SubsName = _("Untitled")) take the
    // new name; a tab that has a file or another name keeps it.
    const QString untitled = tr("Untitled");
    if (untitled != m_untitledTitle) {
        for (const auto id : m_workspace.documents()) {
            const std::string *title = m_workspace.title(id);
            const auto destination = m_files->destination(id);
            if (title && QString::fromStdString(*title) == m_untitledTitle && (!destination || destination->value.empty()))
                m_workspace.setTitle(id, untitled.toStdString());
        }
        m_untitledTitle = untitled;
    }
    m_shell->retranslate();
    m_video->retranslate();
    m_audio->retranslate();
    emit tabsChanged();
    // The Options dialog's dictionary choice names its empty case.
    if (m_optionsLists.dictionarySymbols.empty() && !m_optionsLists.dictionaryNames.empty()) {
        m_optionsLists.dictionaryNames = {tr("Put files .dic and .aff to \"Dictionary\" folder").toStdString()};
        emit settingsListsChanged({QString::fromStdString(m_optionsLists.dictionaryNames.front())});
    }
}

namespace {

// Whether `o` takes part in Qt Quick Controls' font inheritance: controls,
// popups' items, labels, text fields and areas, an ApplicationWindow.
bool inheritsFonts(const QObject *o)
{
    for (const QMetaObject *m = o->metaObject(); m; m = m->superClass())
        for (const char *name : {"QQuickControl", "QQuickLabel", "QQuickTextField", "QQuickTextArea",
                                 "QQuickApplicationWindow"})
            if (qstrcmp(m->className(), name) == 0)
                return true;
    return false;
}

// Controls resolve their font when they are made (QGuiApplication::font())
// and afterwards follow only their parents, so the program font goes to each
// window's outermost ones (an ApplicationWindow itself), which hand it down.
// Their other font attributes (a bold title) stay.
void giveFont(QObject *o, const QFont &font)
{
    if (inheritsFonts(o)) {
        QFont own = o->property("font").value<QFont>();
        own.setFamilies(font.families());
        own.setPointSizeF(font.pointSizeF());
        o->setProperty("font", own);
        return;
    }
    if (auto *item = qobject_cast<QQuickItem *>(o))
        for (QQuickItem *child : item->childItems())
            giveFont(child, font);
}

} // namespace

// Options.GetFont() (config.cpp:946-971): PROGRAM_FONT at PROGRAM_FONT_SIZE
// points, 10 when the size is 0 and Tahoma when the face is empty; the
// platform substitutes a face it does not have. Every control follows, also
// in windows made before the change (legacy: the frame, and the dialogs
// DestroyDialogs makes again).
void Application::applyProgramFont()
{
    if (!m_localisation)
        return; // not started yet
    QFont font = m_startFont;
    const QString family = m_settings->text("program.font");
    const int size = m_settings->integer("program.fontSize");
    font.setFamilies({family.isEmpty() ? QStringLiteral("Tahoma") : family});
    font.setPointSize(size ? size : 10);
    if (font == QGuiApplication::font())
        return;
    QGuiApplication::setFont(font);
    for (QWindow *w : QGuiApplication::allWindows())
        if (auto *quick = qobject_cast<QQuickWindow *>(w))
            giveFont(inheritsFonts(quick) ? static_cast<QObject *>(quick) : quick->contentItem(), font);
}

} // namespace hikari::app
