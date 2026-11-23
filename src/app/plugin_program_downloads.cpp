#include "hikari/app/plugin_program_downloads.h"

#include "hikari/plugins/archive.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QtCore/private/qzipreader_p.h>

#include <algorithm>
#include <filesystem>

namespace hikari::app {

namespace fs = std::filesystem;

namespace {

constexpr int kMaxRedirects = 5;

QString qs(const std::string &text)
{
    return QString::fromStdString(text);
}

fs::path pathOf(const QString &path)
{
    return fs::path(path.toStdU16String());
}

QString downloadName(const plugins::ProgramDownload &download)
{
    const QString name = QUrl(qs(download.url)).fileName();
    return plugins::goodPackagePath(name.toStdString()) ? name : QStringLiteral("program");
}

const plugins::ProgramRun *here(const plugins::Program &program)
{
    const auto it = program.platforms.find(std::string(plugins::thisPlatform()));
    return it == program.platforms.end() ? nullptr : &it->second;
}

void makeExecutable(const QString &file)
{
    QFile::setPermissions(file, QFile::permissions(file) | QFileDevice::ExeOwner | QFileDevice::ExeUser);
}

std::expected<void, plugins::UnpackProblem> unpackZip(const QString &archive, const QString &into)
{
    using Kind = plugins::UnpackProblem::Kind;
    QZipReader zip(archive);
    if (zip.status() != QZipReader::NoError || !zip.isReadable())
        return std::unexpected(plugins::UnpackProblem{Kind::NotAnArchive, {}});
    plugins::UnpackClaims claims(plugins::kProgramDownloadLimits);
    for (const QZipReader::FileInfo &entry : zip.fileInfoList()) {
        std::string path = entry.filePath.toStdString();
        while (path.ends_with('/'))
            path.pop_back();
        if (entry.isSymLink)
            return std::unexpected(plugins::UnpackProblem{Kind::Link, path});
        if (entry.isDir) {
            if (auto claimed = claims.folder(path); !claimed)
                return claimed;
            QDir(into).mkpath(qs(path));
            continue;
        }
        if (auto claimed = claims.file(path, std::uint64_t(std::max<qint64>(entry.size, 0))); !claimed)
            return claimed;
        const QByteArray data = zip.fileData(entry.filePath);
        if (data.size() != entry.size || zip.status() != QZipReader::NoError)
            return std::unexpected(plugins::UnpackProblem{Kind::Truncated, path});
        const QString target = QDir(into).filePath(qs(path));
        QDir().mkpath(QFileInfo(target).absolutePath());
        QFile out(target);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(data) != data.size())
            return std::unexpected(plugins::UnpackProblem{Kind::Write, path});
        out.close();
        if (entry.permissions & QFileDevice::ExeOwner)
            makeExecutable(target);
    }
    return {};
}

}

struct PluginProgramDownloads::Fetch {
    std::string plugin;
    plugins::ProgramDownload download;
    QString title;
    QPointer<QNetworkReply> reply;
    std::unique_ptr<QFile> partial;
    QCryptographicHash hash{QCryptographicHash::Sha256};
    qint64 received = 0;
    QString refusal;
};

PluginProgramDownloads::PluginProgramDownloads(Wiring wiring, QObject *parent) : QObject(parent), m_wiring(std::move(wiring)) {}

PluginProgramDownloads::~PluginProgramDownloads()
{
    for (const auto &fetch : m_fetches)
        if (fetch->reply) {
            disconnect(fetch->reply, nullptr, this, nullptr);
            fetch->reply->abort();
        }
}

QString PluginProgramDownloads::folder(const std::string &pluginId) const
{
    return QDir(m_wiring.root).filePath(qs(pluginId));
}

std::set<std::string> PluginProgramDownloads::hashes(const plugins::Manifest &manifest)
{
    std::set<std::string> out;
    for (const plugins::Program &program : manifest.programs)
        if (const plugins::ProgramRun *run = here(program); run && run->download)
            out.insert(run->download->sha256);
    return out;
}

QString PluginProgramDownloads::file(const std::string &pluginId, const plugins::ProgramRun &run) const
{
    if (!run.download || m_wiring.root.isEmpty())
        return {};
    const QDir kept(QDir(folder(pluginId)).filePath(qs(run.download->sha256)));
    const QString target =
        kept.filePath(run.download->archive.empty() ? downloadName(*run.download) : qs(run.download->entry));
    return QFileInfo(target).isFile() ? target : QString();
}

