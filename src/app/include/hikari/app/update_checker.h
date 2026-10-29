#pragma once

#include "hikari/app/update_installer.h"
#include "settings_store.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include <memory>
#include <vector>

namespace hikari::app {

struct UpdateFeed {
    QUrl releases;        // the list's first page
    QString downloadBase; // + "v<V>/<asset>"
    QString pageBase;     // + "v<V>": the release page
    bool paginate = false;
    static UpdateFeed github();
    // <base>releases.json, <base>download/v<V>/<asset>, <base>releases/tag/v<V>.
    static UpdateFeed local(const QUrl &base);
};

class UpdateChecker : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool autoCheck READ autoCheck WRITE setAutoCheck NOTIFY optionsChanged)
    Q_PROPERTY(bool stableOnly READ stableOnly WRITE setStableOnly NOTIFY optionsChanged)
    Q_PROPERTY(bool autoInstall READ autoInstall WRITE setAutoInstall NOTIFY optionsChanged)
    Q_PROPERTY(bool checking READ checking NOTIFY checkingChanged)
    Q_PROPERTY(QString version READ version CONSTANT)
    // e.g. "0.1.0 (AppImage)".
    Q_PROPERTY(QString versionLabel READ versionLabel CONSTANT)
    Q_PROPERTY(bool installsUpdates READ installsUpdates CONSTANT)
    // False for an AUR install with a channel (aur-channel.txt), whose package decides.
    Q_PROPERTY(bool stableOnlyOption READ stableOnlyOption CONSTANT)
    Q_PROPERTY(QObject *installer READ installerObject CONSTANT)
    // Without "v"; empty for none.
    Q_PROPERTY(QString skippedVersion READ skippedVersion WRITE skipVersion NOTIFY optionsChanged)
    Q_PROPERTY(QString aurCommand READ aurCommand CONSTANT)
    Q_PROPERTY(bool aur READ aur CONSTANT)
public:
    static QUrl defaultFeed() { return UpdateFeed::github().releases; }

    // Empty `settingsFile`: options in memory. A default environment: portable, no cache.
    UpdateChecker(QString settingsFile, UpdateFeed feed, QString version, UpdateEnvironment environment = {},
                  QObject *parent = nullptr);
    UpdateChecker(ui::SettingsStore &settings, UpdateFeed feed, QString version, UpdateEnvironment environment = {},
                  QObject *parent = nullptr);
    ~UpdateChecker() override;

    bool autoCheck() const { return m_autoCheck; }
    void setAutoCheck(bool on);
    bool stableOnly() const { return m_stableOnly; }
    void setStableOnly(bool on);
    bool autoInstall() const { return m_autoInstall; }
    void setAutoInstall(bool on);
    bool checking() const;
    QString version() const { return m_version; }
    QString versionLabel() const;
    bool installsUpdates() const;
    bool stableOnlyOption() const;
    QString skippedVersion() const { return m_skippedVersion; }
    // The automatic check stays quiet about `version` until a newer release is found; "" clears it.
    Q_INVOKABLE void skipVersion(const QString &version);
    QString aurCommand() const;
    bool aur() const;
    qint64 nextCheck() const { return m_nextCheck; }
    void setNextCheck(qint64 secondsSinceEpoch);
    UpdateInstaller &installer() { return *m_installer; }
    QObject *installerObject() { return m_installer.get(); }

    // Always checks and reports every outcome.
    Q_INVOKABLE void checkNow();
    // Only when the automatic check is on and due; silent unless a newer release is found, which
    // downloads without asking when autoInstall is on.
    Q_INVOKABLE bool checkOnStartup();
    // Call before the check: reports what an earlier update left through notice().
    void startUp();
    Q_INVOKABLE void remindInAWeek();
    // Ignores any URL outside the feed's pageBase.
    Q_INVOKABLE void openReleasePage(const QString &url);
    // `version` without "v".
    Q_INVOKABLE QString releasePageOf(const QString &version) const { return pageUrl(QStringLiteral("v") + version); }
    // Downloads the offer last reported.
    Q_INVOKABLE void downloadOffer();
    void reload();
    // Reports only releasesRead and leaves the next check's time; a running check's list serves it.
    void readReleases();
    const std::vector<application::ReleaseEntry> &releases() const { return m_releases; }
    // stableOnly as a check applies it, with an AUR install's channel.
    bool stableOnlyApplied() const;
    const UpdateFeed &feed() const { return m_feed; }

signals:
    void optionsChanged();
    void checkingChanged();
    // "available" with {name, tag, version, url, notes, prerelease, aurCarries, offer,
    // requirement, requirementIsOs}, "current" or "failed". Without
    // `interactive` only "available" is reported. `offer` is "install",
    // "notify", "requirement" or "aur".
    void finished(const QString &outcome, const QVariantMap &release, bool interactive);
    // `atStart`: the last update's failure, which the installer left; else a download's failure.
    void notice(const QString &message, bool atStart);
    void releasesRead(bool ok);

private:
    struct Run;
    void check(bool interactive);
    void start(bool listOnly, bool interactive);
    void requestPage(int page);
    void pageFinished();
    void walk();
    void manifestFinished();
    void offer(const application::ReleaseEntry &release, const application::ReleasePackages &packages);
    void end(const QString &outcome);
    QNetworkReply *get(const QUrl &url, bool api);
    QString assetUrl(const QString &tag, const QString &asset) const;
    QString pageUrl(const QString &tag) const;
    void load();
    void save() const;

    std::unique_ptr<ui::SettingsStore> m_ownedSettings;
    ui::SettingsStore *m_settings;
    UpdateFeed m_feed;
    QString m_version;
    bool m_autoCheck = false;
    bool m_stableOnly = true;
    bool m_autoInstall = true;
    qint64 m_nextCheck = 0;
    QString m_skippedVersion;
    QNetworkAccessManager m_network;
    std::unique_ptr<Run> m_run;
    std::unique_ptr<UpdateInstaller> m_installer;
    QString m_offerVersion;
    application::UpdatePackage m_offerPackage;
    bool m_offerInstallable = false;
    std::vector<application::ReleaseEntry> m_releases;
};

} // namespace hikari::app
