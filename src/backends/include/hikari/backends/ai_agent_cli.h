#pragma once

#include "hikari/backends/ai_channel.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#include <functional>
#include <map>
#include <memory>
#include <optional>

namespace hikari::backends::ai {

/// How the user installed an agent CLI, proven from where its binary really lives (docs/research/cli-updates.md).
enum class AgentInstallMethod { Native, Npm, Pnpm, Bun, Yarn, Volta, Homebrew, WinGet, Chocolatey, Mise, Unknown };

/// Where the latest version of an install is read from.
enum class AgentLatestSource { Npm, HomebrewCask, HomebrewFormula, ClaudeChannel };

/// What a detected install allows: an update HikariSub may run, or a command for the user to copy.
struct AgentInstall {
    AgentInstallMethod method = AgentInstallMethod::Unknown;
    /// Empty when HikariSub never runs the update itself.
    QString program;
    QStringList arguments;
    std::map<QString, QString> environment;
    /// For the user to run; empty when HikariSub can't name one.
    QString manualCommand;
    AgentLatestSource latest = AgentLatestSource::Npm;
    /// The npm package, cask or formula the latest version is read from.
    QString package;
};

/// What detection reads from the machine; tests point it at fake install layouts.
struct AgentHost {
    QString home;
    QString voltaHome;
    bool windows = false;
    /// A tool by name, looked for in `dirs` first and then on PATH; empty when missing.
    std::function<QString(const QString &name, const QStringList &dirs)> findTool;
};
AgentHost currentAgentHost();

/// `agent` is "claude" or "codex"; `path` is what PATH resolves, `realPath` the same with its links resolved.
AgentInstall detectAgentInstall(const QString &agent, const QString &path, const QString &realPath, const AgentHost &host);

/// The first x.y.z[-pre] in a CLI's --version output, or empty.
QString versionIn(const QString &output);

struct AgentCli {
    QString agent;
    QString path;
    QString realPath;
    QString version;
};

/// Runs `path --version`; calls back with the version, or empty when it didn't answer.
void agentVersion(const QString &path, std::function<void(QString)> done);

/// Where `agent` ("claude" or "codex") is: on `dirs`, or on PATH when `dirs` is empty. Empty when it isn't found.
QString findAgentCli(const QString &agent, const QStringList &dirs = {});
/// Finds `agent` with findAgentCli and asks it for its version. Nullopt when it isn't there.
void probeAgentCli(const QString &agent, std::function<void(std::optional<AgentCli>)> done, const QStringList &dirs = {});

enum class AgentBadge { Current, UpdateAvailable, Unsupported, KnownBroken, Unknown };
AgentBadge agentBadge(const QString &installed, const QString &latest, const AgentCompat &compat);

/// Claude's autoUpdatesChannel from <home>/.claude/settings.json: "latest" or "stable".
QString claudeUpdateChannel(const QString &home);

/// Where AgentLatestVersions asks; tests point it at a local server.
struct AgentLatestSources {
    QString npm = QStringLiteral("https://registry.npmjs.org/");
    QString homebrew = QStringLiteral("https://formulae.brew.sh/api/");
    QString claude = QStringLiteral("https://downloads.claude.ai/claude-code-releases/");
};

/// Asks each install method's own source for the latest version, at most once a day per source, and keeps the answers
/// in a small JSON cache.
class AgentLatestVersions final : public QObject {
    Q_OBJECT
public:
    explicit AgentLatestVersions(QString cacheFile, AgentLatestSources sources = {}, QObject *parent = nullptr);

    /// `channel` is Claude's autoUpdatesChannel, for installs from Claude's installer. Answers from the cache when it is
    /// less than a day old, and with an empty version when the source couldn't be read.
    void latest(const AgentInstall &install, const QString &channel, std::function<void(QString)> done);

    QUrl url(const AgentInstall &install, const QString &channel) const;

    static constexpr qint64 kMaxAgeSeconds = 24 * 60 * 60;
    static constexpr qint64 kMaxAnswerBytes = 1 << 20;

private:
    QString m_cacheFile;
    AgentLatestSources m_sources;
    QNetworkAccessManager m_network;
};

/// Runs an install's update outside any sandbox, then asks the CLI for its version again. An unchanged version counts as
/// a failure: package managers can exit 0 without updating.
class AgentUpdate final : public QObject {
    Q_OBJECT
public:
    enum class Outcome { Updated, Unchanged, Failed, TimedOut, Cancelled };
    Q_ENUM(Outcome)

    explicit AgentUpdate(QObject *parent = nullptr);
    ~AgentUpdate() override;

    /// `cliPath` is asked for --version afterwards; `before` is the version it reported first.
    void start(const AgentInstall &install, const QString &cliPath, const QString &before);
    void cancel();
    bool running() const { return m_process != nullptr; }

    /// claude update prints nothing while it downloads about 300 MB, and killing it mid-download leaves a broken binary.
    static constexpr int kTimeoutMs = 30 * 60 * 1000;
    void setTimeoutMs(int ms) { m_timeoutMs = ms; }

signals:
    /// `lastLine` is the update's last line of output, shown on a failure.
    void finished(hikari::backends::ai::AgentUpdate::Outcome outcome, QString version, QString lastLine);

private:
    void exited(int code, QProcess::ExitStatus status);
    void finish(Outcome outcome, const QString &version);

    std::unique_ptr<QProcess> m_process;
    QTimer m_timeout;
    QByteArray m_output;
    QString m_cliPath;
    QString m_before;
    int m_timeoutMs = kTimeoutMs;
    bool m_cancelled = false;
    bool m_timedOut = false;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

}