std::vector<const plugins::Program *> PluginProgramDownloads::missing(const std::string &pluginId,
                                                                      const plugins::Manifest &manifest) const
{
    std::vector<const plugins::Program *> out;
    for (const plugins::Program &program : manifest.programs)
        if (const plugins::ProgramRun *run = here(program); run && run->download && file(pluginId, *run).isEmpty())
            out.push_back(&program);
    return out;
}

bool PluginProgramDownloads::fetching(const std::string &pluginId) const
{
    return std::ranges::any_of(m_fetches, [&](const auto &f) { return f->plugin == pluginId; });
}

QString PluginProgramDownloads::problem(const std::string &pluginId) const
{
    const auto it = m_problems.find(pluginId);
    return it == m_problems.end() ? QString() : it->second;
}

void PluginProgramDownloads::fail(const std::string &pluginId, const QString &problem)
{
    m_problems[pluginId] = problem;
    emit changed(qs(pluginId));
}

void PluginProgramDownloads::fetch(const std::string &pluginId, const plugins::Manifest &manifest)
{
    const std::vector<const plugins::Program *> wanted = missing(pluginId, manifest);
    if (wanted.empty() || m_wiring.root.isEmpty() || !m_wiring.network)
        return;
    if (!m_wiring.networkAllowed || !m_wiring.networkAllowed()) {
        fail(pluginId, QCoreApplication::translate("HikariSub.Plugins",
                                                   "Its programs aren’t downloaded: “Let plugins connect to the internet” is off."));
        return;
    }
    m_problems.erase(pluginId);
    for (const plugins::Program *program : wanted) {
        const plugins::ProgramDownload &download = *here(*program)->download;
        if (std::ranges::any_of(m_fetches, [&](const auto &f) {
                return f->plugin == pluginId && f->download.sha256 == download.sha256;
            }))
            continue;
        auto fetch = std::make_unique<Fetch>();
        fetch->plugin = pluginId;
        fetch->download = download;
        fetch->title = qs(program->title);
        QDir().mkpath(folder(pluginId));
        fetch->partial = std::make_unique<QFile>(QDir(folder(pluginId)).filePath(QStringLiteral(".partial-") + qs(download.sha256)));
        if (!fetch->partial->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            fail(pluginId, QCoreApplication::translate("HikariSub.Plugins", "%1 couldn’t be downloaded: %2")
                               .arg(fetch->title, fetch->partial->errorString()));
            continue;
        }
        QNetworkRequest request(QUrl(qs(download.url)));
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
        request.setMaximumRedirectsAllowed(kMaxRedirects);
        request.setTransferTimeout(m_wiring.transferTimeoutMs);
        request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
        request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
        request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
        request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
        request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("HikariSub/%1").arg(QCoreApplication::applicationVersion()));
        Fetch *raw = fetch.get();
        QNetworkReply *reply = m_wiring.network->get(request);
        raw->reply = reply;
        m_fetches.push_back(std::move(fetch));
        connect(reply, &QNetworkReply::redirected, this, [raw, reply](const QUrl &next) {
            if (next.scheme() != QStringLiteral("https")) {
                raw->refusal = QStringLiteral("a redirect left HTTPS");
                reply->abort();
                return;
            }
            emit reply->redirectAllowed();
        });
        connect(reply, &QNetworkReply::readyRead, this, [raw, reply] {
            const QByteArray data = reply->readAll();
            raw->received += data.size();
            if (raw->received > raw->download.size) {
                raw->refusal = QStringLiteral("it is larger than the plugin says");
                reply->abort();
                return;
            }
            raw->hash.addData(data);
            if (raw->partial->write(data) != data.size()) {
                raw->refusal = raw->partial->errorString();
                reply->abort();
            }
        });
        connect(reply, &QNetworkReply::finished, this, [this, raw] { finish(raw); });
    }
    if (fetching(pluginId))
        emit changed(qs(pluginId));
}

