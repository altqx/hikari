#pragma once

#include "hikari/app/discord_ipc.h"
#include "settings_store.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>

namespace hikari::app {

// Shows the mode, and with its own opt-in the file name, in Discord's Rich Presence while
// privacy.discordPresence is on. Without Discord it retries quietly.
class DiscordPresence : public QObject {
    Q_OBJECT
public:
    struct Config {
        QString clientId = defaultClientId();
        // Empty: DiscordIpc::defaultCandidates().
        QStringList candidates;
        int retryMs = 20 * 1000;
        // Discord takes 5 activity updates per 20 seconds.
        int updateGapMs = 4 * 1000;
        qint64 pid = QCoreApplication::applicationPid();
    };
    // HIKARI_DISCORD_CLIENT_ID, the HikariSub application by default.
    static QString defaultClientId();
    static QUrl siteUrl() { return QUrl(QStringLiteral("https://hikarisub.altqx.com")); }

    DiscordPresence(ui::SettingsStore &settings, Config config, QObject *parent = nullptr);
    ~DiscordPresence() override;

    bool enabled() const { return m_enabled; }
    bool connected() const { return m_ipc.isReady(); }
    // A workspace preset name: Editing, Timing, Translation, Typesetting or Karaoke.
    void setMode(const QString &preset);
    // Empty without a document.
    void setFileName(const QString &name);
    // What goes to Discord now; empty while off.
    QJsonObject activity() const;

private:
    void settingChanged(const QString &id);
    void start();
    void stop();
    void ipcReady();
    void ipcClosed();
    void update();

    ui::SettingsStore &m_settings;
    Config m_config;
    DiscordIpc m_ipc;
    QTimer m_retryTimer;
    QTimer m_updateTimer;
    QElapsedTimer m_sinceSent;
    QJsonObject m_sent;
    QString m_mode = QStringLiteral("Editing");
    QString m_fileName;
    qint64 m_startedAt = 0;
    bool m_enabled = false;
};

}
