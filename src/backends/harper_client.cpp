#include "hikari/backends/harper_client.h"

#include "hikari/core/grammar.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>

namespace hikari::backends {

namespace {

constexpr QByteArrayView kHeaderEnd = "\r\n\r\n";

int offsetOf(const QString &text, const QJsonObject &position)
{
    int line = position.value(QStringLiteral("line")).toInt();
    int at = 0;
    while (line > 0 && at < text.size()) {
        const int next = text.indexOf(QLatin1Char('\n'), at);
        if (next < 0)
            return int(text.size());
        at = next + 1;
        --line;
    }
    return std::min<int>(int(text.size()), at + std::max(0, position.value(QStringLiteral("character")).toInt()));
}

QJsonObject positionOf(const QString &text, int offset)
{
    const int lineStart = int(text.lastIndexOf(QLatin1Char('\n'), std::max(0, offset - 1))) + 1;
    return {{QStringLiteral("line"), int(text.left(offset).count(QLatin1Char('\n')))},
            {QStringLiteral("character"), offset - (offset > 0 ? lineStart : 0)}};
}

std::pair<int, int> rangeOf(const QString &text, const QJsonObject &range)
{
    return {offsetOf(text, range.value(QStringLiteral("start")).toObject()),
            offsetOf(text, range.value(QStringLiteral("end")).toObject())};
}

} // namespace

HarperClient::HarperClient(QString program, QString workingDirectory, QObject *parent)
    : QObject(parent), m_program(std::move(program)), m_workingDirectory(std::move(workingDirectory))
{
    qRegisterMetaType<GrammarFinding>();
    qRegisterMetaType<QList<GrammarFinding>>();
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_process.setStandardErrorFile(QProcess::nullDevice());
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &HarperClient::readOutput);
    connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        lost(status == QProcess::CrashExit ? QStringLiteral("harper-ls crashed")
                                           : QStringLiteral("harper-ls ended (exit code %1)").arg(code));
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            lost(QStringLiteral("harper-ls could not be started: %1").arg(m_process.errorString()));
    });
}

HarperClient::~HarperClient()
{
    stop();
}

void HarperClient::setSettings(const QJsonObject &settings)
{
    if (settings == m_settings)
        return;
    m_settings = settings;
    if (running())
        stop();
}

quint64 HarperClient::check(const QString &text)
{
    const quint64 ticket = m_nextTicket++;
    m_checks[ticket] = Check{text, QStringLiteral("untitled:hikari/%1").arg(ticket)};
    if (!running())
        start();
    else
        openNext();
    return ticket;
}

void HarperClient::cancel(quint64 ticket)
{
    const auto it = m_checks.find(ticket);
    if (it == m_checks.end())
        return;
    // Closing an open text now could overlap the next didOpen, deadlocking harper-ls 3.0.0.
    if (it->second.opened)
        it->second.cancelled = true;
    else
        m_checks.erase(it);
}

void HarperClient::stop()
{
    m_checks.clear();
    m_requests.clear();
    m_buffer.clear();
    m_ready = false;
    if (!running())
        return;
    const QSignalBlocker blocker(m_process);
    m_process.kill();
    m_process.waitForFinished(2000);
}

void HarperClient::start()
{
    m_ready = false;
    m_buffer.clear();
    m_process.setProgram(m_program);
    m_process.setArguments({QStringLiteral("--stdio")});
    m_process.setWorkingDirectory(m_workingDirectory);
    ++m_starts;
    m_process.start();
    request(QStringLiteral("initialize"),
            {{QStringLiteral("processId"), qint64(QCoreApplication::applicationPid())},
             {QStringLiteral("rootUri"), QJsonValue::Null},
             {QStringLiteral("clientInfo"), QJsonObject{{QStringLiteral("name"), QStringLiteral("HikariSub")}}},
             {QStringLiteral("capabilities"),
              QJsonObject{{QStringLiteral("workspace"), QJsonObject{{QStringLiteral("configuration"), true}}}}}},
            0, -1);
}

void HarperClient::send(const QJsonObject &message)
{
    if (!running())
        return;
    QJsonObject framed = message;
    framed.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    const QByteArray body = QJsonDocument(framed).toJson(QJsonDocument::Compact);
    m_process.write("Content-Length: " + QByteArray::number(body.size()) + kHeaderEnd.toByteArray() + body);
}

