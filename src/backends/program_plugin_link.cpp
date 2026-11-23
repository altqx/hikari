#include "hikari/backends/program_plugin_link.h"

#include "hikari/backends/plugin_protocol.h"
#include "hikari/backends/program_process.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaObject>

#include <expected>

namespace hikari::backends {

using helper::Outcome;
using helper::Reader;
using helper::Writer;
using plugin::Service;

namespace {

constexpr int kServerError = -32000;
constexpr int kMethodNotFound = -32601;
constexpr int kInvalidParams = -32602;
constexpr int kInternalError = -32603;

std::string compact(const QJsonValue &value)
{
    if (value.isObject())
        return QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact).toStdString();
    if (value.isArray())
        return QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact).toStdString();
    const QByteArray wrapped = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return wrapped.mid(1, wrapped.size() - 2).toStdString();
}

QJsonValue parsed(const std::string &json)
{
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString("[" + json + "]"));
    return document.isArray() ? document.array().first() : QJsonValue();
}

QString sourceKey(const QString &source)
{
    if (source == QLatin1String("panel") || source == QLatin1String("dialog"))
        return source;
    if (source == QLatin1String("quick_pick"))
        return QStringLiteral("pick");
    if (source == QLatin1String("input_box"))
        return QStringLiteral("box");
    return {};
}

std::vector<std::byte> bytesOf(const QByteArray &data)
{
    const auto *first = reinterpret_cast<const std::byte *>(data.constData());
    return {first, first + data.size()};
}

std::vector<std::byte> bytesOf(const QString &text)
{
    return bytesOf(text.toUtf8());
}

std::expected<std::vector<std::byte>, QString> exported(const QJsonValue &result)
{
    const QJsonObject file = result.toObject();
    const QJsonValue data = file.value(QStringLiteral("data"));
    const QString encoding = file.value(QStringLiteral("encoding")).toString(QStringLiteral("utf8"));
    if (!data.isString())
        return std::unexpected(QStringLiteral("an export must answer {data, encoding} with the file as a string"));
    if (encoding == QLatin1String("utf8"))
        return bytesOf(data.toString());
    if (encoding != QLatin1String("base64"))
        return std::unexpected(QStringLiteral("an export's encoding must be \"utf8\" or \"base64\""));
    auto decoded = QByteArray::fromBase64Encoding(data.toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded)
        return std::unexpected(QStringLiteral("the export's file isn't base64"));
    return bytesOf(*decoded);
}

std::expected<std::vector<std::byte>, QString> imported(const QJsonValue &result)
{
    if (!result.isObject())
        return std::unexpected(QStringLiteral("an import must answer {lines, styles, info}"));
    return bytesOf(QJsonDocument(result.toObject()).toJson(QJsonDocument::Compact));
}

std::vector<std::byte> named(Service service, const std::string &name, const std::string &json)
{
    Writer w;
    w.i32(static_cast<std::int32_t>(service)).str(name).str(json);
    return w.take();
}

Service serviceFor(const QString &method)
{
    if (method.startsWith(QLatin1String("panels.")) || method.startsWith(QLatin1String("dialogs.")) ||
        method.startsWith(QLatin1String("quick_pick.")) || method.startsWith(QLatin1String("input_box.")))
        return Service::Ui;
    if (method.startsWith(QLatin1String("files.")))
        return Service::Files;
    if (method == QLatin1String("net.fetch"))
        return Service::Net;
    if (method.startsWith(QLatin1String("job.")))
        return Service::Job;
    return Service::Call;
}

}

ProgramPluginLink::ProgramPluginLink(std::unique_ptr<ProgramProcess> process, bool background, QObject *parent)
    : ProgramPluginLink(std::move(process), background, Limits{}, parent)
{
}

