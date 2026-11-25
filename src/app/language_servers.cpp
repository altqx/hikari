#include "hikari/app/language_servers.h"

#include "hikari/backends/program_process.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>

#include <algorithm>
#include <memory>

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

QString uriOf(std::uint64_t document, std::uint64_t line, ProblemField field)
{
    if (line)
        return QStringLiteral("hikari://document/%1/line/%2/%3").arg(document).arg(line).arg(fieldName(field));
    return QStringLiteral("hikari://document/%1/%2").arg(document).arg(fieldName(field));
}

QString fromU8(const std::u8string &text)
{
    return QString::fromUtf8(reinterpret_cast<const char *>(text.data()), qsizetype(text.size()));
}

bool offers(const QJsonObject &capabilities, const QString &provider)
{
    const QJsonValue value = capabilities.value(provider);
    return value.isObject() || (value.isBool() && value.toBool());
}

struct Span {
    int line = -1;
    int from = 0;
    int to = 0;
};

// A range inside one LSP line, or line -1.
Span spanOf(const QJsonValue &range)
{
    const QJsonObject start = range[QStringLiteral("start")].toObject();
    const QJsonObject end = range[QStringLiteral("end")].toObject();
    const int line = start.value(QStringLiteral("line")).toInt(-1);
    if (line < 0 || end.value(QStringLiteral("line")).toInt(-1) != line)
        return {};
    return {line, start.value(QStringLiteral("character")).toInt(-1), end.value(QStringLiteral("character")).toInt(-1)};
}

bool isWordCharacter(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_');
}

std::pair<int, int> wordAt(const QString &text, int offset)
{
    int from = std::clamp(offset, 0, int(text.size()));
    int to = from;
    while (from > 0 && isWordCharacter(text[from - 1]))
        --from;
    while (to < text.size() && isWordCharacter(text[to]))
        ++to;
    return {from, to};
}

QString contentsText(const QJsonValue &contents)
{
    if (contents.isString())
        return contents.toString();
    if (contents.isObject())
        return contents[QStringLiteral("value")].toString();
    QStringList parts;
    for (const QJsonValue &part : contents.toArray())
        if (const QString text = contentsText(part); !text.isEmpty())
            parts.push_back(text);
    return parts.join(QStringLiteral("\n\n"));
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

std::vector<std::pair<int, int>> blankedRanges(const QString &text)
{
    std::vector<std::pair<int, int>> out;
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text[i] == QLatin1Char('{')) {
            const qsizetype end = text.indexOf(QLatin1Char('}'), i + 1);
            if (end < 0)
                break;
            out.emplace_back(int(i), int(end + 1));
            i = end;
        } else if (text[i] == QLatin1Char('\\') && i + 1 < text.size() &&
                   (text[i + 1] == QLatin1Char('N') || text[i + 1] == QLatin1Char('n') || text[i + 1] == QLatin1Char('h'))) {
            out.emplace_back(int(i), int(i + 2));
            ++i;
        }
    }
    return out;
}

QString blankTags(const QString &text)
{
    QString out = text;
    for (const auto &[from, to] : blankedRanges(text))
        std::fill(out.begin() + from, out.begin() + to, QLatin1Char(' '));
    return out;
}

namespace {

QString lineText(const core::LineRecord &line, ProblemField field, plugins::LanguageServer::Tags tags)
{
    if (line.comment)
        return {};
    QString text = fromU8(application::problemFieldText(line, field));
    text.replace(QLatin1Char('\r'), QLatin1Char(' ')).replace(QLatin1Char('\n'), QLatin1Char(' '));
    return tags == plugins::LanguageServer::Tags::Blank ? blankTags(text) : text;
}

void addLine(ServerText &out, QStringList &lines, const core::LineRecord &line, ProblemField field,
             plugins::LanguageServer::Tags tags)
{
    QString text = lineText(line, field, tags);
    out.lines.push_back(line.id);
    out.lengths.push_back(int(text.size()));
    out.comments.push_back(line.comment);
    out.fields.push_back(application::problemFieldText(line, field));
    lines.push_back(std::move(text));
}

}