void HarperClient::request(const QString &method, const QJsonObject &params, quint64 check, int finding)
{
    const qint64 id = m_nextId++;
    m_requests[id] = {check, finding};
    send({{QStringLiteral("id"), id}, {QStringLiteral("method"), method}, {QStringLiteral("params"), params}});
}

void HarperClient::notify(const QString &method, const QJsonObject &params)
{
    send({{QStringLiteral("method"), method}, {QStringLiteral("params"), params}});
}

void HarperClient::openNext()
{
    // One text at a time: overlapping didOpen settings pulls deadlock harper-ls 3.0.0.
    if (!m_ready || std::ranges::any_of(m_checks, [](const auto &c) { return c.second.opened; }))
        return;
    const auto next = std::ranges::find_if(m_checks, [](const auto &c) { return !c.second.opened; });
    if (next == m_checks.end())
        return;
    auto &check = next->second;
    check.opened = true;
    notify(QStringLiteral("textDocument/didOpen"),
           {{QStringLiteral("textDocument"),
             QJsonObject{{QStringLiteral("uri"), check.uri},
                         {QStringLiteral("languageId"), QStringLiteral("plaintext")},
                         {QStringLiteral("version"), 1},
                         {QStringLiteral("text"), check.text}}}});
}

void HarperClient::readOutput()
{
    m_buffer += m_process.readAllStandardOutput();
    while (true) {
        const qsizetype headerEnd = m_buffer.indexOf(kHeaderEnd);
        if (headerEnd < 0)
            return;
        qsizetype length = -1;
        for (const QByteArray &line : m_buffer.left(headerEnd).split('\n')) {
            const QByteArray field = line.trimmed();
            if (field.toLower().startsWith("content-length:"))
                length = field.mid(15).trimmed().toLongLong();
        }
        if (length < 0) {
            lost(QStringLiteral("harper-ls sent a frame without a length"));
            return;
        }
        const qsizetype bodyStart = headerEnd + kHeaderEnd.size();
        if (m_buffer.size() < bodyStart + length)
            return;
        const QJsonDocument document = QJsonDocument::fromJson(m_buffer.mid(bodyStart, length));
        m_buffer.remove(0, bodyStart + length);
        if (document.isObject())
            handle(document.object());
        if (!running())
            return;
    }
}

void HarperClient::handle(const QJsonObject &message)
{
    const QString method = message.value(QStringLiteral("method")).toString();
    if (!method.isEmpty()) {
        if (message.contains(QStringLiteral("id"))) {
            // A server request needs an answer: the settings, or null for anything else.
            QJsonValue result = QJsonValue::Null;
            if (method == QLatin1String("workspace/configuration")) {
                QJsonArray answers;
                const auto items = message.value(QStringLiteral("params")).toObject().value(QStringLiteral("items")).toArray();
                for (qsizetype i = 0; i < std::max<qsizetype>(1, items.size()); ++i)
                    answers.append(QJsonObject{{QStringLiteral("harper-ls"), m_settings}});
                result = answers;
            }
            send({{QStringLiteral("id"), message.value(QStringLiteral("id"))}, {QStringLiteral("result"), result}});
        } else if (method == QLatin1String("textDocument/publishDiagnostics")) {
            diagnostics(message.value(QStringLiteral("params")).toObject());
        }
        return;
    }
    const auto it = m_requests.find(message.value(QStringLiteral("id")).toInteger(-1));
    if (it == m_requests.end())
        return;
    const auto [ticket, finding] = it->second;
    m_requests.erase(it);
    if (finding < 0) {
        if (message.contains(QStringLiteral("error"))) {
            lost(QStringLiteral("harper-ls refused to start: %1")
                     .arg(message.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString()));
            return;
        }
        notify(QStringLiteral("initialized"), {});
        m_ready = true;
        openNext();
        return;
    }
    actions(ticket, finding, message.value(QStringLiteral("result")));
}

