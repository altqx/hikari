#pragma once

#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>

#include <cstring>
#include <optional>

namespace hikari::test {

class FakeReply : public QNetworkReply {
public:
    FakeReply(const QNetworkRequest &request, std::optional<QByteArray> body, QObject *parent)
        : QNetworkReply(parent), m_body(body.value_or(QByteArray()))
    {
        setRequest(request);
        setUrl(request.url());
        setOperation(QNetworkAccessManager::GetOperation);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, body ? 200 : 404);
        if (!body)
            setError(ContentNotFoundError, QStringLiteral("Not found"));
        QMetaObject::invokeMethod(this, [this] {
            emit downloadProgress(m_body.size(), m_body.size());
            if (isFinished())
                return;
            emit readyRead();
            setFinished(true);
            emit finished();
        }, Qt::QueuedConnection);
    }
    void abort() override
    {
        if (isFinished())
            return;
        setError(OperationCanceledError, QStringLiteral("Cancelled"));
        setFinished(true);
        emit errorOccurred(OperationCanceledError);
        emit finished();
    }
    qint64 bytesAvailable() const override { return m_body.size() - m_offset + QIODevice::bytesAvailable(); }
    bool isSequential() const override { return true; }

protected:
    qint64 readData(char *data, qint64 max) override
    {
        const qint64 n = std::min(max, qint64(m_body.size()) - m_offset);
        std::memcpy(data, m_body.constData() + m_offset, std::size_t(n));
        m_offset += n;
        return n;
    }

private:
    QByteArray m_body;
    qint64 m_offset = 0;
};

// Serves `files` by URL, 404 for the rest, and records every request.
class FakeNetwork : public QNetworkAccessManager {
public:
    QMap<QString, QByteArray> files;
    QList<QNetworkRequest> requests;

protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override
    {
        requests << request;
        const QString url = request.url().toString();
        return new FakeReply(request, files.contains(url) ? std::optional(files.value(url)) : std::nullopt, this);
    }
};

}