ServerText serverText(const core::Document &document, ProblemField field, plugins::LanguageServer::Tags tags)
{
    ServerText out;
    QStringList lines;
    for (const core::LineRecord *line : document.lines())
        addLine(out, lines, *line, field, tags);
    out.text = lines.join(QLatin1Char('\n'));
    return out;
}

ServerText lineServerText(const core::LineRecord &line, ProblemField field, plugins::LanguageServer::Tags tags)
{
    ServerText out;
    QStringList lines;
    addLine(out, lines, line, field, tags);
    out.text = lines.front();
    return out;
}

namespace {

struct Diagnosed {
    Problem problem;
    int line = 0;
    QJsonObject diagnostic;
};

std::vector<Diagnosed> diagnosed(const QJsonArray &diagnostics, const ServerText &text, ProblemField field)
{
    std::vector<Diagnosed> out;
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
        out.push_back({std::move(problem), line, diagnostic});
    }
    return out;
}

std::vector<Problem> problemsOf(std::vector<Diagnosed> entries)
{
    std::vector<Problem> out;
    out.reserve(entries.size());
    for (Diagnosed &entry : entries)
        out.push_back(std::move(entry.problem));
    return out;
}

bool sameDiagnostic(const QJsonObject &a, const QJsonObject &b)
{
    return a.value(QStringLiteral("range")) == b.value(QStringLiteral("range")) &&
           a.value(QStringLiteral("message")) == b.value(QStringLiteral("message"));
}

}

std::vector<Problem> diagnosticProblems(const QJsonArray &diagnostics, const ServerText &text, ProblemField field)
{
    return problemsOf(diagnosed(diagnostics, text, field));
}

std::optional<application::ProblemFix> quickFix(const QJsonObject &action, const QString &uri, const ServerText &text, int line,
                                                plugins::LanguageServer::Tags tags)
{
    const QString title = action.value(QStringLiteral("title")).toString();
    const QString kind = action.value(QStringLiteral("kind")).toString();
    if (title.isEmpty() || action.contains(QStringLiteral("command")) || action.contains(QStringLiteral("disabled")) ||
        (!kind.isEmpty() && kind != QLatin1String("quickfix") && !kind.startsWith(QLatin1String("quickfix."))))
        return std::nullopt;
    if (line < 0 || std::size_t(line) >= text.lines.size() || text.comments[std::size_t(line)])
        return std::nullopt;
    const QJsonObject edit = action.value(QStringLiteral("edit")).toObject();
    QJsonArray edits;
    for (const QJsonValue &change : edit.value(QStringLiteral("documentChanges")).toArray()) {
        if (change[QStringLiteral("kind")].isString() || change[QStringLiteral("textDocument")][QStringLiteral("uri")].toString() != uri)
            return std::nullopt;
        for (const QJsonValue &e : change[QStringLiteral("edits")].toArray())
            edits.append(e);
    }
    const QJsonObject changes = edit.value(QStringLiteral("changes")).toObject();
    for (auto it = changes.begin(); it != changes.end(); ++it) {
        if (it.key() != uri)
            return std::nullopt;
        for (const QJsonValue &e : it.value().toArray())
            edits.append(e);
    }
    if (edits.isEmpty())
        return std::nullopt;
    const int length = text.lengths[std::size_t(line)];
    const auto blanked = tags == plugins::LanguageServer::Tags::Blank ? blankedRanges(fromU8(text.fields[std::size_t(line)]))
                                                                      : std::vector<std::pair<int, int>>{};
    application::ProblemFix fix{title.toStdString(), {}};
    for (const QJsonValue &e : edits) {
        const Span span = spanOf(e[QStringLiteral("range")]);
        const QString insert = e[QStringLiteral("newText")].toString();
        if (span.line != line || span.from < 0 || span.from > span.to || span.to > length || insert.contains(QLatin1Char('\n')) ||
            insert.contains(QLatin1Char('\r')))
            return std::nullopt;
        const bool touches = std::ranges::any_of(blanked, [&](const auto &b) {
            return span.from == span.to ? b.first < span.from && span.from < b.second : span.from < b.second && b.first < span.to;
        });
        if (touches)
            return std::nullopt;
        fix.edits.push_back({std::size_t(span.from), std::size_t(span.to), insert.toStdString()});
    }
    std::ranges::sort(fix.edits, {}, &application::ProblemEdit::from);
    for (std::size_t i = 1; i < fix.edits.size(); ++i)
        if (fix.edits[i].from < fix.edits[i - 1].to)
            return std::nullopt;
    return fix;
}

