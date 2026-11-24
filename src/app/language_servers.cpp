#include "hikari/app/language_servers.h"

#include "hikari/backends/program_process.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>

#include <algorithm>

namespace hikari::app {

namespace {

using application::Problem;
using application::ProblemField;
using application::ProblemSeverity;
using backends::LspConnection;

QString fieldName(ProblemField field)
{
    return field == ProblemField::Translation ? QStringLiteral("translation") : QStringLiteral("text");
}

std::optional<ProblemField> fieldNamed(const std::string &name)
{
    if (name == "text")
        return ProblemField::Text;
    if (name == "translation")
        return ProblemField::Translation;
    return std::nullopt;
}

QString uriOf(std::uint64_t document, ProblemField field)
{
    return QStringLiteral("hikari://document/%1/%2").arg(document).arg(fieldName(field));
}

QJsonValue toQJson(const api::Json &value)
{
    const QJsonDocument wrapped = QJsonDocument::fromJson(QByteArray::fromStdString("[" + value.dump() + "]"));
    return wrapped.array().at(0);
}

ProblemSeverity severityOf(const QJsonValue &value)
{
    switch (value.toInt(1)) {
    case 2:
        return ProblemSeverity::Warning;
    case 3:
        return ProblemSeverity::Info;
    case 4:
        return ProblemSeverity::Hint;
    default:
        return ProblemSeverity::Error;
    }
}

QString codeOf(const QJsonValue &code)
{
    if (code.isString())
        return code.toString();
    if (code.isDouble())
        return QString::number(code.toInteger());
    return {};
}

int clamped(const QJsonValue &value, int low, int high)
{
    return std::clamp(value.toInt(low), low, std::max(low, high));
}

}

QString blankTags(const QString &text)
{
    QString out = text;
    for (qsizetype i = 0; i < out.size(); ++i) {
        if (out[i] == QLatin1Char('{')) {
            const qsizetype end = out.indexOf(QLatin1Char('}'), i + 1);
            if (end < 0)
                break;
            std::fill(out.begin() + i, out.begin() + end + 1, QLatin1Char(' '));
            i = end;
        } else if (out[i] == QLatin1Char('\\') && i + 1 < out.size() &&
                   (out[i + 1] == QLatin1Char('N') || out[i + 1] == QLatin1Char('n') || out[i + 1] == QLatin1Char('h'))) {
            out[i] = out[i + 1] = QLatin1Char(' ');
            ++i;
        }
    }
    return out;
}

ServerText serverText(const core::Document &document, ProblemField field, plugins::LanguageServer::Tags tags)
{
    ServerText out;
    QStringList lines;
    for (const core::LineRecord *line : document.lines()) {
        const std::u8string &raw = application::problemFieldText(*line, field);
        QString text;
        if (!line->comment) {
            text = QString::fromUtf8(reinterpret_cast<const char *>(raw.data()), qsizetype(raw.size()));
            text.replace(QLatin1Char('\r'), QLatin1Char(' ')).replace(QLatin1Char('\n'), QLatin1Char(' '));
            if (tags == plugins::LanguageServer::Tags::Blank)
                text = blankTags(text);
        }
        out.lines.push_back(line->id);
        out.lengths.push_back(int(text.size()));
        out.comments.push_back(line->comment);
        out.fields.push_back(raw);
        lines.push_back(std::move(text));
    }
    out.text = lines.join(QLatin1Char('\n'));
    return out;
}

std::vector<Problem> diagnosticProblems(const QJsonArray &diagnostics, const ServerText &text, ProblemField field)
{
    std::vector<Problem> out;
    for (const QJsonValue &value : diagnostics) {
        if (out.size() == application::Problems::kMostPerDocument)
            break;
        const QJsonObject diagnostic = value.toObject();
        const QJsonObject range = diagnostic.value(QStringLiteral("range")).toObject();
        const QJsonObject start = range.value(QStringLiteral("start")).toObject();
        const QJsonObject end = range.value(QStringLiteral("end")).toObject();
        const int line = start.value(QStringLiteral("line")).toInt(-1);
        const QString message = diagnostic.value(QStringLiteral("message")).toString();
        if (line < 0 || std::size_t(line) >= text.lines.size() || text.comments[std::size_t(line)] || message.isEmpty())
            continue;
        const int length = text.lengths[std::size_t(line)];
        const int from = clamped(start.value(QStringLiteral("character")), 0, length);
        const int endLine = end.value(QStringLiteral("line")).toInt(line);
        const int to = endLine == line ? clamped(end.value(QStringLiteral("character")), from, length)
                                       : endLine > line ? length : from;
        Problem problem;
        problem.line = text.lines[std::size_t(line)];
        problem.field = field;
        problem.from = std::size_t(from);
        problem.to = std::size_t(to);
        problem.severity = severityOf(diagnostic.value(QStringLiteral("severity")));
        problem.message = message.toStdString();
        problem.code = codeOf(diagnostic.value(QStringLiteral("code"))).toStdString();
        problem.against = text.fields[std::size_t(line)];
        out.push_back(std::move(problem));
    }
    return out;
}

LanguageServers::LanguageServers(Wiring wiring, QObject *parent) : LanguageServers(std::move(wiring), Limits{}, parent) {}

LanguageServers::LanguageServers(Wiring wiring, Limits limits, QObject *parent)
    : QObject(parent), m_wiring(std::move(wiring)), m_limits(limits)
{
}

LanguageServers::~LanguageServers()
{
    for (auto &[key, server] : m_servers)
        if (server->connection)
            server->connection->kill();
}

std::string LanguageServers::source(const Server &server) const
{
    return "plugin:" + server.entry.plugin + "/" + server.entry.server.id;
}

void LanguageServers::serversChanged()
{
    const std::vector<PluginRuntime::LanguageServer> wanted = m_wiring.servers ? m_wiring.servers() : std::vector<PluginRuntime::LanguageServer>{};
    for (auto it = m_servers.begin(); it != m_servers.end();) {
        const auto found = std::ranges::find_if(wanted, [&](const PluginRuntime::LanguageServer &w) {
            return w.plugin == it->first.first && w.server.id == it->first.second;
        });
        if (found != wanted.end() && found->revision == it->second->entry.revision) {
            ++it;
            continue;
        }
        retire(*it->second);
        it = m_servers.erase(it);
    }
    for (const PluginRuntime::LanguageServer &entry : wanted) {
        const Key key{entry.plugin, entry.server.id};
        if (m_servers.contains(key))
            continue;
        auto server = std::make_unique<Server>();
        server->entry = entry;
        server->flush.setSingleShot(true);
        server->initializeDeadline.setSingleShot(true);
        connect(&server->flush, &QTimer::timeout, this, [this, key] {
            if (const auto it = m_servers.find(key); it != m_servers.end())
                flush(*it->second);
        });
        connect(&server->initializeDeadline, &QTimer::timeout, this, [this, key] {
            const auto it = m_servers.find(key);
            if (it != m_servers.end() && it->second->connection)
                it->second->connection->kill();
            ended(key, true, QStringLiteral("it didn’t answer initialize in time"));
        });
        m_servers.emplace(key, std::move(server));
    }
    emit statesChanged();
    sync();
}

void LanguageServers::retire(Server &server)
{
    if (m_wiring.problems)
        m_wiring.problems->clear(source(server), std::nullopt, std::nullopt);
    if (!server.connection)
        return;
    disconnect(server.connection.get(), nullptr, this, nullptr);
    LspConnection *connection = server.connection.get();
    connect(connection, &LspConnection::errorLine, this, [this, plugin = server.entry.plugin](const QString &line) {
        if (m_wiring.log)
            m_wiring.log(plugin, line);
    });
    connect(connection, &LspConnection::ended, this, [this, connection] {
        std::erase_if(m_retiring, [&](const auto &c) { return c.get() == connection; });
    }, Qt::QueuedConnection);
    connection->shutdown();
    m_retiring.push_back(std::move(server.connection));
}

void LanguageServers::sync()
{
    const Open open = m_wiring.open ? m_wiring.open() : Open{};
    for (auto &[key, server] : m_servers) {
        if (server->entry.server.scope != plugins::LanguageServer::Scope::Document)
            continue;
        bool changed = false;
        for (auto it = server->texts.begin(); it != server->texts.end();) {
            const bool stillOpen = std::ranges::any_of(open, [&](const auto &o) { return o.first.value == it->first.first && o.second; });
            if (stillOpen) {
                ++it;
                continue;
            }
            close(*server, it->second);
            it = server->texts.erase(it);
        }
        for (const auto &[id, session] : open) {
            if (!session)
                continue;
            for (const std::string &name : server->entry.server.fields) {
                const auto field = fieldNamed(name);
                if (!field)
                    continue;
                Text &text = server->texts[{id.value, int(*field)}];
                if (text.revision == session->revision())
                    continue;
                text.document = id.value;
                text.field = *field;
                text.revision = session->revision();
                text.pending = serverText(session->document(), *field, server->entry.server.tags);
                text.dirty = true;
                changed = true;
            }
        }
        if (!changed)
            continue;
        if (!server->connection && server->state != State::Stopped)
            start(*server);
        server->flush.start(m_limits.syncMs);
    }
}

void LanguageServers::close(Server &server, Text &text)
{
    if (text.opened && server.connection && server.state == State::Running)
        server.connection->notify(QStringLiteral("textDocument/didClose"),
                                  QJsonObject{{QStringLiteral("textDocument"),
                                               QJsonObject{{QStringLiteral("uri"), uriOf(text.document, text.field)}}}});
    if (m_wiring.problems)
        m_wiring.problems->clear(source(server), application::DocumentId{text.document}, fieldName(text.field).toStdString());
}

void LanguageServers::start(Server &server)
{
    const Key key{server.entry.plugin, server.entry.server.id};
    auto options = m_wiring.start ? m_wiring.start(key.first, key.second)
                                  : std::unexpected(QStringLiteral("language servers aren’t available here"));
    if (!options) {
        setState(server, State::Stopped, options.error());
        return;
    }
    server.connection = std::make_unique<LspConnection>(std::make_unique<backends::ProgramProcess>(std::move(*options)),
                                                        m_limits.connection);
    LspConnection *connection = server.connection.get();
    connection->setHandler([this, key](const QString &method, const QJsonValue &params) {
        const auto it = m_servers.find(key);
        if (it == m_servers.end())
            return std::expected<QJsonValue, LspConnection::Failure>(QJsonValue::Null);
        return serve(*it->second, method, params);
    });
    connect(connection, &LspConnection::notification, this, [this, key](const QString &method, const QJsonValue &params) {
        const auto it = m_servers.find(key);
        if (it == m_servers.end())
            return;
        if (method == QLatin1String("textDocument/publishDiagnostics"))
            diagnostics(*it->second, params);
        else if (method == QLatin1String("window/logMessage") || method == QLatin1String("window/showMessage"))
            log(*it->second, params[QStringLiteral("message")].toString());
    });
    connect(connection, &LspConnection::errorLine, this, [this, key](const QString &line) {
        if (const auto it = m_servers.find(key); it != m_servers.end())
            log(*it->second, line);
    });
    connect(connection, &LspConnection::ended, this,
            [this, key](bool crashed, const QString &why) { ended(key, crashed, why); }, Qt::QueuedConnection);
    if (auto started = connection->start(); !started) {
        server.connection.reset();
        setState(server, State::Stopped, started.error());
        return;
    }
    setState(server, State::Starting);
    initialize(server);
}

void LanguageServers::initialize(Server &server)
{
    const Key key{server.entry.plugin, server.entry.server.id};
    const QJsonObject capabilities{
        {QStringLiteral("general"), QJsonObject{{QStringLiteral("positionEncodings"), QJsonArray{QStringLiteral("utf-16")}}}},
        {QStringLiteral("workspace"), QJsonObject{{QStringLiteral("configuration"), true}}},
        {QStringLiteral("textDocument"),
         QJsonObject{{QStringLiteral("synchronization"), QJsonObject{{QStringLiteral("dynamicRegistration"), false}}},
                     {QStringLiteral("publishDiagnostics"), QJsonObject{{QStringLiteral("versionSupport"), true}}}}},
        {QStringLiteral("window"), QJsonObject{{QStringLiteral("showMessage"), QJsonObject{}}}},
    };
    QJsonObject params{
        {QStringLiteral("processId"), qint64(QCoreApplication::applicationPid())},
        {QStringLiteral("clientInfo"), QJsonObject{{QStringLiteral("name"), QStringLiteral("HikariSub")}}},
        {QStringLiteral("rootUri"), QJsonValue::Null},
        {QStringLiteral("workspaceFolders"), QJsonValue::Null},
        {QStringLiteral("capabilities"), capabilities},
    };
    if (!server.entry.server.initializationOptions.isNull())
        params.insert(QStringLiteral("initializationOptions"), toQJson(server.entry.server.initializationOptions));
    server.initializeDeadline.start(m_limits.initializeMs);
    server.connection->request(QStringLiteral("initialize"), params,
                               [this, key](std::expected<QJsonValue, LspConnection::Failure> result) {
                                   const auto it = m_servers.find(key);
                                   if (it == m_servers.end() || !it->second->connection)
                                       return;
                                   Server &server = *it->second;
                                   server.initializeDeadline.stop();
                                   if (!result) {
                                       ended(key, true, result.error().message);
                                       return;
                                   }
                                   server.connection->notify(QStringLiteral("initialized"), QJsonObject{});
                                   setState(server, State::Running);
                                   flush(server);
                               });
}

void LanguageServers::flush(Server &server)
{
    if (server.state != State::Running || !server.connection)
        return;
    for (auto &[key, text] : server.texts) {
        if (!text.dirty)
            continue;
        const QString uri = uriOf(text.document, text.field);
        const qint64 version = qint64(text.revision.value_or(0));
        if (!text.opened)
            server.connection->notify(QStringLiteral("textDocument/didOpen"),
                                      QJsonObject{{QStringLiteral("textDocument"),
                                                   QJsonObject{{QStringLiteral("uri"), uri},
                                                               {QStringLiteral("languageId"), QString::fromStdString(server.entry.server.languageId)},
                                                               {QStringLiteral("version"), version},
                                                               {QStringLiteral("text"), text.pending.text}}}});
        else
            server.connection->notify(
                QStringLiteral("textDocument/didChange"),
                QJsonObject{{QStringLiteral("textDocument"), QJsonObject{{QStringLiteral("uri"), uri}, {QStringLiteral("version"), version}}},
                            {QStringLiteral("contentChanges"), QJsonArray{QJsonObject{{QStringLiteral("text"), text.pending.text}}}}});
        text.opened = true;
        text.dirty = false;
        text.sent = text.pending;
        text.sentVersion = version;
    }
}

void LanguageServers::diagnostics(Server &server, const QJsonValue &params)
{
    const QString uri = params[QStringLiteral("uri")].toString();
    for (auto &[key, text] : server.texts) {
        if (!text.opened || uriOf(text.document, text.field) != uri)
            continue;
        if (const QJsonValue version = params[QStringLiteral("version")]; version.isDouble() && version.toInteger() != text.sentVersion)
            return;
        if (m_wiring.problems)
            m_wiring.problems->set("plugin:" + server.entry.plugin, source(server), server.entry.server.title,
                                   application::DocumentId{text.document}, fieldName(text.field).toStdString(),
                                   diagnosticProblems(params[QStringLiteral("diagnostics")].toArray(), text.sent, text.field));
        return;
    }
}

std::expected<QJsonValue, LspConnection::Failure> LanguageServers::serve(const Server &server, const QString &method,
                                                                        const QJsonValue &params) const
{
    if (method == QLatin1String("workspace/configuration")) {
        const QJsonValue settings = toQJson(m_wiring.settings ? m_wiring.settings(server.entry.plugin) : api::Json::object());
        QJsonArray out;
        for (const QJsonValue &item : params[QStringLiteral("items")].toArray()) {
            const QString section = item[QStringLiteral("section")].toString();
            out.append(section.isEmpty() ? settings : settings[section]);
        }
        return out;
    }
    if (method == QLatin1String("window/workDoneProgress/create") || method == QLatin1String("client/registerCapability") ||
        method == QLatin1String("client/unregisterCapability") || method == QLatin1String("window/showMessageRequest") ||
        method == QLatin1String("workspace/workspaceFolders"))
        return QJsonValue::Null;
    return std::unexpected(LspConnection::Failure{LspConnection::kMethodNotFound, QStringLiteral("Unknown method")});
}

void LanguageServers::ended(Key key, bool crashed, const QString &why)
{
    const auto it = m_servers.find(key);
    if (it == m_servers.end())
        return;
    Server &server = *it->second;
    if (!server.connection)
        return;
    server.initializeDeadline.stop();
    server.connection->disconnect(this);
    server.connection->kill();
    server.connection.release()->deleteLater();
    if (m_wiring.problems)
        m_wiring.problems->clear(source(server), std::nullopt, std::nullopt);
    for (auto &[textKey, text] : server.texts) {
        text.opened = false;
        text.dirty = true;
    }
    if (!crashed) {
        setState(server, State::Idle);
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    server.crashes.push_back(now);
    while (!server.crashes.empty() && now - server.crashes.front() > m_limits.restartWindowMs)
        server.crashes.pop_front();
    log(server, QCoreApplication::translate("HikariSub.Plugins", "%1 stopped: %2").arg(QString::fromStdString(server.entry.server.title), why));
    if (int(server.crashes.size()) > m_limits.restarts) {
        setState(server, State::Stopped, why);
        return;
    }
    setState(server, State::Idle);
    if (!server.texts.empty())
        start(server);
}

LanguageServers::State LanguageServers::state(const std::string &plugin, const std::string &server) const
{
    const auto it = m_servers.find({plugin, server});
    return it == m_servers.end() ? State::Idle : it->second->state;
}

QString LanguageServers::stoppedReason(const std::string &plugin, const std::string &server) const
{
    const auto it = m_servers.find({plugin, server});
    return it == m_servers.end() ? QString() : it->second->reason;
}

void LanguageServers::startAgain(const std::string &plugin, const std::string &server)
{
    const auto it = m_servers.find({plugin, server});
    if (it == m_servers.end() || it->second->state != State::Stopped)
        return;
    it->second->crashes.clear();
    setState(*it->second, State::Idle);
    if (!it->second->texts.empty())
        start(*it->second);
}

void LanguageServers::setState(Server &server, State state, QString reason)
{
    if (server.state == state && server.reason == reason)
        return;
    server.state = state;
    server.reason = std::move(reason);
    emit statesChanged();
}

void LanguageServers::log(const Server &server, const QString &line) const
{
    if (m_wiring.log && !line.isEmpty())
        m_wiring.log(server.entry.plugin, line);
}

}
