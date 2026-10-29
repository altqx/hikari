#include "hikari/app/single_instance.h"

#include "hikari/application/update_check.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSaveFile>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sddl.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace hikari::app {

namespace {

constexpr char kMagic[] = "HIKARISUB-OPEN\1";
constexpr qsizetype kMagicSize = sizeof(kMagic) - 1;
constexpr quint32 kVersion = 1;
constexpr quint32 kMaxPayload = 1 << 20;
constexpr char kAck = '\1';
// The commit: this byte, then the sender's receipt wait in ms (32-bit big-endian).
constexpr char kCommit = '\2';
constexpr qsizetype kCommitSize = 1 + 4;
constexpr char kTaken = '\3';
// The holder takes a commit only within half the sender's receipt wait after its ack (at most
// kMaxCommitWindowMs), so a taken commit is always receipted before the sender gives up.
constexpr int kMaxCommitWindowMs = 5000;
constexpr int kMinReceiptWaitMs = 200;
constexpr int kReadTimeoutMs = 5000;

QByteArray encode(const SingleInstance::Request &request)
{
    QByteArray payload;
    QDataStream out(&payload, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_0);
    out << kVersion << request.files << request.activationToken;
    QByteArray frame(kMagic, kMagicSize);
    QDataStream length(&frame, QIODevice::Append);
    length << quint32(payload.size());
    return frame + payload;
}

bool decode(const QByteArray &buffer, SingleInstance::Request &request, bool &bad)
{
    const qsizetype header = kMagicSize + 4;
    if (!QByteArray(kMagic, kMagicSize).startsWith(buffer.left(kMagicSize))) {
        bad = true;
        return false;
    }
    if (buffer.size() < header)
        return false;
    quint32 size = 0;
    QDataStream length(buffer.mid(kMagicSize, 4));
    length >> size;
    if (size > kMaxPayload) {
        bad = true;
        return false;
    }
    if (buffer.size() < header + qsizetype(size))
        return false;
    if (buffer.size() > header + qsizetype(size)) {
        bad = true;
        return false;
    }
    QDataStream in(buffer.mid(header));
    in.setVersion(QDataStream::Qt_6_0);
    quint32 version = 0;
    in >> version >> request.files >> request.activationToken;
    if (in.status() != QDataStream::Ok || version != kVersion || !in.atEnd()) {
        bad = true;
        return false;
    }
    return true;
}

#ifdef _WIN32
std::vector<char> tokenUser(HANDLE process)
{
    HANDLE token = nullptr;
    if (!::OpenProcessToken(process, TOKEN_QUERY, &token))
        return {};
    DWORD size = 0;
    ::GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<char> buffer(size);
    const bool ok = size > 0 && ::GetTokenInformation(token, TokenUser, buffer.data(), size, &size);
    ::CloseHandle(token);
    return ok ? buffer : std::vector<char>();
}

PSID sidOf(std::vector<char> &user)
{
    return reinterpret_cast<TOKEN_USER *>(user.data())->User.Sid;
}

DWORD integrityLevel(HANDLE process)
{
    HANDLE token = nullptr;
    if (!::OpenProcessToken(process, TOKEN_QUERY, &token))
        return 0;
    DWORD size = 0;
    ::GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &size);
    std::vector<char> buffer(size);
    DWORD level = 0;
    if (size > 0 && ::GetTokenInformation(token, TokenIntegrityLevel, buffer.data(), size, &size)) {
        const PSID label = reinterpret_cast<TOKEN_MANDATORY_LABEL *>(buffer.data())->Label.Sid;
        const UCHAR count = *::GetSidSubAuthorityCount(label);
        if (count > 0)
            level = *::GetSidSubAuthority(label, count - 1);
    }
    ::CloseHandle(token);
    return level;
}

