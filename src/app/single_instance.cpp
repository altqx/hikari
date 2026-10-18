#include "hikari/app/single_instance.h"

#include "hikari/application/update_check.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QThread>
#include <QTimer>
#include <QUrl>

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
// The sender's answer to the ack: it hands over and exits. A sender that gave
// up before the ack (it runs alone) never sends it, so its request is dropped.
constexpr char kCommit = '\2';
// How long a sender that has the ack waits to send the commit and for the
// holder to close.
constexpr int kCommitMs = 1000;
// A connection that has not sent its whole request and commit by then is dropped.
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

// The whole request when `buffer` holds one; false while more is to come.
// A malformed one sets `bad`.
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
// The process token's user SID; empty when it cannot be read.
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

// The pipe's server is this user's, in this session; it may then take the
// foreground (the start that the user launched still may give it away).
bool acceptServer(QLocalSocket &socket)
{
    const auto pipe = reinterpret_cast<HANDLE>(socket.socketDescriptor());
    ULONG pid = 0;
    if (!::GetNamedPipeServerProcessId(pipe, &pid))
        return false;
    DWORD ours = 0, theirs = 0;
    if (!::ProcessIdToSessionId(::GetCurrentProcessId(), &ours) || !::ProcessIdToSessionId(pid, &theirs) ||
        ours != theirs)
        return false;
    HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return false;
    auto server = tokenUser(process);
    ::CloseHandle(process);
    auto self = tokenUser(::GetCurrentProcess());
    if (server.empty() || self.empty() || !::EqualSid(sidOf(server), sidOf(self)))
        return false;
    ::AllowSetForegroundWindow(pid);
    return true;
}
#else
// The other end is this user's (the folder is private already).
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

// A folder only this user can enter: no link, the user's own, no group or
// other permissions. `create` makes it first when it is missing.
bool privateFolder(const QString &path, bool create)
{
    const QByteArray name = QFile::encodeName(path);
    if (create)
        ::mkdir(name.constData(), 0700);
    struct stat info{};
    if (::lstat(name.constData(), &info) != 0)
        return false;
    return S_ISDIR(info.st_mode) && info.st_uid == ::getuid() && (info.st_mode & 077) == 0;
}

// The socket's path fits the system's socket address (108 bytes on Linux).
bool fitsSocket(const QString &path)
{
    return QFile::encodeName(path).size() < qsizetype(sizeof(sockaddr_un::sun_path));
}
#endif

} // namespace

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

SingleInstance::Location SingleInstance::forSession()
{
    const QString prefix = QStringLiteral("io.github.altqx.HikariSub.Instance-");
#ifdef _WIN32
    auto user = tokenUser(::GetCurrentProcess());
    DWORD session = 0;
    LPWSTR sid = nullptr;
    if (user.empty() || !::ProcessIdToSessionId(::GetCurrentProcessId(), &session) ||
        !::ConvertSidToStringSidW(sidOf(user), &sid))
        return {};
    const QString name = prefix + QString::fromWCharArray(sid);
    ::LocalFree(sid);
    // The pipe namespace spans the sessions; the Local\ one is the session's.
    return {name + QLatin1Char('-') + QString::number(session), QStringLiteral("Local\\") + name};
#else
    // The graphical session: its Wayland display, else its X display.
    QString display = qEnvironmentVariable("WAYLAND_DISPLAY");
    display = display.isEmpty() ? QStringLiteral("x11:") + qEnvironmentVariable("DISPLAY") : QStringLiteral("wayland:") + display;
    const QString tag =
        QString::fromLatin1(QCryptographicHash::hash(display.toUtf8(), QCryptographicHash::Sha256).toHex().left(16));
    auto in = [&](const QString &folder) -> Location {
        const QString base = folder + QLatin1Char('/') + prefix + tag;
        return {base + QStringLiteral(".socket"), base + QStringLiteral(".lock")};
    };
    // $XDG_RUNTIME_DIR when only the user can enter it and the socket's path
    // fits there; else a private folder in the temporary folder.
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

SingleInstance::SingleInstance(Location location, QObject *parent)
    : QObject(parent)
    , m_location(std::move(location))
{
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
        // The holder may have just exited (or crashed): then this start is the instance.
        if (tryLock())
            return serve();
        if (waited.elapsed() >= timeoutMs)
            return Role::Alone;
        QThread::msleep(50);
    }
}

SingleInstance::Role SingleInstance::serve()
{
    if (listen())
        return Role::Primary;
    // No later start could reach this one: it gives the instance up and runs
    // alone, and the next start claims it.
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
    // From here this start hands over: the holder opens the files once it
    // has the commit.
    socket.putChar(kCommit);
    while (socket.bytesToWrite() > 0)
        if (!socket.waitForBytesWritten(kCommitMs))
            return false;
    // The holder closes once it has read the commit; a pipe closed first may
    // lose what is still in it.
    if (socket.state() == QLocalSocket::ConnectedState)
        socket.waitForDisconnected(kCommitMs);
    return true;
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
    // Abandoned: its holder ended without releasing it (a crash).
    const DWORD result = ::WaitForSingleObject(m_mutex, 0);
    m_owned = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
    return m_owned;
#else
    if (m_lockFd >= 0)
        return true;
    // Never removed, so two starts always lock the same file; the system
    // drops the lock with its holder, and an exec (deb, rpm updates) too.
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
    // A socket file a crashed holder left; this start holds the lock now.
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
    unlock();
}

void SingleInstance::accept()
{
    while (QLocalSocket *socket = m_server ? m_server->nextPendingConnection() : nullptr) {
        // Owned by this object, not the server: release() may end the server
        // while a request is being read.
        socket->setParent(this);
#ifndef _WIN32
        if (!sameUser(*socket)) {
            socket->abort();
            socket->deleteLater();
            continue;
        }
#endif
        QTimer::singleShot(kReadTimeoutMs, socket, [socket] { socket->abort(); });
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] { read(socket); });
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        read(socket);
    }
}

void SingleInstance::read(QLocalSocket *socket)
{
    const QVariant acked = socket->property("hikariAcked");
    if (acked.isValid()) {
        char commit = 0;
        if (!socket->getChar(&commit))
            return;
        socket->setProperty("hikariAcked", QVariant());
        if (commit != kCommit || socket->bytesAvailable() > 0) {
            socket->abort();
            return;
        }
        socket->disconnectFromServer();
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
    // The request is opened only once its sender commits: one that has given
    // up waiting (and runs alone) is gone or never answers.
    socket->setProperty("hikariAcked", QVariant::fromValue(request));
    socket->putChar(kAck);
    socket->flush();
}

} // namespace hikari::app
