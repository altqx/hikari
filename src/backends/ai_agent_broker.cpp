#include "hikari/backends/ai_agent_broker.h"

#include "hikari/backends/ai_sidecar_host.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QProcess>

#include <algorithm>
#include <array>
#include <set>
#include <string_view>

#ifndef Q_OS_WIN
#include <csignal>
#include <sys/types.h>
#endif
#ifdef Q_OS_LINUX
#include <sys/prctl.h>
#endif

namespace hikari::backends::ai {

namespace {

constexpr qsizetype kMaxLineBytes = 64 << 20;

constexpr std::array kRefusedClaudeFlags = {
    "--dangerously-skip-permissions", "--allow-dangerously-skip-permissions", "--add-dir", "--plugin-dir",
    "--plugin-dir-no-mcp", "--settings", "--managed-settings",
};

constexpr std::array kCodexClientMethods = {
    "initialize", "initialized", "thread/start", "thread/resume", "thread/fork", "thread/read", "thread/list",
    "thread/loaded/list", "thread/turns/list", "thread/items/list", "thread/unsubscribe", "thread/archive",
    "thread/unarchive", "thread/name/set", "thread/compact/start", "turn/start", "turn/steer", "turn/interrupt",
    "model/list", "account/read", "account/rateLimits/read", "getAuthStatus", "mcpServerStatus/list",
};
constexpr std::array kCodexThreadMethods = {"thread/start", "thread/resume", "thread/fork"};

template <std::size_t N>
bool listed(const std::array<const char *, N> &names, std::string_view name)
{
    return std::ranges::any_of(names, [&](const char *n) { return name == n; });
}

bool onlySdkServers(const QString &config)
{
    const auto parsed = api::Json::parse(config.toStdString());
    const api::Json *servers = parsed ? parsed->find("mcpServers") : nullptr;
    if (!servers || !servers->isObject())
        return false;
    return std::ranges::all_of(servers->asObject(), [](const api::Json::Member &server) {
        const api::Json *type = server.value.find("type");
        return type && *type == api::Json("sdk");
    });
}

QByteArray line(const api::Json &message)
{
    return QByteArray::fromStdString(message.dump());
}

api::Json errorReply(const api::Json &id, int code, std::string message)
{
    return api::Json::object({{"id", id}, {"error", api::Json::object({{"code", code}, {"message", std::move(message)}})}});
}

class CodexGuard final : public AgentGuard {
public:
    Lines fromSidecar(const QByteArray &text) override
    {
        auto parsed = api::Json::parse(text.toStdString());
        if (!parsed || !parsed->isObject())
            return {};
        api::Json message = std::move(*parsed);
        const api::Json *id = message.find("id");
        const api::Json *method = message.find("method");
        if (!method) {
            if (!id || !m_forwarded.erase(id->dump()))
                return {};
            return {{line(message)}, {}};
        }
        const std::string name = method->isString() ? method->asString() : std::string();
        if (!listed(kCodexClientMethods, name)) {
            if (!id)
                return {};
            return {{}, {line(errorReply(*id, rpc::kMethodNotFound, "Hikari doesn't let the AI use " + name))}};
        }
        const bool thread = listed(kCodexThreadMethods, name);
        if (const api::Json *params = message.find("params"); params && params->isObject() &&
                                                              (thread || name == "turn/start")) {
            api::Json &p = message["params"];
            std::erase_if(p.asObject(), [](const api::Json::Member &m) {
                return m.key == "config" || m.key == "approvalsReviewer";
            });
            p["approvalPolicy"] = "untrusted";
            if (thread)
                p["sandbox"] = "read-only";
            else
                p["sandboxPolicy"] = api::Json::object({{"type", "readOnly"}, {"networkAccess", false}});
        }
        return {{line(message)}, {}};
    }

