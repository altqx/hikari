#pragma once

#include "hikari/app/discord_activity.h"
#include "hikari/app/discord_ipc.h"
#include "hikari/api/host.h"
#include "settings_store.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include <map>
#include <set>

namespace hikari::app {

// Shows what the user is doing in Discord's Rich Presence while privacy.discordPresence is on
// (docs/ux/discord-presence.md). Without Discord it retries quietly.
class DiscordPresence : public QObject, public api::PresenceHost {
    Q_OBJECT
public:
    struct Config {
        QString clientId = defaultClientId();
        // Empty: DiscordIpc::defaultCandidates().
        QStringList candidates;
        int retryMs = 20 * 1000;
        // Discord takes 5 activity updates per 20 seconds.
        int updateGapMs = 4 * 1000;
        // privacy.discordIdleMinutes counts in these.
        int minuteMs = 60 * 1000;
        qint64 pid = QCoreApplication::applicationPid();
    };
    struct Tab {
        bool open = false;
        quint64 id = 0; // changes when the current tab does
        QString fileName;
        QString title;
        QString projectName;
    };
    // HIKARI_DISCORD_CLIENT_ID, the HikariSub application by default.
    static QString defaultClientId();
    static QUrl siteUrl() { return QUrl(QStringLiteral("https://hikarisub.org")); }

    DiscordPresence(ui::SettingsStore &settings, Config config, QObject *parent = nullptr);
    ~DiscordPresence() override;

    bool enabled() const { return m_enabled; }
    bool connected() const { return m_ipc.isReady(); }
    // A workspace preset name: Editing, Timing, Translation, Typesetting or Karaoke.
    void setMode(const QString &preset);
    void setTab(const Tab &tab);
    void setProject(const discord::ProjectChoice &project);
    void setGoing(discord::Activity activity, bool going);
    // Counts `activity` as going for `ms` from now.
    void holdGoing(discord::Activity activity, int ms);
    void noteInput();
    void setInBackground(bool background);
    bool idle() const { return m_idle; }
    void setPluginText(std::string_view plugin, std::string_view text) override;
    // Drops the text of every plugin not in `running`.
    void keepPluginTexts(const QSet<QString> &running);
    // What goes to Discord now; empty while off.
    QJsonObject activity() const;
    // What the Settings window's staged `values` would send now, presence off or not.
    Q_INVOKABLE QVariantMap preview(const QVariantMap &values) const;

private:
    discord::Scene scene(const discord::Choices &choices) const;
    bool going(discord::Activity activity) const;
    void settingChanged(const QString &id);
    void restartIdle();
    void start();
    void stop();
    void ipcReady();
    void ipcClosed();
    void update();
    void send();

    ui::SettingsStore &m_settings;
    Config m_config;
    DiscordIpc m_ipc;
    QTimer m_retryTimer;
    QTimer m_updateTimer;
    QTimer m_idleTimer;
    QElapsedTimer m_sinceSent;
    QJsonObject m_sent;
    QString m_mode = QStringLiteral("Editing");
    Tab m_tab;
    discord::ProjectChoice m_project;
    std::set<discord::Activity> m_going;
    std::map<discord::Activity, QTimer> m_holds;
    QHash<QString, QString> m_pluginTexts;
    qint64 m_startedAt = 0;
    qint64 m_tabSince = 0;
    bool m_idle = false;
    bool m_background = false;
    bool m_enabled = false;
};

}
