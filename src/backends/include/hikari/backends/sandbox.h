#pragma once

#include <QProcess>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>

namespace hikari::backends::sandbox {

enum class Network { None, Any };

/// What a confined process may reach. Paths that don't exist are left out.
struct Profile {
    /// The AppContainer's name on Windows: letters, digits, '.', '-' and '_', at most 64 characters.
    QString name;
    QStringList readExecute;
    QStringList read;
    QStringList readWrite;
    Network network = Network::None;
};

/// What this machine enforces of a profile.
struct Coverage {
    bool files = false;
    bool network = false;
    /// Why something isn't enforced, for the log; empty when everything is.
    QString missing;

    bool complete() const { return files && network; }
};

/// What this machine can enforce of a profile with `network`, without creating or starting anything.
Coverage systemCoverage(Network network);

/// Starts processes confined to a profile, plus what any process needs to run here: on Linux the runtime
/// libraries, TLS roots, a few devices and its own /proc entry; on Windows what Windows grants every AppContainer.
/// Without a profile nothing is confined. On Windows each process runs in a kill-on-close job it joins before its
/// first instruction.
class Launcher {
public:
    explicit Launcher(std::optional<Profile> profile);
    ~Launcher();
    Launcher(const Launcher &) = delete;
    Launcher &operator=(const Launcher &) = delete;

    const Coverage &coverage() const;
    /// The AppContainer's SID as text ("S-1-15-2-…") on Windows; empty without a container or elsewhere.
    QString containerSid() const;
    /// Whether process `pid` runs in a job of a process this launcher started; false elsewhere.
    bool runs(qint64 pid) const;
    /// Sets `process` up to start confined. On Unix `inChild` runs first in the child and must be async-signal-safe.
    void prepare(QProcess &process, void (*inChild)() = nullptr);
    /// Ends every process started through this launcher on Windows; elsewhere the caller kills its process group.
    void killTree();

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

}
