#include "hikari/app/update_installer.h"

#include "hikari/backends/show_in_folder.h"

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

QString UpdateInstaller::versionDir(const QString &version) const
{
    return m_env.cacheDir + QLatin1Char('/') + version;
}

QString UpdateInstaller::pendingPath() const
{
    return m_env.cacheDir + QStringLiteral("/pending.json");
}

QString UpdateInstaller::resultPath() const
{
    return m_env.cacheDir + QStringLiteral("/last-result.json");
}

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
    if (QFile result(resultPath()); result.open(QIODevice::ReadOnly)) {
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
    const QString token = packageKindToken(m_env.package.kind);
    if (QFile record(pendingPath()); record.open(QIODevice::ReadOnly)) {
        const QJsonObject p = QJsonDocument::fromJson(record.read(application::kMaxManifestBytes)).object();
        record.close();
        Pending pending{p.value(QStringLiteral("version")).toString(), p.value(QStringLiteral("kind")).toString(),
                        p.value(QStringLiteral("file")).toString(), p.value(QStringLiteral("sha256")).toString(),
                        qint64(p.value(QStringLiteral("size")).toDouble()), p.value(QStringLiteral("manual")).toBool()};
        if (p.value(QStringLiteral("schema")).toInt() == 1 &&
            !application::isNewerVersion(std8(pending.version), std8(m_running))) {
            // Installed (or older): an AppImage's old image goes with it.
            QFile::remove(pending.file);
            record.remove();
            if (m_env.package.kind == PackageKind::AppImage && !m_env.package.appImage.isEmpty())
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
    // The folders of versions this one has reached.
    const QDir cache(m_env.cacheDir);
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
        d.file = QFileInfo(m_env.package.appImage).absolutePath() + QStringLiteral("/.HikariSub-%1.AppImage").arg(version);
    else
        d.file = versionDir(version) + QLatin1Char('/') + name;
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
    using application::PackageTool;
    PackageTool tool = PackageTool::Apt;
    if (m_env.package.kind == PackageKind::Rpm)
        tool = QStandardPaths::findExecutable(QStringLiteral("dnf")).isEmpty() &&
                       !QStandardPaths::findExecutable(QStringLiteral("zypper")).isEmpty()
                   ? PackageTool::Zypper
                   : PackageTool::Dnf;
    return QString::fromStdString(application::manualInstallCommand(tool, std8(m_pending.file)));
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
        backends::selectInFolder(m_pending.file);
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
    if (!verify(m_pending.file, m_pending.size, m_pending.sha256)) {
        QFile::remove(m_pending.file);
        dropPending();
        return {};
    }
    switch (m_env.package.kind) {
    case PackageKind::NsisUser:
    case PackageKind::NsisMachine:
        return installNsis();
    case PackageKind::AppImage:
        return installAppImage();
    case PackageKind::Deb:
    case PackageKind::Rpm:
        return installNative(busy);
    default:
        return {};
    }
}

UpdateInstaller::ExitAction UpdateInstaller::installNsis()
{
#ifdef _WIN32
    const bool machine = m_env.package.kind == PackageKind::NsisMachine;
    QString dir = QDir::toNativeSeparators(QFileInfo(m_env.executable).absolutePath());
    while (dir.endsWith(QLatin1Char('\\')))
        dir.chop(1);
    const std::wstring setup = QDir::toNativeSeparators(m_pending.file).toStdWString();
    const std::wstring arguments = QString::fromStdString(application::nsisUpdateArguments(
                                                              machine, m_restart, qint64(GetCurrentProcessId()), std8(dir)))
                                       .toStdWString();
    if (machine) {
        // One UAC prompt; the installer starts the program again unelevated.
        SHELLEXECUTEINFOW info{};
        info.cbSize = sizeof(info);
        info.fMask = SEE_MASK_NOASYNC;
        info.lpVerb = L"runas";
        info.lpFile = setup.c_str();
        info.lpParameters = arguments.c_str();
        info.nShow = SW_SHOWNORMAL;
        if (!ShellExecuteExW(&info)) {
            const DWORD error = GetLastError();
            if (error != ERROR_CANCELLED) // declined: still pending
                writeResult(m_pending.version, 4,
                            QCoreApplication::translate("HikariSub.MainWindow", "The installer of HikariSub %1 could not be started. Run %2 to install it.")
                                .arg(m_pending.version, QDir::toNativeSeparators(m_pending.file)));
        }
        return {};
    }
    std::wstring command = L"\"" + setup + L"\" " + arguments;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(setup.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup,
                       &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    } else {
        writeResult(m_pending.version, 2,
                    QCoreApplication::translate("HikariSub.MainWindow", "The installer of HikariSub %1 could not be started. Run %2 to install it.")
                        .arg(m_pending.version, QDir::toNativeSeparators(m_pending.file)));
    }
#endif
    return {};
}

UpdateInstaller::ExitAction UpdateInstaller::installAppImage()
{
#ifndef _WIN32
    const QString image = m_env.package.appImage, old = image + QStringLiteral(".old"), fresh = m_pending.file;
    const QString version = m_pending.version;
    const QByteArray imagePath = QFile::encodeName(image), oldPath = QFile::encodeName(old);
    QFile::remove(old);
    // The old image stays as .old: a hard link, else moved, else copied.
    if (::link(imagePath.constData(), oldPath.constData()) != 0 && !replaceFile(image, old) && !QFile::copy(image, old)) {
        writeResult(version, 2, QCoreApplication::translate("HikariSub.MainWindow", "Cannot keep a copy of %1, so the update was not installed.").arg(image));
        return {};
    }
    if (!replaceFile(fresh, image)) {
        if (!QFileInfo::exists(image))
            replaceFile(old, image);
        writeResult(version, 2, QCoreApplication::translate("HikariSub.MainWindow", "Cannot replace %1, so the update was not installed.").arg(image));
        return {};
    }
    // pending.json stays: the new version's first start drops it with .old.
    if (!m_restart)
        return {};
    // The watchdog: the new image reports its window by creating the health
    // file; if it exits first or stays silent, the old image comes back.
    const QString health = m_env.cacheDir + QStringLiteral("/health-") + version;
    QFile::remove(health);
    QProcess next;
    next.setProgram(image);
    next.setArguments({QStringLiteral("--hikari-updated-from=") + m_running, QStringLiteral("--hikari-health=") + health});
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const char *name : {"APPDIR", "APPIMAGE", "ARGV0", "OWD"})
        env.remove(QString::fromLatin1(name));
    next.setProcessEnvironment(env);
    next.setWorkingDirectory(QDir::homePath());
    qint64 pid = 0;
    if (next.startDetached(&pid)) {
        QElapsedTimer waited;
        waited.start();
        while (waited.elapsed() < m_env.healthTimeoutMs) {
            if (QFileInfo::exists(health)) {
                QFile::remove(health);
                return {};
            }
            if (!alive(pid))
                break;
            QThread::msleep(100);
        }
        if (alive(pid))
            ::kill(pid_t(pid), SIGTERM);
    }
    replaceFile(old, image);
    writeResult(version, 1,
                QCoreApplication::translate("HikariSub.MainWindow", "HikariSub %1 did not start, so the version you had was put back.").arg(version));
    QProcess::startDetached(image, {}, QDir::homePath());
#endif
    return {};
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
    helper.setArguments({m_env.helper, QStringLiteral("install"), packageKindToken(m_env.package.kind), m_pending.file,
                         m_pending.sha256, version});
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
