#include "hikari/backends/ai_sidecar_download.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>

#include <zlib.h>

#include <array>

namespace hikari::backends::ai {

namespace {

constexpr qint64 kMaxManifestBytes = 64 * 1024;
constexpr qint64 kMaxExecutableBytes = qint64{512} << 20;

bool allowedGitHubHost(const QString &host)
{
    for (const char *allowed : {"github.com", "objects.githubusercontent.com", "release-assets.githubusercontent.com"})
        if (host.compare(QLatin1String(allowed), Qt::CaseInsensitive) == 0)
            return true;
    return false;
}

QString origin(const QUrl &url)
{
    return url.scheme() + QStringLiteral("://") + url.authority();
}

bool gunzip(const QString &from, const QString &to)
{
    QFile in(from);
    QFile out(to);
    if (!in.open(QIODevice::ReadOnly) || !out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    z_stream stream{};
    if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK)
        return false;
    std::array<char, 1 << 16> input{};
    std::array<char, 1 << 16> output{};
    int status = Z_OK;
    qint64 written = 0;
    while (status != Z_STREAM_END) {
        const qint64 read = in.read(input.data(), input.size());
        if (read <= 0)
            break;
        stream.next_in = reinterpret_cast<Bytef *>(input.data());
        stream.avail_in = static_cast<uInt>(read);
        do {
            stream.next_out = reinterpret_cast<Bytef *>(output.data());
            stream.avail_out = static_cast<uInt>(output.size());
            status = inflate(&stream, Z_NO_FLUSH);
            if (status != Z_OK && status != Z_STREAM_END) {
                inflateEnd(&stream);
                return false;
            }
            const qint64 produced = static_cast<qint64>(output.size() - stream.avail_out);
            written += produced;
            if (written > kMaxExecutableBytes || out.write(output.data(), produced) != produced) {
                inflateEnd(&stream);
                return false;
            }
        } while (stream.avail_out == 0 && status != Z_STREAM_END);
    }
    inflateEnd(&stream);
    return status == Z_STREAM_END && in.atEnd() && out.flush();
}

}

QString sidecarPlatform()
{
#ifdef Q_OS_WIN
    return QStringLiteral("windows-x64");
#else
    return QStringLiteral("linux-x64");
#endif
}

QString sidecarAssetName(const QString &version, const QString &platform)
{
    return QStringLiteral("hikari-ai-%1-%2.gz").arg(version, platform);
}

QString sidecarExecutableName()
{
#ifdef Q_OS_WIN
    return QStringLiteral("hikari-ai.exe");
#else
    return QStringLiteral("hikari-ai");
#endif
}

std::optional<SidecarAsset> sidecarAssetIn(const QByteArray &manifest, const QString &version, const QString &platform)
{
    const QJsonDocument document = QJsonDocument::fromJson(manifest);
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("schema")).toInt() != 1 || root.value(QStringLiteral("version")).toString() != version)
        return std::nullopt;
    const QJsonObject entry =
        root.value(QLatin1String(kSidecarManifestMember)).toObject().value(platform).toObject();
    SidecarAsset asset{entry.value(QStringLiteral("name")).toString(),
                       static_cast<qint64>(entry.value(QStringLiteral("size")).toDouble()),
                       entry.value(QStringLiteral("sha256")).toString()};
    static const QRegularExpression hex(QStringLiteral("^[0-9a-f]{64}$"));
    if (asset.name != sidecarAssetName(version, platform) || asset.size <= 0 || asset.size > kMaxSidecarAssetBytes ||
        !hex.match(asset.sha256).hasMatch())
        return std::nullopt;
    return asset;
}

SidecarSource SidecarSource::gitHub()
{
    return {QStringLiteral("https://github.com/altqx/hikari/releases/download/"), true};
}

SidecarSource SidecarSource::local(const QUrl &base)
{
    QString root = base.toString();
    if (!root.endsWith(QLatin1Char('/')))
        root += QLatin1Char('/');
    return {root, false};
}

AiSidecarDownload::AiSidecarDownload(QString root, QString version, SidecarSource source, QObject *parent)
    : QObject(parent), m_root(std::move(root)), m_version(std::move(version)), m_source(std::move(source))
{
}

AiSidecarDownload::~AiSidecarDownload()
{
    if (m_reply)
        m_reply->abort();
    discard();
}

QString AiSidecarDownload::versionDirectory() const
{
    return m_root + QLatin1Char('/') + m_version;
}

std::optional<QString> AiSidecarDownload::installedProgram() const
{
    const QString program = versionDirectory() + QLatin1Char('/') + sidecarExecutableName();
    if (!QFileInfo(program).isFile())
        return std::nullopt;
    return program;
}

