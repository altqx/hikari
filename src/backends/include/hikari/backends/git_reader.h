#pragma once

#include "hikari/application/vcs.h"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

namespace hikari::backends {

// Runs the git on PATH read-only: a fixed set of plumbing commands with the system config,
// pager, hooks, fsmonitor, signatures and every transport off, each under a time and size cap.
class GitReader : public QObject, public application::VcsPort {
public:
    struct Limits {
        int timeoutMs = 10'000;
        qint64 fileBytes = 16 << 20;
    };

    explicit GitReader(QObject *parent = nullptr);

    // Empty: git isn't installed. QStandardPaths::findExecutable("git") when unset.
    void setGit(std::optional<QString> program) { m_git = std::move(program); }
    void setLimits(Limits limits) { m_limits = limits; }

    void refs(const std::string &at, int commits,
              std::function<void(std::expected<application::VcsRefs, application::VcsFailure>)> done) override;
    void show(const std::string &file, const std::string &ref,
              std::function<void(std::expected<application::VcsFile, application::VcsFailure>)> done) override;

    struct Output {
        bool started = false, timedOut = false, tooLarge = false;
        int exitCode = -1;
        QByteArray out, err;
    };

private:
    using Ran = std::function<void(Output)>;
    void git(const QString &folder, const QStringList &args, qint64 cap, const QByteArray &input, Ran done);
    QString program() const;

    std::optional<QString> m_git;
    Limits m_limits;
};

}
