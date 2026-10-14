#include "hikari/app/update_installer.h"

#include "hikari/app/update_apply.h"
#include "hikari/backends/desktop_handoff.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QOperatingSystemVersion>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>

#include <climits>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#else
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <unistd.h>
#if defined(__GLIBC__)
#include <gnu/libc-version.h>
#endif
#endif

namespace hikari::app {

using application::PackageKind;

namespace {

std::string std8(const QString &s)
{
    return s.toStdString();
}

QString hexOf(const QByteArray &digest)
{
    return QString::fromLatin1(digest.toHex());
}

bool isNative(PackageKind kind)
{
    return kind == PackageKind::Deb || kind == PackageKind::Rpm;
}

#ifndef _WIN32
bool alive(qint64 pid)
{
    if (pid <= 0 || (::kill(pid_t(pid), 0) != 0 && errno != EPERM))
        return false;
    // A detached child that has exited stays a zombie until its new parent
    // reaps it, and a container's init may never do that: a zombie is gone.
    QFile stat(QStringLiteral("/proc/%1/stat").arg(pid));
    if (!stat.open(QIODevice::ReadOnly))
        return true;
    const QByteArray line = stat.read(1024);
    const qsizetype close = line.lastIndexOf(')');
    return close < 0 || close + 2 >= line.size() || line.at(close + 2) != 'Z';
}

// rename(2): atomic, and it replaces the target, which QFile::rename does not.
bool replaceFile(const QString &from, const QString &to)
{
    return ::rename(QFile::encodeName(from).constData(), QFile::encodeName(to).constData()) == 0;
}
#endif

} // namespace

UpdateEnvironment UpdateEnvironment::current()
{
    UpdateEnvironment env;
    env.package = currentPackageKind();
    env.cacheDir = defaultCacheDir();
    env.executable = QCoreApplication::applicationFilePath();
#ifdef _WIN32
    const auto os = QOperatingSystemVersion::current();
    env.osVersion = QStringLiteral("%1.%2.%3").arg(os.majorVersion()).arg(os.minorVersion()).arg(os.microVersion());
#else
#if defined(__GLIBC__)
    env.glibcVersion = QString::fromLatin1(gnu_get_libc_version());
#endif
    env.pkexec = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
#endif
    env.allowRedirect = [](const QUrl &url) {
        return url.scheme() == QLatin1String("https") && url.port() == -1 && url.userInfo().isEmpty() &&
               application::isAllowedDownloadHost(url.host().toStdString());
    };
    return env;
}

QString UpdateEnvironment::defaultCacheDir()
{
#ifdef _WIN32
    const QString base = qEnvironmentVariable("LOCALAPPDATA");
    if (base.isEmpty())
        return {};
    return QDir::fromNativeSeparators(base) + QStringLiteral("/HikariSub/Updates");
#else
    QString base = qEnvironmentVariable("XDG_CACHE_HOME");
    if (base.isEmpty() || QDir::isRelativePath(base))
        base = QDir::homePath() + QStringLiteral("/.cache");
    return base + QStringLiteral("/HikariSub/Updates");
#endif
}

UpdateInstaller::UpdateInstaller(UpdateEnvironment environment, QString runningVersion, QObject *parent)
    : QObject(parent), m_env(std::move(environment)), m_running(std::move(runningVersion))
{
    m_connectTimer.setSingleShot(true);
    m_deadline.setSingleShot(true);
    connect(&m_connectTimer, &QTimer::timeout, this, [this] {
        if (m_reply)
            m_reply->abort();
    });
    connect(&m_deadline, &QTimer::timeout, this, [this] {
        if (m_reply)
            m_reply->abort();
    });
}

UpdateInstaller::~UpdateInstaller()
{
    abortDownload();
}

// This copy's download folder for a version: Updates/<copy>/<V>, so that
// each copy clears only its own.
QString UpdateInstaller::versionDir(const QString &version) const
{
    return m_env.cacheDir + QLatin1Char('/') + copyKey() + QLatin1Char('/') + version;
}

// This copy's name in the shared cache (updateCopyKey): copies of other
// kinds, and AppImages at other paths, keep records of their own.
QString UpdateInstaller::copyKey() const
{
    return QString::fromStdString(
        application::updateCopyKey(std8(packageKindToken(m_env.package.kind)), std8(m_env.package.appImage)));
}

QString UpdateInstaller::pendingPath() const
{
    return m_env.cacheDir + QStringLiteral("/pending-%1.json").arg(copyKey());
}

QString UpdateInstaller::resultPath() const
{
    return m_env.cacheDir + QStringLiteral("/last-result-%1.json").arg(copyKey());
}

// The version of the NSIS update handed to its installer at the last quit.
QString UpdateInstaller::installingPath() const
{
    return m_env.cacheDir + QStringLiteral("/installing-%1").arg(copyKey());
}

// An ordinary quit's AppImage update, for the next start: its version.
QString UpdateInstaller::atStartPath() const
{
    return m_env.cacheDir + QStringLiteral("/at-start-%1").arg(copyKey());
}

std::optional<UpdateInstaller::Pending> UpdateInstaller::readPending() const
{
    QFile record(pendingPath());
    if (!record.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QJsonObject p = QJsonDocument::fromJson(record.read(application::kMaxManifestBytes)).object();
    if (p.value(QStringLiteral("schema")).toInt() != 1)
        return std::nullopt;
    return Pending{p.value(QStringLiteral("version")).toString(), p.value(QStringLiteral("kind")).toString(),
                   p.value(QStringLiteral("file")).toString(), p.value(QStringLiteral("sha256")).toString(),
                   qint64(p.value(QStringLiteral("size")).toDouble()), p.value(QStringLiteral("manual")).toBool()};
}

// An AppImage's download, beside the image it replaces (appImageStagingPath:
// hidden, named for the image and the version).
QString UpdateInstaller::appImageStaging(const QString &version) const
{
    return QString::fromStdString(application::appImageStagingPath(std8(m_env.package.appImage), std8(version)));
}

// The cache is the user's, shared by every copy they keep, and each copy
// has a record of its own (copyKey); as a second guard, a record of another
// kind is the other copy's (an AppImage beside an installed package, say),
// and one staged for another AppImage, in another folder or under another
// name, is that copy's to install. Either is left alone. A record whose
// kind cannot be read is this copy's to drop.
bool UpdateInstaller::belongsToAnotherCopy(const Pending &pending) const
{
    const auto kind = application::parsePackageKind(std8(pending.kind));
    if (!kind)
        return false;
    if (pending.kind != packageKindToken(m_env.package.kind))
        return true;
    return *kind == PackageKind::AppImage && pending.file != appImageStaging(pending.version);
}

#ifndef _WIN32
// Stops a started image that did not report its window: its whole process
// group (it was started in a session of its own, so the AppImage runtime
// and the app it runs go together), with SIGTERM, then SIGKILL after a
// grace, and waits for it, so that the image put back does not find it
// still holding the single-instance lock.
// Whether a process of that group still runs. Zombies do not count: they
// stay in their group until reaped, which a container's init may never do,
// and kill(-group, 0) still finds them.
static bool groupAlive(pid_t group)
{
    if (::kill(-group, 0) != 0 && errno == ESRCH)
        return false;
    QDir proc(QStringLiteral("/proc"));
    if (!proc.exists())
        return true;
    const QStringList entries = proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    bool listed = false;
    for (const QString &entry : entries) {
        bool number = false;
        entry.toInt(&number);
        if (!number)
            continue;
        QFile stat(QStringLiteral("/proc/%1/stat").arg(entry));
        if (!stat.open(QIODevice::ReadOnly))
            continue;
        const QByteArray line = stat.read(1024);
        const qsizetype close = line.lastIndexOf(')');
        if (close < 0)
            continue;
        listed = true;
        // After "(comm)": state, ppid, pgrp.
        const QList<QByteArray> fields = line.mid(close + 2).split(' ');
        if (fields.size() < 3 || fields.at(2).toLongLong() != group)
            continue;
        if (fields.at(0) != "Z")
            return true;
    }
    // Nothing could be read: as kill() says.
    return !listed;
}

static void stopImage(qint64 pid)
{
    if (pid <= 0)
        return;
    const pid_t group = pid_t(pid);
    auto groupGone = [group] { return !groupAlive(group); };
    auto waitFor = [&](int ms) {
        QElapsedTimer waited;
        waited.start();
        while (waited.elapsed() < ms) {
            if (!alive(pid) && groupGone())
                return true;
            QThread::msleep(50);
        }
        return false;
    };
    ::kill(-group, SIGTERM);
    if (waitFor(5000))
        return;
    if (!groupGone())
        ::kill(-group, SIGKILL);
    waitFor(5000);
}
#endif

void UpdateInstaller::setState(const QString &state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged();
}

bool UpdateInstaller::canInstall() const
{
    if (m_env.cacheDir.isEmpty() || !application::installsUpdates(m_env.package.kind))
        return false;
    if (m_env.package.kind == PackageKind::AppImage) {
        // The new image goes beside the old one, to be renamed over it.
        const QFileInfo image(m_env.package.appImage);
        return image.isFile() && image.isWritable() && QFileInfo(image.absolutePath()).isWritable();
    }
#ifdef _WIN32
    return m_env.package.kind == PackageKind::NsisUser || m_env.package.kind == PackageKind::NsisMachine;
#else
    return isNative(m_env.package.kind);
#endif
}

QString UpdateInstaller::start()
{
    if (m_env.cacheDir.isEmpty())
        return {};
    QString message;
    QString installing;
    if (QFile marker(installingPath()); marker.open(QIODevice::ReadOnly)) {
        installing = QString::fromUtf8(marker.read(64)).trimmed();
        marker.close();
        marker.remove();
    }
    if (QFile result(resultPath()); result.open(QIODevice::ReadOnly)) {
        installing.clear();
        const QJsonObject r = QJsonDocument::fromJson(result.read(application::kMaxManifestBytes)).object();
        result.close();
        result.remove();
        if (!r.value(QStringLiteral("ok")).toBool()) {
            message = r.value(QStringLiteral("message")).toString();
            if (message.isEmpty())
                message = QCoreApplication::translate("HikariSub.MainWindow", "The update to HikariSub %1 could not be installed (error %2).")
                              .arg(r.value(QStringLiteral("version")).toString())
                              .arg(r.value(QStringLiteral("code")).toInt());
        }
    }
    // Handed to the installer, no result here, and still not installed: the
    // installer failed, and told the account that ran it, if anyone.
    if (application::isReleaseVersion(std8(installing)) && application::isNewerVersion(std8(installing), std8(m_running)))
        message = QCoreApplication::translate("HikariSub.MainWindow", "HikariSub %1 was not installed: its installer did not finish. If another administrator account approved it, that account was told why.")
                      .arg(installing);
    const QString token = packageKindToken(m_env.package.kind);
    if (QFile record(pendingPath()); record.open(QIODevice::ReadOnly)) {
        const QJsonObject p = QJsonDocument::fromJson(record.read(application::kMaxManifestBytes)).object();
        record.close();
        Pending pending{p.value(QStringLiteral("version")).toString(), p.value(QStringLiteral("kind")).toString(),
                        p.value(QStringLiteral("file")).toString(), p.value(QStringLiteral("sha256")).toString(),
                        qint64(p.value(QStringLiteral("size")).toDouble()), p.value(QStringLiteral("manual")).toBool()};
        if (belongsToAnotherCopy(pending)) {
            // Another copy's update: neither offered nor removed.
        } else if (p.value(QStringLiteral("schema")).toInt() == 1 &&
            !application::isNewerVersion(std8(pending.version), std8(m_running))) {
            // Installed (or older): an AppImage's old image goes with it,
            // unless a watchdog waits for this start's report (reportHealthy).
            QFile::remove(pending.file);
            record.remove();
            if (m_env.package.kind == PackageKind::AppImage && !m_env.package.appImage.isEmpty() && !m_watched)
                QFile::remove(m_env.package.appImage + QStringLiteral(".old"));
        } else if (p.value(QStringLiteral("schema")).toInt() != 1 || pending.kind != token ||
                   !QFileInfo(pending.file).isFile() || QFileInfo(pending.file).size() != pending.size ||
                   !canInstall()) {
            QFile::remove(pending.file);
            record.remove();
        } else {
            pending.manual = pending.manual || (isNative(m_env.package.kind) && m_env.pkexec.isEmpty());
            m_pending = pending;
            setState(pending.manual ? QStringLiteral("manual") : QStringLiteral("ready"));
        }
    }
    // This copy's folders of versions this one has reached; other copies'
    // downloads are theirs to clear.
    const QDir cache(m_env.cacheDir + QLatin1Char('/') + copyKey());
    for (const QString &name : cache.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        if (application::parseSemVer(std8(name)) && !application::isNewerVersion(std8(name), std8(m_running)))
            QDir(cache.filePath(name)).removeRecursively();
    return message;
}

bool UpdateInstaller::verify(const QString &file, qint64 size, const QString &sha256) const
{
    QFile f(file);
    if (f.size() != size || !f.open(QIODevice::ReadOnly))
        return false;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&f))
        return false;
    return hexOf(hash.result()).compare(sha256, Qt::CaseInsensitive) == 0;
}

bool UpdateInstaller::writePending(const Pending &pending) const
{
    QSaveFile out(pendingPath());
    if (!QDir().mkpath(m_env.cacheDir) || !out.open(QIODevice::WriteOnly))
        return false;
    QJsonObject p{{QStringLiteral("schema"), 1},
                  {QStringLiteral("version"), pending.version},
                  {QStringLiteral("kind"), pending.kind},
                  {QStringLiteral("file"), pending.file},
                  {QStringLiteral("sha256"), pending.sha256},
                  {QStringLiteral("size"), double(pending.size)},
                  {QStringLiteral("downloaded"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
    if (pending.manual)
        p.insert(QStringLiteral("manual"), true);
    out.write(QJsonDocument(p).toJson(QJsonDocument::Compact));
    return out.commit();
}

void UpdateInstaller::dropPending()
{
    QFile::remove(pendingPath());
    m_pending = {};
    m_restart = false;
    setState(QStringLiteral("idle"));
}

void UpdateInstaller::writeResult(const QString &version, int code, const QString &message) const
{
    QSaveFile out(resultPath());
    if (!QDir().mkpath(m_env.cacheDir) || !out.open(QIODevice::WriteOnly))
        return;
    out.write(QJsonDocument(QJsonObject{{QStringLiteral("version"), version},
                                        {QStringLiteral("ok"), false},
                                        {QStringLiteral("code"), code},
                                        {QStringLiteral("message"), message}})
                  .toJson(QJsonDocument::Compact));
    out.commit();
}

void UpdateInstaller::makeReady(const Pending &pending)
{
    m_pending = pending;
    m_pending.manual = isNative(m_env.package.kind) && m_env.pkexec.isEmpty();
    writePending(m_pending);
    m_progress = 1;
    emit progressChanged();
    // stateChanged also when it was ready for another version already.
    m_state.clear();
    setState(m_pending.manual ? QStringLiteral("manual") : QStringLiteral("ready"));
    emit ready();
}

void UpdateInstaller::download(const QString &version, const application::UpdatePackage &package, const QUrl &url,
                               bool interactive)
{
    if (!canInstall() || !application::isNewerVersion(std8(version), std8(m_running)))
        return;
    if (m_reply) {
        if (m_downloading.version == version) {
            m_interactive = m_interactive || interactive;
            return;
        }
        abortDownload(); // a newer check result wins
    }
    if ((m_state == QLatin1String("ready") || m_state == QLatin1String("manual")) && m_pending.version == version) {
        emit ready();
        return;
    }
    const QString name = QString::fromStdString(package.name);
    Pending d{version, packageKindToken(m_env.package.kind), {}, QString::fromStdString(package.sha256), package.size,
              false};
    if (m_env.package.kind == PackageKind::AppImage)
        d.file = appImageStaging(version);
    else
        d.file = versionDir(version) + QLatin1Char('/') + name;
    if (d.file.isEmpty())
        return; // an AppImage path that is not absolute
    if (verify(d.file, d.size, d.sha256)) {
        makeReady(d);
        return;
    }
    QDir().mkpath(QFileInfo(d.file).absolutePath());
    m_part = std::make_unique<QFile>(d.file + QStringLiteral(".part"));
    if (!m_part->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_part.reset();
        m_interactive = interactive;
        failDownload(QCoreApplication::translate("HikariSub.MainWindow", "Cannot write the update to %1").arg(QFileInfo(d.file).absolutePath()));
        return;
    }
    m_downloading = d;
    m_interactive = interactive;
    m_received = 0;
    m_hash.reset();
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("HikariSub/%1").arg(m_running));
    request.setTransferTimeout(60 * 1000); // no byte for a minute: stalled
    m_reply = m_network.get(request);
    QNetworkReply *reply = m_reply;
    connect(reply, &QNetworkReply::redirected, this, [this, reply](const QUrl &to) {
        if (m_env.allowRedirect && m_env.allowRedirect(to))
            emit reply->redirectAllowed();
        else
            reply->abort();
    });
    connect(reply, &QNetworkReply::metaDataChanged, &m_connectTimer, &QTimer::stop);
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        if (reply != m_reply || !reply->isOpen())
            return; // aborted
        m_connectTimer.stop();
        const QByteArray bytes = reply->readAll();
        m_received += bytes.size();
        if (m_received > m_downloading.size || m_part->write(bytes) != bytes.size()) {
            reply->abort();
            return;
        }
        m_hash.addData(bytes);
        m_progress = m_downloading.size > 0 ? double(m_received) / double(m_downloading.size) : 0;
        emit progressChanged();
    });
    connect(reply, &QNetworkReply::finished, this, &UpdateInstaller::finishDownload);
    m_connectTimer.start(30 * 1000);
    m_deadline.start(int(std::min<qint64>(application::downloadDeadlineMs(package.size), INT_MAX)));
    m_progress = 0;
    emit progressChanged();
    setState(QStringLiteral("downloading"));
}

void UpdateInstaller::abortDownload()
{
    if (!m_reply)
        return;
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
    m_connectTimer.stop();
    m_deadline.stop();
    if (m_part) {
        m_part->close();
        m_part->remove();
        m_part.reset();
    }
    setState(m_pending.version.isEmpty() ? QStringLiteral("idle")
                                         : (m_pending.manual ? QStringLiteral("manual") : QStringLiteral("ready")));
}

void UpdateInstaller::failDownload(const QString &message)
{
    setState(m_pending.version.isEmpty() ? QStringLiteral("idle")
                                         : (m_pending.manual ? QStringLiteral("manual") : QStringLiteral("ready")));
    if (m_interactive)
        emit failed(message);
}

void UpdateInstaller::finishDownload()
{
    QNetworkReply *reply = m_reply;
    if (!reply)
        return;
    m_reply = nullptr;
    reply->deleteLater();
    m_connectTimer.stop();
    m_deadline.stop();
    const QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    const bool ok = reply->error() == QNetworkReply::NoError && (!status.isValid() || status.toInt() == 200);
    if (ok) {
        // What is left after the last readyRead.
        const QByteArray rest = reply->readAll();
        m_received += rest.size();
        if (m_received <= m_downloading.size && m_part->write(rest) == rest.size())
            m_hash.addData(rest);
    }
    const QString part = m_part->fileName();
    const bool written = m_part->flush();
    m_part->close();
    m_part.reset();
    if (!ok || !written) {
        QFile::remove(part);
        failDownload(QCoreApplication::translate("HikariSub.MainWindow", "Cannot download the update: %1").arg(reply->errorString()));
        return;
    }
    if (m_received != m_downloading.size ||
        hexOf(m_hash.result()).compare(m_downloading.sha256, Qt::CaseInsensitive) != 0) {
        QFile::remove(part);
        failDownload(QCoreApplication::translate("HikariSub.MainWindow", "The downloaded update does not match its checksum, so it was deleted."));
        return;
    }
    if (m_env.package.kind == PackageKind::AppImage)
        QFile::setPermissions(part, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
                                        QFileDevice::ReadGroup | QFileDevice::ExeGroup | QFileDevice::ReadOther |
                                        QFileDevice::ExeOther);
    QFile::remove(m_downloading.file);
    if (!QFile::rename(part, m_downloading.file)) {
        QFile::remove(part);
        failDownload(QCoreApplication::translate("HikariSub.MainWindow", "Cannot write the update to %1").arg(QFileInfo(m_downloading.file).absolutePath()));
        return;
    }
    makeReady(m_downloading);
}

QString UpdateInstaller::manualCommand() const
{
    if (m_state != QLatin1String("manual"))
        return {};
    return QString::fromStdString(
        application::manualInstallCommand(std8(m_env.helper), std8(m_pending.file), std8(m_pending.version)));
}

void UpdateInstaller::restartNow()
{
    if (m_state != QLatin1String("ready") || m_restart)
        return;
    m_restart = true;
    emit restartRequestedChanged();
}

void UpdateInstaller::cancelRestart()
{
    if (!m_restart)
        return;
    m_restart = false;
    emit restartRequestedChanged();
}

void UpdateInstaller::showInFolder() const
{
    if (!m_pending.file.isEmpty())
        backends::desktop::selectInFolder(m_pending.file);
}

void UpdateInstaller::copyInstallCommand() const
{
    if (auto *clipboard = QGuiApplication::clipboard(); clipboard && !manualCommand().isEmpty())
        clipboard->setText(manualCommand());
}

UpdateInstaller::ExitAction UpdateInstaller::installAtExit(const std::function<std::function<void()>()> &busy)
{
    abortDownload();
    if (m_state != QLatin1String("ready") || m_pending.version.isEmpty() ||
        !application::isNewerVersion(std8(m_pending.version), std8(m_running)))
        return {};
    // Checked again: the file sat in the user's cache since its download.
    // This only weeds out a broken download; the record is the user's to
    // change, so what installs checks the file against the release itself.
    if (!verify(m_pending.file, m_pending.size, m_pending.sha256)) {
        const bool restart = m_restart;
        const QString version = m_pending.version;
        QFile::remove(m_pending.file);
        dropPending();
        writeResult(version, 2,
                    QCoreApplication::translate("HikariSub.MainWindow", "The download of HikariSub %1 changed or went missing, so it was not installed. It is downloaded again at the next check.")
                        .arg(version));
        // Restart now still restarts: the version installed, which says why.
        if (restart)
            return startInstalled();
        return {};
    }
    switch (m_env.package.kind) {
    case PackageKind::NsisUser:
    case PackageKind::NsisMachine:
        return installNsis(busy);
    case PackageKind::AppImage:
        return installAppImage();
    case PackageKind::Deb:
    case PackageKind::Rpm:
        return installNative(busy);
    default:
        return {};
    }
}

UpdateInstaller::ExitAction UpdateInstaller::installNsis(const std::function<std::function<void()>()> &busy)
{
#ifdef _WIN32
    // The downloaded installer is never what runs elevated: the installed
    // program itself, with --hikari-apply-update, checks it against the
    // release's SHA256SUMS and starts it (update_apply.h). Elevated through
    // one UAC prompt for all users; unelevated for the user's own install.
    const bool machine = m_env.package.kind == PackageKind::NsisMachine;
    const QString version = m_pending.version;
    application::ApplyRequest request;
    request.version = std8(version);
    request.file = QDir::toNativeSeparators(m_pending.file).toStdString();
    request.pid = GetCurrentProcessId();
    request.relaunch = m_restart;
    const std::wstring program = QDir::toNativeSeparators(m_env.executable).toStdWString();
    const std::wstring parameters = update_apply::commandLine(request);
    const auto startAgain = [this] {
        if (m_restart)
            startInstalled();
    };
    // UAC is asked to run this program only when the user cannot have
    // changed it: package-kind.txt says "all users" in a per-user copy as
    // easily as in Program Files, and a DLL put beside the program would
    // run elevated before any check (update_apply.h).
    if (machine && !update_apply::isElevated() &&
        !update_apply::isProtectedFromUser(QDir::toNativeSeparators(QFileInfo(m_env.executable).absolutePath()).toStdWString())) {
        // The download stays for the user to run; the update is not tried again.
        QFile::remove(pendingPath());
        writeResult(version, application::kApplyFailed,
                    QCoreApplication::translate("HikariSub.MainWindow", "HikariSub is installed for all users in a folder that can be changed without administrator rights, so HikariSub %1 was not installed automatically. Run %2 to install it.")
                        .arg(version, QDir::toNativeSeparators(m_pending.file)));
        startAgain();
        return {};
    }
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOASYNC | SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = machine ? L"runas" : nullptr;
    info.lpFile = program.c_str();
    info.lpParameters = parameters.c_str();
    info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info) || !info.hProcess) {
        if (GetLastError() != ERROR_CANCELLED) // declined: still pending
            writeResult(version, application::kApplyFailed,
                        QCoreApplication::translate("HikariSub.MainWindow", "The installer of HikariSub %1 could not be started. Run %2 to install it.")
                            .arg(version, QDir::toNativeSeparators(m_pending.file)));
        startAgain();
        return {};
    }
    std::function<void()> hide = busy ? busy() : std::function<void()>();
    QEventLoop loop; // the busy notice keeps painting
    QTimer poll;
    connect(&poll, &QTimer::timeout, &loop, [&loop, process = info.hProcess] {
        if (WaitForSingleObject(process, 0) == WAIT_OBJECT_0)
            loop.quit();
    });
    poll.start(100);
    if (WaitForSingleObject(info.hProcess, 0) != WAIT_OBJECT_0)
        loop.exec();
    poll.stop();
    DWORD code = DWORD(application::kApplyFailed);
    if (!GetExitCodeProcess(info.hProcess, &code))
        code = DWORD(application::kApplyFailed);
    CloseHandle(info.hProcess);
    if (hide)
        hide();
    if (code == DWORD(application::kApplyOk)) {
        // The installer waits for this process to exit. It writes a failure
        // to last-result.json under the LocalAppData of the account that
        // runs it, for all users the administrator who approved UAC, maybe
        // not this user: the next start tells this user from this file.
        if (QSaveFile installing(installingPath());
            installing.open(QIODevice::WriteOnly) && installing.write(version.toUtf8()) >= 0)
            installing.commit();
        return {};
    }
    QString message;
    switch (int(code)) {
    case application::kApplyMismatch:
    case application::kApplyBadArguments:
        // Not the release's file, or not newer: it will not become right.
        message = QCoreApplication::translate("HikariSub.MainWindow", "The downloaded HikariSub %1 package did not pass its checks, so it was not installed.").arg(version);
        QFile::remove(m_pending.file);
        QFile::remove(pendingPath());
        break;
    case application::kApplyNoSums:
        message = QCoreApplication::translate("HikariSub.MainWindow", "HikariSub %1 could not be checked against its release on GitHub, so it was not installed. It will be tried again when you next quit.").arg(version);
        break;
    default:
        message = QCoreApplication::translate("HikariSub.MainWindow", "The installer of HikariSub %1 could not be started. Run %2 to install it.")
                      .arg(version, QDir::toNativeSeparators(m_pending.file));
        break;
    }
    writeResult(version, int(code), message);
    startAgain();