std::vector<CompletionItem> serverCompletions(const QJsonValue &answer, int line, const QString &text, int offset, const QString &source)
{
    const QJsonArray items = answer.isArray() ? answer.toArray() : answer[QStringLiteral("items")].toArray();
    const QJsonObject defaults = answer[QStringLiteral("itemDefaults")].toObject();
    const int length = int(text.size());
    std::vector<std::pair<QString, CompletionItem>> ranked;
    for (const QJsonValue &value : items) {
        const QJsonObject item = value.toObject();
        CompletionItem out;
        out.label = item.value(QStringLiteral("label")).toString();
        if (out.label.isEmpty())
            continue;
        QJsonValue range;
        if (const QJsonObject edit = item.value(QStringLiteral("textEdit")).toObject(); !edit.isEmpty()) {
            out.insert = edit.value(QStringLiteral("newText")).toString();
            range = edit.contains(QStringLiteral("range")) ? edit.value(QStringLiteral("range")) : edit.value(QStringLiteral("replace"));
        } else {
            out.insert = item.value(item.contains(QStringLiteral("textEditText")) ? QStringLiteral("textEditText") : QStringLiteral("insertText"))
                             .toString(out.label);
            range = defaults.value(QStringLiteral("editRange"));
            if (range[QStringLiteral("replace")].isObject())
                range = range[QStringLiteral("replace")];
        }
        if (range.isObject()) {
            const Span span = spanOf(range);
            if (span.line != line || span.from < 0 || span.from > span.to || span.to > length)
                continue;
            out.from = span.from;
            out.to = span.to;
        } else {
            out.from = wordAt(text, offset).first;
            out.to = std::clamp(offset, out.from, length);
        }
        out.snippet = item.value(QStringLiteral("insertTextFormat")).toInt(defaults.value(QStringLiteral("insertTextFormat")).toInt(1)) == 2;
        out.detail = item.value(QStringLiteral("detail")).toString();
        out.source = source;
        ranked.emplace_back(item.value(QStringLiteral("sortText")).toString(out.label), std::move(out));
    }
    std::ranges::stable_sort(ranked, {}, &std::pair<QString, CompletionItem>::first);
    std::vector<CompletionItem> out;
    for (auto &[sort, item] : ranked)
        if (out.size() < kMostCompletions)
            out.push_back(std::move(item));
    return out;
}

std::optional<HoverText> serverHover(const QJsonValue &answer, int line, const QString &text, int offset, const QString &source)
{
    const QString contents = contentsText(answer[QStringLiteral("contents")]).trimmed();
    if (contents.isEmpty())
        return std::nullopt;
    const Span span = spanOf(answer[QStringLiteral("range")]);
    if (span.line == line && span.from >= 0 && span.from <= span.to && span.to <= text.size())
        return HoverText{contents, source, span.from, span.to};
    const auto [from, to] = wordAt(text, offset);
    return HoverText{contents, source, from, to};
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
        bool changed = false;
        if (server->entry.server.scope == plugins::LanguageServer::Scope::Line)
            syncLines(*server, open, changed);
        else
            syncDocuments(*server, open, changed);
        if (!changed)
            continue;
        if (!server->connection && server->state != State::Stopped && !server->texts.empty())
            start(*server);
        server->flush.start(m_limits.syncMs);
    }
}

