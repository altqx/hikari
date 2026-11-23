#include "hikari/app/discord_presence.h"

#include "workspace_layout.h"

#include <QDateTime>

#include <algorithm>
#include <utility>

namespace hikari::app {

namespace {

constexpr char kEnabled[] = "privacy.discordPresence";
constexpr char kIdleMinutes[] = "privacy.discordIdleMinutes";
constexpr char kIdleBackground[] = "privacy.discordIdleBackground";
constexpr int kFlushOnQuitMs = 250;

qint64 now()
{
    return QDateTime::currentSecsSinceEpoch();
}

}

QString DiscordPresence::defaultClientId()
{
    return QStringLiteral(HIKARI_DISCORD_CLIENT_ID);
}

DiscordPresence::DiscordPresence(ui::SettingsStore &settings, Config config, QObject *parent)
    : QObject(parent), m_settings(settings), m_config(std::move(config)),
      m_ipc(m_config.clientId, m_config.candidates.isEmpty() ? DiscordIpc::defaultCandidates() : m_config.candidates)
{
    m_retryTimer.setSingleShot(true);
    m_retryTimer.setInterval(m_config.retryMs);
    connect(&m_retryTimer, &QTimer::timeout, &m_ipc, &DiscordIpc::open);
    m_updateTimer.setSingleShot(true);
    connect(&m_updateTimer, &QTimer::timeout, this, &DiscordPresence::send);
    m_idleTimer.setSingleShot(true);
    connect(&m_idleTimer, &QTimer::timeout, this, [this] {
        m_idle = true;
        update();
    });
    connect(&m_ipc, &DiscordIpc::ready, this, &DiscordPresence::ipcReady);
    connect(&m_ipc, &DiscordIpc::closed, this, &DiscordPresence::ipcClosed);
    connect(&m_settings, &ui::SettingsStore::changed, this, &DiscordPresence::settingChanged);
    if (m_settings.boolean(kEnabled) && !m_config.clientId.isEmpty())
        start();
}

DiscordPresence::~DiscordPresence()
{
    if (m_ipc.setActivity({}, m_config.pid))
        m_ipc.flush(kFlushOnQuitMs);
    m_ipc.close();
}

void DiscordPresence::setMode(const QString &preset)
{
    m_mode = ui::WorkspaceLayoutController::presets().contains(preset) ? preset : QStringLiteral("Editing");
    update();
}

void DiscordPresence::setTab(const Tab &tab)
{
    if (tab.open != m_tab.open || tab.id != m_tab.id)
        m_tabSince = now();
    m_tab = tab;
    update();
}

void DiscordPresence::setProject(const discord::ProjectChoice &project)
{
    m_project = project;
    update();
}

void DiscordPresence::setGoing(discord::Activity activity, bool going)
{
    if (going)
        m_going.insert(activity);
    else
        m_going.erase(activity);
    update();
}

void DiscordPresence::holdGoing(discord::Activity activity, int ms)
{
    QTimer &hold = m_holds[activity];
    if (!hold.isSingleShot()) {
        hold.setSingleShot(true);
        connect(&hold, &QTimer::timeout, this, &DiscordPresence::update);
    }
    hold.start(ms);
    update();
}

bool DiscordPresence::going(discord::Activity activity) const
{
    const auto hold = m_holds.find(activity);
    return m_going.contains(activity) || (hold != m_holds.end() && hold->second.isActive());
}

void DiscordPresence::noteInput()
{
    if (std::exchange(m_idle, false)) {
        m_startedAt = m_tabSince = now();
        update();
    }
    restartIdle();
}

void DiscordPresence::setInBackground(bool background)
{
    m_background = background;
    restartIdle();
}

void DiscordPresence::restartIdle()
{
    if (!m_enabled || (m_background && !m_settings.boolean(kIdleBackground))) {
        m_idleTimer.stop();
        return;
    }
    const qint64 minutes = std::clamp<qint64>(m_settings.value(QLatin1String(kIdleMinutes)).toLongLong(), 1, 120);
    m_idleTimer.start(int(minutes * m_config.minuteMs));
}

void DiscordPresence::setPluginText(std::string_view plugin, std::string_view text)
{
    const QString id = QString::fromUtf8(plugin.data(), qsizetype(plugin.size()));
    if (text.empty())
        m_pluginTexts.remove(id);
    else
        m_pluginTexts.insert(id, QString::fromUtf8(text.data(), qsizetype(text.size())));
    update();
}

void DiscordPresence::keepPluginTexts(const QSet<QString> &running)
{
    m_pluginTexts.removeIf([&running](const auto &entry) { return !running.contains(entry.key()); });
    update();
}

discord::Scene DiscordPresence::scene(const discord::Choices &choices) const
{
    discord::Scene s;
    s.tabOpen = m_tab.open;
    s.layoutMode = m_mode;
    s.activity = discord::shownActivity(choices, [this](discord::Activity a) { return going(a); });
    s.fileName = m_tab.fileName;
    s.title = m_tab.title;
    s.projectName = m_tab.projectName;
    s.project = m_project;
    s.pluginText = m_pluginTexts.value(choices.plugin);
    s.idle = m_idle;
    if (choices.time == QLatin1String("file")) {
        if (m_tab.open)
            s.since = m_tabSince;
    } else if (choices.time != QLatin1String("hidden")) {
        s.since = m_startedAt ? m_startedAt : now();
    }
    return s;
}

QJsonObject DiscordPresence::activity() const
{
    if (!m_enabled)
        return {};
    const discord::Choices choices = discord::Choices::fromStore(m_settings);
    return discord::compose(choices, scene(choices));
}

QVariantMap DiscordPresence::preview(const QVariantMap &values) const
{
    const discord::Choices choices = discord::Choices::read(
        [&](const QString &id) { return values.contains(id) ? values.value(id) : m_settings.value(id); });
    discord::Scene s = scene(choices);
    s.idle = false;
    return discord::compose(choices, s).toVariantMap();
}

void DiscordPresence::settingChanged(const QString &id)
{
    if (!id.startsWith(QLatin1String("privacy.discord")))
        return;
    if (id == QLatin1String(kIdleMinutes) || id == QLatin1String(kIdleBackground))
        restartIdle();
    if (id != QLatin1String(kEnabled)) {
        update();
        return;
    }
    const bool enable = m_settings.boolean(kEnabled) && !m_config.clientId.isEmpty();
    if (enable == m_enabled)
        return;
    if (enable)
        start();
    else
        stop();
}

void DiscordPresence::start()
{
    m_enabled = true;
    m_idle = false;
    m_startedAt = m_tabSince = now();
    restartIdle();
    m_ipc.open();
}

void DiscordPresence::stop()
{
    m_enabled = false;
    m_retryTimer.stop();
    m_updateTimer.stop();
    m_idleTimer.stop();
    m_ipc.setActivity({}, m_config.pid);
    m_ipc.close();
    m_sent = {};
}

void DiscordPresence::ipcReady()
{
    m_sent = {};
    update();
}

void DiscordPresence::ipcClosed()
{
    m_updateTimer.stop();
    m_sent = {};
    if (m_enabled)
        m_retryTimer.start();
}

void DiscordPresence::update()
{
    if (!m_enabled || !m_ipc.isReady() || m_updateTimer.isActive())
        return;
    const qint64 wait = m_sinceSent.isValid() ? m_config.updateGapMs - m_sinceSent.elapsed() : 0;
    m_updateTimer.start(int(std::max<qint64>(0, wait)));
}

void DiscordPresence::send()
{
    if (!m_enabled || !m_ipc.isReady() || activity() == m_sent)
        return;
    m_sent = activity();
    m_ipc.setActivity(m_sent, m_config.pid);
    m_sinceSent.start();
}

}
