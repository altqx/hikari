#pragma once

// WS5: one HikariSub per user session. The first start claims the session's
// instance and listens; a later start hands its files (none: just the
// window's activation) to it and exits:
//
//   - the claim is a lock the system drops with its holder, so a crashed
//     instance never blocks the next start: on Windows a mutex in the
//     session's Local\ namespace, owned by the main thread; elsewhere an
//     flock on a file in a folder only the user can enter
//     ($XDG_RUNTIME_DIR, or a private folder in the temporary folder);
//   - the holder listens on a local socket (a named pipe on Windows) named
//     for the user and the session, taking a socket file a crash left;
//   - a start that cannot hand over (the holder is not answering, or the
//     socket is not the user's own) starts its own window without claiming,
//     so startup is never blocked; so does a start that holds the lock but
//     cannot listen, which gives the lock up.
//
// The installer's running-app check is another object, the Global\ mutex
// composition.cpp creates (distribution.md).
//
// Each message is "HIKARISUB-OPEN\1", a 32-bit big-endian length and a
// QDataStream of the files (absolute) and the launch's activation token; the
// holder answers one byte once it has the request.

#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

class QLocalServer;
class QLocalSocket;

namespace hikari::app {

// The arguments of a start that are files: the program's own switches
// (application::parseStartupArguments) are not, a file: URL is its local
// path, and a relative path is taken from `workingDirectory`.
QStringList launchFiles(const QStringList &arguments, const QString &workingDirectory);

class SingleInstance : public QObject {
    Q_OBJECT
public:
    // Where the session's instance is: the socket's (pipe's) name, and the
    // lock (a file path, or on Windows the mutex's name).
    struct Location {
        QString server;
        QString lock;
    };
    // This user's and this session's: on Windows named for the user's SID
    // and the session id; elsewhere for the user and the display
    // ($WAYLAND_DISPLAY, else $DISPLAY). Empty when no private folder could
    // be had.
    static Location forSession();

    // What a start hands over.
    struct Request {
        QStringList files;
        QString activationToken; // XDG_ACTIVATION_TOKEN on Wayland
    };

    enum class Role {
        Primary,   // this start holds the instance and listens
        Forwarded, // the instance took the request: this start exits
        Alone,     // none could be reached: this start runs without claiming
    };

    explicit SingleInstance(Location location, QObject *parent = nullptr);
    ~SingleInstance() override;

    // Claims the instance or hands `request` to its holder, waiting at most
    // `timeoutMs` for an answer (the holder may still be starting). Without
    // `handOver` a start that cannot claim runs Alone.
    Role claim(const Request &request, bool handOver = true, int timeoutMs = 5000);
    // The holder stops listening and gives the instance up (before an
    // update starts its new version, and at exit).
    void release();
    bool listening() const;

signals:
    // A later start's request, on the holder.
    void requested(const hikari::app::SingleInstance::Request &request);

private:
    bool tryLock();
    void unlock();
    bool listen();
    Role serve();
    bool handOver(const Request &request, int timeoutMs);
    void accept();
    void read(QLocalSocket *socket);

    Location m_location;
    std::unique_ptr<QLocalServer> m_server;
#ifdef _WIN32
    void *m_mutex = nullptr;
    bool m_owned = false;
#else
    int m_lockFd = -1;
#endif
};

} // namespace hikari::app

Q_DECLARE_METATYPE(hikari::app::SingleInstance::Request)