void LanguageServers::syncDocuments(Server &server, const Open &open, bool &changed)
{
    for (auto it = server.texts.begin(); it != server.texts.end();) {
        const bool stillOpen = std::ranges::any_of(open, [&](const auto &o) { return o.first.value == std::get<0>(it->first) && o.second; });
        if (stillOpen) {
            ++it;
            continue;
        }
        close(server, it->second);
        it = server.texts.erase(it);
        changed = true;
    }
    for (const auto &[id, session] : open) {
        if (!session)
            continue;
        for (const std::string &name : server.entry.server.fields) {
            const auto field = fieldNamed(name);
            if (!field)
                continue;
            Text &text = server.texts[{id.value, 0, int(*field)}];
            if (text.revision == session->revision())
                continue;
            text.document = id.value;
            text.field = *field;
            text.revision = session->revision();
            text.pending = serverText(session->document(), *field, server.entry.server.tags);
            text.dirty = true;
            changed = true;
        }
    }
}

void LanguageServers::syncLines(Server &server, const Open &open, bool &changed)
{
    const Editing editing = m_wiring.editing ? m_wiring.editing() : std::nullopt;
    const application::EditSession *session = nullptr;
    if (editing)
        for (const auto &[id, s] : open)
            if (id == editing->first)
                session = s;
    const core::LineRecord *line = nullptr;
    if (session)
        for (const core::LineRecord *record : session->document().lines())
            if (record->id == editing->second)
                line = record;
    for (auto it = server.texts.begin(); it != server.texts.end();) {
        if (line && std::get<0>(it->first) == editing->first.value && std::get<1>(it->first) == line->id.value) {
            ++it;
            continue;
        }
        close(server, it->second);
        it = server.texts.erase(it);
        changed = true;
    }
    if (!line)
        return;
    for (const std::string &name : server.entry.server.fields) {
        const auto field = fieldNamed(name);
        if (!field)
            continue;
        Text &text = server.texts[{editing->first.value, line->id.value, int(*field)}];
        if (text.revision == session->revision())
            continue;
        text.document = editing->first.value;
        text.line = line->id.value;
        text.field = *field;
        text.revision = session->revision();
        ServerText pending = lineServerText(*line, *field, server.entry.server.tags);
        if (text.opened && pending.text == text.sent.text) {
            text.sent = pending;
            continue;
        }
        text.pending = std::move(pending);
        text.dirty = true;
        changed = true;
    }
}

void LanguageServers::close(Server &server, Text &text)
{
    if (text.opened && server.connection && server.state == State::Running)
        server.connection->notify(QStringLiteral("textDocument/didClose"),
                                  QJsonObject{{QStringLiteral("textDocument"),
                                               QJsonObject{{QStringLiteral("uri"), uriOf(text.document, text.line, text.field)}}}});
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
                     {QStringLiteral("publishDiagnostics"), QJsonObject{{QStringLiteral("versionSupport"), true}}},
                     {QStringLiteral("completion"),
                      QJsonObject{{QStringLiteral("completionItem"), QJsonObject{{QStringLiteral("snippetSupport"), true}}}}},
                     {QStringLiteral("hover"),
                      QJsonObject{{QStringLiteral("contentFormat"), QJsonArray{QStringLiteral("plaintext"), QStringLiteral("markdown")}}}},
                     {QStringLiteral("codeAction"),
                      QJsonObject{{QStringLiteral("codeActionLiteralSupport"),
                                   QJsonObject{{QStringLiteral("codeActionKind"),
                                                QJsonObject{{QStringLiteral("valueSet"), QJsonArray{QStringLiteral("quickfix")}}}}}}}}}},
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
                                   server.capabilities = (*result)[QStringLiteral("capabilities")].toObject();
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
        const QString uri = uriOf(text.document, text.line, text.field);
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
        if (!text.opened || uriOf(text.document, text.line, text.field) != uri)
            continue;
        if (const QJsonValue version = params[QStringLiteral("version")]; version.isDouble() && version.toInteger() != text.sentVersion)
            return;
        const QJsonArray diagnostics = params[QStringLiteral("diagnostics")].toArray();
        ++text.published;
        setProblems(server, text, diagnosticProblems(diagnostics, text.sent, text.field));
        requestFixes(server, text, diagnostics);
        return;
    }
}

