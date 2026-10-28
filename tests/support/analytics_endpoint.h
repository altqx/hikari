#pragma once

// AN1 (#404): a fake PostHog capture endpoint on the loopback, which records
// each request and answers 200; and the check every test of what is sent
// shares: only the table's events and properties, and no string that could
// be free text (a path, a file name, a line's text, a name the user gave).

#include "hikari/app/analytics.h"

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

namespace hikari::test {

class AnalyticsEndpoint : public QObject {
public:
    struct Request {
        QString method;
        QString path;
        QByteArray body;
    };

    AnalyticsEndpoint()
    {
        m_server.listen(QHostAddress::LocalHost);
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] { read(socket); });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }

    bool listening() const { return m_server.isListening(); }
    quint16 port() const { return m_server.serverPort(); }
    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1/batch/").arg(m_server.serverPort())); }

    // Every event of every request, in order.
    QJsonArray events() const
    {
        QJsonArray out;
        for (const Request &r : requests)
            for (const QJsonValue &e : QJsonDocument::fromJson(r.body).object().value(QStringLiteral("batch")).toArray())
                out.append(e);
        return out;
    }

    QList<Request> requests;

    // Whether every event in `batch` is the table's, with the common
    // properties and its own only, and every string in it is an id, a
    // bucket, a version, a UUID or a time: never free text. `why` says
    // what failed.
    static bool closedOnly(const QJsonArray &batch, QString *why)
    {
        static const QStringList common{QStringLiteral("distinct_id"), QStringLiteral("$process_person_profile"),
                                        QStringLiteral("$geoip_disable"), QStringLiteral("$session_id"),
                                        QStringLiteral("$lib"), QStringLiteral("$lib_version")};
        for (const QJsonValue &value : batch) {
            const QJsonObject event = value.toObject();
            const QString name = event.value(QStringLiteral("event")).toString();
            if (event.keys() != QStringList{QStringLiteral("distinct_id"), QStringLiteral("event"),
                                            QStringLiteral("properties"), QStringLiteral("timestamp")}) {
                *why = name + QStringLiteral(": keys ") + event.keys().join(u',');
                return false;
            }
            QJsonObject own = event.value(QStringLiteral("properties")).toObject();
            for (const QString &key : common) {
                if (!own.contains(key)) {
                    *why = name + QStringLiteral(": no ") + key;
                    return false;
                }
                own.remove(key);
            }
            if (!hikari::app::Analytics::conforms(name, own)) {
                *why = name + QStringLiteral(" does not conform: ") +
                       QString::fromUtf8(QJsonDocument(own).toJson(QJsonDocument::Compact));
                return false;
            }
            if (!plainStrings(event, why)) {
                *why = name + QStringLiteral(": ") + *why;
                return false;
            }
        }
        return true;
    }

private:
    // Each string: ids, buckets, versions, UUIDs and ISO times look so; a
    // path's slashes, a sentence's spaces, other scripts' letters do not.
    // The one string with a space is the table's "user script".
    static bool plainStrings(const QJsonValue &value, QString *why)
    {
        static const QRegularExpression plain(QStringLiteral("^[A-Za-z0-9_.:+<>$-]{0,80}$"));
        if (value.isString()) {
            const QString s = value.toString();
            if (s != QLatin1String("user script") && !plain.match(s).hasMatch()) {
                *why = QStringLiteral("free text? \"") + s + QLatin1Char('"');
                return false;
            }
            return true;
        }
        if (value.isArray()) {
            for (const QJsonValue &v : value.toArray())
                if (!plainStrings(v, why))
                    return false;
            return true;
        }
        if (value.isObject()) {
            const QJsonObject o = value.toObject();
            for (auto it = o.begin(); it != o.end(); ++it)
                if (!plainStrings(QJsonValue(it.key()), why) || !plainStrings(it.value(), why))
                    return false;
        }
        return true;
    }

    void read(QTcpSocket *socket)
    {
        QByteArray data = socket->property("request").toByteArray() + socket->readAll();
        socket->setProperty("request", data);
        const qsizetype headerEnd = data.indexOf("\r\n\r\n");
        if (headerEnd < 0)
            return;
        const QByteArray head = data.left(headerEnd);
        qsizetype length = 0;
        for (const QByteArray &line : head.split('\n'))
            if (line.toLower().startsWith("content-length:"))
                length = line.mid(15).trimmed().toLongLong();
        if (data.size() < headerEnd + 4 + length)
            return;
        const QList<QByteArray> first = head.split('\n').value(0).split(' ');
        requests.append({QString::fromLatin1(first.value(0)), QString::fromLatin1(first.value(1)),
                         data.mid(headerEnd + 4, length)});
        const QByteArray body = "{\"status\":1}";
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                      QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
};

} // namespace hikari::test
