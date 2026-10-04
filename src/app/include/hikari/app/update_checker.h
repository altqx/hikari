#pragma once

// P8: GLOBAL_CHECK_FOR_UPDATES and the automatic check at start (legacy
// UpdateChecker at 20d647c4), notification only: it reads the release list
// and offers the release page; it never downloads or installs anything.
// The options (legacy UPDATER_AUTO_CHECK, UPDATER_CHECK_FOR_STABLE and
// UPDATER_NEXT_CHECK) live in the settings registry (updater.*).

#include "settings_store.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantMap>

#include <memory>

namespace hikari::app {

class UpdateChecker : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool autoCheck READ autoCheck WRITE setAutoCheck NOTIFY optionsChanged)
    Q_PROPERTY(bool stableOnly READ stableOnly WRITE setStableOnly NOTIFY optionsChanged)
    Q_PROPERTY(bool checking READ checking NOTIFY checkingChanged)
    Q_PROPERTY(QString version READ version CONSTANT)
public:
    // The legacy release list: GitHub's ten newest releases.
    static QUrl defaultFeed();

    // Options in the INI file `settingsFile` (in memory when empty), or in
    // the application's store.
    UpdateChecker(QString settingsFile, QUrl feed, QString version, QObject *parent = nullptr);
    UpdateChecker(ui::SettingsStore &settings, QUrl feed, QString version, QObject *parent = nullptr);

    bool autoCheck() const { return m_autoCheck; }
    void setAutoCheck(bool on);
    bool stableOnly() const { return m_stableOnly; }
    void setStableOnly(bool on);
    bool checking() const { return m_reply != nullptr; }
    QString version() const { return m_version; }
    qint64 nextCheck() const { return m_nextCheck; }
    void setNextCheck(qint64 secondsSinceEpoch);

    // The menu entry: always checks and reports every outcome.
    Q_INVOKABLE void checkNow();
    // At start: only with the automatic check on and its time reached;
    // silent unless a newer release is found.
    Q_INVOKABLE bool checkOnStartup();
    // "Remind me in a week".
    Q_INVOKABLE void remindInAWeek();
    Q_INVOKABLE void openReleasePage(const QString &url);
    // Reads the options again (after "Set default").
    void reload();

signals:
    void optionsChanged();
    void checkingChanged();
    // "available" with {name, tag, url, notes}, "current" or "failed".
    // Without `interactive` only "available" is reported.
    void finished(const QString &outcome, const QVariantMap &release, bool interactive);

private:
    void check(bool interactive);
    void load();
    void save() const;

    std::unique_ptr<ui::SettingsStore> m_ownedSettings;
    ui::SettingsStore *m_settings;
    QUrl m_feed;
    QString m_version;
    bool m_autoCheck = false;
    bool m_stableOnly = true;
    qint64 m_nextCheck = 0;
    QNetworkAccessManager m_network;
    QNetworkReply *m_reply = nullptr;
};

} // namespace hikari::app
