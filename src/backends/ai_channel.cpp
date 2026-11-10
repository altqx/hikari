#include "hikari/backends/ai_channel.h"

#include "hikari/application/update_check.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>

namespace hikari::backends::ai {

namespace {

const QStringList kPlatforms{QStringLiteral("linux-x64"), QStringLiteral("windows-x64")};

bool isVersion(const QString &text)
{
    static const QRegularExpression version(QStringLiteral("^[0-9]+\\.[0-9]+\\.[0-9]+(-[0-9A-Za-z.-]+)?$"));
    return version.match(text).hasMatch();
}

bool isRuntimeId(const QString &text)
{
    static const QRegularExpression id(QStringLiteral("^[0-9]+\\.[0-9]+\\.[0-9]+-[0-9a-f]{8}$"));
    return id.match(text).hasMatch();
}

std::optional<ChannelAsset> asset(const QJsonValue &value, const QString &name, qint64 cap)
{
    const QJsonObject entry = value.toObject();
    ChannelAsset out{entry.value(QStringLiteral("name")).toString(),
                     static_cast<qint64>(entry.value(QStringLiteral("size")).toDouble()),
                     entry.value(QStringLiteral("sha256")).toString()};
    static const QRegularExpression hex(QStringLiteral("^[0-9a-f]{64}$"));
    if (out.name != name || out.size <= 0 || out.size > cap || !hex.match(out.sha256).hasMatch())
        return std::nullopt;
    return out;
}

std::optional<SidecarRelease> release(const QJsonValue &value)
{
    const QJsonObject entry = value.toObject();
    SidecarRelease out;
    out.version = entry.value(QStringLiteral("version")).toString();
    out.protocol = entry.value(QStringLiteral("protocol")).toInt();
    out.requiredApi = entry.value(QStringLiteral("required_api")).toInt();
    out.runtime = entry.value(QStringLiteral("runtime")).toString();
    if (!isVersion(out.version) || out.protocol <= 0 || out.requiredApi <= 0 || !isRuntimeId(out.runtime))
        return std::nullopt;
    const auto bundle = asset(entry.value(QStringLiteral("bundle")), bundleAssetName(out.version), kMaxBundleAssetBytes);
    if (!bundle)
        return std::nullopt;
    out.bundle = *bundle;
    const QJsonObject packages = entry.value(QStringLiteral("packages")).toObject();
    for (auto it = packages.begin(); it != packages.end(); ++it)
        out.packages[it.key()] = it.value().toString();
    return out;
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

QString runtimeExecutableName()
{
#ifdef Q_OS_WIN
    return QStringLiteral("hikari-ai-runtime.exe");
#else
    return QStringLiteral("hikari-ai-runtime");
#endif
}

QString runtimeAssetName(const QString &runtime, const QString &platform)
{
    return QStringLiteral("hikari-ai-runtime-%1-%2.gz").arg(runtime, platform);
}

QString bundleAssetName(const QString &version)
{
    return QStringLiteral("hikari-ai-%1.zip").arg(version);
}

QString channelTag(const QString &version)
{
    return QStringLiteral("ai-v") + version;
}

int compareVersions(const QString &a, const QString &b)
{
    const auto left = application::parseSemVer(a.toStdString());
    const auto right = application::parseSemVer(b.toStdString());
    if (!left || !right)
        return left ? 1 : right ? -1 : 0;
    return application::compareSemVer(*left, *right);
}

const ChannelAsset *Channel::runtimeAsset(const QString &runtime, const QString &platform) const
{
    const auto byId = runtimes.find(runtime);
    if (byId == runtimes.end())
        return nullptr;
    const auto byPlatform = byId->second.find(platform);
    return byPlatform == byId->second.end() ? nullptr : &byPlatform->second;
}

std::optional<Channel> parseChannel(const QByteArray &manifest)
{
    const QJsonObject root = QJsonDocument::fromJson(manifest).object();
    if (root.value(QStringLiteral("schema")).toInt() != 1)
        return std::nullopt;
    Channel channel;
    channel.version = root.value(QStringLiteral("version")).toString();
    if (!isVersion(channel.version))
        return std::nullopt;
    const QJsonObject runtimes = root.value(QStringLiteral("runtimes")).toObject();
    for (auto it = runtimes.begin(); it != runtimes.end(); ++it) {
        if (!isRuntimeId(it.key()))
            return std::nullopt;
        const QJsonObject platforms = it.value().toObject();
        for (auto p = platforms.begin(); p != platforms.end(); ++p) {
            if (!kPlatforms.contains(p.key()))
                continue;
            const auto runtime = asset(p.value(), runtimeAssetName(it.key(), p.key()), kMaxRuntimeAssetBytes);
            if (!runtime)
                return std::nullopt;
            channel.runtimes[it.key()][p.key()] = *runtime;
        }
    }
    for (const QJsonValue &value : root.value(QStringLiteral("releases")).toArray()) {
        const auto entry = release(value);
        if (!entry || !channel.runtimes.contains(entry->runtime))
            return std::nullopt;
        channel.releases.push_back(*entry);
    }
    if (channel.releases.empty())
        return std::nullopt;
    std::ranges::stable_sort(channel.releases, [](const SidecarRelease &a, const SidecarRelease &b) {
        return compareVersions(a.version, b.version) > 0;
    });
    if (channel.releases.front().version != channel.version)
        return std::nullopt;
    for (const QJsonValue &value : root.value(QStringLiteral("blocked")).toArray())
        if (isVersion(value.toString()))
            channel.blocked.push_back(value.toString());
    const QJsonObject agents = root.value(QStringLiteral("agents")).toObject();
    for (const char *name : kAgentClis) {
        const QJsonObject entry = agents.value(QLatin1String(name)).toObject();
        AgentCompat compat;
        if (const QString minimum = entry.value(QStringLiteral("minimum")).toString(); isVersion(minimum))
            compat.minimum = minimum;
        for (const QJsonValue &value : entry.value(QStringLiteral("broken")).toArray())
            if (isVersion(value.toString()))
                compat.broken.push_back(value.toString());
        if (!compat.minimum.isEmpty() || !compat.broken.isEmpty())
            channel.agents.emplace(QLatin1String(name), std::move(compat));
    }
    return channel;
}

bool runs(const SidecarRelease &release, SidecarCompat compat)
{
    return release.protocol == compat.protocol && release.requiredApi <= compat.apiVersion;
}

const SidecarRelease *chooseRelease(const Channel &channel, SidecarCompat compat, const QStringList &skipped,
                                    const QString &platform)
{
    for (const SidecarRelease &release : channel.releases)
        if (runs(release, compat) && !channel.blocked.contains(release.version) && !skipped.contains(release.version) &&
            channel.runtimeAsset(release.runtime, platform))
            return &release;
    return nullptr;
}

}