ProgramPluginLink::ProgramPluginLink(std::unique_ptr<ProgramProcess> process, bool background, Limits limits, QObject *parent)
    : PluginLink(parent), m_process(std::move(process)), m_background(background), m_limits(limits)
{
    m_initializeTimer.setSingleShot(true);
    connect(&m_initializeTimer, &QTimer::timeout, this, [this] {
        appendDiagnostics(QStringLiteral("It didn't answer initialize in %1 seconds.\n").arg(m_limits.initializeMs / 1000).toUtf8());
        lose();
    });
    connect(m_process.get(), &ProgramProcess::output, this, &ProgramPluginLink::onOutput);
    connect(m_process.get(), &ProgramProcess::errorOutput, this, &ProgramPluginLink::onErrorOutput);
    connect(m_process.get(), &ProgramProcess::finished, this, [this] { lose(); });
}

ProgramPluginLink::~ProgramPluginLink()
{
    if (m_process)
        disconnect(m_process.get(), nullptr, this, nullptr);
}

qint64 ProgramPluginLink::processId() const
{
    return m_process ? m_process->processId() : 0;
}

void ProgramPluginLink::start()
{
    if (!m_process->start()) {
        m_gone = true;
        emit refused(m_process->refusal());
        return;
    }
    m_ready = true;
    emit started();
}

void ProgramPluginLink::stop(bool now)
{
    m_ready = false;
    m_gone = true;
    m_pending.clear();
    m_calls.clear();
    m_initializeTimer.stop();
    if (!m_process)
        return;
    disconnect(m_process.get(), nullptr, this, nullptr);
    ProgramProcess *process = m_process.release();
    if (now || !process->running()) {
        process->kill();
        process->deleteLater();
        return;
    }
    send(QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), double(m_nextId++)},
                     {QStringLiteral("method"), QStringLiteral("shutdown")}, {QStringLiteral("params"), QJsonObject{}}},
         process);
    send(QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("method"), QStringLiteral("exit")}}, process);
    process->closeInput();
    connect(process, &ProgramProcess::finished, process, &QObject::deleteLater);
    QTimer::singleShot(m_limits.shutdownMs, process, [process] {
        process->kill();
        process->deleteLater();
    });
}

std::expected<std::uint64_t, helper::HostError> ProgramPluginLink::request(std::uint64_t, std::vector<std::byte> payload,
                                                                         helper::HelperHost::Handler handler)
{
    return send(std::move(payload), std::move(handler), false);
}

std::expected<std::uint64_t, helper::HostError> ProgramPluginLink::push(std::uint64_t, std::vector<std::byte> payload,
                                                                      helper::HelperHost::Handler handler)
{
    return send(std::move(payload), std::move(handler), true);
}

