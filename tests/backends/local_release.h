#pragma once

#include "hikari/backends/ai_sidecar_download.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

#include <map>
#include <vector>

#include <zlib.h>

namespace hikari::test {

using backends::ai::kSidecarManifestMember;
using backends::ai::sidecarPlatform;

inline QByteArray gzip(const QByteArray &data)
{
    z_stream stream{};
    deflateInit2(&stream, Z_BEST_COMPRESSION, Z_DEFLATED, 16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
    QByteArray out(static_cast<qsizetype>(deflateBound(&stream, static_cast<uLong>(data.size()))), '\0');
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
    stream.avail_in = static_cast<uInt>(data.size());
    stream.next_out = reinterpret_cast<Bytef *>(out.data());
    stream.avail_out = static_cast<uInt>(out.size());
    deflate(&stream, Z_FINISH);
    out.resize(static_cast<qsizetype>(stream.total_out));
    deflateEnd(&stream);
    return out;
}

class LocalRelease {
public:
    LocalRelease()
    {
        server.listen(QHostAddress::LocalHost);
        QObject::connect(&server, &QTcpServer::newConnection, [this] {
            while (QTcpSocket *socket = server.nextPendingConnection()) {
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    const QByteArray request = socket->peek(socket->bytesAvailable());
                    if (!request.contains("\r\n\r\n"))
                        return;
                    socket->readAll();
                    const QByteArray path = request.split(' ').value(1);
                    requested.push_back(path);
                    const auto it = files.find(path);
                    const QByteArray body = it == files.end() ? QByteArray("missing") : it->second;
                    socket->write((it == files.end() ? "HTTP/1.1 404 Not Found\r\n" : "HTTP/1.1 200 OK\r\n") +
                                  QByteArray("Content-Length: ") + QByteArray::number(body.size()) +
                                  "\r\nConnection: close\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }
    QUrl base() const { return QUrl(QStringLiteral("http://127.0.0.1:%1/download/").arg(server.serverPort())); }

    QTcpServer server;
    std::map<QByteArray, QByteArray> files;
    std::vector<QByteArray> requested;
};

inline QByteArray manifest(const QString &version, const QString &name, qint64 size, const QByteArray &sha256)
{
    QJsonObject entry{{QStringLiteral("name"), name}, {QStringLiteral("size"), size},
                      {QStringLiteral("sha256"), QString::fromLatin1(sha256)}};
    QJsonObject root{{QStringLiteral("schema"), 1}, {QStringLiteral("version"), version},
                     {QString::fromLatin1(kSidecarManifestMember), QJsonObject{{sidecarPlatform(), entry}}}};
    return QJsonDocument(root).toJson();
}

}
