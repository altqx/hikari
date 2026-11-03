#include "hikari/backends/ai_sidecar_download.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <map>
#include <optional>

#include <zlib.h>

using namespace hikari::backends::ai;

namespace {

const QString kVersion = QStringLiteral("0.1.0");

bool waitFor(const std::function<bool()> &done, int ms = 10'000)
{
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

QByteArray gzip(const QByteArray &data)
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

class Server {
public:
    Server()
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

QByteArray manifest(const QString &name, qint64 size, const QByteArray &sha256)
{
    QJsonObject entry{{QStringLiteral("name"), name}, {QStringLiteral("size"), size},
                      {QStringLiteral("sha256"), QString::fromLatin1(sha256)}};
    QJsonObject root{{QStringLiteral("schema"), 1}, {QStringLiteral("version"), kVersion},
                     {QString::fromLatin1(kSidecarManifestMember), QJsonObject{{sidecarPlatform(), entry}}}};
    return QJsonDocument(root).toJson();
}

struct Outcome {
    std::optional<QString> program;
    std::optional<SidecarDownloadError> error;
    std::optional<SidecarAsset> described;
    bool done() const { return program || error || described; }
};

class AiSidecarDownloadTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        static int argc = 1;
        static char name[] = "hikari_backends_ai_sidecar_download_tests";
        static char *argv[] = {name, nullptr};
        static QCoreApplication app(argc, argv);
    }

    void serve(const QByteArray &asset, const QByteArray &manifestBody)
    {
        const QString name = sidecarAssetName(kVersion, sidecarPlatform());
        server.files["/download/v0.1.0/" + name.toLatin1()] = asset;
        server.files["/download/v0.1.0/hikarisub-update.json"] = manifestBody;
    }

    Outcome run(bool install = true)
    {
        AiSidecarDownload download(root(), kVersion, SidecarSource::local(server.base()));
        Outcome outcome;
        QObject::connect(&download, &AiSidecarDownload::installed, [&](const QString &p) { outcome.program = p; });
        QObject::connect(&download, &AiSidecarDownload::failed,
                         [&](SidecarDownloadError e, const QString &) { outcome.error = e; });
        QObject::connect(&download, &AiSidecarDownload::described,
                         [&](const SidecarAsset &a) { outcome.described = a; });
        install ? download.install() : download.describe();
        EXPECT_TRUE(waitFor([&] { return outcome.done(); }));
        return outcome;
    }

    QString root() const { return dir.filePath(QStringLiteral("ai/sidecar")); }
    QStringList filesInVersion() const
    {
        return QDir(root() + QLatin1Char('/') + kVersion).entryList(QDir::Files | QDir::Hidden);
    }

    QTemporaryDir dir;
    Server server;
    const QByteArray executable = QByteArray("#!/bin/sh\necho sidecar\n").repeated(2000);
};

TEST_F(AiSidecarDownloadTest, InstallsAVerifiedSidecarAndRemovesOtherVersions)
{
    QDir().mkpath(root() + QStringLiteral("/0.0.9"));
    const QByteArray packed = gzip(executable);
    serve(packed, manifest(sidecarAssetName(kVersion, sidecarPlatform()), packed.size(),
                           QCryptographicHash::hash(packed, QCryptographicHash::Sha256).toHex()));
    const Outcome outcome = run();
    ASSERT_TRUE(outcome.program) << (outcome.error ? int(*outcome.error) : -1);
    QFile file(*outcome.program);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    EXPECT_EQ(file.readAll(), executable);
    EXPECT_TRUE(file.permissions() & QFile::ExeOwner);
    EXPECT_EQ(filesInVersion(), QStringList{sidecarExecutableName()});
    EXPECT_FALSE(QDir(root() + QStringLiteral("/0.0.9")).exists());
    AiSidecarDownload again(root(), kVersion, SidecarSource::local(server.base()));
    EXPECT_EQ(again.installedProgram(), outcome.program);
}

TEST_F(AiSidecarDownloadTest, ADownloadWithTheWrongHashIsDeletedAndNeverRun)
{
    QDir().mkpath(root() + QStringLiteral("/0.0.9"));
    const QByteArray packed = gzip(executable);
    serve(packed, manifest(sidecarAssetName(kVersion, sidecarPlatform()), packed.size(), QByteArray(64, 'a')));
    const Outcome outcome = run();
    ASSERT_TRUE(outcome.error);
    EXPECT_EQ(*outcome.error, SidecarDownloadError::Mismatch);
    EXPECT_TRUE(filesInVersion().isEmpty()) << filesInVersion().join(u',').toStdString();
    EXPECT_TRUE(QDir(root() + QStringLiteral("/0.0.9")).exists());
    AiSidecarDownload again(root(), kVersion, SidecarSource::local(server.base()));
    EXPECT_FALSE(again.installedProgram());
}

TEST_F(AiSidecarDownloadTest, ADownloadLongerThanTheManifestSaysIsDeleted)
{
    const QByteArray packed = gzip(executable);
    serve(packed, manifest(sidecarAssetName(kVersion, sidecarPlatform()), packed.size() - 10,
                           QCryptographicHash::hash(packed, QCryptographicHash::Sha256).toHex()));
    const Outcome outcome = run();
    ASSERT_TRUE(outcome.error);
    EXPECT_TRUE(*outcome.error == SidecarDownloadError::Mismatch || *outcome.error == SidecarDownloadError::Network);
    EXPECT_TRUE(filesInVersion().isEmpty());
}

TEST_F(AiSidecarDownloadTest, AVerifiedFileThatIsNotGzipIsDeleted)
{
    serve(executable, manifest(sidecarAssetName(kVersion, sidecarPlatform()), executable.size(),
                               QCryptographicHash::hash(executable, QCryptographicHash::Sha256).toHex()));
    const Outcome outcome = run();
    ASSERT_TRUE(outcome.error);
    EXPECT_EQ(*outcome.error, SidecarDownloadError::Unpack);
    EXPECT_TRUE(filesInVersion().isEmpty());
}

TEST_F(AiSidecarDownloadTest, AManifestWithoutTheSidecarDownloadsNothing)
{
    serve(QByteArray("x"), QByteArray(R"({"schema": 1, "version": "0.1.0", "packages": {}})"));
    const Outcome outcome = run();
    ASSERT_TRUE(outcome.error);
    EXPECT_EQ(*outcome.error, SidecarDownloadError::NoAsset);
    EXPECT_EQ(server.requested, std::vector<QByteArray>{"/download/v0.1.0/hikarisub-update.json"});
}

TEST_F(AiSidecarDownloadTest, DescribeTellsTheSizeWithoutDownloading)
{
    const QByteArray packed = gzip(executable);
    serve(packed, manifest(sidecarAssetName(kVersion, sidecarPlatform()), packed.size(),
                           QCryptographicHash::hash(packed, QCryptographicHash::Sha256).toHex()));
    const Outcome outcome = run(false);
    ASSERT_TRUE(outcome.described);
    EXPECT_EQ(outcome.described->size, packed.size());
    EXPECT_EQ(server.requested.size(), 1u);
}

TEST(SidecarAssetIn, RefusesAnAssetNamedForAnotherVersionOrPlatformAndABadHash)
{
    const QString platform = sidecarPlatform();
    const QByteArray hash(64, 'b');
    EXPECT_TRUE(sidecarAssetIn(manifest(sidecarAssetName(kVersion, platform), 10, hash), kVersion, platform));
    EXPECT_FALSE(sidecarAssetIn(manifest(sidecarAssetName(QStringLiteral("0.0.9"), platform), 10, hash), kVersion,
                                platform));
    EXPECT_FALSE(sidecarAssetIn(manifest(QStringLiteral("../hikari-ai"), 10, hash), kVersion, platform));
    EXPECT_FALSE(sidecarAssetIn(manifest(sidecarAssetName(kVersion, platform), 10, QByteArray(64, 'G')), kVersion,
                                platform));
    EXPECT_FALSE(sidecarAssetIn(manifest(sidecarAssetName(kVersion, platform), 0, hash), kVersion, platform));
    EXPECT_FALSE(sidecarAssetIn(manifest(sidecarAssetName(kVersion, platform), 10, hash), QStringLiteral("0.1.1"),
                                platform));
}

}