void HarperClient::diagnostics(const QJsonObject &params)
{
    const QString uri = params.value(QStringLiteral("uri")).toString();
    auto it = m_checks.begin();
    while (it != m_checks.end() && it->second.uri != uri)
        ++it;
    if (it == m_checks.end() || !it->second.opened || it->second.actionsPending > 0 || !it->second.findings.isEmpty())
        return;
    const quint64 ticket = it->first;
    Check &check = it->second;
    const QJsonArray found = params.value(QStringLiteral("diagnostics")).toArray();
    for (const QJsonValue &value : found) {
        const QJsonObject diagnostic = value.toObject();
        const auto [start, end] = rangeOf(check.text, diagnostic.value(QStringLiteral("range")).toObject());
        if (start >= end)
            continue;
        // harper-ls messages are Markdown.
        const QString message = QString::fromStdU16String(
            core::grammar::plainMessage(diagnostic.value(QStringLiteral("message")).toString().toStdU16String()));
        check.findings.append(
            {start, end, diagnostic.value(QStringLiteral("code")).toVariant().toString(), message, {}, false});
    }
    if (check.findings.isEmpty() || check.cancelled) {
        finish(ticket);
        return;
    }
    // harper-ls answers a code action with the lints at one position: ask at each start.
    check.actionsPending = int(check.findings.size());
    for (int i = 0; i < check.findings.size(); ++i) {
        const QJsonObject at = positionOf(check.text, check.findings[i].start);
        request(QStringLiteral("textDocument/codeAction"),
                {{QStringLiteral("textDocument"), QJsonObject{{QStringLiteral("uri"), check.uri}}},
                 {QStringLiteral("range"), QJsonObject{{QStringLiteral("start"), at}, {QStringLiteral("end"), at}}},
                 {QStringLiteral("context"), QJsonObject{{QStringLiteral("diagnostics"), QJsonArray{found[i]}}}}},
                ticket, i);
    }
}

void HarperClient::actions(quint64 ticket, int index, const QJsonValue &result)
{
    const auto it = m_checks.find(ticket);
    if (it == m_checks.end() || index >= it->second.findings.size())
        return;
    Check &check = it->second;
    GrammarFinding &finding = check.findings[index];
    const QString covered = check.text.mid(finding.start, finding.end - finding.start);
    for (const QJsonValue &value : result.toArray()) {
        const QJsonObject action = value.toObject();
        const QJsonValue command = action.value(QStringLiteral("command"));
        // A bare Command, not a CodeAction: harper-ls's add-to-dictionary.
        if (command.isString()) {
            const QJsonArray arguments = action.value(QStringLiteral("arguments")).toArray();
            if (command.toString() == QLatin1String("HarperAddToUserDict") && !arguments.isEmpty()
                && arguments.first().toString() == covered)
                finding.word = true;
            continue;
        }
        const QJsonObject changes = action.value(QStringLiteral("edit")).toObject().value(QStringLiteral("changes")).toObject();
        for (const QJsonValue &edit : changes.value(check.uri).toArray()) {
            const QJsonObject e = edit.toObject();
            const QString with = e.value(QStringLiteral("newText")).toString();
            // harper-ls 3.0.0 offers some fixes twice, once with a trailing space: keep the first.
            if (rangeOf(check.text, e.value(QStringLiteral("range")).toObject()) == std::pair{finding.start, finding.end}
                && std::ranges::none_of(finding.suggestions,
                                        [&](const QString &s) { return s.trimmed() == with.trimmed(); }))
                finding.suggestions.append(with);
        }
    }
    if (--check.actionsPending == 0)
        finish(ticket);
}

void HarperClient::finish(quint64 ticket)
{
    const auto it = m_checks.find(ticket);
    if (it == m_checks.end())
        return;
    const QList<GrammarFinding> findings = std::move(it->second.findings);
    const bool cancelled = it->second.cancelled;
    notify(QStringLiteral("textDocument/didClose"),
           {{QStringLiteral("textDocument"), QJsonObject{{QStringLiteral("uri"), it->second.uri}}}});
    m_checks.erase(it);
    openNext();
    if (!cancelled)
        emit checked(ticket, findings);
}

void HarperClient::lost(const QString &message)
{
    m_ready = false;
    m_requests.clear();
    m_buffer.clear();
    m_checks.clear();
    if (running()) {
        const QSignalBlocker blocker(m_process);
        m_process.kill();
        m_process.waitForFinished(2000);
    }
    emit failed(message);
}

} // namespace hikari::backends
