#include "hikari/backends/ai_agent_cli.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

namespace hikari::backends::ai {

namespace {

constexpr int kVersionTimeoutMs = 10'000;
constexpr int kLatestTimeoutMs = 15'000;
constexpr int kKillGraceMs = 5'000;
constexpr qsizetype kOutputTailBytes = 4096;

QString npmPackage(const QString &agent)
{
    return agent == QLatin1String("claude") ? QStringLiteral("@anthropic-ai/claude-code") : QStringLiteral("@openai/codex");
}

QString homebrewName(const QString &agent)
{
    return agent == QLatin1String("claude") ? QStringLiteral("claude-code") : QStringLiteral("codex");
}

QString directoryOf(const QString &path)
{
    return QFileInfo(path).absolutePath();
}

QRegularExpression pattern(const QString &expression, bool windows)
{
    return QRegularExpression(expression, windows ? QRegularExpression::CaseInsensitiveOption
                                                  : QRegularExpression::NoPatternOption);
}

AgentInstall manual(AgentInstallMethod method, QString command, AgentLatestSource latest, QString package)
{
    return {.method = method, .manualCommand = std::move(command), .latest = latest, .package = std::move(package)};
}

AgentInstall packageManager(AgentInstallMethod method, const QString &tool, QStringList arguments, const QString &shown,
                            const QString &package, const AgentHost &host)
{
    AgentInstall install = manual(method, QStringList{shown, arguments.join(QLatin1Char(' '))}.join(QLatin1Char(' ')),
                                  AgentLatestSource::Npm, package);
    // QProcess starts npm, pnpm and yarn .cmd shims only through cmd.exe, whose quoting no path is safe with.
    const bool cmdShim = host.windows && method != AgentInstallMethod::Bun && method != AgentInstallMethod::Volta;
    if (!tool.isEmpty() && !cmdShim) {
        install.program = tool;
        install.arguments = std::move(arguments);
    }
    return install;
}

}

AgentHost currentAgentHost()
{
    const QString volta = qEnvironmentVariable("VOLTA_HOME");
    return {.home = QDir::homePath(),
            .voltaHome = volta.isEmpty() ? QDir::homePath() + QStringLiteral("/.volta") : QDir::fromNativeSeparators(volta),
#ifdef Q_OS_WIN
            .windows = true,
#endif
            .findTool = [](const QString &name, const QStringList &dirs) {
                const QString found = dirs.isEmpty() ? QString() : QStandardPaths::findExecutable(name, dirs);
                return found.isEmpty() ? QStandardPaths::findExecutable(name) : found;
            }};
}

AgentInstall detectAgentInstall(const QString &agent, const QString &path, const QString &realPath, const AgentHost &host)
{
    const auto slashed = [&](QString s) { return host.windows ? s.replace(u'\\', u'/') : s; };
    const QString p = slashed(path);
    const QString r = slashed(realPath.isEmpty() ? path : realPath);
    const QString package = npmPackage(agent);
    const QString latest = package + QStringLiteral("@latest");
    const bool claude = agent == QLatin1String("claude");
    const auto either = [&](const QRegularExpression &re) { return re.match(p).hasMatch() || re.match(r).hasMatch(); };
    const auto find = [&](const QString &tool, const QStringList &dirs) {
        return host.findTool ? host.findTool(tool, dirs) : QString();
    };

    if (claude && host.windows) {
        if (either(pattern(QStringLiteral("/WinGet/(Links|Packages/Anthropic\\.ClaudeCode_[^/]*)/claude\\.exe$"), true)))
            return manual(AgentInstallMethod::WinGet, QStringLiteral("winget upgrade Anthropic.ClaudeCode"),
                          AgentLatestSource::Npm, package);
        if (either(pattern(QStringLiteral("/chocolatey/(bin|lib/claude-code/tools)/claude\\.exe$"), true)))
            return manual(AgentInstallMethod::Chocolatey, QStringLiteral("choco upgrade claude-code"),
                          AgentLatestSource::Npm, package);
    }
    if (either(pattern(QStringLiteral("/mise/(installs|shims|command-wrappers)/"), host.windows)))
        return manual(AgentInstallMethod::Mise, {}, AgentLatestSource::Npm, package);

    const bool fromNodeModules = r.contains(QLatin1String("/node_modules/"));
    if (claude && !fromNodeModules
        && (pattern(QStringLiteral("/\\.local/bin/claude(\\.exe)?$"), host.windows).match(p).hasMatch()
            || either(pattern(QStringLiteral("/\\.local/share/claude/|/\\.claude/local/"), host.windows))))
        return {.method = AgentInstallMethod::Native,
                .program = path,
                .arguments = {QStringLiteral("update")},
                .manualCommand = QStringLiteral("claude update"),
                .latest = AgentLatestSource::ClaudeChannel};
    if (!claude && !fromNodeModules) {
        if (const qsizetype at = r.indexOf(QLatin1String("/packages/standalone/")); at > 0)
            return {.method = AgentInstallMethod::Native,
                    .program = path,
                    .arguments = {QStringLiteral("update")},
                    .environment = {{QStringLiteral("CODEX_HOME"), QDir::toNativeSeparators(r.left(at))}},
                    .manualCommand = QStringLiteral("codex update"),
                    .latest = AgentLatestSource::Npm,
                    .package = package};
    }

    if (either(pattern(QStringLiteral("/\\.bun/(bin|install/global/node_modules)/"), host.windows)))
        return packageManager(AgentInstallMethod::Bun, find(QStringLiteral("bun"), {directoryOf(p)}),
                              {QStringLiteral("i"), QStringLiteral("-g"), latest}, QStringLiteral("bun"), package, host);
    if (either(pattern(QStringLiteral("(/\\.local/share/pnpm|/Library/pnpm|/AppData/Local/pnpm|/pnpm/global)/"),
                       host.windows)))
        return packageManager(AgentInstallMethod::Pnpm, find(QStringLiteral("pnpm"), {directoryOf(p)}),
                              {QStringLiteral("add"), QStringLiteral("-g"), latest}, QStringLiteral("pnpm"), package,
                              host);
    if (pattern(QStringLiteral("/yarn/(data/)?global/node_modules/"), host.windows).match(r).hasMatch())
        return packageManager(AgentInstallMethod::Yarn, find(QStringLiteral("yarn"), {}),
                              {QStringLiteral("global"), QStringLiteral("add"), latest}, QStringLiteral("yarn"),
                              package, host);
    if (QFileInfo(r).baseName() == QLatin1String("volta-shim") && !host.voltaHome.isEmpty()
        && QFileInfo::exists(host.voltaHome + QStringLiteral("/tools/image/packages/") + package))
        return packageManager(AgentInstallMethod::Volta, find(QStringLiteral("volta"), {host.voltaHome + QStringLiteral("/bin")}),
                              {QStringLiteral("install"), latest}, QStringLiteral("volta"), package, host);

    const QString modules = QStringLiteral("/lib/node_modules/") + package + QLatin1Char('/');
    if (const qsizetype at = r.indexOf(modules); at >= 0) {
        const QString prefix = r.left(at);
        return packageManager(AgentInstallMethod::Npm, find(QStringLiteral("npm"), {prefix + QStringLiteral("/bin")}),
                              {QStringLiteral("install"), QStringLiteral("-g"), QStringLiteral("--prefix"),
                               QDir::toNativeSeparators(prefix), latest},
                              QStringLiteral("npm"), package, host);
    }
    if (host.windows
        && QFileInfo::exists(directoryOf(p) + QStringLiteral("/node_modules/") + package + QStringLiteral("/package.json")))
        return packageManager(AgentInstallMethod::Npm, {}, {QStringLiteral("install"), QStringLiteral("-g"), latest},
                              QStringLiteral("npm"), package, host);

    static const QRegularExpression keg(QStringLiteral("^(.*)/(Cellar|Caskroom)/([^/]+)/[^/]+/"));
    if (const QRegularExpressionMatch m = keg.match(r); m.hasMatch()) {
        const QString brew = m.captured(1) + QStringLiteral("/bin/brew");
        const bool cask = m.captured(2) == QLatin1String("Caskroom");
        const QString name = m.captured(3);
        if (QFileInfo(brew).isExecutable()) {
            QStringList arguments{QStringLiteral("upgrade")};
            if (cask)
                arguments << QStringLiteral("--cask");
            arguments << name;
            return {.method = AgentInstallMethod::Homebrew,
                    .program = brew,
                    .arguments = arguments,
                    .manualCommand = QStringLiteral("brew ") + arguments.join(QLatin1Char(' ')),
                    .latest = cask ? AgentLatestSource::HomebrewCask : AgentLatestSource::HomebrewFormula,
                    .package = name};
        }
    }
    return manual(AgentInstallMethod::Unknown, {}, AgentLatestSource::Npm, package);
}

QString versionIn(const QString &output)
{
    static const QRegularExpression version(QStringLiteral("\\b(\\d+\\.\\d+\\.\\d+(?:-[0-9A-Za-z.]+)?)"));
    return version.match(output).captured(1);
}

void agentVersion(const QString &path, std::function<void(QString)> done)
{
    auto *process = new QProcess;
    auto answer = std::make_shared<std::function<void(QString)>>([process, done = std::move(done)](QString version) mutable {
        auto once = std::move(done);
        done = nullptr;
        process->deleteLater();
        if (once)
            once(version);
    });
    QObject::connect(process, &QProcess::finished, process, [process, answer](int code, QProcess::ExitStatus status) {
        const bool ok = status == QProcess::NormalExit && code == 0;
        (*answer)(ok ? versionIn(QString::fromUtf8(process->readAllStandardOutput())) : QString());
    });
    QObject::connect(process, &QProcess::errorOccurred, process, [answer](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            (*answer)({});
    });
    QTimer::singleShot(kVersionTimeoutMs, process, [process] { process->kill(); });
    process->start(path, {QStringLiteral("--version")});
}

QString findAgentCli(const QString &agent, const QStringList &dirs)
{
    return QStandardPaths::findExecutable(agent, dirs);
}

void probeAgentCli(const QString &agent, std::function<void(std::optional<AgentCli>)> done, const QStringList &dirs)
{
    const QString path = findAgentCli(agent, dirs);
    if (path.isEmpty()) {
        done(std::nullopt);
        return;
    }
    const QString realPath = QFileInfo(path).canonicalFilePath();
    agentVersion(path, [agent, path, realPath, done = std::move(done)](QString version) {
        done(AgentCli{.agent = agent, .path = path, .realPath = realPath, .version = std::move(version)});
    });
}

AgentBadge agentBadge(const QString &installed, const QString &latest, const AgentCompat &compat)
{
    if (installed.isEmpty())
        return AgentBadge::Unknown;
    if (compat.broken.contains(installed))
        return AgentBadge::KnownBroken;
    if (!compat.minimum.isEmpty() && compareVersions(installed, compat.minimum) < 0)
        return AgentBadge::Unsupported;
    if (latest.isEmpty())
        return AgentBadge::Unknown;
    return compareVersions(installed, latest) < 0 ? AgentBadge::UpdateAvailable : AgentBadge::Current;
}

QString claudeUpdateChannel(const QString &home)
{
    QFile file(home + QStringLiteral("/.claude/settings.json"));
    if (!file.open(QIODevice::ReadOnly))
        return QStringLiteral("latest");
    const QString channel = QJsonDocument::fromJson(file.read(1 << 20)).object().value(QLatin1String("autoUpdatesChannel")).toString();
    return channel == QLatin1String("stable") ? channel : QStringLiteral("latest");
}

AgentLatestVersions::AgentLatestVersions(QString cacheFile, AgentLatestSources sources, QObject *parent)
    : QObject(parent), m_cacheFile(std::move(cacheFile)), m_sources(std::move(sources))
{
}

QUrl AgentLatestVersions::url(const AgentInstall &install, const QString &channel) const
{
    switch (install.latest) {
    case AgentLatestSource::Npm:
        return QUrl(m_sources.npm + install.package + QStringLiteral("/latest"));
    case AgentLatestSource::HomebrewCask:
        return QUrl(m_sources.homebrew + QStringLiteral("cask/") + install.package + QStringLiteral(".json"));
    case AgentLatestSource::HomebrewFormula:
        return QUrl(m_sources.homebrew + QStringLiteral("formula/") + install.package + QStringLiteral(".json"));
    case AgentLatestSource::ClaudeChannel:
        return QUrl(m_sources.claude + (channel == QLatin1String("stable") ? channel : QStringLiteral("latest")));
    }
    return {};
}

namespace {

QJsonObject readCache(const QString &file)
{
    QFile f(file);
    return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.read(1 << 20)).object() : QJsonObject();
}

void writeCache(const QString &file, const QJsonObject &cache)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QSaveFile f(file);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(cache).toJson(QJsonDocument::Compact));
        f.commit();
    }
}