std::expected<std::uint64_t, helper::HostError> ProgramPluginLink::send(std::vector<std::byte> payload,
                                                                      helper::HelperHost::Handler handler, bool isEvent)
{
    if (!m_ready)
        return std::unexpected(helper::HostError::NotReady);
    const std::uint64_t request = m_nextRequest++;
    Reader in(payload);
    QString method;
    QJsonObject params;
    auto reply = Pending::Reply::Plain;
    if (isEvent) {
        const QString event = QString::fromStdString(in.str());
        const QJsonValue data = parsed(in.str());
        if (event == QLatin1String("settings:changed")) {
            const QJsonObject change = data.toObject();
            m_settings.insert(change.value(QStringLiteral("id")).toString(), change.value(QStringLiteral("value")));
        }
        const qsizetype colon = event.indexOf(QLatin1Char(':'));
        const QString source = event.left(colon);
        if (const QString key = sourceKey(source); colon > 0 && !key.isEmpty()) {
            method = source;
            params = {{key, event.mid(colon + 1)}, {QStringLiteral("event"), data}};
        } else {
            method = QStringLiteral("event");
            params = {{QStringLiteral("name"), event}, {QStringLiteral("data"), data}};
        }
        if (m_muted.contains(event)) {
            deliver(handler, helper::Event{helper::Kind::Terminal, Outcome::Ok, {}, 0});
            return request;
        }
    } else {
        const auto command = static_cast<plugin::Command>(in.i32());
        if (command == plugin::Command::Load) {
            const auto loadRequest = plugin::decodeLoad(in);
            if (!loadRequest) {
                deliver(handler, helper::Event{helper::Kind::Terminal, Outcome::InvalidInput, {}, 0});
                return request;
            }
            m_commands = loadRequest->commands;
            m_settings = parsed(loadRequest->settings).toObject();
            reply = Pending::Reply::Load;
            method = QStringLiteral("initialize");
            params = {{QStringLiteral("protocol"), 1},
                      {QStringLiteral("api"), 1},
                      {QStringLiteral("plugin"), QJsonObject{{QStringLiteral("id"), QString::fromStdString(loadRequest->id)},
                                                             {QStringLiteral("version"), QString::fromStdString(loadRequest->version)}}},
                      {QStringLiteral("locale"), QString::fromStdString(loadRequest->locale)},
                      {QStringLiteral("settings"), m_settings}};
            m_initializeTimer.start(m_limits.initializeMs);
        } else if (command == plugin::Command::Run) {
            method = QStringLiteral("command");
            const QString id = QString::fromStdString(in.str());
            const QJsonObject call = parsed(in.str()).toObject();
            params = {{QStringLiteral("command"), id},
                      {QStringLiteral("args"), call.value(QStringLiteral("args")).isUndefined() ? QJsonValue::Null : call.value(QStringLiteral("args"))},
                      {QStringLiteral("context"), call.value(QStringLiteral("context")).toObject()}};
        } else if (command == plugin::Command::Export) {
            reply = Pending::Reply::Export;
            method = QStringLiteral("export");
            const QString format = QString::fromStdString(in.str());
            const QJsonValue document = parsed(in.str());
            params = {{QStringLiteral("format"), format}, {QStringLiteral("document"), document},
                      {QStringLiteral("options"), parsed(in.str())}};
        } else if (command == plugin::Command::Import) {
            reply = Pending::Reply::Import;
            method = QStringLiteral("import");
            const QString format = QString::fromStdString(in.str());
            const std::string data = in.str();
            params = {{QStringLiteral("format"), format},
                      {QStringLiteral("data"), QString::fromLatin1(QByteArray::fromStdString(data).toBase64())},
                      {QStringLiteral("name"), QString::fromStdString(in.str())}};
        } else {
            deliver(handler, helper::Event{helper::Kind::Terminal, Outcome::Unsupported, {}, 0});
            return request;
        }
    }
    const std::int64_t id = m_nextId++;
    m_pending.emplace(id, Pending{request, std::move(handler), reply});
    m_open = request;
    send(QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), double(id)},
                     {QStringLiteral("method"), method}, {QStringLiteral("params"), params}});
    return request;
}

void ProgramPluginLink::deliver(const helper::HelperHost::Handler &handler, std::expected<helper::Event, helper::HostError> event)
{
    QMetaObject::invokeMethod(this, [handler, event = std::move(event)] { handler(event); }, Qt::QueuedConnection);
}

void ProgramPluginLink::send(const QJsonObject &message, ProgramProcess *to)
{
    QByteArray line = QJsonDocument(message).toJson(QJsonDocument::Compact);
    line += '\n';
    (to ? to : m_process.get())->write(line);
}

void ProgramPluginLink::respond(const QJsonValue &id, const QJsonValue &result)
{
    if (id.isUndefined() || m_gone)
        return;
    send(QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), id}, {QStringLiteral("result"), result}});
}

void ProgramPluginLink::respondError(const QJsonValue &id, int code, const QString &message, const QJsonValue &data)
{
    if (id.isUndefined() || m_gone)
        return;
    QJsonObject error{{QStringLiteral("code"), code}, {QStringLiteral("message"), message}};
    if (!data.isUndefined())
        error.insert(QStringLiteral("data"), data);
    send(QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), id}, {QStringLiteral("error"), error}});
}

void ProgramPluginLink::onOutput(const QByteArray &bytes)
{
    m_buffer += bytes;
    for (qsizetype end = m_buffer.indexOf('\n'); end >= 0 && !m_gone; end = m_buffer.indexOf('\n')) {
        const QByteArray line = m_buffer.left(end);
        m_buffer.remove(0, end + 1);
        if (line.size() > m_limits.maxLine) {
            appendDiagnostics("A message was larger than 96 MiB.\n");
            lose();
            return;
        }
        if (!line.trimmed().isEmpty())
            onLine(line);
    }
    if (m_buffer.size() > m_limits.maxLine && !m_gone) {
        appendDiagnostics("A message was larger than 96 MiB.\n");
        lose();
    }
}