bool trusted(ULONG pid)
{
    DWORD ours = 0, theirs = 0;
    if (!::ProcessIdToSessionId(::GetCurrentProcessId(), &ours) || !::ProcessIdToSessionId(pid, &theirs) ||
        ours != theirs)
        return false;
    HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return false;
    auto other = tokenUser(process);
    const DWORD level = integrityLevel(process);
    ::CloseHandle(process);
    auto self = tokenUser(::GetCurrentProcess());
    return !other.empty() && !self.empty() && ::EqualSid(sidOf(other), sidOf(self)) && level != 0 &&
           level >= integrityLevel(::GetCurrentProcess());
}

// AllowSetForegroundWindow works only while this start, which the user launched, may still set it.
bool acceptServer(QLocalSocket &socket)
{
    ULONG pid = 0;
    if (!::GetNamedPipeServerProcessId(reinterpret_cast<HANDLE>(socket.socketDescriptor()), &pid) || !trusted(pid))
        return false;
    ::AllowSetForegroundWindow(pid);
    return true;
}

// The pipe's access list admits any process of this user, an unelevated one included.
bool acceptClient(QLocalSocket &socket)
{
    ULONG pid = 0;
    return ::GetNamedPipeClientProcessId(reinterpret_cast<HANDLE>(socket.socketDescriptor()), &pid) && trusted(pid);
}
#else
// MUST KILL: outside Linux this trusts the private folder alone
bool sameUser(QLocalSocket &socket)
{
#ifdef __linux__
    ucred peer{};
    socklen_t size = sizeof peer;
    if (::getsockopt(int(socket.socketDescriptor()), SOL_SOCKET, SO_PEERCRED, &peer, &size) != 0)
        return false;
    return peer.uid == ::getuid();
#else
    Q_UNUSED(socket);
    return true;
#endif
}

bool privateFolder(const QString &path, bool create)
{
    const QByteArray name = QFile::encodeName(path);
    if (create)
        ::mkdir(name.constData(), 0700);
    struct stat info{};
    if (::lstat(name.constData(), &info) != 0)
        return false;
    return S_ISDIR(info.st_mode) && info.st_uid == ::getuid() && (info.st_mode & 0777) == 0700;
}

// ":0", ":0.0" and "unix:0" are one X display; a relative Wayland name is in $XDG_RUNTIME_DIR.
QString sessionDisplay()
{
    const QString wayland = qEnvironmentVariable("WAYLAND_DISPLAY");
    if (!wayland.isEmpty()) {
        const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
        const bool named = !QDir::isAbsolutePath(wayland) && !runtime.isEmpty();
        return QStringLiteral("wayland:") + QDir::cleanPath(named ? runtime + QLatin1Char('/') + wayland : wayland);
    }
    QString display = qEnvironmentVariable("DISPLAY");
    if (const qsizetype colon = display.lastIndexOf(QLatin1Char(':')); colon >= 0) {
        QString host = display.left(colon), number = display.mid(colon + 1);
        if (const qsizetype dot = number.indexOf(QLatin1Char('.')); dot >= 0)
            number.truncate(dot);
        if (host == QLatin1String("unix"))
            host.clear();
        display = host + QLatin1Char(':') + number;
    }
    return QStringLiteral("x11:") + display;
}

bool fitsSocket(const QString &path)
{
    return QFile::encodeName(path).size() < qsizetype(sizeof(sockaddr_un::sun_path));
}
#endif

}

QStringList launchFiles(const QStringList &arguments, const QString &workingDirectory)
{
    std::vector<std::string> utf8;
    for (const QString &argument : arguments)
        utf8.push_back(argument.toStdString());
    QStringList files;
    const QDir base(workingDirectory);
    for (const auto &argument : application::parseStartupArguments(utf8).rest) {
        QString path = QString::fromStdString(argument);
        if (path.isEmpty())
            continue;
        if (path.startsWith(QLatin1String("file:"), Qt::CaseInsensitive)) {
            const QUrl url(path);
            if (!url.isLocalFile())
                continue;
            path = url.toLocalFile();
        }
        files << QDir::cleanPath(base.absoluteFilePath(path));
    }
    return files;
}

