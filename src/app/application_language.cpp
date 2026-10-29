#include "hikari/app/application.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>

namespace hikari::app {

void Application::startLocalisation()
{
    m_localisation = std::make_unique<Localisation>();
    m_workspaceLayout->reservePresetNames(m_localisation->translationsOf(
        "HikariSub.Workspace", ui::WorkspaceLayoutController::presets()));
    // "0" and "1" are old stored values for English.
    if (const QString stored = m_settings->text("program.language"); stored == u"0" || stored == u"1")
        m_settings->set("program.language", QString());
    // MUST KILL: set before switchLanguage() so it holds the English title.
    m_untitledTitle = QCoreApplication::translate("HikariSub.Documents", "Untitled");
    connect(m_localisation.get(), &Localisation::languageChanged, this, &Application::languageSwitched);
    m_localisation->setPseudo(Localisation::pseudoNamed(qEnvironmentVariable("HIKARI_PSEUDOLOCALE")));
    switchLanguage();
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
    if (!m_localisation->setLanguage(m_settings->text("program.language")))
        m_log->notice(ui::LogController::Warning, QCoreApplication::translate("HikariSub.Options", "Language"),
                      QCoreApplication::translate("HikariSub.Options", "Cannot find translation, language change failed"));
}

void Application::languageSwitched()
{
    const QString untitled = QCoreApplication::translate("HikariSub.Documents", "Untitled");
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
    m_statusBar->retranslate();
    m_workspaceLayout->retranslate();
    emit tabsChanged();
    if (m_optionsLists.dictionarySymbols.empty() && !m_optionsLists.dictionaryNames.empty()) {
        m_optionsLists.dictionaryNames = {
            QCoreApplication::translate("HikariSub.Options", "Put files .dic and .aff to \"Dictionary\" folder").toStdString()};
        emit settingsListsChanged({QString::fromStdString(m_optionsLists.dictionaryNames.front())});
    }
}

namespace {

bool inheritsFonts(const QObject *o)
{
    for (const QMetaObject *m = o->metaObject(); m; m = m->superClass())
        for (const char *name : {"QQuickControl", "QQuickLabel", "QQuickTextField", "QQuickTextArea",
                                 "QQuickApplicationWindow"})
            if (qstrcmp(m->className(), name) == 0)
                return true;
    return false;
}

// Qt Quick controls read QGuiApplication::font() only when made, then follow their parent.
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

void Application::applyProgramFont()
{
    if (!m_localisation)
        return;
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