QString latestIn(AgentLatestSource source, const QByteArray &body)
{
    if (source == AgentLatestSource::ClaudeChannel)
        return versionIn(QString::fromUtf8(body.trimmed()));
    const QJsonObject json = QJsonDocument::fromJson(body).object();
    const QJsonValue value = source == AgentLatestSource::HomebrewFormula
                                 ? json.value(QLatin1String("versions")).toObject().value(QLatin1String("stable"))
                                 : json.value(QLatin1String("version"));
    return versionIn(value.toString());
}

}

void AgentLatestVersions::latest(const AgentInstall &install, const QString &channel, std::function<void(QString)> done)
{
    const QUrl source = url(install, channel);
    const QString key = source.toString();
    const QJsonObject cached = readCache(m_cacheFile).value(key).toObject();
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const qint64 checked = cached.value(QLatin1String("checked")).toInteger();
    if (!cached.isEmpty() && checked <= now && now - checked < kMaxAgeSeconds) {
        done(cached.value(QLatin1String("version")).toString());
        return;
    }
    QNetworkRequest request(source);
    request.setTransferTimeout(kLatestTimeoutMs);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("HikariSub"));
    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64) {
        if (received > kMaxAnswerBytes)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, key, sourceKind = install.latest, stale = cached.value(QLatin1String("version")).toString(),
             done = std::move(done)] {
                reply->deleteLater();
                QString version;
                if (reply->error() == QNetworkReply::NoError)
                    version = latestIn(sourceKind, reply->read(kMaxAnswerBytes));
                if (version.isEmpty())
                    version = stale;
                QJsonObject cache = readCache(m_cacheFile);
                cache.insert(key, QJsonObject{{QStringLiteral("version"), version},
                                              {QStringLiteral("checked"), QDateTime::currentSecsSinceEpoch()}});
                writeCache(m_cacheFile, cache);
                done(version);
            });
}

