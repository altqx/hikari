#pragma once

#include "hikari/application/update_signature.h"
#include "hikari/backends/ai_channel.h"
#include "minisign_signer.h"

#include <QBuffer>
#include <QByteArray>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <private/qzipwriter_p.h>

#include <map>
#include <vector>

#include <zlib.h>

namespace hikari::test {

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

inline QByteArray zip(const std::map<QString, QByteArray> &files)
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    QZipWriter writer(&buffer);
    for (const auto &[name, content] : files)
        writer.addFile(name, content);
    writer.close();
    return buffer.data();
}

inline QString sha256(const QByteArray &data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

/// An HTTP server on localhost that serves `files` by path, with an ETag that a matching If-None-Match turns into 304.
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
                    QByteArray ifNoneMatch;
                    for (const QByteArray &line : request.split('\n'))
                        if (line.toLower().startsWith("if-none-match:"))
                            ifNoneMatch = line.mid(14).trimmed();
                    const auto it = files.find(path);
                    const QByteArray body = it == files.end() ? QByteArray("missing") : it->second;
                    const QByteArray etag =
                        '"' + QCryptographicHash::hash(body, QCryptographicHash::Sha1).toHex() + '"';
                    if (it != files.end() && !ifNoneMatch.isEmpty() && ifNoneMatch == etag) {
                        ++notModified;
                        socket->write("HTTP/1.1 304 Not Modified\r\nETag: " + etag +
                                      "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                    } else {
                        socket->write((it == files.end() ? "HTTP/1.1 404 Not Found\r\n" : "HTTP/1.1 200 OK\r\n") +
                                      QByteArray("ETag: ") + etag + "\r\nContent-Length: " +
                                      QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                    }
                    socket->disconnectFromHost();
                });
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }
    QUrl base() const { return QUrl(QStringLiteral("http://127.0.0.1:%1/download/").arg(server.serverPort())); }
    int fetches(const QByteArray &suffix) const
    {
        int n = 0;
        for (const QByteArray &path : requested)
            n += path.endsWith(suffix) ? 1 : 0;
        return n;
    }

    QTcpServer server;
    std::map<QByteArray, QByteArray> files;
    std::vector<QByteArray> requested;
    int notModified = 0;
};

/// The `ai-channel` release a LocalRelease serves: runtimes, bundles and the signed manifest.
class FakeChannel {
public:
    struct Release {
        QString version;
        QString runtime = QStringLiteral("1.4.4-0123abcd");
        QByteArray bundle = "--mode ok";
        int protocol = 1;
        int requiredApi = 1;
        std::map<QString, QByteArray> extra;
    };

    explicit FakeChannel(LocalRelease &server) : m_server(server) {}

    void addRuntime(const QString &id, const QByteArray &executable)
    {
        const QByteArray asset = gzip(executable);
        const QString name = backends::ai::runtimeAssetName(id, backends::ai::sidecarPlatform());
        put(name, asset);
        m_runtimes[id] = QJsonObject{{backends::ai::sidecarPlatform(), entry(name, asset)}};
    }

    void addRelease(const Release &release)
    {
        std::map<QString, QByteArray> files = release.extra;
        files[QStringLiteral("hikari-ai.js")] = release.bundle;
        files[QStringLiteral("hikari-ai.js.jsc")] = "bytecode";
        const QByteArray asset = zip(files);
        const QString name = backends::ai::bundleAssetName(release.version);
        put(name, asset);
        m_releases.push_back(QJsonObject{{QStringLiteral("version"), release.version},
                                         {QStringLiteral("protocol"), release.protocol},
                                         {QStringLiteral("required_api"), release.requiredApi},
                                         {QStringLiteral("runtime"), release.runtime},
                                         {QStringLiteral("bundle"), entry(name, asset)},
                                         {QStringLiteral("packages"), QJsonObject{{QStringLiteral("openai"),
                                                                                   QStringLiteral("7.45.0")}}}});
    }

    QByteArray manifest(const QString &newest, const QStringList &blocked = {}) const
    {
        QJsonArray releases;
        for (auto it = m_releases.rbegin(); it != m_releases.rend(); ++it)
            releases.push_back(*it);
        return QJsonDocument(QJsonObject{{QStringLiteral("schema"), 1},
                                         {QStringLiteral("version"), newest},
                                         {QStringLiteral("runtimes"), m_runtimes},
                                         {QStringLiteral("releases"), releases},
                                         {QStringLiteral("blocked"), QJsonArray::fromStringList(blocked)}})
            .toJson();
    }

    /// Publishes the manifest, signed for `ai-v<signedFor>` (the newest release when empty).
    void publish(const MinisignSigner &signer, const QStringList &blocked = {}, QString signedFor = {})
    {
        const QString newest = m_releases.back().value(QStringLiteral("version")).toString();
        publishBytes(signer, manifest(newest, blocked), signedFor.isEmpty() ? newest : signedFor);
    }

    void publishBytes(const MinisignSigner &signer, const QByteArray &manifest, const QString &signedFor)
    {
        put(QString::fromLatin1(backends::ai::kChannelManifest), manifest);
        put(QString::fromLatin1(backends::ai::kChannelSignature),
            QByteArray::fromStdString(signer.sign(
                manifest.toStdString(),
                application::releaseTrustedComment(backends::ai::channelTag(signedFor).toStdString(),
                                                   backends::ai::kChannelManifest))));
    }

    QByteArray path(const QString &name) const { return "/download/ai-channel/" + name.toLatin1(); }

private:
    void put(const QString &name, const QByteArray &data) { m_server.files[path(name)] = data; }
    static QJsonObject entry(const QString &name, const QByteArray &asset)
    {
        return {{QStringLiteral("name"), name}, {QStringLiteral("size"), asset.size()},
                {QStringLiteral("sha256"), sha256(asset)}};
    }

    LocalRelease &m_server;
    QJsonObject m_runtimes;
    std::vector<QJsonObject> m_releases;
};

}
