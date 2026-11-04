#include "hikari/backends/ai_sidecar_host.h"

#include <QDir>
#include <QFile>
#include <QLoggingCategory>

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

#ifndef Q_OS_WIN
#include <csignal>
#include <sys/types.h>
#endif
#ifdef Q_OS_LINUX
#include <sys/prctl.h>
#endif

namespace hikari::backends::ai {

namespace {

Q_LOGGING_CATEGORY(lcSandbox, "hikari.ai.sandbox")

constexpr std::array kInherited = {
    "PATH", "HOME", "USER", "LOGNAME", "LANG", "LANGUAGE", "LC_ALL", "LC_CTYPE", "LC_MESSAGES", "TZ", "TMPDIR",
    "TEMP", "TMP", "XDG_RUNTIME_DIR", "SSL_CERT_FILE", "SSL_CERT_DIR", "HTTP_PROXY", "HTTPS_PROXY", "NO_PROXY",
    "http_proxy", "https_proxy", "no_proxy", "SystemRoot", "SystemDrive", "windir", "ComSpec", "PATHEXT",
    "USERPROFILE", "USERNAME", "USERDOMAIN", "HOMEDRIVE", "HOMEPATH", "APPDATA", "LOCALAPPDATA", "ProgramData",
    "ProgramFiles", "ProgramFiles(x86)", "ProgramW6432", "NUMBER_OF_PROCESSORS", "PROCESSOR_ARCHITECTURE",
};

constexpr std::string_view kApiCall = "_hikari/api.call";
constexpr std::string_view kCancelRequest = "$/cancel_request";

api::Json errorBody(int code, std::string message, api::Json data = {})
{
    auto error = api::Json::object({{"code", code}, {"message", std::move(message)}});
    if (!data.isNull())
        error["data"] = std::move(data);
    return api::Json::object({{"error", std::move(error)}});
}

void failApi(SidecarRequest &request, api::Error error)
{
    auto data = api::Json::object({{"code", std::move(error.code)}});
    if (!error.data.isNull())
        data["data"] = std::move(error.data);
    request.fail(rpc::kHikariApiError, std::move(error.message), std::move(data));
}

SidecarFailure failure(SidecarError error, std::string message = {})
{
    return SidecarFailure{error, 0, std::move(message), {}};
}

}

QProcessEnvironment sidecarEnvironment(const QProcessEnvironment &from)
{
    QProcessEnvironment clean;
    for (const char *name : kInherited) {
        const QString key = QString::fromLatin1(name);
        if (from.contains(key))
            clean.insert(key, from.value(key));
    }
    return clean;
}

SidecarRequest::SidecarRequest(AiSidecarHost &host, api::Json id, std::uint64_t generation)
    : m_host(&host), m_alive(host.m_alive), m_id(std::move(id)), m_generation(generation)
{
}

bool SidecarRequest::open() const
{
    const auto alive = m_alive.lock();
    return !m_answered && alive && *alive && m_host->m_generation == m_generation;
}

void SidecarRequest::respond(api::Json result)
{
    if (!open())
        return;
    m_answered = true;
    m_host->answer(*this, api::Json::object({{"result", std::move(result)}}));
}

void SidecarRequest::fail(int code, std::string message, api::Json data)
{
    if (!open())
        return;
    m_answered = true;
    m_host->answer(*this, errorBody(code, std::move(message), std::move(data)));
}

void SidecarRequest::notify(const std::string &method, api::Json params)
{
    if (open())
        m_host->send(api::Json::object({{"jsonrpc", "2.0"}, {"method", method}, {"params", std::move(params)}}));
}

void SidecarRequest::onCancel(std::function<void()> cancel)
{
    m_cancel = std::move(cancel);
}

void SidecarRequest::cancel()
{
    if (auto cancel = std::exchange(m_cancel, {}))
        cancel();
}

AiSidecarHost::AiSidecarHost(Options options, api::Api *hikari, api::Caller caller, QObject *parent)
    : QObject(parent), m_options(std::move(options)), m_hikari(hikari), m_caller(std::move(caller))
{
    preparePrivateFolder();
    m_launcher = std::make_unique<sandbox::Launcher>(m_options.sandbox);
    handle(std::string(kApiCall), [this](const api::Json &params, std::shared_ptr<SidecarRequest> request) {
        callApi(params, std::move(request));
    });
    if (m_options.sandbox && !m_launcher->coverage().missing.isEmpty())
        qCWarning(lcSandbox) << "the AI sidecar's sandbox is incomplete:" << m_launcher->coverage().missing;
    m_clock.start();
    m_handshakeTimer.setSingleShot(true);
    m_startTimer.setSingleShot(true);
    m_killTimer.setSingleShot(true);
    connect(&m_handshakeTimer, &QTimer::timeout, this,
            [this] { lose(QStringLiteral("the sidecar didn't answer initialize in time")); });
    connect(&m_startTimer, &QTimer::timeout, this, &AiSidecarHost::startProcess);
    connect(&m_killTimer, &QTimer::timeout, this, [this] {
        killTree();
        processGone();
    });
}

AiSidecarHost::~AiSidecarHost()
{
    *m_alive = false;
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->disconnect(this);
        m_process->closeWriteChannel();
        m_process->waitForFinished(m_options.shutdownMs);
    }
    killTree();
}