void ProgramPluginLink::onLine(const QByteArray &line)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(line, &error);
    if (!document.isObject() || document.object().value(QStringLiteral("jsonrpc")) != QLatin1String("2.0")) {
        appendDiagnostics("It wrote something other than JSON-RPC on stdout: " + line.left(200) + '\n');
        lose();
        return;
    }
    const QJsonObject message = document.object();
    if (message.contains(QStringLiteral("method"))) {
        onCall(message);
        return;
    }
    const QJsonValue id = message.value(QStringLiteral("id"));
    if (id.isDouble())
        onAnswer(static_cast<std::int64_t>(id.toDouble()), message);
}

void ProgramPluginLink::onAnswer(std::int64_t id, const QJsonObject &message)
{
    const auto it = m_pending.find(id);
    if (it == m_pending.end())
        return;
    Pending pending = std::move(it->second);
    m_pending.erase(it);
    if (pending.request == m_open)
        m_open = 0;
    endCalls(pending.request);
    const QJsonValue error = message.value(QStringLiteral("error"));
    helper::Event event{helper::Kind::Terminal, Outcome::Ok, {}, 0};
    if (error.isObject()) {
        event.outcome = Outcome::Failed;
        event.payload = bytesOf(error.toObject().value(QStringLiteral("message")).toString());
    } else if (pending.reply == Pending::Reply::Load) {
        m_initializeTimer.stop();
        event.payload = plugin::encodeStrings(m_commands);
    } else if (pending.reply != Pending::Reply::Plain) {
        auto made = pending.reply == Pending::Reply::Export ? exported(message.value(QStringLiteral("result")))
                                                            : imported(message.value(QStringLiteral("result")));
        event.outcome = made ? Outcome::Ok : Outcome::Failed;
        event.payload = made ? std::move(*made) : bytesOf(made.error());
    }
    pending.handler(event);
}

void ProgramPluginLink::endCalls(std::uint64_t request)
{
    for (auto it = m_calls.begin(); it != m_calls.end();) {
        if (it->second.request != request) {
            ++it;
            continue;
        }
        respondError(it->second.id, kServerError, QStringLiteral("The request this call was made in has ended."),
                     QJsonObject{{QStringLiteral("code"), QStringLiteral("refused")}});
        it = m_calls.erase(it);
    }
}