AgentUpdate::AgentUpdate(QObject *parent) : QObject(parent)
{
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        if (!m_process)
            return;
        if (!m_cancelled)
            m_timedOut = true;
        m_process->kill();
    });
}

AgentUpdate::~AgentUpdate()
{
    if (m_process) {
        m_process->disconnect();
        m_process->kill();
        m_process->waitForFinished(2000);
    }
}

void AgentUpdate::start(const AgentInstall &install, const QString &cliPath, const QString &before)
{
    if (m_process || install.program.isEmpty())
        return;
    m_cliPath = cliPath;
    m_before = before;
    m_output.clear();
    m_cancelled = false;
    m_timedOut = false;
    m_process = std::make_unique<QProcess>();
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    for (const auto &[name, value] : install.environment)
        environment.insert(name, value);
    m_process->setProcessEnvironment(environment);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    m_process->setStandardInputFile(QProcess::nullDevice());
    connect(m_process.get(), &QProcess::readyRead, this, [this] {
        m_output += m_process->readAll();
        if (m_output.size() > kOutputTailBytes)
            m_output = m_output.right(kOutputTailBytes);
    });
    connect(m_process.get(), &QProcess::finished, this, &AgentUpdate::exited);
    connect(m_process.get(), &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            finish(Outcome::Failed, {});
    });
    m_timeout.start(m_timeoutMs);
    m_process->start(install.program, install.arguments);
}