std::uint64_t AiSidecarHost::request(const std::string &method, api::Json params, SidecarReply reply)
{
    const std::uint64_t id = m_nextId++;
    auto message = api::Json::object({{"jsonrpc", "2.0"}, {"id", id}, {"method", method}});
    if (!params.isNull())
        message["params"] = std::move(params);
    const std::size_t bytes = message.dump().size() + 1;
    std::optional<SidecarFailure> refusal;
    if (m_state == State::Stopped)
        refusal = failure(SidecarError::Stopped, "the sidecar crashed too often");
    else if (m_state == State::Refused)
        refusal = failure(SidecarError::Refused, "the sidecar doesn't match this app");
    else if (m_pending.size() >= m_options.maxOutstanding ||
             m_pendingBytes + bytes > m_options.maxOutstandingBytes || bytes > m_options.maxMessageBytes)
        refusal = failure(SidecarError::Busy, "too many requests are outstanding");
    if (refusal) {
        QMetaObject::invokeMethod(this, [alive = std::weak_ptr(m_alive), reply = std::move(reply),
                                         refusal = std::move(*refusal)] {
            if (!alive.expired())
                reply(std::unexpected(refusal));
        }, Qt::QueuedConnection);
        return id;
    }
    m_pending.emplace(id, Pending{method, std::move(reply), bytes, false});
    m_pendingBytes += bytes;
    m_queue.push_back(id);
    m_queuedMessages.emplace(id, std::move(message));
    if (m_state == State::Ready)
        sendQueued();
    else
        start();
    return id;
}

void AiSidecarHost::cancel(std::uint64_t request)
{
    const auto it = m_pending.find(request);
    if (it == m_pending.end())
        return;
    if (it->second.sent) {
        send(api::Json::object({{"jsonrpc", "2.0"}, {"method", kCancelRequest},
                                {"params", api::Json::object({{"requestId", request}})}}));
        return;
    }
    std::erase(m_queue, request);
    m_queuedMessages.erase(request);
    finish(request, std::unexpected(failure(SidecarError::Cancelled, "cancelled")));
}

void AiSidecarHost::handle(const std::string &method, SidecarRequestHandler handler)
{
    m_handlers.insert_or_assign(method, std::move(handler));
}

void AiSidecarHost::notify(const std::string &method, api::Json params)
{
    if (m_state != State::Ready)
        return;
    auto message = api::Json::object({{"jsonrpc", "2.0"}, {"method", method}});
    if (!params.isNull())
        message["params"] = std::move(params);
    send(message);
}

void AiSidecarHost::start()
{
    if (m_state != State::Idle)
        return;
    const qint64 now = m_clock.elapsed();
    std::erase_if(m_crashes, [&](qint64 at) { return now - at > m_options.crashWindowMs; });
    m_state = State::Starting;
    const int delay = m_crashes.empty() ? 0 : m_options.firstBackoffMs << (m_crashes.size() - 1);
    m_startTimer.start(delay);
}

