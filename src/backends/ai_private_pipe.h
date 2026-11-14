#pragma once

#include <QString>
#include <QtGlobal>

#include <functional>
#include <memory>

class QLocalSocket;

namespace hikari::backends::ai {

/// A named pipe that only the user, their Low-integrity processes and at most one AppContainer may open, refusing
/// remote clients: it hands its first client that `admits` accepts to `accepted` and then takes no other. Windows
/// only.
class PrivatePipe {
public:
    using Admits = std::function<bool(qint64 pid)>;
    using Accepted = std::function<void(QLocalSocket *socket)>;

    PrivatePipe(Admits admits, Accepted accepted);
    ~PrivatePipe();
    PrivatePipe(const PrivatePipe &) = delete;
    PrivatePipe &operator=(const PrivatePipe &) = delete;

    /// `name` is the full `\\.\pipe\…` path; an empty `containerSid` admits no AppContainer. False with `error` set
    /// when the pipe can't be made exactly so.
    bool listen(const QString &name, const QString &containerSid, QString *error);

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

}