    Lines fromAgent(const QByteArray &text) override
    {
        const auto parsed = api::Json::parse(text.toStdString());
        if (!parsed || !parsed->isObject())
            return {{}, {text}};
        const api::Json *id = parsed->find("id");
        const api::Json *method = parsed->find("method");
        if (!id || !method || !method->isString())
            return {{}, {text}};
        const std::string name = method->asString();
        const auto answer = [&](api::Json result) {
            return Lines{{line(api::Json::object({{"id", *id}, {"result", std::move(result)}}))}, {}};
        };
        if (name == "item/commandExecution/requestApproval" || name == "item/fileChange/requestApproval")
            return answer(api::Json::object({{"decision", "decline"}}));
        if (name == "execCommandApproval" || name == "applyPatchApproval")
            return answer(api::Json::object(
                {{"decision", api::Json::object({{"denied", api::Json::object({{"rejection", kDeclined}})}})}}));
        if (name == "item/permissions/requestApproval")
            return answer(api::Json::object({{"permissions", api::Json::object()}, {"scope", "turn"}}));
        m_forwarded.insert(id->dump());
        return {{}, {text}};
    }

private:
    static constexpr const char *kDeclined = "Hikari's AI can't run commands or change files.";
    std::set<std::string> m_forwarded;
};

}

QString refusedClaudeArguments(const QStringList &arguments)
{
    std::optional<bool> toolsOff;
    bool strictMcp = false;
    for (qsizetype i = 0; i < arguments.size(); ++i) {
        const QString &argument = arguments[i];
        const qsizetype equals = argument.indexOf(QLatin1Char('='));
        const QString name = equals < 0 ? argument : argument.first(equals);
        const QString value = equals < 0 ? arguments.value(i + 1) : argument.mid(equals + 1);
        if (listed(kRefusedClaudeFlags, name.toStdString()))
            return QStringLiteral("%1 isn't allowed").arg(name);
        if (name == QLatin1String("--permission-mode") && value == QLatin1String("bypassPermissions"))
            return QStringLiteral("--permission-mode bypassPermissions isn't allowed");
        if (name == QLatin1String("--tools"))
            toolsOff = value.isEmpty();
        if (name == QLatin1String("--strict-mcp-config"))
            strictMcp = true;
        if (name == QLatin1String("--mcp-config") && !onlySdkServers(value))
            return QStringLiteral("--mcp-config may only name Hikari's in-process servers");
    }
    if (!toolsOff.value_or(false))
        return QStringLiteral("Claude's built-in tools must be off (--tools=)");
    if (!strictMcp)
        return QStringLiteral("--strict-mcp-config is required");
    return {};
}

std::unique_ptr<AgentGuard> codexGuard()
{
    return std::make_unique<CodexGuard>();
}

namespace {

QProcessEnvironment privateTemporary(const QString &work)
{
    const QString temporary = QDir::toNativeSeparators(work + QStringLiteral("/tmp"));
    QDir().mkpath(temporary);
    QProcessEnvironment environment;
    for (const char *name : {"TMPDIR", "TEMP", "TMP"})
        environment.insert(QString::fromLatin1(name), temporary);
    return environment;
}

bool isScript(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) && file.read(2) == "#!";
}

QString codexBinary(const QString &program)
{
    const QFileInfo found(program);
    const QString installed = found.canonicalFilePath();
    QString scope;
    if (found.suffix().compare(QLatin1String("cmd"), Qt::CaseInsensitive) == 0)
        scope = found.absolutePath() + QStringLiteral("/node_modules/@openai");
    else if (isScript(installed))
        scope = QFileInfo(installed).absolutePath() + QStringLiteral("/../..");
    else
        return installed;
#ifdef Q_OS_WIN
    const QString name = QStringLiteral("codex.exe");
#else
    const QString name = QStringLiteral("codex");
#endif
    // npm's launcher runs <@openai>/codex-<platform>/vendor/<target>/bin/codex, or codex/vendor/… in older packages.
    const QDir packages(scope);
    for (const QString &package : packages.entryList({QStringLiteral("codex-*"), QStringLiteral("codex")}, QDir::Dirs)) {
        const QDir vendor(packages.filePath(package + QStringLiteral("/vendor")));
        for (const QString &target : vendor.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            if (const QFileInfo binary(vendor.filePath(target + QStringLiteral("/bin/") + name)); binary.isExecutable())
                return binary.canonicalFilePath();
    }
    return {};
}

}

AgentSpec claudeAgent(const QString &program, const QString &home, const QString &work)
{
    const QString installed = QFileInfo(program).canonicalFilePath();
    AgentSpec spec;
    spec.program = installed;
    spec.profile = {.name = QStringLiteral("HikariSub.AI.Claude"),
                    .readExecute = {QFileInfo(installed).absolutePath()},
                    .readWrite = {home + QStringLiteral("/.claude"), home + QStringLiteral("/.claude.json"), work},
                    .network = sandbox::Network::Any};
    spec.workingDirectory = work;
    spec.environment = privateTemporary(work);
    spec.sidecarEnvironment = {QStringLiteral("CLAUDE_CODE_ENTRYPOINT"), QStringLiteral("CLAUDE_AGENT_SDK_VERSION"),
                               QStringLiteral("CLAUDE_CODE_SDK_READS_SESSION_STATE"),
                               QStringLiteral("CLAUDE_CODE_SDK_READS_STARTUP_MARKS")};
    spec.refuse = refusedClaudeArguments;
    return spec;
}

std::optional<AgentSpec> codexAgent(const QString &program, const QString &codexHome, const QString &work)
{
    const QString binary = codexBinary(program);
    if (binary.isEmpty())
        return std::nullopt;
    QDir().mkpath(codexHome);
    AgentSpec spec;
    spec.program = binary;
    spec.arguments = {QStringLiteral("app-server")};
    spec.profile = {.name = QStringLiteral("HikariSub.AI.Codex"),
                    .readExecute = {QFileInfo(binary).absolutePath()},
                    .readWrite = {codexHome, work},
                    .network = sandbox::Network::Any};
    spec.workingDirectory = work;
    spec.environment = privateTemporary(work);
    spec.environment.insert(QStringLiteral("CODEX_HOME"), QDir::toNativeSeparators(codexHome));
    spec.refuse = [](const QStringList &arguments) {
        return arguments.isEmpty() ? QString() : QStringLiteral("Codex takes no arguments from the sidecar");
    };
    spec.guard = codexGuard;
    return spec;
}

struct AgentBroker::Running {
    std::uint64_t id = 0;
    std::unique_ptr<QProcess> process;
    std::unique_ptr<sandbox::Launcher> launcher;
    std::unique_ptr<AgentGuard> guard;
    QByteArray fromSidecar;
    QByteArray fromAgent;
    qint64 pid = 0;