#else
    Q_UNUSED(busy);
#endif
    return {};
}

UpdateInstaller::ExitAction UpdateInstaller::installAppImage()
{
#ifndef _WIN32
    if (!m_restart) {
        // An ordinary quit: nothing would watch the new image's first start,
        // so the next start replaces the image and watches it instead.
        QSaveFile marker(atStartPath());
        if (marker.open(QIODevice::WriteOnly) && marker.write(m_pending.version.toUtf8()) >= 0)
            marker.commit();
        return {};
    }
    // Nothing replaced and nothing started (a folder that became
    // read-only): the version still here starts again, which says why.
    if (!replaceAppImage({}))
        return startInstalled();
#endif
    return {};
}

// A Restart now that installed nothing starts the version still here. On
// Linux it takes this process's place (an AppImage's mount stays with it);
// on Windows it starts beside this one, which is exiting.
UpdateInstaller::ExitAction UpdateInstaller::startInstalled() const
{
#ifdef _WIN32
    QProcess::startDetached(m_env.executable, {});
    return {};
#else
    return {m_env.executable, {}};
#endif
}

bool UpdateInstaller::installAppImageAtStart(const QStringList &arguments)
{
#ifndef _WIN32
    if (m_env.package.kind != PackageKind::AppImage || m_env.cacheDir.isEmpty() || m_env.package.appImage.isEmpty())
        return false;
    QFile marker(atStartPath());
    if (!marker.open(QIODevice::ReadOnly))
        return false;
    const QString version = QString::fromUtf8(marker.read(64)).trimmed();
    marker.close();
    const auto pending = readPending();
    // Another copy's record: that copy's next start installs it.
    if (pending && belongsToAnotherCopy(*pending))
        return false;
    marker.remove(); // once: a failure below leaves the update to start()
    if (!pending || pending->version != version || pending->kind != packageKindToken(m_env.package.kind) ||
        !application::isNewerVersion(std8(pending->version), std8(m_running)) || !canInstall() ||
        !verify(pending->file, pending->size, pending->sha256))
        return false;
    m_pending = *pending;
    m_restart = true;
    return replaceAppImage(arguments);
#else
    Q_UNUSED(arguments);
    return false;
#endif
}

