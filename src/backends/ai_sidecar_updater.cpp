#include "hikari/backends/ai_sidecar_updater.h"

#include "hikari/application/update_signature.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <private/qzipreader_p.h>

#include <zlib.h>

#include <array>

namespace hikari::backends::ai {

namespace {

constexpr qint64 kMaxExecutableBytes = qint64{512} << 20;
constexpr qint64 kMaxBundleBytes = qint64{256} << 20;
const QStringList kBundleFiles{QStringLiteral("hikari-ai.js"), QStringLiteral("hikari-ai.js.jsc")};

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

bool unzipBundle(const QString &from, const QString &to)
{
    QZipReader zip(from);
    if (zip.status() != QZipReader::NoError || !zip.isReadable())
        return false;
    QStringList seen;
    qint64 total = 0;
    for (const QZipReader::FileInfo &entry : zip.fileInfoList()) {
        if (!entry.isFile || !kBundleFiles.contains(entry.filePath) || seen.contains(entry.filePath))
            return false;
        seen.push_back(entry.filePath);
        total += entry.size;
    }
    if (!seen.contains(QLatin1String(kBundleEntry)) || total > kMaxBundleBytes)
        return false;
    for (const QString &name : seen) {
        const QByteArray content = zip.fileData(name);
        QFile out(to + QLatin1Char('/') + name);
        if (zip.status() != QZipReader::NoError || !out.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
            out.write(content) != content.size())
            return false;
    }
    return true;
}

QByteArray readFile(const QString &path, qint64 cap)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > cap)
        return {};
    return file.readAll();
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
}

QJsonArray toArray(const QStringList &list)
{
    return QJsonArray::fromStringList(list);
}

QStringList toList(const QJsonValue &value)
{
    QStringList out;
    for (const QJsonValue &item : value.toArray())
        out.push_back(item.toString());
    return out;
}

}

SidecarSource SidecarSource::gitHub()
{
    return {QStringLiteral("https://github.com/altqx/hikari/releases/download/"), true, application::releaseKeys()};
}

SidecarSource SidecarSource::local(const QUrl &base, std::vector<minisign::PublicKey> keys)
{
    QString root = base.toString();
    if (!root.endsWith(QLatin1Char('/')))
        root += QLatin1Char('/');
    return {root, false, std::move(keys)};
}

AiSidecarUpdater::AiSidecarUpdater(QString root, SidecarCompat compat, SidecarSource source, QObject *parent)
    : QObject(parent), m_root(std::move(root)), m_compat(compat), m_source(std::move(source))
{
    load();
}

AiSidecarUpdater::~AiSidecarUpdater()
{
    if (m_reply)
        m_reply->abort();
    discard();
}

QString AiSidecarUpdater::bundleDirectory(const QString &version) const
{
    return m_root + QStringLiteral("/bundle/") + version;
}

QString AiSidecarUpdater::runtimeDirectory(const QString &runtime) const
{
    return m_root + QStringLiteral("/runtime/") + runtime;
}

std::optional<SidecarInstall> AiSidecarUpdater::installation(const QString &version) const
{
    if (version.isEmpty())
        return std::nullopt;
    const QString bundle = bundleDirectory(version);
    const QJsonObject release =
        QJsonDocument::fromJson(readFile(bundle + QStringLiteral("/release.json"), 64 * 1024)).object();
    const QString runtimeId = release.value(QStringLiteral("runtime")).toString();
    if (runtimeId.isEmpty() || runtimeId.contains(QLatin1Char('/')) || runtimeId.contains(QLatin1Char('\\')))
        return std::nullopt;
    SidecarInstall install{version, runtimeDirectory(runtimeId) + QLatin1Char('/') + runtimeExecutableName(),
                           bundle + QLatin1Char('/') + QLatin1String(kBundleEntry), {}};
    if (!QFileInfo(install.runtime).isFile() || !QFileInfo(install.bundle).isFile())
        return std::nullopt;
    const QJsonObject packages = release.value(QStringLiteral("packages")).toObject();
    for (auto it = packages.begin(); it != packages.end(); ++it)
        install.packages[it.key()] = it.value().toString();
    return install;
}

bool AiSidecarUpdater::usable(const QString &version) const
{
    return !version.isEmpty() && !m_blocked.contains(version) && !m_skipped.contains(version) &&
           installation(version).has_value();
}

std::optional<SidecarInstall> AiSidecarUpdater::current() const
{
    if (usable(m_current))
        return installation(m_current);
    if (usable(m_previous))
        return installation(m_previous);
    return std::nullopt;
}