void ProgramPluginLink::onCall(const QJsonObject &message)
{
    const QJsonValue id = message.contains(QStringLiteral("id")) ? message.value(QStringLiteral("id")) : QJsonValue(QJsonValue::Undefined);
    const QString method = message.value(QStringLiteral("method")).toString();
    const QJsonValue paramsValue = message.value(QStringLiteral("params"));
    if (!paramsValue.isUndefined() && !paramsValue.isObject()) {
        respondError(id, kInvalidParams, QStringLiteral("params must be an object."),
                     QJsonObject{{QStringLiteral("pointer"), QString()}});
        return;
    }
    const QJsonObject params = paramsValue.toObject();
    if (method == QLatin1String("hikari/settings.get")) {
        const QString setting = params.value(QStringLiteral("id")).toString();
        if (!m_settings.contains(setting))
            respondError(id, kServerError, QStringLiteral("The plugin declares no setting “%1”.").arg(setting),
                         QJsonObject{{QStringLiteral("code"), QStringLiteral("not_found")}});
        else
            respond(id, m_settings.value(setting));
        return;
    }
    if (method == QLatin1String("hikari/unsubscribe")) {
        for (const QJsonValue &event : params.value(QStringLiteral("events")).toArray())
            m_muted.insert(event.toString());
        respond(id, QJsonValue::Null);
        return;
    }
    if (!m_open && !m_background) {
        respondError(id, kServerError,
                     QStringLiteral("Without the background permission, a program plugin calls HikariSub only while it answers HikariSub."),
                     QJsonObject{{QStringLiteral("code"), QStringLiteral("not_granted")},
                                 {QStringLiteral("data"), QJsonObject{{QStringLiteral("reason"), QStringLiteral("not_background")}}}});
        return;
    }
    const std::uint64_t key = m_nextCall++;
    Call &record = m_calls[key];
    record.id = id;
    record.request = m_open;
    record.params = params;
    if (method == QLatin1String("hikari/notify")) {
        const QString level = params.value(QStringLiteral("level")).toString();
        const auto value = level == QLatin1String("error")     ? plugin::Level::Error
                           : level == QLatin1String("warning") ? plugin::Level::Warning
                                                               : plugin::Level::Info;
        Writer w;
        w.i32(static_cast<std::int32_t>(Service::Notify)).i32(static_cast<std::int32_t>(value)).u8(0)
            .str(params.value(QStringLiteral("text")).toString().toStdString());
        record.service = static_cast<std::int32_t>(Service::Notify);
        forward(key, record, w.take());
    } else if (method == QLatin1String("hikari/settings.set")) {
        record.service = static_cast<std::int32_t>(Service::SetSetting);
        forward(key, record,
                named(Service::SetSetting, params.value(QStringLiteral("id")).toString().toStdString(),
                      compact(params.value(QStringLiteral("value")))));
    } else if (method == QLatin1String("hikari/subscribe")) {
        record.stage = Stage::Subscribe;
        record.service = static_cast<std::int32_t>(Service::Subscribe);
        record.waiting = 0;
        finishCall(key, {}, false);
    } else if (method == QLatin1String("hikari/edit.begin")) {
        record.stage = Stage::EditLock;
        record.service = static_cast<std::int32_t>(Service::Lock);
        Writer w;
        w.i32(static_cast<std::int32_t>(Service::Lock));
        forward(key, record, w.take());
    } else if (method == QLatin1String("hikari/edit.end")) {
        record.stage = Stage::EditGroupEnd;
        record.service = static_cast<std::int32_t>(Service::Call);
        forward(key, record, named(Service::Call, "document.group_end", "{}"));
    } else {
        const Service kind = serviceFor(method);
        record.service = static_cast<std::int32_t>(kind);
        forward(key, record, named(kind, method.toStdString(), compact(params)));
    }
}

void ProgramPluginLink::forward(std::uint64_t call, Call &record, std::vector<std::byte> payload)
{
    if (record.request == 0) {
        emit outsideService(call, payload);
        return;
    }
    for (auto &[id, pending] : m_pending)
        if (pending.request == record.request) {
            pending.handler(helper::Event{helper::Kind::Service, Outcome::Ok, std::move(payload), call});
            return;
        }
    endCalls(record.request);
}

bool ProgramPluginLink::answer(std::uint64_t, std::uint64_t call, Outcome outcome, std::vector<std::byte> payload)
{
    const auto it = m_calls.find(call);
    if (it == m_calls.end())
        return false;
    if (outcome != Outcome::Ok) {
        finishCall(call, "HikariSub refused the call.", true);
        return true;
    }
    const auto service = static_cast<Service>(it->second.service);
    std::string text;
    if (service != Service::Notify && service != Service::Unlock) {
        Reader in(payload);
        text = in.str();
    }
    finishCall(call, text, false);
    return true;
}