QNetworkReply *AiSidecarDownload::get(const QUrl &url, qint64 cap)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("HikariSub/%1").arg(m_version));
    request.setTransferTimeout(60 * 1000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
    QNetworkReply *reply = m_network.get(request);
    const QString from = origin(url);
    connect(reply, &QNetworkReply::redirected, this, [this, reply, from](const QUrl &to) {
        const bool https = to.scheme() == QLatin1String("https") && to.port() == -1 && to.userInfo().isEmpty();
        if (m_source.github ? https && allowedGitHubHost(to.host()) : origin(to) == from)
            emit reply->redirectAllowed();
        else
            reply->abort();
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [reply, cap](qint64 received, qint64) {
        if (received > cap)
            reply->abort();
    });
    return reply;
}

void AiSidecarDownload::describe()
{
    fetchManifest(false);
}

void AiSidecarDownload::install()
{
    if (const auto program = installedProgram()) {
        emit installed(*program);
        return;
    }
    fetchManifest(true);
}

void AiSidecarDownload::abort()
{
    if (m_reply)
        m_reply->abort();
}

void AiSidecarDownload::fetchManifest(bool thenDownload)
{
    if (m_reply)
        return;
    const QUrl url(m_source.downloadBase + QStringLiteral("v%1/hikarisub-update.json").arg(m_version));
    m_reply = get(url, kMaxManifestBytes);
    connect(m_reply, &QNetworkReply::finished, this, [this, thenDownload, reply = m_reply.data()] {
        reply->deleteLater();
        m_reply = nullptr;
        if (reply->error() != QNetworkReply::NoError)
            return fail(SidecarDownloadError::Manifest, reply->errorString());
        const auto asset = sidecarAssetIn(reply->readAll(), m_version, sidecarPlatform());
        if (!asset)
            return fail(SidecarDownloadError::NoAsset);
        m_asset = asset;
        if (thenDownload)
            download(*asset);
        else
            emit described(*asset);
    });
}

void AiSidecarDownload::download(const SidecarAsset &asset)
{
    QDir().mkpath(versionDirectory());
    m_part.setFileName(versionDirectory() + QLatin1Char('/') + asset.name + QStringLiteral(".part"));
    if (!m_part.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return fail(SidecarDownloadError::Disk, m_part.errorString());
    m_hash.reset();
    m_received = 0;
    m_reply = get(QUrl(m_source.downloadBase + QStringLiteral("v%1/").arg(m_version) + asset.name), asset.size);
    connect(m_reply, &QNetworkReply::readyRead, this, [this] {
        const QByteArray data = m_reply->readAll();
        m_received += data.size();
        if (m_received > m_asset->size || m_part.write(data) != data.size()) {
            m_reply->abort();
            return;
        }
        m_hash.addData(data);
        emit progress(m_received, m_asset->size);
    });
    connect(m_reply, &QNetworkReply::finished, this, &AiSidecarDownload::finishDownload);
}

void AiSidecarDownload::finishDownload()
{
    QNetworkReply *reply = m_reply.data();
    reply->deleteLater();
    m_reply = nullptr;
    const QByteArray rest = reply->readAll();
    m_received += rest.size();
    if (m_received <= m_asset->size) {
        m_part.write(rest);
        m_hash.addData(rest);
    }
    m_part.close();
    if (reply->error() != QNetworkReply::NoError)
        return fail(SidecarDownloadError::Network, reply->errorString());
    if (m_received != m_asset->size || QString::fromLatin1(m_hash.result().toHex()) != m_asset->sha256)
        return fail(SidecarDownloadError::Mismatch);
    const QString program = versionDirectory() + QLatin1Char('/') + sidecarExecutableName();
    const QString unpacking = program + QStringLiteral(".part");
    if (!gunzip(m_part.fileName(), unpacking)) {
        QFile::remove(unpacking);
        return fail(SidecarDownloadError::Unpack);
    }
    m_part.remove();
    QFile::setPermissions(unpacking, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    QFile::remove(program);
    if (!QFile::rename(unpacking, program)) {
        QFile::remove(unpacking);
        return fail(SidecarDownloadError::Disk);
    }
    const QDir root(m_root);
    for (const QString &other : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        if (other != m_version)
            QDir(root.filePath(other)).removeRecursively();
    emit installed(program);
}

void AiSidecarDownload::discard()
{
    if (m_part.fileName().isEmpty())
        return;
    m_part.close();
    m_part.remove();
}

void AiSidecarDownload::fail(SidecarDownloadError error, const QString &detail)
{
    discard();
    emit failed(error, detail);
}

}
