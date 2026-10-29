#pragma once

#include "hikari/app/package_kind.h"
#include "hikari/application/update_check.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>

#include <functional>
#include <memory>
#include <optional>

class QNetworkReply;

namespace hikari::app {

struct UpdateEnvironment {
    PackageInfo package;
    // Empty: nothing is downloaded or installed.
    QString cacheDir;
    QString osVersion;    // Windows major.minor.build
    QString glibcVersion;
    QString executable;   // the running program (NSIS: its folder is the install dir)
    QString pkexec;       // empty: none, the manual fallback
    // yay or paru on PATH, for AUR installs only; empty: none found.
    QString aurHelper;
    QString helper = QStringLiteral("/opt/hikarisub/libexec/hikarisub-update-helper");
    QString relaunch = QStringLiteral("/usr/bin/hikarisub"); // deb, rpm
    int healthTimeoutMs = 60 * 1000;
    std::function<bool(const QUrl &)> allowRedirect;

    static UpdateEnvironment current();
    // %LOCALAPPDATA%\HikariSub\Updates, or ${XDG_CACHE_HOME:-~/.cache}/HikariSub/Updates.
    static QString defaultCacheDir();
};

class UpdateInstaller : public QObject {
    Q_OBJECT
    // "idle", "downloading", "ready" or "manual" (deb or rpm without pkexec).
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged) // 0 to 1
    Q_PROPERTY(QString readyVersion READ readyVersion NOTIFY stateChanged)
    Q_PROPERTY(QString readyFile READ readyFile NOTIFY stateChanged)
    Q_PROPERTY(QString manualCommand READ manualCommand NOTIFY stateChanged)
    Q_PROPERTY(bool restartRequested READ restartRequested NOTIFY restartRequestedChanged)
public:
    UpdateInstaller(UpdateEnvironment environment, QString runningVersion, QObject *parent = nullptr);
    ~UpdateInstaller() override;

    const UpdateEnvironment &environment() const { return m_env; }
    QString state() const { return m_state; }
    double progress() const { return m_progress; }
    QString readyVersion() const { return m_pending.version; }
    QString readyFile() const { return m_pending.file; }
    QString manualCommand() const;
    bool restartRequested() const { return m_restart; }

    // Drops what an installed update left, takes up a pending one, and returns the last failed
    // hand-off's message, once.
    QString start();
    // A verified copy already there is taken at once; a download of another version is
    // cancelled. Failures emit failed() only when `interactive`.
    void download(const QString &version, const application::UpdatePackage &package, const QUrl &url,
                  bool interactive);
    // An AppImage's folder must be writable.
    bool canInstall() const;

    // The next quit relaunches; a cancelled quit review calls cancelRestart().
    Q_INVOKABLE void restartNow();
    Q_INVOKABLE void cancelRestart();
    Q_INVOKABLE void showInFolder() const;
    Q_INVOKABLE void copyInstallCommand() const;

    // Called after the event loop; the caller execs `program` (deb and rpm relaunch).
    struct ExitAction {
        QString program; // empty: just exit
        QStringList arguments;
    };
    // `busy` shows the "Installing update..." notice and returns its hide function.
    ExitAction installAtExit(const std::function<std::function<void()>()> &busy = {});

    // `healthFile` must be in the update cache; also drops $APPIMAGE.old.
    void reportHealthy(const QString &healthFile);
    // Call before start(), which then leaves $APPIMAGE.old to reportHealthy().
    void expectHealthReport() { m_watched = true; }
    // True when another process took over (the new version, or the old one put back), so this
    // one only exits.
    bool installAppImageAtStart(const QStringList &arguments);

signals:
    void stateChanged();
    void progressChanged();
    void restartRequestedChanged();
    void ready();
    void failed(const QString &message);

private:
    struct Pending {
        QString version, kind, file, sha256;
        qint64 size = 0;
        bool manual = false;
    };
    void setState(const QString &state);
    void finishDownload();
    void failDownload(const QString &message);
    void abortDownload();
    bool verify(const QString &file, qint64 size, const QString &sha256) const;
    void makeReady(const Pending &pending);
    bool writePending(const Pending &pending) const;
    void dropPending();
    void writeResult(const QString &version, int code, const QString &message) const;
    QString versionDir(const QString &version) const;
    QString copyKey() const;
    QString pendingPath() const;
    QString resultPath() const;
    QString atStartPath() const;
    QString installingPath() const;
    std::optional<Pending> readPending() const;
    QString appImageStaging(const QString &version) const;
    bool belongsToAnotherCopy(const Pending &pending) const;
    bool replaceAppImage(const QStringList &arguments);
    ExitAction installNsis(const std::function<std::function<void()>()> &busy);
    ExitAction installAppImage();
    ExitAction startInstalled() const;
    ExitAction installNative(const std::function<std::function<void()>()> &busy);

    UpdateEnvironment m_env;
    QString m_running;
    QString m_state = QStringLiteral("idle");
    double m_progress = 0;
    bool m_restart = false;
    bool m_watched = false;
    Pending m_pending;

    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    std::unique_ptr<QFile> m_part;
    QCryptographicHash m_hash{QCryptographicHash::Sha256};
    Pending m_downloading;
    QString m_target;
    qint64 m_received = 0;
    bool m_interactive = false;
    QTimer m_connectTimer, m_deadline;
};

} // namespace hikari::app