// The swap and the watchdog: true when a process was started in this one's
// place (the new version, or the old one put back).
bool UpdateInstaller::replaceAppImage(const QStringList &arguments)
{
#ifndef _WIN32
    const QString image = m_env.package.appImage, old = image + QStringLiteral(".old"), fresh = m_pending.file;
    const QString version = m_pending.version;
    const QByteArray imagePath = QFile::encodeName(image), oldPath = QFile::encodeName(old);
    QFile::remove(old);
    // The old image stays as .old: a hard link, else moved, else copied.
    if (::link(imagePath.constData(), oldPath.constData()) != 0 && !replaceFile(image, old) && !QFile::copy(image, old)) {
        writeResult(version, 2, QCoreApplication::translate("HikariSub.MainWindow", "Cannot keep a copy of %1, so the update was not installed.").arg(image));
        return false;
    }
    if (!replaceFile(fresh, image)) {
        if (!QFileInfo::exists(image))
            replaceFile(old, image);
        writeResult(version, 2, QCoreApplication::translate("HikariSub.MainWindow", "Cannot replace %1, so the update was not installed.").arg(image));
        return false;
    }
    // The pending record stays: the new version's first start drops it with .old.
    // The watchdog: the new image reports its window by creating the health
    // file; if it exits first or stays silent, the old image comes back.
    // Named for this copy too: another AppImage's watchdog, waiting on the
    // same version, must not take this one's report for its own.
    const QString health = m_env.cacheDir + QStringLiteral("/health-%1-%2").arg(copyKey(), version);
    QFile::remove(health);
    QProcess next;
    next.setProgram(image);
    next.setArguments(QStringList{QStringLiteral("--hikari-updated-from=") + m_running, QStringLiteral("--hikari-health=") + health} +
                      arguments);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const char *name : {"APPDIR", "APPIMAGE", "ARGV0", "OWD"})
        env.remove(QString::fromLatin1(name));
    next.setProcessEnvironment(env);
    next.setWorkingDirectory(QDir::homePath());
    // A session of its own: the watchdog can stop its whole process group.
    next.setUnixProcessParameters(QProcess::UnixProcessFlag::CreateNewSession);
    qint64 pid = 0;
    if (next.startDetached(&pid)) {
        QElapsedTimer waited;
        waited.start();
        while (waited.elapsed() < m_env.healthTimeoutMs) {
            if (QFileInfo::exists(health)) {
                QFile::remove(health);
                return true;
            }
            if (!alive(pid))
                break;
            QThread::msleep(100);
        }
        stopImage(pid);
    }
    replaceFile(old, image);
    writeResult(version, 1,
                QCoreApplication::translate("HikariSub.MainWindow", "HikariSub %1 did not start, so the version you had was put back.").arg(version));
    QProcess::startDetached(image, arguments, QDir::homePath());
    return true;
