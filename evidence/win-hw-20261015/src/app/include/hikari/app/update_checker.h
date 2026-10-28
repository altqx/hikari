#pragma once

// P8: GLOBAL_CHECK_FOR_UPDATES and the automatic check at start (legacy
// UpdateChecker at 20d647c4); ADR 0019: the check reads the release list a
// page at a time, the candidates' update manifests, and offers what the
// install's package kind takes: a download that installs at the next quit
// (NSIS, AppImage, deb, rpm), the AUR notice, or the release page. The
// options (legacy UPDATER_AUTO_CHECK, UPDATER_CHECK_FOR_STABLE and
// UPDATER_NEXT_CHECK, and "Install updates automatically") live in the
// settings registry (updater.*).

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

// Where releases are listed and downloaded from.
struct UpdateFeed {
    QUrl releases;        // the list's first page
    QString downloadBase; // + "v<V>/<asset>"
    QString pageBase;     // + "v<V>": the release page
    bool paginate = false;
    // GitHub's release list, assets and pages.
    static UpdateFeed github();
    // A local feed (tests, HIKARISUB_UPDATE_FEED in a test build): <base>releases.json,
    // <base>download/v<V>/<asset>, <base>releases/tag/v<V>.
    static UpdateFeed local(const QUrl &base);
};

class UpdateChecker : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool autoCheck READ autoCheck WRITE setAutoCheck NOTIFY optionsChanged)
    Q_PROPERTY(bool stableOnly READ stableOnly WRITE setStableOnly NOTIFY optionsChanged)
    Q_PROPERTY(bool autoInstall READ autoInstall WRITE setAutoInstall NOTIFY optionsChanged)
    Q_PROPERTY(bool checking READ checking NOTIFY checkingChanged)
    Q_PROPERTY(QString version READ version CONSTANT)
    // "0.1.0 (AppImage)": About names the kind for bug reports.
    Q_PROPERTY(QString versionLabel READ versionLabel CONSTANT)
    // Whether this kind installs updates ("Install updates automatically" shows).
    Q_PROPERTY(bool installsUpdates READ installsUpdates CONSTANT)
    Q_PROPERTY(QObject *installer READ installerObject CONSTANT)
public:
    // The GitHub release list.
    static QUrl defaultFeed() { return UpdateFeed::github().releases; }

    // Options in the INI file `settingsFile` (in memory when empty), or in
    // the application's store. Without an environment: portable, no cache.
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
    bool checking() const { return m_run != nullptr; }
    QString version() const { return m_version; }
    QString versionLabel() const;
    bool installsUpdates() const;
    qint64 nextCheck() const { return m_nextCheck; }
    void setNextCheck(qint64 secondsSinceEpoch);
    UpdateInstaller &installer() { return *m_installer; }
    QObject *installerObject() { return m_installer.get(); }

    // The menu entry: always checks and reports every outcome.
    Q_INVOKABLE void checkNow();
    // At start: only with the automatic check on and its time reached;
    // silent unless a newer release is found. An installable one is
    // downloaded without asking when "Install updates automatically" is on.
    Q_INVOKABLE bool checkOnStartup();
    // At start, before the check: what an earlier update left (notice()).
    void startUp();
    // "Remind me in a week".
    Q_INVOKABLE void remindInAWeek();
    // Opens a release page this checker built.
    Q_INVOKABLE void openReleasePage(const QString &url);
    // The release page of `version` (no "v").
    Q_INVOKABLE QString releasePageOf(const QString &version) const { return pageUrl(QStringLiteral("v") + version); }
    // "Download and install" for the offer last reported.
    Q_INVOKABLE void downloadOffer();
    // Reads the options again (after "Set default").
    void reload();

signals:
    void optionsChanged();
    void checkingChanged();
    // "available" with {name, tag, version, url, notes, prerelease, offer,
    // requirement, requirementIsOs}, "current" or "failed". Without
    // `interactive` only "available" is reported. `offer` is "install",
    // "notify", "requirement" or "aur".
    void finished(const QString &outcome, const QVariantMap &release, bool interactive);
    // A message for the user: the last update's failure, or a download's.
    void notice(const QString &message);

private:
    struct Run;
    void check(bool interactive);
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
    QNetworkAccessManager m_network;
    std::unique_ptr<Run> m_run;
    std::unique_ptr<UpdateInstaller> m_installer;
    // The offer last reported, for downloadOffer().
    QString m_offerVersion;
    application::UpdatePackage m_offerPackage;
    bool m_offerInstallable = false;
};

} // namespace hikari::app