void PluginProgramDownloads::finish(Fetch *raw)
{
    const auto it = std::ranges::find_if(m_fetches, [raw](const auto &f) { return f.get() == raw; });
    if (it == m_fetches.end())
        return;
    const std::unique_ptr<Fetch> fetch = std::move(*it);
    m_fetches.erase(it);
    if (fetch->reply)
        fetch->reply->deleteLater();
    fetch->partial->close();
    const auto failed = [&](const QString &why) {
        fetch->partial->remove();
        fail(fetch->plugin, QCoreApplication::translate("HikariSub.Plugins", "%1 couldn’t be downloaded: %2").arg(fetch->title, why));
    };
    const int status = fetch->reply ? fetch->reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() : 0;
    if (!fetch->refusal.isEmpty())
        return failed(fetch->refusal);
    if (!fetch->reply || fetch->reply->error() != QNetworkReply::NoError)
        return failed(fetch->reply ? fetch->reply->errorString() : QString());
    if (status < 200 || status > 299)
        return failed(QStringLiteral("HTTP %1").arg(status));
    if (fetch->received != fetch->download.size || fetch->hash.result().toHex().toStdString() != fetch->download.sha256)
        return failed(QCoreApplication::translate("HikariSub.Plugins", "it isn’t the file the plugin names."));
    if (const QString problem = place(*fetch); !problem.isEmpty())
        return failed(problem);
    fetch->partial->remove();
    emit changed(qs(fetch->plugin));
}

QString PluginProgramDownloads::place(const Fetch &fetch)
{
    const QDir root(folder(fetch.plugin));
    const QString staging = root.filePath(QStringLiteral(".unpack-") + qs(fetch.download.sha256));
    const QString kept = root.filePath(qs(fetch.download.sha256));
    QDir(staging).removeRecursively();
    QDir().mkpath(staging);
    std::expected<void, plugins::UnpackProblem> unpacked;
    if (fetch.download.archive == "tar.gz")
        unpacked = plugins::unpackTarGz(pathOf(fetch.partial->fileName()), pathOf(staging), plugins::kProgramDownloadLimits);
    else if (fetch.download.archive == "zip")
        unpacked = unpackZip(fetch.partial->fileName(), staging);
    else if (const QString target = QDir(staging).filePath(downloadName(fetch.download)); QFile::copy(fetch.partial->fileName(), target))
        makeExecutable(target);
    else
        unpacked = std::unexpected(plugins::UnpackProblem{plugins::UnpackProblem::Kind::Write, {}});
    const QString entry = QDir(staging).filePath(qs(fetch.download.entry));
    QString problem;
    if (!unpacked)
        problem = QCoreApplication::translate("HikariSub.Plugins", "it couldn’t be unpacked (%1).")
                      .arg(unpacked.error().path.empty() ? QStringLiteral("/") : qs(unpacked.error().path));
    else if (!fetch.download.archive.empty() && !QFileInfo(entry).isFile())
        problem = QCoreApplication::translate("HikariSub.Plugins", "it doesn’t hold %1.").arg(qs(fetch.download.entry));
    if (problem.isEmpty()) {
        QDir(kept).removeRecursively();
        if (!QDir().rename(staging, kept))
            problem = QCoreApplication::translate("HikariSub.Plugins", "it couldn’t be kept.");
    }
    if (problem.isEmpty() && !fetch.download.archive.empty())
        makeExecutable(QDir(kept).filePath(qs(fetch.download.entry)));
    QDir(staging).removeRecursively();
    return problem;
}

void PluginProgramDownloads::keepOnly(const std::string &pluginId, const std::set<std::string> &hashes)
{
    const QDir root(folder(pluginId));
    for (const QString &name : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        if (!hashes.contains(name.toStdString()))
            QDir(root.filePath(name)).removeRecursively();
}

void PluginProgramDownloads::forget(const std::string &pluginId)
{
    std::erase_if(m_fetches, [&](const auto &f) {
        if (f->plugin != pluginId)
            return false;
        if (f->reply) {
            disconnect(f->reply, nullptr, this, nullptr);
            f->reply->abort();
            f->reply->deleteLater();
        }
        return true;
    });
    m_problems.erase(pluginId);
    QDir(folder(pluginId)).removeRecursively();
}

void PluginProgramDownloads::keepPlugins(const std::set<std::string> &installed)
{
    if (m_wiring.root.isEmpty())
        return;
    for (const QString &name : QDir(m_wiring.root).entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        if (!installed.contains(name.toStdString()))
            forget(name.toStdString());
}

}