void ProgramPluginLink::finishCall(std::uint64_t key, const std::string &text, bool failed)
{
    const auto it = m_calls.find(key);
    if (it == m_calls.end())
        return;
    Call &record = it->second;
    const QJsonValue id = record.id;
    auto done = [this, key] { m_calls.erase(key); };
    if (failed) {
        respondError(id, kInternalError, QString::fromStdString(text));
        done();
        return;
    }
    const auto service = static_cast<Service>(record.service);
    const auto refusal = [&](const std::string &reason) {
        respondError(id, kServerError, QString::fromStdString(reason), QJsonObject{{QStringLiteral("code"), QStringLiteral("refused")}});
        done();
    };
    switch (record.stage) {
    case Stage::Subscribe: {
        if (!text.empty())
            return refusal(text);
        const QJsonArray events = record.params.value(QStringLiteral("events")).toArray();
        if (record.waiting >= events.size()) {
            respond(id, QJsonValue::Null);
            return done();
        }
        const QString event = events.at(record.waiting++).toString();
        m_muted.erase(event);
        Writer w;
        w.i32(static_cast<std::int32_t>(Service::Subscribe)).str(event.toStdString());
        return forward(key, record, w.take());
    }
    case Stage::EditLock: {
        if (!text.empty()) {
            respondError(id, kServerError, QString::fromStdString(text), QJsonObject{{QStringLiteral("code"), QStringLiteral("busy")}});
            return done();
        }
        record.stage = Stage::EditGroupBegin;
        record.service = static_cast<std::int32_t>(Service::Call);
        const QJsonObject begin{{QStringLiteral("name"), record.params.value(QStringLiteral("name"))}};
        return forward(key, record, named(Service::Call, "document.group_begin", compact(begin)));
    }
    case Stage::EditGroupEnd: {
        record.stage = Stage::EditUnlock;
        record.service = static_cast<std::int32_t>(Service::Unlock);
        record.params = QJsonObject{{QStringLiteral("ended"), parsed(text)}};
        Writer w;
        w.i32(static_cast<std::int32_t>(Service::Unlock));
        return forward(key, record, w.take());
    }
    case Stage::EditUnlock:
        respondEnvelope(id, record.params.value(QStringLiteral("ended")).toObject());
        return done();
    case Stage::EditGroupBegin:
    case Stage::Single:
        break;
    }
    if (service == Service::Notify || service == Service::Unlock) {
        respond(id, QJsonValue::Null);
        return done();
    }
    if (service == Service::SetSetting || service == Service::Lock) {
        if (!text.empty())
            return refusal(text);
        respond(id, QJsonValue::Null);
        return done();
    }
    respondEnvelope(id, parsed(text).toObject());
    done();
}

void ProgramPluginLink::respondEnvelope(const QJsonValue &id, const QJsonObject &envelope)
{
    if (!envelope.contains(QStringLiteral("error"))) {
        respond(id, envelope.value(QStringLiteral("result")));
        return;
    }
    const QJsonObject error = envelope.value(QStringLiteral("error")).toObject();
    const QString code = error.value(QStringLiteral("code")).toString();
    const QString message = error.value(QStringLiteral("message")).toString();
    if (code == QLatin1String("unknown_operation"))
        respondError(id, kMethodNotFound, message);
    else if (code == QLatin1String("invalid_params"))
        respondError(id, kInvalidParams, message, error.value(QStringLiteral("data")));
    else
        respondError(id, kServerError, message,
                     QJsonObject{{QStringLiteral("code"), code}, {QStringLiteral("data"), error.value(QStringLiteral("data"))}});
}

void ProgramPluginLink::onErrorOutput(const QByteArray &bytes)
{
    appendDiagnostics(bytes);
    m_errorLine += bytes;
    for (qsizetype end = m_errorLine.indexOf('\n'); end >= 0; end = m_errorLine.indexOf('\n')) {
        const QString line = QString::fromUtf8(m_errorLine.left(end)).trimmed();
        m_errorLine.remove(0, end + 1);
        if (!line.isEmpty())
            emit logLine(line);
    }
    if (m_errorLine.size() > 4096) {
        emit logLine(QString::fromUtf8(m_errorLine.left(4096)));
        m_errorLine.clear();
    }
}

void ProgramPluginLink::appendDiagnostics(const QByteArray &bytes)
{
    m_diagnostics += bytes;
    if (m_diagnostics.size() > m_limits.maxDiagnostics)
        m_diagnostics.remove(0, m_diagnostics.size() - m_limits.maxDiagnostics);
}

void ProgramPluginLink::lose()
{
    if (m_gone)
        return;
    m_gone = true;
    m_ready = false;
    m_initializeTimer.stop();
    if (m_process) {
        disconnect(m_process.get(), nullptr, this, nullptr);
        m_process->kill();
    }
    auto pending = std::move(m_pending);
    m_pending.clear();
    m_calls.clear();
    for (auto &[id, request] : pending)
        request.handler(std::unexpected(helper::HostError::HelperLost));
    emit lost();
}

}