void LanguageServers::setProblems(const Server &server, const Text &text, std::vector<Problem> problems)
{
    if (m_wiring.problems)
        m_wiring.problems->set("plugin:" + server.entry.plugin, source(server), server.entry.server.title,
                               application::DocumentId{text.document}, fieldName(text.field).toStdString(), std::move(problems));
}

void LanguageServers::requestFixes(Server &server, Text &text, const QJsonArray &diagnostics)
{
    if (!offers(server.capabilities, QStringLiteral("codeActionProvider")) || !server.connection)
        return;
    struct Gather {
        std::vector<Diagnosed> entries;
        std::vector<std::vector<std::pair<bool, application::ProblemFix>>> fixes;
        std::vector<qint64> asked;
        std::size_t outstanding = 0;
        bool issued = false;
        bool finished = false;
    };
    auto gather = std::make_shared<Gather>();
    gather->entries = diagnosed(diagnostics, text.sent, text.field);
    gather->fixes.resize(gather->entries.size());
    const Key key{server.entry.plugin, server.entry.server.id};
    const TextKey textKey{text.document, text.line, int(text.field)};
    const std::uint64_t published = text.published;
    const QString uri = uriOf(text.document, text.line, text.field);
    const auto finish = [this, gather, key, textKey, published] {
        if (gather->finished)
            return;
        gather->finished = true;
        const auto it = m_servers.find(key);
        if (it == m_servers.end())
            return;
        const auto found = it->second->texts.find(textKey);
        if (found == it->second->texts.end() || found->second.published != published)
            return;
        bool any = false;
        for (std::size_t i = 0; i < gather->entries.size(); ++i) {
            auto &fixes = gather->fixes[i];
            std::ranges::stable_sort(fixes, [](const auto &a, const auto &b) { return a.first && !b.first; });
            for (auto &[preferred, fix] : fixes)
                if (gather->entries[i].problem.fixes.size() < 8)
                    gather->entries[i].problem.fixes.push_back(std::move(fix));
            any = any || !gather->entries[i].problem.fixes.empty();
        }
        if (any)
            setProblems(*it->second, found->second, problemsOf(std::move(gather->entries)));
    };
    std::map<int, QJsonArray> lines;
    for (const Diagnosed &entry : gather->entries)
        if (lines.size() < m_limits.mostCodeActionLines || lines.contains(entry.line))
            lines[entry.line].append(entry.diagnostic);
    const ServerText sent = text.sent;
    const auto tags = server.entry.server.tags;
    for (const auto &[line, onLine] : lines) {
        const QJsonObject params{
            {QStringLiteral("textDocument"), QJsonObject{{QStringLiteral("uri"), uri}}},
            {QStringLiteral("range"),
             QJsonObject{{QStringLiteral("start"), QJsonObject{{QStringLiteral("line"), line}, {QStringLiteral("character"), 0}}},
                         {QStringLiteral("end"),
                          QJsonObject{{QStringLiteral("line"), line}, {QStringLiteral("character"), sent.lengths[std::size_t(line)]}}}}},
            {QStringLiteral("context"),
             QJsonObject{{QStringLiteral("diagnostics"), onLine},
                         {QStringLiteral("only"), QJsonArray{QStringLiteral("quickfix")}},
                         {QStringLiteral("triggerKind"), 2}}},
        };
        ++gather->outstanding;
        const qint64 id = server.connection->request(
            QStringLiteral("textDocument/codeAction"), params,
            [gather, finish, line, onLine, uri, sent, tags](std::expected<QJsonValue, LspConnection::Failure> result) {
                if (gather->finished)
                    return;
                --gather->outstanding;
                if (result)
                    for (const QJsonValue &value : result->toArray()) {
                        const QJsonObject action = value.toObject();
                        auto fix = quickFix(action, uri, sent, line, tags);
                        if (!fix)
                            continue;
                        const QJsonArray named = action.value(QStringLiteral("diagnostics")).toArray();
                        const bool preferred = action.value(QStringLiteral("isPreferred")).toBool();
                        for (std::size_t i = 0; i < gather->entries.size(); ++i) {
                            const Diagnosed &entry = gather->entries[i];
                            if (entry.line != line)
                                continue;
                            const bool meant = named.isEmpty() ? onLine.size() == 1
                                                               : std::ranges::any_of(named, [&](const QJsonValue &d) {
                                                                     return sameDiagnostic(d.toObject(), entry.diagnostic);
                                                                 });
                            if (meant)
                                gather->fixes[i].emplace_back(preferred, *fix);
                        }
                    }
                if (gather->issued && gather->outstanding == 0)
                    finish();
            });
        gather->asked.push_back(id);
    }
    gather->issued = true;
    if (gather->outstanding == 0)
        return finish();
    QTimer::singleShot(m_limits.codeActionMs, this, [this, gather, finish, key] {
        if (gather->finished)
            return;
        if (const auto it = m_servers.find(key); it != m_servers.end() && it->second->connection)
            for (const qint64 id : gather->asked)
                it->second->connection->cancel(id);
        finish();
    });
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
    server.capabilities = {};
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

std::vector<LanguageServers::Stopped> LanguageServers::stopped(const std::string &plugin) const
{
    std::vector<Stopped> out;
    for (const auto &[key, server] : m_servers)
        if (key.first == plugin && server->state == State::Stopped)
            out.push_back({key.second, QString::fromStdString(server->entry.server.title), server->reason});
    return out;
}

bool LanguageServers::serves(const Server &server, const QString &field, const QString &provider) const
{
    return server.state == State::Running && server.connection &&
           std::ranges::find(server.entry.server.fields, field.toStdString()) != server.entry.server.fields.end() &&
           offers(server.capabilities, provider);
}

QStringList LanguageServers::triggers(const QString &field) const
{
    QStringList out;
    for (const auto &[key, server] : m_servers)
        if (serves(*server, field, QStringLiteral("completionProvider")))
            for (const QJsonValue &trigger : server->capabilities[QStringLiteral("completionProvider")][QStringLiteral("triggerCharacters")].toArray())
                if (const QString t = trigger.toString(); !t.isEmpty() && !out.contains(t))
                    out.push_back(t);
    return out;
}

bool LanguageServers::hovers(const QString &field) const
{
    return std::ranges::any_of(m_servers, [&](const auto &entry) { return serves(*entry.second, field, QStringLiteral("hoverProvider")); });
}

std::optional<std::pair<LanguageServers::Text *, int>> LanguageServers::textFor(Server &server, const LanguageProviders::Request &request)
{
    const auto field = fieldNamed(request.field.toStdString());
    if (!field)
        return std::nullopt;
    const bool lineScope = server.entry.server.scope == plugins::LanguageServer::Scope::Line;
    const auto it = server.texts.find({request.document, lineScope ? request.line : 0, int(*field)});
    if (it == server.texts.end())
        return std::nullopt;
    if (it->second.dirty)
        flush(server);
    Text &text = it->second;
    if (!text.opened)
        return std::nullopt;
    const auto at = std::ranges::find(text.sent.lines, core::LineId{request.line});
    if (at == text.sent.lines.end() || text.sent.comments[std::size_t(at - text.sent.lines.begin())])
        return std::nullopt;
    return std::pair{&text, int(at - text.sent.lines.begin())};
}

template<typename Answer>
void LanguageServers::ask(const QString &method, const QString &provider, const LanguageProviders::Request &request,
                          std::function<Answer(const QJsonValue &, const Server &, int line)> read,
                          std::function<void(std::vector<Answer>)> done)
{
    struct Gather {
        std::vector<std::optional<Answer>> answers;
        std::size_t waiting = 0;
        bool finished = false;
        std::function<void(std::vector<Answer>)> done;
    };
    auto gather = std::make_shared<Gather>();
    gather->done = std::move(done);
    const auto finish = [gather] {
        if (gather->finished)
            return;
        gather->finished = true;
        std::vector<Answer> out;
        for (auto &answer : gather->answers)
            if (answer)
                out.push_back(std::move(*answer));
        gather->done(std::move(out));
    };
    const bool completion = provider == QLatin1String("completionProvider");
    const auto order = m_wiring.servers ? m_wiring.servers() : std::vector<PluginRuntime::LanguageServer>{};
    for (const PluginRuntime::LanguageServer &entry : order) {
        const Key key{entry.plugin, entry.server.id};
        const auto it = m_servers.find(key);
        if (it == m_servers.end() || !serves(*it->second, request.field, provider))
            continue;
        Server &server = *it->second;
        if (completion && !request.trigger.isEmpty() &&
            !server.capabilities[provider][QStringLiteral("triggerCharacters")].toArray().contains(request.trigger))
            continue;
        const auto target = textFor(server, request);
        if (!target)
            continue;
        const int line = target->second;
        QJsonObject params{
            {QStringLiteral("textDocument"), QJsonObject{{QStringLiteral("uri"), uriOf(target->first->document, target->first->line, target->first->field)}}},
            {QStringLiteral("position"), QJsonObject{{QStringLiteral("line"), line}, {QStringLiteral("character"), request.offset}}},
        };
        if (completion) {
            QJsonObject context{{QStringLiteral("triggerKind"), request.trigger.isEmpty() ? 1 : 2}};
            if (!request.trigger.isEmpty())
                context.insert(QStringLiteral("triggerCharacter"), request.trigger);
            params.insert(QStringLiteral("context"), context);
        }
        const std::size_t slot = gather->answers.size();
        gather->answers.emplace_back();
        ++gather->waiting;
        auto answered = std::make_shared<bool>(false);
        const auto settle = [gather, finish, answered] {
            if (!std::exchange(*answered, true) && --gather->waiting == 0)
                finish();
        };
        const qint64 id = server.connection->request(
            method, params, [this, gather, settle, slot, read, key, line, answered](std::expected<QJsonValue, LspConnection::Failure> result) {
                if (*answered)
                    return;
                if (const auto it = m_servers.find(key); result && it != m_servers.end() && !gather->finished)
                    gather->answers[slot] = read(*result, *it->second, line);
                settle();
            });
        QTimer::singleShot(m_limits.answerMs, this, [this, key, id, settle, answered] {
            if (*answered)
                return;
            if (const auto it = m_servers.find(key); it != m_servers.end() && it->second->connection)
                it->second->connection->cancel(id);
            settle();
        });
    }
    if (gather->waiting == 0)
        finish();
}

void LanguageServers::complete(const LanguageProviders::Request &request, LanguageProviders::Completions done)
{
    ask<std::vector<CompletionItem>>(
        QStringLiteral("textDocument/completion"), QStringLiteral("completionProvider"), request,
        [text = request.text, offset = request.offset](const QJsonValue &answer, const Server &server, int line) {
            return serverCompletions(answer, line, text, offset, QString::fromStdString(server.entry.server.title));
        },
        [done = std::move(done)](std::vector<std::vector<CompletionItem>> answers) {
            std::vector<CompletionItem> items;
            for (auto &answer : answers)
                for (auto &item : answer)
                    if (items.size() < kMostCompletions)
                        items.push_back(std::move(item));
            done(std::move(items));
        });
}

void LanguageServers::hover(const LanguageProviders::Request &request, LanguageProviders::Hover done)
{
    ask<std::optional<HoverText>>(
        QStringLiteral("textDocument/hover"), QStringLiteral("hoverProvider"), request,
        [text = request.text, offset = request.offset](const QJsonValue &answer, const Server &server, int line) {
            return serverHover(answer, line, text, offset, QString::fromStdString(server.entry.server.title));
        },
        [done = std::move(done)](std::vector<std::optional<HoverText>> answers) {
            for (auto &answer : answers)
                if (answer)
                    return done(std::move(answer));
            done(std::nullopt);
        });
}

}