void AiSidecarHost::startProcess()
{
    m_buffer.clear();
    m_diagnostics.clear();
    ++m_generation;
    dropIncoming();
    preparePrivateFolder();
    QProcessEnvironment environment = sidecarEnvironment(m_options.environment);
    if (m_options.sandbox && !m_options.workingDirectory.isEmpty()) {
        const QString temporary = QDir::toNativeSeparators(m_options.workingDirectory + QStringLiteral("/tmp"));
        QDir().mkpath(temporary);
        for (const char *name : {"TMPDIR", "TEMP", "TMP"})
            environment.insert(QString::fromLatin1(name), temporary);
    }
    m_process = std::make_unique<QProcess>();
    m_process->setProgram(m_options.program);
    m_process->setArguments(m_options.arguments);
    m_process->setWorkingDirectory(m_options.workingDirectory);
    m_process->setProcessEnvironment(environment);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
#ifndef Q_OS_WIN
    m_process->setUnixProcessParameters(QProcess::UnixProcessFlag::CreateNewSession |
                                        QProcess::UnixProcessFlag::CloseFileDescriptors |
                                        QProcess::UnixProcessFlag::ResetSignalHandlers);
#endif
#ifdef Q_OS_LINUX
    m_launcher->prepare(*m_process, [] { ::prctl(PR_SET_PDEATHSIG, SIGKILL); });
#else
    m_launcher->prepare(*m_process);
#endif
    connect(m_process.get(), &QProcess::readyReadStandardOutput, this, &AiSidecarHost::readOutput);
    connect(m_process.get(), &QProcess::readyReadStandardError, this, &AiSidecarHost::readDiagnostics);
    connect(m_process.get(), &QProcess::started, this, &AiSidecarHost::handshake);
    connect(m_process.get(), &QProcess::finished, this, &AiSidecarHost::processGone);
    connect(m_process.get(), &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            processGone();
    });
    m_handshakeTimer.start(m_options.handshakeMs);
    m_process->start();
}

