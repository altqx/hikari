#pragma once

// The seamless update (ADR 0019): the chosen package is downloaded in the
// background into the update cache, its size and SHA-256 checked as it
// arrives, and recorded in pending.json; the window then offers "Restart
// now" and "Later". The hand-off happens after the normal quit (every
// document reviewed and the main window closed), per package kind:
//   - NSIS: the installed program with --hikari-apply-update (update_apply.h)
//     checks the installer against the release's SHA256SUMS, from a private
//     copy for all users, and starts it in its update mode; all-users scope
//     asks UAC once, for the installed program;
//   - AppImage: the new image replaces $APPIMAGE (the old one kept as
//     $APPIMAGE.old) and a watchdog waits for the new process to report its
//     window, putting the old image back if it does not. With Restart now
//     that happens at the quit; after an ordinary quit it waits for the next
//     start, which then starts the new image under the watchdog, so a
//     version that cannot start is always put back;
//   - deb and rpm: pkexec runs the packaged helper with the file and the
//     version only; it checks the file against the release's SHA256SUMS,
//     which it downloads itself, and installs with apt, dnf or zypper; without pkexec the file is offered with the command that
//     installs it.
// The AUR and portable copies never get here: they are told only.

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

// What the updater needs to know of this process and system; tests replace
// any of it.
struct UpdateEnvironment {
    PackageInfo package;
    // <cache>/HikariSub/Updates; empty: nothing is downloaded or installed.
    QString cacheDir;
    QString osVersion;    // Windows major.minor.build
    QString glibcVersion; // glibc's version
    QString executable;   // the running program (NSIS: its folder is the install dir)
    QString pkexec;       // empty: none, the manual fallback
    QString helper = QStringLiteral("/opt/hikarisub/libexec/hikarisub-update-helper");
    QString relaunch = QStringLiteral("/usr/bin/hikarisub"); // deb, rpm
    int healthTimeoutMs = 60 * 1000;
    // Whether a download may follow a redirect to this URL.
    std::function<bool(const QUrl &)> allowRedirect;

    // This process: currentPackageKind(), the cache folder, pkexec on PATH.
    static UpdateEnvironment current();
    // %LOCALAPPDATA%\HikariSub\Updates, or ${XDG_CACHE_HOME:-~/.cache}/HikariSub/Updates.
    static QString defaultCacheDir();
};

class UpdateInstaller : public QObject {
    Q_OBJECT
    // "idle", "downloading", "ready" (Restart now / Later) or "manual" (deb
    // or rpm without pkexec: the file and its install command).
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

    // At start: drops what an installed update left (a record or folder of
    // a version not newer than this one), takes up a pending update, and
    // returns the message of the last failed hand-off, once.
    QString start();
    // Downloads `package` of `version` from `url`; a verified copy already
    // there is taken at once. A download of another version is cancelled.
    // `interactive`: a failure is reported (failed()); otherwise silent.
    void download(const QString &version, const application::UpdatePackage &package, const QUrl &url,
                  bool interactive);
    // Whether the kind can take a download at all (the AppImage's folder
    // must be writable).
    bool canInstall() const;

    // Restart now: the quit that follows relaunches. A cancelled quit
    // review takes it back.
    Q_INVOKABLE void restartNow();
    Q_INVOKABLE void cancelRestart();
    // The manual fallback's buttons.
    Q_INVOKABLE void showInFolder() const;
    Q_INVOKABLE void copyInstallCommand() const;

    // After the event loop: hands a ready update to its installer. What is
    // left for the caller is a program to exec (deb and rpm relaunch).
    struct ExitAction {
        QString program; // empty: just exit
        QStringList arguments;
    };
    // `busy` shows the "Installing update..." notice and returns a function
    // that hides it (deb, rpm and NSIS).
    ExitAction installAtExit(const std::function<std::function<void()>()> &busy = {});

    // The new process of an AppImage update: reports its window to the
    // watchdog (a file in the update cache only) and drops $APPIMAGE.old.
    void reportHealthy(const QString &healthFile);
    // This start is the watched first start of an AppImage update: start()
    // leaves $APPIMAGE.old to reportHealthy, since the watchdog puts it
    // back if no report comes.
    void expectHealthReport() { m_watched = true; }
    // The start after an ordinary quit left an AppImage update to install:
    // replaces the image and starts the new one, with `arguments`, under the
    // watchdog. True when another process took over (the new version, or
    // the old one put back and started again), so this one only exits.
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

    // The download in flight.
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
