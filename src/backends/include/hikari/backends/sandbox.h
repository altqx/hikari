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
    /// HikariSub's own code and runtimes only: on Windows every AppContainer may read and run them.
    QStringList readExecute;
    QStringList read;
    QStringList readWrite;
    Network network = Network::None;
    /// On Windows, run through this helper with a restricted Low-integrity token on a private desktop instead of in
    /// the AppContainer; the process may then read what the user can, and write only `readWrite`. Ignored elsewhere.
    std::optional<QString> lowRightsHelper;
};

/// The fixed profiles HikariSub's agents, AI sidecar and Lua nodes run in; plugins' profiles aren't among them.
enum class OwnProfile { Sidecar, Codex, Claude, Acp, OpenCode, Antigravity, LuaNodes, Count };

inline QString profileName(OwnProfile profile)
{
    switch (profile) {
    case OwnProfile::Sidecar:
        return QStringLiteral("HikariSub.AI.Sidecar");
    case OwnProfile::Codex:
        return QStringLiteral("HikariSub.AI.Codex");
    case OwnProfile::Claude:
        return QStringLiteral("HikariSub.AI.Claude");
    case OwnProfile::Acp:
        return QStringLiteral("HikariSub.AI.Acp");
    case OwnProfile::OpenCode:
        return QStringLiteral("HikariSub.AI.OpenCode");
    case OwnProfile::Antigravity:
        return QStringLiteral("HikariSub.AI.Antigravity");
    case OwnProfile::LuaNodes:
        return QStringLiteral("HikariSub.Lua.Nodes");
    case OwnProfile::Count:
        break;
    }
    return {};
}

inline QStringList ownProfileNames()
{
    QStringList names;
    for (int i = 0; i < int(OwnProfile::Count); ++i)
        names << profileName(OwnProfile(i));
    return names;
}

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
/// first instruction; a low-rights profile's job also forbids UI access outside its desktop. While no process of a
/// low-rights profile runs, its `readWrite` folders are closed to every low-rights process.
class Launcher {
public:
    explicit Launcher(std::optional<Profile> profile);
    ~Launcher();
    Launcher(const Launcher &) = delete;
    Launcher &operator=(const Launcher &) = delete;

    const Coverage &coverage() const;
    /// The AppContainer's SID as text ("S-1-15-2-…") on Windows; empty for a low-rights profile, without a container
    /// or elsewhere.
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
