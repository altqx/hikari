#include "hikari/app/discord_presence.h"

#include "workspace_layout.h"

#include <QDateTime>
#include <QJsonArray>

#include <utility>

namespace hikari::app {

namespace {

constexpr char kEnabled[] = "privacy.discordPresence";
constexpr char kFileName[] = "privacy.discordFileName";
constexpr qsizetype kMaxText = 128;
constexpr int kFlushOnQuitMs = 250;

QString fit(QString text)
{
    if (text.size() > kMaxText)
        text = text.left(kMaxText - 1) + QChar(0x2026);
    return text;
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
    connect(&m_updateTimer, &QTimer::timeout, this, &DiscordPresence::update);
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

void DiscordPresence::setFileName(const QString &name)
{
    m_fileName = name;
    update();
}

QJsonObject DiscordPresence::activity() const
{
    if (!m_enabled)
        return {};
    const QString mode = ui::WorkspaceLayoutController::presetLabel(m_mode);
    const bool withFile = m_settings.boolean(kFileName) && !m_fileName.isEmpty();
    const QString state = withFile ? QStringLiteral("%1 · %2").arg(mode, m_fileName) : mode;
    return {
        {QStringLiteral("details"), QCoreApplication::translate("HikariSub.Discord", "Editing subtitles")},
        {QStringLiteral("state"), fit(state)},
        {QStringLiteral("timestamps"), QJsonObject{{QStringLiteral("start"), m_startedAt}}},
        {QStringLiteral("assets"),
         QJsonObject{{QStringLiteral("large_image"), QStringLiteral("hikari")},
                     {QStringLiteral("large_text"), QStringLiteral("HikariSub")},
                     {QStringLiteral("small_image"), m_mode.toLower()},
                     {QStringLiteral("small_text"), mode}}},
        {QStringLiteral("buttons"),
         QJsonArray{QJsonObject{{QStringLiteral("label"), QCoreApplication::translate("HikariSub.Discord", "Get HikariSub")},
                                {QStringLiteral("url"), siteUrl().toString()}}}},
    };
}

void DiscordPresence::settingChanged(const QString &id)
{
    if (id == QLatin1String(kFileName)) {
        update();
        return;
    }
    if (id != QLatin1String(kEnabled))
        return;
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
    m_startedAt = QDateTime::currentSecsSinceEpoch();
    m_ipc.open();
}

void DiscordPresence::stop()
{
    m_enabled = false;
    m_retryTimer.stop();
    m_updateTimer.stop();
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
    if (!m_enabled || !m_ipc.isReady() || activity() == m_sent)
        return;
    if (m_sinceSent.isValid() && m_sinceSent.elapsed() < m_config.updateGapMs) {
        if (!m_updateTimer.isActive())
            m_updateTimer.start(int(m_config.updateGapMs - m_sinceSent.elapsed()));
        return;
    }
    m_sent = activity();
    m_ipc.setActivity(m_sent, m_config.pid);
    m_sinceSent.start();
}

}