QString AiSidecarUpdater::previous() const
{
    return usable(m_current) && usable(m_previous) ? m_previous : QString();
}

std::optional<SidecarInstall> AiSidecarUpdater::staged() const
{
    return usable(m_staged) ? installation(m_staged) : std::nullopt;
}

void AiSidecarUpdater::load()
{
    const QJsonObject state = QJsonDocument::fromJson(readFile(m_root + QStringLiteral("/state.json"), 256 * 1024)).object();
    m_current = state.value(QStringLiteral("current")).toString();
    m_previous = state.value(QStringLiteral("previous")).toString();
    m_staged = state.value(QStringLiteral("staged")).toString();
    m_skipped = toList(state.value(QStringLiteral("skipped")));
    m_blocked = toList(state.value(QStringLiteral("blocked")));
    m_seen = state.value(QStringLiteral("seen")).toString();
    m_available = state.value(QStringLiteral("available")).toString();
    m_etag = state.value(QStringLiteral("etag")).toString().toLatin1();
    m_checked = QDateTime::fromString(state.value(QStringLiteral("checked")).toString(), Qt::ISODate);
}

void AiSidecarUpdater::save() const
{
    QDir().mkpath(m_root);
    const QJsonObject state{{QStringLiteral("current"), m_current},
                            {QStringLiteral("previous"), m_previous},
                            {QStringLiteral("staged"), m_staged},
                            {QStringLiteral("skipped"), toArray(m_skipped)},
                            {QStringLiteral("blocked"), toArray(m_blocked)},
                            {QStringLiteral("seen"), m_seen},
                            {QStringLiteral("available"), m_available},
                            {QStringLiteral("etag"), QString::fromLatin1(m_etag)},
                            {QStringLiteral("checked"), m_checked.toString(Qt::ISODate)}};
    writeFile(m_root + QStringLiteral("/state.json"), QJsonDocument(state).toJson());
}