namespace {

QStringList readHandedOn(const QString &path, qint64 maxAgeSeconds)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QDataStream in(&file);
    in.setVersion(QDataStream::Qt_6_0);
    qint64 kept = 0;
    QStringList files;
    in >> kept >> files;
    const qint64 age = QDateTime::currentSecsSinceEpoch() - kept;
    return in.status() == QDataStream::Ok && age >= 0 && age <= maxAgeSeconds ? files : QStringList();
}

}

bool saveHandedOn(const QString &path, const QStringList &files)
{
    QStringList kept = readHandedOn(path, std::numeric_limits<qint64>::max());
    for (const QString &file : files)
        if (!kept.contains(file))
            kept << file;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly))
        return false;
    QDataStream stream(&out);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << QDateTime::currentSecsSinceEpoch() << kept;
    return stream.status() == QDataStream::Ok && out.commit();
}

QStringList takeHandedOn(const QString &path, qint64 maxAgeSeconds)
{
    if (!QFile::exists(path))
        return {};
    // Taken by one start only: two that come at once cannot both rename it.
    const QString taken = path + QStringLiteral(".") + QString::number(QCoreApplication::applicationPid());
    QFile::remove(taken);
    if (!QFile::rename(path, taken))
        return {};
    const QStringList files = readHandedOn(taken, maxAgeSeconds);
    QFile::remove(taken);
    return files;
}

SingleInstance::Location SingleInstance::forSession()
{
    const QString prefix = QStringLiteral("io.github.altqx.HikariSub.Instance-");
#ifdef _WIN32
    auto user = tokenUser(::GetCurrentProcess());
    const DWORD level = integrityLevel(::GetCurrentProcess());
    DWORD session = 0;
    LPWSTR sid = nullptr;
    if (user.empty() || level == 0 || !::ProcessIdToSessionId(::GetCurrentProcessId(), &session) ||
        !::ConvertSidToStringSidW(sidOf(user), &sid))
        return {};
    QString name = prefix + QString::fromWCharArray(sid);
    ::LocalFree(sid);
    if (level != SECURITY_MANDATORY_MEDIUM_RID)
        name += QStringLiteral("-IL") + QString::number(level, 16);
    // The pipe namespace spans the sessions; the Local\ one is the session's.
    return {name + QLatin1Char('-') + QString::number(session), QStringLiteral("Local\\") + name};
#else
    const QString tag = QString::fromLatin1(
        QCryptographicHash::hash(sessionDisplay().toUtf8(), QCryptographicHash::Sha256).toHex().left(16));
    auto in = [&](const QString &folder) -> Location {
        const QString base = folder + QLatin1Char('/') + prefix + tag;
        return {base + QStringLiteral(".socket"), base + QStringLiteral(".lock")};
    };
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (!runtime.isEmpty() && QDir::isAbsolutePath(runtime) && fitsSocket(in(runtime).server) &&
        privateFolder(runtime, false))
        return in(runtime);
    const QString own = QDir::tempPath() + QStringLiteral("/hikarisub-") + QString::number(::getuid());
    if (fitsSocket(in(own).server) && privateFolder(own, true))
        return in(own);
    return {};
#endif
}

bool SingleInstance::trustsUserFiles()
{
#ifdef _WIN32
    const DWORD level = integrityLevel(::GetCurrentProcess());
    return level != 0 && level <= SECURITY_MANDATORY_MEDIUM_RID;
#else
    return true;
#endif
}

SingleInstance::SingleInstance(Location location, QObject *parent)
    : QObject(parent)
    , m_location(std::move(location))
{
    m_clock.start();
}

SingleInstance::~SingleInstance()
{
    release();
}

