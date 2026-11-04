#pragma once

#include "hikari/minisign/minisign.h"

#include <QCryptographicHash>
#include <QFile>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

#include <optional>
#include <vector>

class QNetworkReply;

namespace hikari::backends::ai {

/// The manifest member that names each platform's sidecar asset.
inline constexpr const char *kSidecarManifestMember = "ai_sidecar";
inline constexpr qint64 kMaxSidecarAssetBytes = qint64{256} << 20;

/// "linux-x64" or "windows-x64".
QString sidecarPlatform();
/// The asset's name on a release, e.g. hikari-ai-0.1.0-linux-x64.gz.
QString sidecarAssetName(const QString &version, const QString &platform);
/// hikari-ai, or hikari-ai.exe on Windows.
QString sidecarExecutableName();

enum class SidecarDownloadError {
    Manifest,  // the release manifest couldn't be fetched, or its signature was refused
    NoAsset,   // the manifest names no sidecar for this version and platform
    Network,   // the asset couldn't be fetched
    Mismatch,  // its size or SHA-256 differs from the manifest's
    Unpack,    // it isn't a valid gzip stream
    Disk,
};

struct SidecarAsset {
    QString name;
    qint64 size = 0;
    QString sha256; // lowercase hex
};

/// This version's and platform's asset in a release manifest, or nullopt when the manifest has none or names it
/// wrongly.
std::optional<SidecarAsset> sidecarAssetIn(const QByteArray &manifest, const QString &version,
                                           const QString &platform);

/// Where releases are downloaded from: GitHub, or a local feed in tests.
struct SidecarSource {
    QString downloadBase; // ends with "/"; a release's files are under v<version>/
    bool github = true;
    // The keys the manifest's .minisig must verify with.
    std::vector<minisign::PublicKey> keys;
    static SidecarSource gitHub();
    static SidecarSource local(const QUrl &base, std::vector<minisign::PublicKey> keys = {});
};

/// Fetches the sidecar of this app's release into <root>/<version>/ and checks it against the release manifest.
/// A file that fails the check is deleted, never run.
class AiSidecarDownload final : public QObject {
    Q_OBJECT
public:
    AiSidecarDownload(QString root, QString version, SidecarSource source = SidecarSource::gitHub(),
                      QObject *parent = nullptr);
    ~AiSidecarDownload() override;

    /// The verified sidecar for this version, when one is installed.
    std::optional<QString> installedProgram() const;
    QString versionDirectory() const;

    /// Reads the manifest: what would be downloaded, for the prompt. Emits described or failed.
    void describe();
    /// Downloads and verifies, then removes other versions' copies. Emits installed or failed.
    void install();
    void abort();

signals:
    void described(hikari::backends::ai::SidecarAsset asset);
    void progress(qint64 received, qint64 total);
    void installed(QString program);
    /// Whatever was downloaded is already deleted. `detail` is the network's own message, when there is one.
    void failed(hikari::backends::ai::SidecarDownloadError error, QString detail);

private:
    QNetworkReply *get(const QUrl &url, qint64 cap);
    void fetchManifest(bool thenDownload);
    void fetchSignature(QByteArray manifest, bool thenDownload);
    void download(const SidecarAsset &asset);
    void finishDownload();
    void fail(SidecarDownloadError error, const QString &detail = {});
    void discard();

    QString m_root;
    QString m_version;
    SidecarSource m_source;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    std::optional<SidecarAsset> m_asset;
    QFile m_part;
    QCryptographicHash m_hash{QCryptographicHash::Sha256};
    qint64 m_received = 0;
};

}