void AiSidecarUpdater::prune()
{
    QStringList bundles;
    QStringList runtimes;
    for (const QString &version : {m_current, m_previous, m_staged})
        if (const auto install = installation(version)) {
            bundles.push_back(version);
            runtimes.push_back(QFileInfo(install->runtime).dir().dirName());
        }
    const QDir root(m_root);
    for (const QString &entry : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        if (entry != QLatin1String("bundle") && entry != QLatin1String("runtime") && entry != QLatin1String("download"))
            QDir(root.filePath(entry)).removeRecursively();
    const auto keepOnly = [](const QString &dir, const QStringList &keep) {
        const QDir parent(dir);
        for (const QString &entry : parent.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            if (!keep.contains(entry))
                QDir(parent.filePath(entry)).removeRecursively();
    };
    keepOnly(m_root + QStringLiteral("/bundle"), bundles);
    keepOnly(m_root + QStringLiteral("/runtime"), runtimes);
}

QNetworkReply *AiSidecarUpdater::get(const QUrl &url, qint64 cap, const QByteArray &etag)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("HikariSub-AI"));
    request.setTransferTimeout(60 * 1000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
    if (!etag.isEmpty())
        request.setRawHeader("If-None-Match", etag);
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

void AiSidecarUpdater::describe()
{
    fetchManifest(Goal::Describe);
}

void AiSidecarUpdater::install()
{
    if (const auto running = current()) {
        emit installed(*running);
        return;
    }
    fetchManifest(Goal::Install);
}

void AiSidecarUpdater::check(bool download)
{
    fetchManifest(download ? Goal::Stage : Goal::Check);
}

void AiSidecarUpdater::abort()
{
    if (m_reply)
        m_reply->abort();
}

void AiSidecarUpdater::fetchManifest(Goal goal)
{
    if (m_reply)
        return;
    const QString cached = m_root + QStringLiteral("/channel.json");
    const bool conditional = (goal == Goal::Check || goal == Goal::Stage) && QFileInfo::exists(cached) &&
                             QFileInfo::exists(cached + QStringLiteral(".minisig"));
    const QString channel = m_source.downloadBase + QLatin1String(kChannelTag) + QLatin1Char('/');
    m_reply = get(QUrl(channel + QLatin1String(kChannelManifest)), kMaxChannelManifestBytes,
                  conditional ? m_etag : QByteArray());
    connect(m_reply, &QNetworkReply::finished, this, [this, goal, cached, reply = m_reply.data()] {
        reply->deleteLater();
        m_reply = nullptr;
        if (reply->error() != QNetworkReply::NoError)
            return fail(SidecarDownloadError::Manifest, reply->errorString());
        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 304)
            return useManifest(goal, readFile(cached, kMaxChannelManifestBytes),
                               readFile(cached + QStringLiteral(".minisig"), qint64(minisign::kMaxSignatureBytes)));
        fetchSignature(goal, reply->readAll(), reply->rawHeader("ETag"));
    });
}

void AiSidecarUpdater::fetchSignature(Goal goal, QByteArray manifest, QByteArray etag)
{
    const QString channel = m_source.downloadBase + QLatin1String(kChannelTag) + QLatin1Char('/');
    m_reply = get(QUrl(channel + QLatin1String(kChannelSignature)), qint64(minisign::kMaxSignatureBytes));
    connect(m_reply, &QNetworkReply::finished, this,
            [this, goal, manifest = std::move(manifest), etag = std::move(etag), reply = m_reply.data()] {
        reply->deleteLater();
        m_reply = nullptr;
        const QByteArray signature = reply->error() == QNetworkReply::NoError ? reply->readAll() : QByteArray();
        if (!signature.isEmpty())
            m_etag = etag;
        useManifest(goal, manifest, signature);
    });
}

void AiSidecarUpdater::useManifest(Goal goal, const QByteArray &manifest, const QByteArray &signature)
{
    const auto channel = parseChannel(manifest);
    if (!channel)
        return fail(SidecarDownloadError::Manifest, QStringLiteral("the AI channel's manifest is malformed"));
    const auto problem = application::releaseSignatureProblem(
        manifest.toStdString(),
        signature.isEmpty() ? std::nullopt : std::optional(signature.toStdString()),
        channelTag(channel->version).toStdString(), kChannelManifest, m_source.keys);
    if (problem)
        return fail(SidecarDownloadError::Manifest, QString::fromLatin1(application::signatureProblemText(*problem)));
    if (!m_seen.isEmpty() && compareVersions(channel->version, m_seen) < 0)
        return fail(SidecarDownloadError::Manifest,
                    QStringLiteral("the AI channel's manifest is older than one already seen"));
    m_seen = channel->version;
    m_blocked = channel->blocked;
    m_checked = QDateTime::currentDateTimeUtc();
    QDir().mkpath(m_root);
    writeFile(m_root + QStringLiteral("/channel.json"), manifest);
    writeFile(m_root + QStringLiteral("/channel.json.minisig"), signature);
    const SidecarRelease *chosen = chooseRelease(*channel, m_compat, m_skipped, sidecarPlatform());
    const QString running = current() ? current()->version : QString();
    m_available = chosen && (running.isEmpty() || compareVersions(chosen->version, running) > 0) ? chosen->version
                                                                                                  : QString();
    save();
    if (goal == Goal::Check || goal == Goal::Stage)
        emit checked();
    if (goal == Goal::Check || (goal == Goal::Stage && m_available.isEmpty() && current()))
        return;
    if (!chosen)
        return fail(SidecarDownloadError::NoAsset);
    std::vector<Step> steps;
    if (!QFileInfo(runtimeDirectory(chosen->runtime) + QLatin1Char('/') + runtimeExecutableName()).isFile())
        steps.push_back({chosen->runtime, *channel->runtimeAsset(chosen->runtime, sidecarPlatform()),
                         runtimeDirectory(chosen->runtime), true});
    if (!installation(chosen->version))
        steps.push_back({chosen->version, chosen->bundle, bundleDirectory(chosen->version), false});
    if (goal == Goal::Describe) {
        qint64 bytes = 0;
        for (const Step &step : steps)
            bytes += step.asset.size;
        emit described(bytes);
        return;
    }
    m_received = 0;
    runSteps(goal, *chosen, std::move(steps));
}

void AiSidecarUpdater::runSteps(Goal goal, SidecarRelease release, std::vector<Step> steps)
{
    if (steps.empty())
        return finished(goal, release);
    const Step &step = steps.front();
    const QString downloads = m_root + QStringLiteral("/download");
    QDir().mkpath(downloads);
    m_part.setFileName(downloads + QLatin1Char('/') + step.asset.name + QStringLiteral(".part"));
    if (!m_part.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return fail(SidecarDownloadError::Disk, m_part.errorString());
    m_hash.reset();
    qint64 total = 0;
    for (const Step &s : steps)
        total += s.asset.size;
    const qint64 before = m_received;
    const qint64 size = step.asset.size;
    const QString channel = m_source.downloadBase + QLatin1String(kChannelTag) + QLatin1Char('/');
    m_reply = get(QUrl(channel + step.asset.name), size);
    connect(m_reply, &QNetworkReply::readyRead, this, [this, before, size, total] {
        const QByteArray data = m_reply->readAll();
        m_received += data.size();
        if (m_received - before > size || m_part.write(data) != data.size()) {
            m_reply->abort();
            return;
        }
        m_hash.addData(data);
        emit progress(m_received, before + total);
    });
    connect(m_reply, &QNetworkReply::finished, this,
            [this, goal, release = std::move(release), steps = std::move(steps)]() mutable {
        finishStep(goal, std::move(release), std::move(steps));
    });
}

void AiSidecarUpdater::finishStep(Goal goal, SidecarRelease release, std::vector<Step> steps)
{
    QNetworkReply *reply = m_reply.data();
    reply->deleteLater();
    m_reply = nullptr;
    const Step step = steps.front();
    steps.erase(steps.begin());
    const QByteArray rest = reply->readAll();
    const qint64 had = m_part.size();
    if (had + rest.size() <= step.asset.size) {
        m_part.write(rest);
        m_hash.addData(rest);
        m_received += rest.size();
    }
    m_part.close();
    if (reply->error() != QNetworkReply::NoError)
        return fail(SidecarDownloadError::Network, reply->errorString());
    if (m_part.size() != step.asset.size || QString::fromLatin1(m_hash.result().toHex()) != step.asset.sha256)
        return fail(SidecarDownloadError::Mismatch);
    const QString unpacking = step.unpackInto + QStringLiteral(".part");
    QDir(unpacking).removeRecursively();
    QDir().mkpath(unpacking);
    bool unpacked = false;
    if (step.runtime) {
        const QString program = unpacking + QLatin1Char('/') + runtimeExecutableName();
        unpacked = gunzip(m_part.fileName(), program) &&
                   QFile::setPermissions(program, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    } else {
        const QJsonObject packages = [&] {
            QJsonObject out;
            for (const auto &[name, version] : release.packages)
                out.insert(name, version);
            return out;
        }();
        unpacked = unzipBundle(m_part.fileName(), unpacking) &&
                   writeFile(unpacking + QStringLiteral("/release.json"),
                             QJsonDocument(QJsonObject{{QStringLiteral("version"), release.version},
                                                       {QStringLiteral("runtime"), release.runtime},
                                                       {QStringLiteral("packages"), packages}})
                                 .toJson());
    }
    m_part.remove();
    if (!unpacked) {
        QDir(unpacking).removeRecursively();
        return fail(SidecarDownloadError::Unpack);
    }
    QDir(step.unpackInto).removeRecursively();
    if (!QDir().rename(unpacking, step.unpackInto)) {
        QDir(unpacking).removeRecursively();
        return fail(SidecarDownloadError::Disk);
    }
    runSteps(goal, std::move(release), std::move(steps));
}

void AiSidecarUpdater::finished(Goal goal, const SidecarRelease &release)
{
    if (goal == Goal::Install) {
        if (usable(m_current))
            m_previous = m_current;
        m_current = release.version;
        if (m_staged == release.version)
            m_staged.clear();
    } else {
        m_staged = release.version;
    }
    if (m_available == m_current)
        m_available.clear();
    save();
    prune();
    if (goal == Goal::Install)
        emit installed(*installation(release.version));
    else
        emit downloaded(release.version);
}

bool AiSidecarUpdater::switchToStaged()
{
    if (!usable(m_staged))
        return false;
    if (usable(m_current))
        m_previous = m_current;
    m_current = m_staged;
    m_staged.clear();
    if (m_available == m_current)
        m_available.clear();
    save();
    prune();
    return true;
}

void AiSidecarUpdater::reject(const QString &version)
{
    if (version.isEmpty())
        return;
    if (!m_skipped.contains(version))
        m_skipped.push_back(version);
    if (m_current == version) {
        m_current = m_previous;
        m_previous.clear();
    }
    if (m_staged == version)
        m_staged.clear();
    if (m_available == version)
        m_available.clear();
    save();
    prune();
}

void AiSidecarUpdater::discard()
{
    if (m_part.fileName().isEmpty())
        return;
    m_part.close();
    m_part.remove();
}

void AiSidecarUpdater::fail(SidecarDownloadError error, const QString &detail)
{
    discard();
    emit failed(error, detail);
}

}