SingleInstance::Role SingleInstance::claim(const Request &request, bool handOver, int timeoutMs)
{
    if (m_location.server.isEmpty() || m_location.lock.isEmpty())
        return Role::Alone;
    if (tryLock())
        return serve();
    if (!handOver)
        return Role::Alone;
    QElapsedTimer waited;
    waited.start();
    for (;;) {
        if (this->handOver(request, int(qMax<qint64>(timeoutMs - waited.elapsed(), 0))))
            return Role::Forwarded;
        if (tryLock())
            return serve();
        if (waited.elapsed() >= timeoutMs)
            return Role::Alone;
        QThread::msleep(50);
    }
}

bool SingleInstance::reclaim()
{
    if (listening())
        return true;
    if (m_location.server.isEmpty() || m_location.lock.isEmpty() || !tryLock())
        return false;
    return serve() == Role::Primary;
}

SingleInstance::Role SingleInstance::serve()
{
    if (listen())
        return Role::Primary;
    unlock();
    return Role::Alone;
}

bool SingleInstance::handOver(const Request &request, int timeoutMs)
{
    QLocalSocket socket;
    socket.connectToServer(m_location.server);
    if (!socket.waitForConnected(qMin(timeoutMs, 1000)))
        return false;
#ifdef _WIN32
    if (!acceptServer(socket))
        return false;
#else
    if (!sameUser(socket))
        return false;
#endif
    QElapsedTimer waited;
    waited.start();
    auto left = [&] { return int(qMax<qint64>(timeoutMs - waited.elapsed(), 0)); };
    socket.write(encode(request));
    while (socket.bytesToWrite() > 0)
        if (!socket.waitForBytesWritten(left()))
            return false;
    while (socket.bytesAvailable() < 1)
        if (!socket.waitForReadyRead(left()))
            return false;
    char answer = 0;
    if (!socket.getChar(&answer) || answer != kAck)
        return false;
    const int wait = std::clamp(left(), kMinReceiptWaitMs, 2 * kMaxCommitWindowMs);
    QByteArray commit(1, kCommit);
    QDataStream(&commit, QIODevice::Append) << quint32(wait);
    if (socket.write(commit) != kCommitSize)
        return false;
    socket.flush();
    QElapsedTimer committing;
    committing.start();
    while (socket.bytesAvailable() < 1 && socket.state() == QLocalSocket::ConnectedState &&
           committing.elapsed() < wait)
        socket.waitForReadyRead(int(qMax<qint64>(wait - committing.elapsed(), 1)));
    if (socket.bytesAvailable() < 1 && socket.state() == QLocalSocket::ConnectedState)
        socket.waitForReadyRead(0);
    char receipt = 0;
    const bool taken = socket.bytesAvailable() > 0 && socket.getChar(&receipt) && receipt == kTaken;
    if (!taken)
        socket.abort();
    return taken;
}

bool SingleInstance::tryLock()
{
#ifdef _WIN32
    if (m_owned)
        return true;
    if (!m_mutex)
        m_mutex = ::CreateMutexW(nullptr, FALSE, reinterpret_cast<LPCWSTR>(m_location.lock.utf16()));
    if (!m_mutex)
        return false;
    const DWORD result = ::WaitForSingleObject(m_mutex, 0);
    m_owned = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
    return m_owned;
#else
    if (m_lockFd >= 0)
        return true;
    // Never removed, so two starts always lock the same file; O_CLOEXEC drops the lock on exec.
    const int fd = ::open(QFile::encodeName(m_location.lock).constData(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0)
        return false;
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
        ::close(fd);
        return false;
    }
    m_lockFd = fd;
    return true;
#endif
}

void SingleInstance::unlock()
{
#ifdef _WIN32
    if (m_owned)
        ::ReleaseMutex(m_mutex);
    m_owned = false;
    if (m_mutex)
        ::CloseHandle(m_mutex);
    m_mutex = nullptr;
#else
    if (m_lockFd >= 0)
        ::close(m_lockFd);
    m_lockFd = -1;
#endif
}

