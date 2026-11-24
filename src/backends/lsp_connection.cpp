#include "hikari/backends/lsp_connection.h"

#include "hikari/backends/program_process.h"

#include <QJsonDocument>

namespace hikari::backends {

namespace {

constexpr QByteArrayView kHeaderEnd = "\r\n\r\n";
constexpr qsizetype kMostErrorLine = 4096;

}

LspConnection::LspConnection(std::unique_ptr<ProgramProcess> process, Limits limits, QObject *parent)
    : QObject(parent), m_process(std::move(process)), m_limits(limits)
{
    m_process->setParent(nullptr);
    connect(m_process.get(), &ProgramProcess::output, this, &LspConnection::onOutput);
    connect(m_process.get(), &ProgramProcess::errorOutput, this, &LspConnection::onErrorOutput);
    connect(m_process.get(), &ProgramProcess::finished, this, [this](int exitCode, bool killed) {
        if (m_stopping || killed)
            finish(false, {});
        else
            finish(true, exitCode < 0 ? QStringLiteral("it crashed or couldn’t start")
                                      : QStringLiteral("it ended with exit code %1").arg(exitCode));
    });
    m_shutdownTimer.setSingleShot(true);
    connect(&m_shutdownTimer, &QTimer::timeout, this, [this] { m_process->kill(); });
}

LspConnection::~LspConnection()
{
    m_done = true;
    disconnect(m_process.get(), nullptr, this, nullptr);
    m_process->kill();
}

std::expected<void, QString> LspConnection::start()
{
    if (!m_process->start())
        return std::unexpected(m_process->refusal());
    return {};
}

bool LspConnection::running() const
{
    return !m_done && m_process->running();
}

qint64 LspConnection::request(const QString &method, const QJsonValue &params, Answer answer)
{
    const qint64 id = m_next++;
    if (m_done) {
        answer(std::unexpected(Failure{kGone, QStringLiteral("the language server isn’t running")}));
        return id;
    }
    m_pending.emplace(id, std::move(answer));
    send({{QStringLiteral("id"), id}, {QStringLiteral("method"), method}, {QStringLiteral("params"), params}});
    return id;
}

void LspConnection::cancel(qint64 id)
{
    if (m_pending.erase(id))
        notify(QStringLiteral("$/cancelRequest"), QJsonObject{{QStringLiteral("id"), id}});
}

void LspConnection::notify(const QString &method, const QJsonValue &params)
{
    send({{QStringLiteral("method"), method}, {QStringLiteral("params"), params}});
}

void LspConnection::shutdown()
{
    if (m_done || m_stopping)
        return;
    m_stopping = true;
    m_shutdownTimer.start(m_limits.shutdownMs);
    const qint64 id = m_next++;
    m_pending.emplace(id, [this](std::expected<QJsonValue, Failure>) {
        notify(QStringLiteral("exit"), QJsonValue::Null);
        m_process->closeInput();
    });
    send({{QStringLiteral("id"), id}, {QStringLiteral("method"), QStringLiteral("shutdown")}, {QStringLiteral("params"), QJsonValue::Null}});
}

void LspConnection::kill()
{
    m_stopping = true;
    m_process->kill();
}

void LspConnection::send(QJsonObject message)
{
    if (m_done)
        return;
    message.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    const QByteArray body = QJsonDocument(message).toJson(QJsonDocument::Compact);
    m_process->write("Content-Length: " + QByteArray::number(body.size()) + kHeaderEnd.toByteArray() + body);
}

void LspConnection::onOutput(const QByteArray &bytes)
{
    m_buffer += bytes;
    while (!m_done) {
        const qsizetype headerEnd = m_buffer.indexOf(kHeaderEnd);
        if (headerEnd < 0) {
            if (m_buffer.size() > kMostErrorLine)
                fail(QStringLiteral("it sent a header without an end"));
            return;
        }
        qsizetype length = -1;
        for (const QByteArray &line : m_buffer.left(headerEnd).split('\n'))
            if (const QByteArray field = line.trimmed(); field.toLower().startsWith("content-length:")) {
                bool ok = false;
                length = field.mid(15).trimmed().toLongLong(&ok);
                if (!ok)
                    length = -1;
            }
        if (length < 0 || length > m_limits.maxFrame) {
            fail(length < 0 ? QStringLiteral("it sent a message without a length")
                            : QStringLiteral("it sent a message larger than %1 bytes").arg(m_limits.maxFrame));
            return;
        }
        const qsizetype bodyStart = headerEnd + kHeaderEnd.size();
        if (m_buffer.size() < bodyStart + length)
            return;
        const QJsonDocument document = QJsonDocument::fromJson(m_buffer.mid(bodyStart, length));
        m_buffer.remove(0, bodyStart + length);
        if (!document.isObject()) {
            fail(QStringLiteral("it sent a message that isn’t a JSON object"));
            return;
        }
        onMessage(document.object());
    }
}

void LspConnection::onErrorOutput(const QByteArray &bytes)
{
    m_errorLine += bytes;
    for (qsizetype end = m_errorLine.indexOf('\n'); end >= 0; end = m_errorLine.indexOf('\n')) {
        const QString line = QString::fromUtf8(m_errorLine.left(end)).trimmed();
        m_errorLine.remove(0, end + 1);
        if (!line.isEmpty())
            emit errorLine(line);
    }
    if (m_errorLine.size() > kMostErrorLine) {
        emit errorLine(QString::fromUtf8(m_errorLine.left(kMostErrorLine)));
        m_errorLine.clear();
    }
}

void LspConnection::onMessage(const QJsonObject &message)
{
    const QJsonValue id = message.value(QStringLiteral("id"));
    const QString method = message.value(QStringLiteral("method")).toString();
    if (method.isEmpty()) {
        const auto it = m_pending.find(id.toInteger(-1));
        if (it == m_pending.end())
            return;
        Answer answer = std::move(it->second);
        m_pending.erase(it);
        if (const QJsonValue error = message.value(QStringLiteral("error")); error.isObject())
            answer(std::unexpected(Failure{error[QStringLiteral("code")].toInt(), error[QStringLiteral("message")].toString()}));
        else
            answer(message.value(QStringLiteral("result")));
        return;
    }
    const QJsonValue params = message.value(QStringLiteral("params"));
    if (id.isUndefined() || id.isNull()) {
        emit notification(method, params);
        return;
    }
    std::expected<QJsonValue, Failure> result =
        m_handler ? m_handler(method, params) : std::unexpected(Failure{kMethodNotFound, QStringLiteral("Unknown method")});
    if (result)
        send({{QStringLiteral("id"), id}, {QStringLiteral("result"), *result}});
    else
        send({{QStringLiteral("id"), id},
              {QStringLiteral("error"),
               QJsonObject{{QStringLiteral("code"), result.error().code}, {QStringLiteral("message"), result.error().message}}}});
}

void LspConnection::fail(const QString &why)
{
    m_process->kill();
    finish(true, why);
}

void LspConnection::finish(bool crashed, const QString &why)
{
    if (m_done)
        return;
    m_done = true;
    m_shutdownTimer.stop();
    auto pending = std::exchange(m_pending, {});
    for (auto &[id, answer] : pending)
        answer(std::unexpected(Failure{kGone, QStringLiteral("the language server ended")}));
    emit ended(crashed, why);
}

}