void AgentUpdate::cancel()
{
    if (!m_process || m_cancelled)
        return;
    m_cancelled = true;
#ifdef Q_OS_WIN
    m_process->kill();
#else
    m_process->terminate();
    m_timeout.start(kKillGraceMs);
#endif
}

void AgentUpdate::exited(int code, QProcess::ExitStatus status)
{
    if (m_cancelled)
        return finish(Outcome::Cancelled, {});
    if (m_timedOut)
        return finish(Outcome::TimedOut, {});
    if (status != QProcess::NormalExit || code != 0)
        return finish(Outcome::Failed, {});
    m_timeout.stop();
    agentVersion(m_cliPath, [this, alive = std::weak_ptr(m_alive)](QString version) {
        if (!alive.lock())
            return;
        if (version.isEmpty())
            finish(Outcome::Failed, {});
        else
            finish(version == m_before ? Outcome::Unchanged : Outcome::Updated, version);
    });
}

void AgentUpdate::finish(Outcome outcome, const QString &version)
{
    m_timeout.stop();
    if (m_process) {
        m_process->disconnect(this);
        m_process.release()->deleteLater();
    }
    const QStringList lines = QString::fromUtf8(m_output).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QString lastLine;
    for (auto it = lines.crbegin(); it != lines.crend() && lastLine.isEmpty(); ++it)
        lastLine = it->trimmed();
    emit finished(outcome, version, lastLine);
}

}