#else
    Q_UNUSED(arguments);
    return false;
#endif
}

UpdateInstaller::ExitAction UpdateInstaller::installNative(const std::function<std::function<void()>()> &busy)
{
    const QString version = m_pending.version;
    ExitAction again; // the version still installed, after a failure with Restart now
    if (m_restart)
        again.program = m_env.executable;
    if (m_env.pkexec.isEmpty())
        return again;
    QProcess helper;
    helper.setProgram(m_env.pkexec);
    // The file and the version only: the helper checks the file against the
    // release's own SHA256SUMS, never a digest of ours.
    helper.setArguments({m_env.helper, QStringLiteral("install"), m_pending.file, version});
    std::function<void()> hide = busy ? busy() : std::function<void()>();
    QElapsedTimer took;
    took.start();
    QEventLoop loop; // the busy notice keeps painting
    connect(&helper, &QProcess::finished, &loop, &QEventLoop::quit);
    connect(&helper, &QProcess::errorOccurred, &loop, [&loop](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            loop.quit();
    });
    helper.start();
    if (helper.state() != QProcess::NotRunning || helper.error() != QProcess::FailedToStart)
        loop.exec();
    if (hide)
        hide();
    const int code = helper.error() == QProcess::FailedToStart || helper.exitStatus() != QProcess::NormalExit
                         ? -1
                         : helper.exitCode();
    if (code == 0) {
        if (!m_restart)
            return {};
        return {m_env.relaunch, {QStringLiteral("--hikari-updated-from=") + m_running}};
    }
    if (code == 126 || code == 127) {
        // Dismissed or not allowed: still pending. Refused at once, without a
        // prompt (no polkit agent): the file and its command, from now on.
        if (code == 127 && took.elapsed() < 2000) {
            m_pending.manual = true;
            writePending(m_pending);
        }
        return again;
    }
    QString message;
    switch (code) {
    case 2:
    case 3:
        message = QCoreApplication::translate("HikariSub.MainWindow", "The downloaded HikariSub %1 package did not pass its checks, so it was not installed.").arg(version);
        QFile::remove(m_pending.file);
        QFile::remove(pendingPath());
        break;
    case application::kApplyNoSums:
        message = QCoreApplication::translate("HikariSub.MainWindow", "HikariSub %1 could not be checked against its release on GitHub, so it was not installed. It will be tried again when you next quit.").arg(version);
        break;
    case 5:
        message = QCoreApplication::translate("HikariSub.MainWindow", "HikariSub %1 was not installed because the package manager is busy. It will be installed when "
                     "you next quit.")
                      .arg(version);
        break;
    default:
        message = QCoreApplication::translate("HikariSub.MainWindow", "The package manager could not install HikariSub %1 (error %2). The file is %3.")
                      .arg(version)
                      .arg(code)
                      .arg(m_pending.file);
        break;
    }
    writeResult(version, code, message);
    return again;
}

void UpdateInstaller::reportHealthy(const QString &healthFile)
{
    if (m_env.cacheDir.isEmpty() || healthFile.isEmpty())
        return;
    const QFileInfo file(healthFile);
    // Only the watchdog's file: the switch creates nothing anywhere else.
    if (file.absolutePath() != QFileInfo(m_env.cacheDir).absoluteFilePath() ||
        !file.fileName().startsWith(QLatin1String("health-")))
        return;
    if (QFile health(healthFile); !health.open(QIODevice::WriteOnly))
        return;
    if (m_env.package.kind == PackageKind::AppImage && !m_env.package.appImage.isEmpty())
        QFile::remove(m_env.package.appImage + QStringLiteral(".old"));
}

} // namespace hikari::app
