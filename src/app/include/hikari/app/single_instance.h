#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

class QLocalServer;
class QLocalSocket;

namespace hikari::app {

// Drops the program's own switches; a file: URL becomes its local path, and a relative path
// resolves against `workingDirectory`.
QStringList launchFiles(const QStringList &arguments, const QString &workingDirectory);

// Adds `files` to those already kept at `path`; false when they could not be written.
bool saveHandedOn(const QString &path, const QStringList &files);
// Removes the file at `path`; files kept more than `maxAgeSeconds` ago are dropped.
QStringList takeHandedOn(const QString &path, qint64 maxAgeSeconds = 3600);

class SingleInstance : public QObject {
    Q_OBJECT
public:
    // server: the socket (pipe) name; lock: a file path, or on Windows the mutex name.
    struct Location {
        QString server;
        QString lock;
    };
    // Per user and display (on Windows per SID, session and integrity level); empty on failure.
    static Location forSession();
    // False on Windows above medium integrity or when the level cannot be read; true elsewhere.
    static bool trustsUserFiles();

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

    // Waits up to `timeoutMs`, plus up to 200 ms for a commit's receipt. Without `handOver` a
    // start that cannot claim runs Alone.
    Role claim(const Request &request, bool handOver = true, int timeoutMs = 5000);
    // For a start running Alone: claims the instance once its lock is free; true once listening.
    bool reclaim();
    // Stops listening, drops the requests not taken yet and gives the instance up.
    void release();
    bool listening() const;

signals:
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
    QElapsedTimer m_clock;
#ifdef _WIN32
    void *m_mutex = nullptr;
    bool m_owned = false;
#else
    int m_lockFd = -1;
#endif
};

} // namespace hikari::app

Q_DECLARE_METATYPE(hikari::app::SingleInstance::Request)