    void kill()
    {
        process->disconnect();
#ifdef Q_OS_WIN
        launcher->killTree();
#else
        if (pid > 0)
            ::kill(-static_cast<pid_t>(pid), SIGKILL);
#endif
        if (process->state() != QProcess::NotRunning) {
            process->kill();
            process->waitForFinished(2000);
        }
    }
};

namespace {

/// Splits complete lines off the front of `buffer`.
std::vector<QByteArray> takeLines(QByteArray &buffer)
{
    std::vector<QByteArray> lines;
    qsizetype start = 0;
    for (qsizetype newline = buffer.indexOf('\n'); newline >= 0; newline = buffer.indexOf('\n', start)) {
        lines.push_back(buffer.sliced(start, newline - start));
        start = newline + 1;
    }
    buffer.remove(0, start);
    return lines;
}

QByteArray joined(const std::vector<QByteArray> &lines)
{
    QByteArray out;
    for (const QByteArray &l : lines)
        out += l + '\n';
    return out;
}

}

AgentBroker::AgentBroker(QObject *parent) : QObject(parent) {}

AgentBroker::~AgentBroker()
{
    killAll();
}

void AgentBroker::add(const std::string &agent, AgentSpec spec)
{
    m_specs.insert_or_assign(agent, std::move(spec));
}

void AgentBroker::attach(AiSidecarHost &host)
{
    for (const char *method : {kAgentStart, kAgentWrite, kAgentCloseInput, kAgentKill})
        host.handle(method, [broker = QPointer(this), method](const api::Json &params,
                                                              std::shared_ptr<SidecarRequest> request) {
            if (!broker)
                return request->fail(rpc::kBusy, "agents aren't available");
            broker->call(method, params, [request](std::expected<api::Json, std::pair<int, std::string>> result) {
                if (result)
                    request->respond(std::move(*result));
                else
                    request->fail(result.error().first, std::move(result.error().second));
            });
        });
    setNotify([host = QPointer(&host)](const std::string &method, api::Json params) {
        if (host)
            host->notify(method, std::move(params));
    });
    for (auto signal : {&AiSidecarHost::lost, &AiSidecarHost::refused})
        connect(&host, signal, this, &AgentBroker::killAll);
    connect(&host, &AiSidecarHost::finished, this, &AgentBroker::killAll);
    connect(&host, &AiSidecarHost::stopped, this, &AgentBroker::killAll);
    connect(&host, &QObject::destroyed, this, &AgentBroker::killAll);
}

void AgentBroker::call(const std::string &method, const api::Json &params, const Answer &answer)
{
    if (method == kAgentStart)
        return start(params, answer);
    Running *running = find(params);
    if (!running)
        return answer(std::unexpected(std::pair{rpc::kInvalidParams, std::string("no such agent process")}));
    if (method == kAgentWrite) {
        const api::Json *data = params.find("data");
        auto decoded = data && data->isString()
                           ? QByteArray::fromBase64Encoding(QByteArray::fromStdString(data->asString()),
                                                            QByteArray::AbortOnBase64DecodingErrors)
                           : QByteArray::FromBase64Result{};
        if (!decoded)
            return answer(std::unexpected(std::pair{rpc::kInvalidParams, std::string("data must be base64")}));
        write(*running, *decoded);
    } else if (method == kAgentCloseInput) {
        running->process->closeWriteChannel();
    } else if (method == kAgentKill) {
        const std::uint64_t id = running->id;
        running->kill();
        exited(id, -1);
    } else {
        return answer(std::unexpected(std::pair{rpc::kMethodNotFound, "unknown method " + method}));
    }
    answer(api::Json::object());
}

AgentBroker::Running *AgentBroker::find(const api::Json &params)
{
    const api::Json *process = params.find("process");
    if (!process || !process->isInt())
        return nullptr;
    const auto it = m_running.find(static_cast<std::uint64_t>(process->asInt()));
    return it == m_running.end() ? nullptr : it->second.get();
}

void AgentBroker::start(const api::Json &params, const Answer &answer)
{
    const auto refuse = [&](int code, std::string why) { answer(std::unexpected(std::pair{code, std::move(why)})); };
    const api::Json *agent = params.find("agent");
    const auto spec = agent && agent->isString() ? m_specs.find(agent->asString()) : m_specs.end();
    if (spec == m_specs.end())
        return refuse(rpc::kInvalidParams, "Hikari doesn't know that agent");
    const api::Json *number = params.find("process");
    if (!number || !number->isInt() || number->asInt() <= 0)
        return refuse(rpc::kInvalidParams, "process must be a positive number");
    const auto id = static_cast<std::uint64_t>(number->asInt());
    if (m_running.contains(id))
        return refuse(rpc::kInvalidParams, "that process is already running");
    if (m_running.size() >= kMaxRunning)
        return refuse(rpc::kBusy, "too many agents are running");
    QStringList arguments;
    if (const api::Json *args = params.find("args")) {
        if (!args->isArray())
            return refuse(rpc::kInvalidParams, "args must be strings");
        for (const api::Json &a : args->asArray()) {
            if (!a.isString())
                return refuse(rpc::kInvalidParams, "args must be strings");
            arguments << QString::fromStdString(a.asString());
        }
    }
    if (spec->second.refuse) {
        if (const QString why = spec->second.refuse(arguments); !why.isEmpty())
            return refuse(rpc::kInvalidParams, why.toStdString());
    }
    QProcessEnvironment environment = sidecarEnvironment(QProcessEnvironment::systemEnvironment());
    environment.insert(spec->second.environment);
    if (const api::Json *env = params.find("env"); env && env->isObject())
        for (const auto &[name, value] : env->asObject())
            if (value.isString() && spec->second.sidecarEnvironment.contains(QString::fromStdString(name)))
                environment.insert(QString::fromStdString(name), QString::fromStdString(value.asString()));

    auto running = std::make_unique<Running>();
    running->id = id;
    running->process = std::make_unique<QProcess>();
    running->launcher = std::make_unique<sandbox::Launcher>(spec->second.profile);
    if (spec->second.guard)
        running->guard = spec->second.guard();
    QProcess &process = *running->process;
    process.setProgram(spec->second.program);
    process.setArguments(spec->second.arguments + arguments);
    process.setWorkingDirectory(spec->second.workingDirectory);
    process.setProcessEnvironment(environment);
    process.setProcessChannelMode(QProcess::SeparateChannels);
#ifndef Q_OS_WIN
    process.setUnixProcessParameters(QProcess::UnixProcessFlag::CreateNewSession |
                                     QProcess::UnixProcessFlag::CloseFileDescriptors |
                                     QProcess::UnixProcessFlag::ResetSignalHandlers);
#endif
#ifdef Q_OS_LINUX
    running->launcher->prepare(process, [] { ::prctl(PR_SET_PDEATHSIG, SIGKILL); });
#else
    running->launcher->prepare(process);
#endif
    connect(&process, &QProcess::readyReadStandardOutput, this,
            [this, id, &process] { output(id, process.readAllStandardOutput(), false); });
    connect(&process, &QProcess::readyReadStandardError, this,
            [this, id, &process] { output(id, process.readAllStandardError(), true); });
    connect(&process, &QProcess::started, this, [r = running.get(), &process] { r->pid = process.processId(); });
    connect(&process, &QProcess::finished, this,
            [this, id](int code, QProcess::ExitStatus status) { exited(id, status == QProcess::NormalExit ? code : -1); });
    connect(&process, &QProcess::errorOccurred, this, [this, id](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            exited(id, -1);
    });
    m_running.emplace(id, std::move(running));
    answer(api::Json::object());
    process.start();
}

void AgentBroker::write(Running &running, const QByteArray &data)
{
    if (!running.guard) {
        running.process->write(data);
        return;
    }
    running.fromSidecar += data;
    if (running.fromSidecar.size() > kMaxLineBytes) {
        const std::uint64_t id = running.id;
        running.kill();
        return exited(id, -1);
    }
    std::vector<QByteArray> back;
    for (const QByteArray &l : takeLines(running.fromSidecar)) {
        AgentGuard::Lines lines = running.guard->fromSidecar(l);
        running.process->write(joined(lines.toAgent));
        back.insert(back.end(), lines.toSidecar.begin(), lines.toSidecar.end());
    }
    if (!back.empty() && m_notify)
        m_notify(kAgentOutput, api::Json::object({{"process", running.id}, {"stream", "stdout"},
                                                  {"data", joined(back).toBase64().toStdString()}}));
}

void AgentBroker::output(std::uint64_t id, const QByteArray &data, bool stderrData)
{
    const auto it = m_running.find(id);
    if (it == m_running.end() || !m_notify)
        return;
    Running &running = *it->second;
    QByteArray out = data;
    if (running.guard && !stderrData) {
        running.fromAgent += data;
        if (running.fromAgent.size() > kMaxLineBytes) {
            running.kill();
            return exited(id, -1);
        }
        std::vector<QByteArray> forward;
        for (const QByteArray &l : takeLines(running.fromAgent)) {
            AgentGuard::Lines lines = running.guard->fromAgent(l);
            running.process->write(joined(lines.toAgent));
            forward.insert(forward.end(), lines.toSidecar.begin(), lines.toSidecar.end());
        }
        out = joined(forward);
    }
    if (!out.isEmpty())
        m_notify(kAgentOutput, api::Json::object({{"process", id}, {"stream", stderrData ? "stderr" : "stdout"},
                                                  {"data", out.toBase64().toStdString()}}));
}

void AgentBroker::exited(std::uint64_t id, int code)
{
    const auto it = m_running.find(id);
    if (it == m_running.end())
        return;
    std::unique_ptr<Running> running = std::move(it->second);
    m_running.erase(it);
    running->process->disconnect(this);
    if (m_notify)
        m_notify(kAgentExit, api::Json::object({{"process", id}, {"code", code}}));
    running->process.release()->deleteLater();
}

void AgentBroker::killAll()
{
    auto running = std::exchange(m_running, {});
    for (auto &[id, r] : running) {
        r->kill();
        r->process.release()->deleteLater();
    }
}

}