bool SingleInstance::listen()
{
    m_server = std::make_unique<QLocalServer>();
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    // MUST KILL: removes even a live holder's socket; safe only because the caller holds the lock
    QLocalServer::removeServer(m_location.server);
    if (!m_server->listen(m_location.server)) {
        m_server.reset();
        return false;
    }
    connect(m_server.get(), &QLocalServer::newConnection, this, &SingleInstance::accept);
    return true;
}

bool SingleInstance::listening() const
{
    return m_server && m_server->isListening();
}

void SingleInstance::release()
{
    if (m_server) {
        m_server->close();
        m_server.reset();
    }
    for (QLocalSocket *socket : findChildren<QLocalSocket *>(Qt::FindDirectChildrenOnly)) {
        socket->disconnect(this);
        if (const QVariant until = socket->property("hikariTakenUntil"); until.isValid()) {
            auto left = [&] { return int(qMax<qint64>(until.toLongLong() - m_clock.elapsed(), 0)); };
            if (socket->bytesToWrite() > 0 && left() > 0)
                socket->waitForBytesWritten(left());
            if (socket->state() != QLocalSocket::UnconnectedState && left() > 0)
                socket->waitForDisconnected(left());
        }
        socket->abort();
        socket->deleteLater();
    }
    unlock();
}

void SingleInstance::accept()
{
    while (QLocalSocket *socket = m_server ? m_server->nextPendingConnection() : nullptr) {
        // MUST KILL: reparented so release() can end the server while a request is being read
        socket->setParent(this);
#ifdef _WIN32
        const bool peer = acceptClient(*socket);
#else
        const bool peer = sameUser(*socket);
#endif
        if (!peer) {
            socket->abort();
            socket->deleteLater();
            continue;
        }
        QTimer::singleShot(kReadTimeoutMs, socket, [socket] {
            if (!socket->property("hikariAcked").isValid() && !socket->property("hikariTakenUntil").isValid())
                socket->abort();
        });
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] { read(socket); });
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        read(socket);
    }
}

void SingleInstance::read(QLocalSocket *socket)
{
    if (socket->property("hikariTakenUntil").isValid()) {
        socket->readAll();
        return;
    }
    const QVariant acked = socket->property("hikariAcked");
    if (acked.isValid()) {
        if (socket->bytesAvailable() < kCommitSize)
            return;
        const QByteArray commit = socket->read(kCommitSize);
        socket->setProperty("hikariAcked", QVariant());
        quint32 wait = 0;
        QDataStream(commit.mid(1)) >> wait;
        const qint64 window = qMin<qint64>(wait / 2, kMaxCommitWindowMs);
        const bool late = m_clock.elapsed() - socket->property("hikariAckedAt").toLongLong() > window;
        if (commit.at(0) != kCommit || socket->bytesAvailable() > 0 || late) {
            socket->abort();
            return;
        }
        // A named pipe's disconnect drops unread data: the sender closes once it has the receipt.
        const int left = int(qMin<quint32>(wait, 2 * kMaxCommitWindowMs));
        socket->setProperty("hikariTakenUntil", m_clock.elapsed() + left);
        socket->putChar(kTaken);
        socket->flush();
        QTimer::singleShot(left, socket, [socket] { socket->abort(); });
        emit requested(acked.value<Request>());
        return;
    }
    QByteArray buffer = socket->property("hikariRequest").toByteArray() + socket->readAll();
    Request request;
    bool bad = false;
    if (!decode(buffer, request, bad)) {
        if (bad || buffer.size() > kMagicSize + 4 + qsizetype(kMaxPayload)) {
            socket->abort();
            return;
        }
        socket->setProperty("hikariRequest", buffer);
        return;
    }
    socket->setProperty("hikariRequest", QVariant());
    socket->setProperty("hikariAcked", QVariant::fromValue(request));
    socket->setProperty("hikariAckedAt", m_clock.elapsed());
    QTimer::singleShot(kMaxCommitWindowMs, socket, [socket] {
        if (!socket->property("hikariTakenUntil").isValid())
            socket->abort();
    });
    socket->putChar(kAck);
    socket->flush();
}

}
