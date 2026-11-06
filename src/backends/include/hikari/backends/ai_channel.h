#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <map>
#include <optional>
#include <vector>

namespace hikari::backends::ai {

/// The rolling release that holds every sidecar asset and the signed manifest (ADR 0031).
inline constexpr const char *kChannelTag = "ai-channel";
inline constexpr const char *kChannelManifest = "hikari-ai-update.json";
inline constexpr const char *kChannelSignature = "hikari-ai-update.json.minisig";
inline constexpr qint64 kMaxChannelManifestBytes = 256 * 1024;
inline constexpr qint64 kMaxRuntimeAssetBytes = qint64{256} << 20;
inline constexpr qint64 kMaxBundleAssetBytes = qint64{64} << 20;

/// "linux-x64" or "windows-x64".
QString sidecarPlatform();
/// hikari-ai-runtime, or hikari-ai-runtime.exe on Windows.
QString runtimeExecutableName();
inline constexpr const char *kBundleEntry = "hikari-ai.js";

QString runtimeAssetName(const QString &runtime, const QString &platform);
QString bundleAssetName(const QString &version);
/// The `ai-v*` tag a manifest's signature names.
QString channelTag(const QString &version);

struct ChannelAsset {
    QString name;
    qint64 size = 0;
    QString sha256; // lowercase hex
};

struct SidecarRelease {
    QString version;
    int protocol = 0;
    int requiredApi = 0;
    QString runtime;
    ChannelAsset bundle;
    std::map<QString, QString> packages;
};

struct Channel {
    /// The newest release's version; the signature's trusted comment names its tag.
    QString version;
    /// Runtime id → platform → asset.
    std::map<QString, std::map<QString, ChannelAsset>> runtimes;
    /// Newest first.
    std::vector<SidecarRelease> releases;
    QStringList blocked;

    const ChannelAsset *runtimeAsset(const QString &runtime, const QString &platform) const;
};

/// Nullopt when the manifest isn't schema 1, names an asset wrongly or lists no release.
std::optional<Channel> parseChannel(const QByteArray &manifest);

/// What an app can run: the same protocol major and a required HA1 version no newer than its own.
struct SidecarCompat {
    int protocol = 0;
    int apiVersion = 0;
};
bool runs(const SidecarRelease &release, SidecarCompat compat);

/// The newest release this app runs that is neither blocked nor skipped, with a runtime for `platform`.
const SidecarRelease *chooseRelease(const Channel &channel, SidecarCompat compat, const QStringList &skipped,
                                    const QString &platform);

/// <0, 0 or >0 as a is older than, as old as or newer than b; an unparsable version is older than any other.
int compareVersions(const QString &a, const QString &b);

}
