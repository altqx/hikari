#pragma once

#include <QByteArray>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>

#include <map>

namespace hikari::testing {

// Serves `files` by path over HTTP on loopback, 404 for the rest.
class FakeFeedServer : public QObject {
public:
    FakeFeedServer()
    {
        m_server.listen(QHostAddress::LocalHost);
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] { answer(socket); });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }

    bool listening() const { return m_server.isListening(); }
    QString url(const QString &path) const
    {
        return QStringLiteral("http://127.0.0.1:%1%2").arg(m_server.serverPort()).arg(path);
    }

    // A DependencyControl feed at /feed.json whose one macro, `nameSpace`, is `script` as /<nameSpace>.lua.
    void publishMacro(const QString &nameSpace, const QString &name, const QByteArray &script,
                      const QString &version = QStringLiteral("1.0.0"))
    {
        const QString path = QLatin1Char('/') + nameSpace + QStringLiteral(".lua");
        files[path] = script;
        const QJsonObject file{{QStringLiteral("name"), QStringLiteral(".lua")},
                               {QStringLiteral("url"), url(path)},
                               {QStringLiteral("sha1"),
                                QString::fromLatin1(QCryptographicHash::hash(script, QCryptographicHash::Sha1).toHex())}};
        const QJsonObject channel{{QStringLiteral("version"), version},
                                  {QStringLiteral("released"), QStringLiteral("2026-11-04")},
                                  {QStringLiteral("default"), true},
                                  {QStringLiteral("files"), QJsonArray{file}}};
        const QJsonObject macro{{QStringLiteral("name"), name},
                                {QStringLiteral("author"), QStringLiteral("test")},
                                {QStringLiteral("channels"), QJsonObject{{QStringLiteral("stable"), channel}}}};
        files[QStringLiteral("/feed.json")] =
            QJsonDocument(QJsonObject{{QStringLiteral("dependencyControlFeedFormatVersion"), QStringLiteral("0.4.0")},
                                      {QStringLiteral("name"), QStringLiteral("test feed")},
                                      {QStringLiteral("macros"), QJsonObject{{nameSpace, macro}}}})
                .toJson();
    }

    // DependencyControl's config trusting this server's feed; it refuses loopback feeds otherwise.
    QByteArray trustingConfig(const QJsonObject &rest = {}) const
    {
        QJsonObject config = rest;
        config.insert(QStringLiteral("config"),
                      QJsonObject{{QStringLiteral("updates"), QJsonObject{{QStringLiteral("blockPrivateHosts"), false}}},
                                  {QStringLiteral("feeds"),
                                   QJsonObject{{QStringLiteral("trustedFeeds"), QJsonArray{url(QStringLiteral("/feed.json"))}},
                                               {QStringLiteral("cacheMaxAge"), 0},
                                               {QStringLiteral("feedFetchTimeout"), 5}}}});
        return QJsonDocument(config).toJson();
    }

    std::map<QString, QByteArray> files;

private:
    void answer(QTcpSocket *socket)
    {
        const QByteArray request = socket->property("request").toByteArray() + socket->readAll();
        socket->setProperty("request", request);
        if (!request.contains("\r\n\r\n"))
            return;
        const auto it = files.find(QString::fromLatin1(request.split(' ').value(1)));
        const QByteArray body = it == files.end() ? QByteArray("not found") : it->second;
        socket->write(QByteArray(it == files.end() ? "HTTP/1.1 404 Not Found" : "HTTP/1.1 200 OK") +
                      "\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" +
                      body);
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
};

}