void AiSidecarHost::preparePrivateFolder()
{
    if (m_options.workingDirectory.isEmpty())
        return;
    QDir().mkpath(m_options.workingDirectory);
    QFile::setPermissions(m_options.workingDirectory, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
}

void AiSidecarHost::handshake()
{
    m_pid = m_process->processId();
    m_initializeId = m_nextId++;
    send(api::Json::object({{"jsonrpc", "2.0"}, {"id", m_initializeId}, {"method", "initialize"},
                            {"params", api::Json::object({{"protocolVersion", kSidecarProtocolVersion},
                                                          {"hikariApiVersion", m_options.hikariApiVersion},
                                                          {"buildId", m_options.buildId}})}}));
}

void AiSidecarHost::send(const api::Json &message)
{
    if (!m_process || m_process->state() == QProcess::NotRunning)
        return;
    QByteArray line = QByteArray::fromStdString(message.dump());
    line.append('\n');
    m_process->write(line);
}

void AiSidecarHost::sendQueued()
{
    while (!m_queue.empty() && m_state == State::Ready) {
        const std::uint64_t id = m_queue.front();
        m_queue.pop_front();
        const auto message = m_queuedMessages.extract(id);
        m_pending.at(id).sent = true;
        send(message.mapped());
    }
}

void AiSidecarHost::readDiagnostics()
{
    if (!m_process)
        return;
    m_diagnostics += m_process->readAllStandardError();
    const auto limit = static_cast<qsizetype>(m_options.maxDiagnosticBytes);
    if (m_diagnostics.size() > limit)
        m_diagnostics.remove(0, m_diagnostics.size() - limit);
}

void AiSidecarHost::readOutput()
{
    if (!m_process)
        return;
    m_buffer += m_process->readAllStandardOutput();
    const std::uint64_t generation = m_generation;
    qsizetype start = 0;
    for (qsizetype newline = m_buffer.indexOf('\n'); newline >= 0; newline = m_buffer.indexOf('\n', start)) {
        const QByteArray line = m_buffer.sliced(start, newline - start);
        start = newline + 1;
        if (static_cast<std::size_t>(line.size()) > m_options.maxMessageBytes)
            return lose(QStringLiteral("a sidecar message is too large"));
        receive(line);
        if (generation != m_generation || !m_process)
            return;
    }
    m_buffer.remove(0, start);
    if (static_cast<std::size_t>(m_buffer.size()) > m_options.maxMessageBytes)
        lose(QStringLiteral("a sidecar message is too large"));
}

void AiSidecarHost::receive(const QByteArray &line)
{
    const auto parsed = api::Json::parse(std::string_view(line.constData(), static_cast<std::size_t>(line.size())));
    if (!parsed || !parsed->isObject() || !parsed->find("jsonrpc") || *parsed->find("jsonrpc") != api::Json("2.0"))
        return lose(QStringLiteral("the sidecar wrote a line that isn't JSON-RPC"));
    const api::Json &message = *parsed;
    const api::Json *id = message.find("id");
    if (const api::Json *method = message.find("method"); method && method->isString()) {
        if (id)
            return receiveRequest(*id, method->asString(), message.find("params"));
        const api::Json *params = message.find("params");
        if (method->asString() == kCancelRequest) {
            if (const api::Json *request = params ? params->find("requestId") : nullptr)
                cancelIncoming(*request);
        } else if (m_onNotification && m_state == State::Ready) {
            m_onNotification(method->asString(), params ? *params : api::Json());
        }
        return;
    }
    if (!id || !id->isInt() || id->asInt() <= 0 || (!message.find("result") && !message.find("error")))
        return lose(QStringLiteral("the sidecar wrote a message that is neither request nor response"));
    receiveResponse(static_cast<std::uint64_t>(id->asInt()), message);
}

void AiSidecarHost::receiveResponse(std::uint64_t id, const api::Json &message)
{
    if (m_state == State::Starting && id == m_initializeId) {
        const api::Json *result = message.find("result");
        const auto field = [&](std::string_view name) { return result ? result->find(name) : nullptr; };
        const api::Json *protocol = field("protocolVersion");
        const api::Json *apiVersion = field("hikariApiVersion");
        const api::Json *build = field("buildId");
        QString mismatch;
        if (!protocol || *protocol != api::Json(kSidecarProtocolVersion))
            mismatch = QStringLiteral("protocol version");
        else if (!apiVersion || *apiVersion != api::Json(m_options.hikariApiVersion))
            mismatch = QStringLiteral("Hikari API version");
        else if (!build || *build != api::Json(m_options.buildId))
            mismatch = QStringLiteral("build id");
        m_handshakeTimer.stop();
        if (!mismatch.isEmpty()) {
            m_state = State::Refused;
            killTree();
            failAll(failure(SidecarError::Refused, "the sidecar doesn't match this app"));
            emit refused(QStringLiteral("The AI sidecar's %1 doesn't match this HikariSub.").arg(mismatch));
            return;
        }
        m_state = State::Ready;
        emit ready();
        sendQueued();
        return;
    }
    const auto it = m_pending.find(id);
    if (it == m_pending.end() || !it->second.sent)
        return lose(QStringLiteral("the sidecar answered a request that isn't outstanding"));
    if (const api::Json *error = message.find("error")) {
        const api::Json *code = error->find("code");
        const api::Json *text = error->find("message");
        const api::Json *data = error->find("data");
        SidecarFailure f{SidecarError::Remote, code && code->isInt() ? static_cast<int>(code->asInt()) : 0,
                         text && text->isString() ? text->asString() : std::string(), data ? *data : api::Json()};
        if (f.code == rpc::kRequestCancelled)
            f.error = SidecarError::Cancelled;
        return finish(id, std::unexpected(std::move(f)));
    }
    finish(id, *message.find("result"));
}

void AiSidecarHost::receiveRequest(const api::Json &id, const std::string &method, const api::Json *params)
{
    if (!id.isInt() && !id.isString())
        return lose(QStringLiteral("the sidecar sent a request with an invalid id"));
    if (m_state != State::Ready)
        return respond(id, errorBody(rpc::kBusy, "initialize first"));
    if (m_incoming.size() >= m_options.maxOutstanding)
        return lose(QStringLiteral("the sidecar went over the outstanding-request limit"));
    const std::string key = id.dump();
    if (m_incoming.contains(key))
        return lose(QStringLiteral("the sidecar reused the id of an outstanding request"));
    const auto handler = m_handlers.find(method);
    if (handler == m_handlers.end())
        return respond(id, errorBody(rpc::kMethodNotFound, "unknown method " + method));
    std::shared_ptr<SidecarRequest> request(new SidecarRequest(*this, id, m_generation));
    m_incoming.emplace(key, request);
    handler->second(params ? *params : api::Json(), std::move(request));
}

void AiSidecarHost::callApi(const api::Json &params, std::shared_ptr<SidecarRequest> request)
{
    const api::Json *operation = params.find("operation");
    if (!operation || !operation->isString())
        return request->fail(rpc::kInvalidParams, "_hikari/api.call needs an operation");
    if (!m_hikari)
        return failApi(*request, {std::string(api::errors::kUnavailable), "Hikari's API isn't available", {}});
    api::Caller caller = m_caller;
    if (const api::Json *mcp = params.find("mcp")) {
        if (!m_mcpGate)
            return failApi(*request, {std::string(api::errors::kUnavailable), "No MCP server is on", {}});
        auto gated = m_mcpGate(*mcp, operation->asString());
        if (!gated)
            return failApi(*request, std::move(gated.error()));
        caller = std::move(*gated);
    }
    const api::Json *operationParams = params.find("params");
    m_hikari->call(caller, operation->asString(), operationParams ? *operationParams : api::Json(),
                   [request = std::move(request)](api::Result result) {
                       if (result)
                           return request->respond(std::move(*result));
                       failApi(*request, std::move(result.error()));
                   });
}

void AiSidecarHost::answer(SidecarRequest &request, api::Json body)
{
    const api::Json id = request.m_id;
    m_incoming.erase(id.dump());
    respond(id, std::move(body));
}

void AiSidecarHost::cancelIncoming(const api::Json &id)
{
    const auto it = m_incoming.find(id.dump());
    if (it == m_incoming.end())
        return;
    const std::shared_ptr<SidecarRequest> request = it->second;
    request->cancel();
}

void AiSidecarHost::dropIncoming()
{
    const auto incoming = std::exchange(m_incoming, {});
    for (const auto &[id, request] : incoming)
        request->cancel();
}

void AiSidecarHost::respond(const api::Json &id, api::Json body)
{
    auto message = api::Json::object({{"jsonrpc", "2.0"}, {"id", id}});
    for (auto &member : body.asObject())
        message[member.key] = std::move(member.value);
    send(message);
}

void AiSidecarHost::finish(std::uint64_t id, SidecarResult result)
{
    const auto it = m_pending.find(id);
    if (it == m_pending.end())
        return;
    Pending pending = std::move(it->second);
    m_pending.erase(it);
    m_pendingBytes -= pending.bytes;
    pending.reply(std::move(result));
}

void AiSidecarHost::failAll(const SidecarFailure &f)
{
    m_queue.clear();
    m_queuedMessages.clear();
    auto pending = std::move(m_pending);
    m_pending.clear();
    m_pendingBytes = 0;
    for (auto &[id, p] : pending)
        p.reply(std::unexpected(f));
}

void AiSidecarHost::lose(const QString &reason)
{
    if (m_state != State::Starting && m_state != State::Ready)
        return;
    m_handshakeTimer.stop();
    m_startTimer.stop();
    readDiagnostics();
    killTree();
    m_crashes.push_back(m_clock.elapsed());
    const qint64 now = m_clock.elapsed();
    std::erase_if(m_crashes, [&](qint64 at) { return now - at > m_options.crashWindowMs; });
    const bool giveUp = static_cast<int>(m_crashes.size()) >= m_options.crashLimit;
    m_state = giveUp ? State::Stopped : State::Idle;
    failAll(failure(SidecarError::SidecarLost, reason.toStdString()));
    emit lost(reason);
    if (giveUp)
        emit stopped(m_diagnostics);
}

void AiSidecarHost::processGone()
{
    if (m_state == State::Stopping) {
        m_killTimer.stop();
        killTree();
        m_state = State::Idle;
        emit finished();
        return;
    }
    lose(QStringLiteral("the sidecar exited"));
}

void AiSidecarHost::shutdown()
{
    m_startTimer.stop();
    m_handshakeTimer.stop();
    if (!m_process || m_process->state() == QProcess::NotRunning) {
        killTree();
        if (m_state == State::Starting || m_state == State::Ready)
            m_state = State::Idle;
        failAll(failure(SidecarError::SidecarLost, "the sidecar was stopped"));
        return;
    }
    if (m_state == State::Stopping)
        return;
    m_state = State::Stopping;
    failAll(failure(SidecarError::SidecarLost, "the sidecar was stopped"));
    m_process->closeWriteChannel();
    m_killTimer.start(m_options.shutdownMs);
}

void AiSidecarHost::reset()
{
    if (m_state == State::Stopped || m_state == State::Refused) {
        m_state = State::Idle;
        m_crashes.clear();
    }
}

void AiSidecarHost::killTree()
{
    ++m_generation;
    dropIncoming();
#ifdef Q_OS_WIN
    m_launcher->killTree();
#else
    if (m_pid > 0)
        ::kill(-static_cast<pid_t>(m_pid), SIGKILL);
#endif
    m_pid = 0;
    if (!m_process)
        return;
    std::unique_ptr<QProcess> process = std::move(m_process);
    process->disconnect(this);
    if (process->state() != QProcess::NotRunning) {
        process->kill();
        process->waitForFinished(2000);
    }
    QProcess *raw = process.release();
    raw->deleteLater();
}

}
