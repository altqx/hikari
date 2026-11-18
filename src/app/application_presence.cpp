#include "hikari/app/application.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QGuiApplication>

#include <utility>

namespace hikari::app {

namespace {

constexpr int kReviewingAfterNoteMs = 5 * 60 * 1000;

discord::ProjectChoice projectChoice(const WindowSettings &window)
{
    const application::Settings &settings = window.settings();
    const std::string choice = settings.text("privacy.discordProject");
    discord::ProjectChoice project;
    if (choice == "hide") {
        project.kind = discord::ProjectChoice::Hide;
    } else if (choice == "own" && window.trusted()) {
        const auto own = [&](const char *id) {
            return window.overridden(id) ? QString::fromStdString(settings.text(id)) : QString();
        };
        project.kind = discord::ProjectChoice::Own;
        project.show = own("privacy.discordShow");
        project.details = own("privacy.discordDetails");
        project.state = own("privacy.discordState");
    }
    return project;
}

}

void Application::wirePresence()
{
    connect(this, &Application::tabsChanged, this, &Application::refreshPresence);
    connect(m_workspaceLayout.get(), &ui::WorkspaceLayoutController::changed, this, &Application::refreshPresence);
    connect(m_fontCollector.get(), &FontCollectorController::stageChanged, this, &Application::refreshPresence);
    if (m_agent)
        connect(m_agent.get(), &AgentController::threadsChanged, this, &Application::refreshPresence);
    m_presenceNotes = m_qcNotes->total();
    connect(m_qcNotes.get(), &QcNotesController::changed, this, [this] {
        if (const int total = m_qcNotes->total(); std::exchange(m_presenceNotes, total) < total)
            m_presence->holdGoing(discord::Activity::Reviewing, kReviewingAfterNoteMs);
    });
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, m_presence.get(),
            [this](Qt::ApplicationState state) { m_presence->setInBackground(state != Qt::ApplicationActive); });
    connect(m_pluginRuntime.get(), &PluginRuntime::commandsChanged, m_presence.get(),
            [this] { m_presence->keepPluginTexts(m_pluginRuntime->runningIds()); });
}

void Application::watchPresence(WindowViews &v)
{
    connect(v.templateEditor.get(), &ui::TemplateEditorController::changed, this, &Application::refreshPresence);
    connect(v.kanjiTimer.get(), &KanjiTimerController::changed, this, &Application::refreshPresence);
}

void Application::presenceActivity(int shellWindow, const QString &key, bool going)
{
    if (going)
        m_presenceGoing[shellWindow].insert(key);
    else
        m_presenceGoing[shellWindow].erase(key);
    refreshPresence();
}

QVariantList Application::presenceModes() const
{
    QVariantList modes;
    const std::pair<const char *, QString> layouts[] = {
        {"Editing", QCoreApplication::translate("HikariSub.Discord", "In the Editing layout, or when no other mode applies")},
        {"Timing", QCoreApplication::translate("HikariSub.Discord", "In the Timing layout, and in kanji timing")},
        {"Translation", QCoreApplication::translate("HikariSub.Discord", "In the Translation layout")},
        {"Typesetting", QCoreApplication::translate("HikariSub.Discord", "In the Typesetting layout, and in full-screen typesetting")},
        {"Karaoke", QCoreApplication::translate("HikariSub.Discord", "In the Karaoke layout")},
    };
    for (const auto &[preset, when] : layouts)
        modes << QVariantMap{{QStringLiteral("key"), QString::fromLatin1(preset).toLower()},
                             {QStringLiteral("name"), ui::WorkspaceLayoutController::presetLabel(QLatin1String(preset))},
                             {QStringLiteral("when"), when},
                             {QStringLiteral("layout"), true}};
    const std::pair<discord::Activity, QString> activities[] = {
        {discord::Activity::Watching, QCoreApplication::translate("HikariSub.Discord", "While Simple mode is open")},
        {discord::Activity::Effects, QCoreApplication::translate("HikariSub.Discord", "While the node-effects mode or the Template editor is open")},
        {discord::Activity::Styling, QCoreApplication::translate("HikariSub.Discord", "While the Style manager is open")},
        {discord::Activity::Spelling, QCoreApplication::translate("HikariSub.Discord", "While the Spellchecker panel is checking")},
        {discord::Activity::Reviewing, QCoreApplication::translate("HikariSub.Discord", "While QC notes has focus, or for 5 minutes after a QC note is added")},
        {discord::Activity::Exporting, QCoreApplication::translate("HikariSub.Discord", "While an export, Collect fonts or a muxing run is going")},
        {discord::Activity::Agent, QCoreApplication::translate("HikariSub.Discord", "While an agent turn runs in the current tab")},
    };
    for (const auto &[activity, when] : activities)
        modes << QVariantMap{{QStringLiteral("key"), discord::activityKey(activity)},
                             {QStringLiteral("name"), discord::activityLabel(activity)},
                             {QStringLiteral("when"), when},
                             {QStringLiteral("layout"), false}};
    return modes;
}

QVariantList Application::presencePlugins() const
{
    return m_pluginRuntime->presencePlugins();
}

void Application::refreshPresence()
{
    const int window = m_workspace.activeWindow();
    const WindowViews &v = activeViews();
    ui::WorkspaceLayoutController &layout = layoutOf(window);
    const std::set<QString> &reported = m_presenceGoing[window];
    const bool kanji = v.kanjiTimer && v.kanjiTimer->isOpen();
    const bool fullScreenTypesetting = reported.contains(QStringLiteral("typesetting"));
    m_presence->setMode(kanji                    ? QStringLiteral("Timing")
                        : fullScreenTypesetting ? QStringLiteral("Typesetting")
                                                : layout.preset());

    DiscordPresence::Tab tab;
    if (const auto target = m_workspace.editingTarget()) {
        tab.open = true;
        tab.id = target->value;
        if (const auto destination = m_files->destination(*target))
            tab.fileName = QFileInfo(QString::fromStdString(destination->value)).fileName();
        if (const application::EditSession *session = targetSession())
            if (const auto title = session->document().scriptInfo(u8"Title"))
                tab.title = QString::fromUtf8(reinterpret_cast<const char *>(title->data()), qsizetype(title->size()));
    }
    if (v.settings) {
        if (!v.settings->root().isEmpty())
            tab.projectName = QFileInfo(v.settings->root()).fileName();
        m_presence->setProject(projectChoice(*v.settings));
    }
    m_presence->setTab(tab);

    const auto qml = [&](discord::Activity a) { return reported.contains(discord::activityKey(a)); };
    using enum discord::Activity;
    m_presence->setGoing(Watching, qml(Watching));
    m_presence->setGoing(Effects, layout.nodeEffects() || (v.templateEditor && v.templateEditor->shown()));
    m_presence->setGoing(Styling, qml(Styling));
    m_presence->setGoing(Spelling, qml(Spelling));
    m_presence->setGoing(Reviewing, qml(Reviewing));
    m_presence->setGoing(Exporting,
                         qml(Exporting) || m_fontCollector->stage() == FontCollectorController::Working);
    m_presence->setGoing(Agent, m_agent && m_agent->running());
}

}
