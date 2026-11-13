#include "hikari/app/plugin_manager.h"

#include "hikari/api/api.h"
#include "hikari/backends/desktop_handoff.h"

#include <QBuffer>
#include <QCollator>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>
#include <private/qzipreader_p.h>

#include <algorithm>
#include <limits>

namespace hikari::app {

namespace {

namespace fs = std::filesystem;
using plugins::InstallRefusal;
using plugins::ManifestProblem;
using plugins::PackageProblem;

fs::path path(const QString &text)
{
    return fs::path(text.toStdU16String());
}

QString text(std::string_view s)
{
    return QString::fromUtf8(s.data(), qsizetype(s.size()));
}

std::span<const std::byte> bytes(const QByteArray &data)
{
    return {reinterpret_cast<const std::byte *>(data.constData()), std::size_t(data.size())};
}

struct Unpacked {
    std::vector<plugins::PackageFile> files;
    QString problem;
};

Unpacked unpack(const QByteArray &data)
{
    QBuffer buffer;
    buffer.setData(data);
    buffer.open(QIODevice::ReadOnly);
    QZipReader zip(&buffer);
    const auto notAPackage = [] {
        return Unpacked{{}, QCoreApplication::translate("HikariSub.Plugins",
                                                        "The file is not a plugin package (a zip holding hikari-plugin.json).")};
    };
    if (zip.status() != QZipReader::NoError || !zip.isReadable())
        return notAPackage();
    const QList<QZipReader::FileInfo> entries = zip.fileInfoList();
    if (entries.isEmpty())
        return notAPackage();
    qint64 total = 0;
    std::size_t count = 0;
    for (const QZipReader::FileInfo &entry : entries) {
        if (entry.isSymLink)
            return Unpacked{{}, QCoreApplication::translate("HikariSub.Plugins",
                                                            "The package holds a link, which HikariSub won’t install: %1")
                                    .arg(entry.filePath)};
        if (entry.isFile) {
            total += entry.size;
            ++count;
        }
    }
    if (total > qint64(plugins::kMaxPackageBytes) || count > plugins::kMaxPackageFiles)
        return Unpacked{{}, QCoreApplication::translate("HikariSub.Plugins",
                                                        "The package is too large: more than %1 files or %2 MB unpacked.")
                                .arg(plugins::kMaxPackageFiles)
                                .arg(plugins::kMaxPackageBytes / (1024 * 1024))};
    Unpacked out;
    for (const QZipReader::FileInfo &entry : entries) {
        if (!entry.isFile)
            continue;
        const QByteArray content = zip.fileData(entry.filePath);
        if (content.size() != entry.size || zip.status() != QZipReader::NoError)
            return notAPackage();
        const auto *begin = reinterpret_cast<const std::byte *>(content.constData());
        out.files.push_back({entry.filePath.toStdString(), {begin, begin + content.size()}});
    }
    return out;
}

QString manifestMessage(const ManifestProblem &problem)
{
    const QString at = problem.at.empty() ? QStringLiteral("/") : text(problem.at);
    switch (problem.kind) {
    case ManifestProblem::Kind::NotJson:
        return QCoreApplication::translate("HikariSub.Plugins", "hikari-plugin.json is not valid JSON: %1").arg(text(problem.detail));
    case ManifestProblem::Kind::DuplicateId:
        return QCoreApplication::translate("HikariSub.Plugins", "hikari-plugin.json repeats the id “%1” at %2.")
            .arg(text(problem.detail), at);
    case ManifestProblem::Kind::NeedsNewerApi:
        return QCoreApplication::translate("HikariSub.Plugins",
                                           "This plugin needs Hikari API %1, and this HikariSub has API %2. Update HikariSub to install it.")
            .arg(problem.neededApi)
            .arg(api::kApiVersion);
    case ManifestProblem::Kind::QmlNeedsFullTrust:
        return QCoreApplication::translate("HikariSub.Plugins", "Only Full trust plugins can ship QML panels.");
    case ManifestProblem::Kind::Schema:
    case ManifestProblem::Kind::BadDefault:
        break;
    }
    return QCoreApplication::translate("HikariSub.Plugins", "hikari-plugin.json fails the plugin schema at %1: %2")
        .arg(at, text(problem.detail));
}

QString packageMessage(const PackageProblem &problem)
{
    switch (problem.kind) {
    case PackageProblem::Kind::TooLarge:
        return QCoreApplication::translate("HikariSub.Plugins", "The package is too large: more than %1 files or %2 MB unpacked.")
            .arg(plugins::kMaxPackageFiles)
            .arg(plugins::kMaxPackageBytes / (1024 * 1024));
    case PackageProblem::Kind::BadPath:
        return QCoreApplication::translate("HikariSub.Plugins", "The package holds a file name HikariSub won’t install: %1")
            .arg(text(problem.path));
    case PackageProblem::Kind::NoManifest:
        return QCoreApplication::translate("HikariSub.Plugins", "The package has no hikari-plugin.json at its top level.");
    case PackageProblem::Kind::MissingMain:
        return QCoreApplication::translate("HikariSub.Plugins", "The package lacks its main file, %1.").arg(text(problem.path));
    case PackageProblem::Kind::Media:
        return QCoreApplication::translate("HikariSub.Plugins", "The package breaks the media rules: %1 %2.")
            .arg(text(problem.path), text(problem.detail));
    case PackageProblem::Kind::MissingPanel:
        return QCoreApplication::translate("HikariSub.Plugins", "The package lacks its panel file, %1.").arg(text(problem.path));
    case PackageProblem::Kind::QmlImport:
        return QCoreApplication::translate("HikariSub.Plugins", "%1 imports %2, which plugin panels can’t use.")
            .arg(text(problem.path), text(problem.detail));
    case PackageProblem::Kind::Manifest:
        break;
    }
    return manifestMessage(problem.manifest);
}

QString refusalMessage(const InstallRefusal &refusal)
{
    switch (refusal.kind) {
    case InstallRefusal::Kind::Package:
        return packageMessage(refusal.package);
    case InstallRefusal::Kind::SignedWithoutKey:
        return QCoreApplication::translate("HikariSub.Plugins",
                                           "The package has a signature, but its manifest names no publicKey to check it with.");
    case InstallRefusal::Kind::Registry:
        return QCoreApplication::translate("HikariSub.Plugins", "plugins.json in the user folder can’t be read: %1")
            .arg(text(refusal.detail));
    case InstallRefusal::Kind::Listing:
        return QCoreApplication::translate("HikariSub.Plugins", "The download isn’t the plugin the marketplace lists: %1.")
            .arg(text(refusal.detail));
    case InstallRefusal::Kind::Signature:
        break;
    }
    switch (refusal.signature) {
    case minisign::Refusal::UntrustedKey:
        return QCoreApplication::translate("HikariSub.Plugins", "The package is signed by a key its manifest doesn’t name.");
    case minisign::Refusal::BadSignature:
        return QCoreApplication::translate("HikariSub.Plugins",
                                           "The signature doesn’t match the package, so it may have been changed after signing.");
    case minisign::Refusal::BadTrustedComment:
        return QCoreApplication::translate("HikariSub.Plugins", "The signature’s trusted comment was changed after signing.");
    case minisign::Refusal::Malformed:
    case minisign::Refusal::TooLarge:
    case minisign::Refusal::UnknownAlgorithm:
        break;
    }
    return QCoreApplication::translate("HikariSub.Plugins", "The .minisig file beside the package is not a minisign signature.");
}

QStringList permissionLines(const plugins::Manifest &manifest)
{
    return app::permissionLines(manifest.tier == plugins::Tier::Unrestricted, manifest.permissions, manifest.hasQmlPanels());
}

QVariantList folderRows(const plugins::Manifest &manifest, const std::map<std::string, plugins::FileGrant> &grants)
{
    QVariantList rows;
    for (const plugins::FileGrantRequest &request : manifest.permissions.files)
        if (const auto grant = grants.find(request.id); grant != grants.end())
            rows << QVariantMap{{QStringLiteral("id"), text(request.id)},
                                {QStringLiteral("title"), text(request.title)},
                                {QStringLiteral("path"), QDir::toNativeSeparators(text(grant->second.path))},
                                {QStringLiteral("writable"), grant->second.writable && request.writable}};
    return rows;
}

QVariant variantOf(const api::Json &json)
{
    const QByteArray wrapped = QByteArray("[") + QByteArray::fromStdString(json.dump()) + "]";
    return QJsonDocument::fromJson(wrapped).array().at(0).toVariant();
}

QString authorsText(const plugins::Manifest &manifest)
{
    QStringList names;
    for (const std::string &author : manifest.authors)
        names << text(author);
    return names.join(QStringLiteral(", "));
}

QStringList contributionLines(const plugins::Manifest &manifest)
{
    QStringList lines;
    for (const plugins::Contribution &command : manifest.commands)
        lines << QCoreApplication::translate("HikariSub.Plugins", "Command: %1").arg(text(command.title));
    for (const plugins::Panel &panel : manifest.panels)
        lines << QCoreApplication::translate("HikariSub.Plugins", "Panel: %1").arg(text(panel.title));
    return lines;
}

QVariantList settingRows(const plugins::Manifest &manifest, const api::Json &stored)
{
    const api::Json values = plugins::settingValues(manifest, stored);
    QVariantList rows;
    for (const plugins::Setting &setting : manifest.settings) {
        QVariantMap row = variantOf(setting.definition).toMap();
        if (const api::Json *value = values.find(setting.id))
            row.insert(QStringLiteral("value"), variantOf(*value));
        rows << row;
    }
    return rows;
}

QString changeName(plugins::Change change)
{
    switch (change) {
    case plugins::Change::Update: return QStringLiteral("update");
    case plugins::Change::Reinstall: return QStringLiteral("reinstall");
    case plugins::Change::Downgrade: return QStringLiteral("downgrade");
    case plugins::Change::Install: break;
    }
    return QStringLiteral("install");
}

QString keyId(const std::optional<minisign::PublicKey> &key)
{
    return key ? QString::fromStdString(key->idText()) : QString();
}

QString problemText(const std::string &problem)
{
    return QCoreApplication::translate("HikariSub.Plugins", "HikariSub couldn’t finish: %1").arg(text(problem));
}

QString noUserFolder()
{
    return QCoreApplication::translate("HikariSub.Plugins", "Plugins need a user folder, and this copy has none.");
}

QVariantList permissionChanges(const plugins::Manifest &current, const plugins::Manifest &next)
{
    const plugins::PermissionIncrease more = plugins::permissionIncrease(current, next);
    QList<bool> added;
    if (next.tier == plugins::Tier::Unrestricted)
        added << more.unrestricted;
    if (next.tier == plugins::Tier::Unrestricted && next.hasQmlPanels())
        added << more.unrestricted;
    if (next.permissions.allDocuments)
        added << more.allDocuments;
    if (!next.permissions.networkHosts.empty())
        added << !more.networkHosts.empty();
    for (const plugins::FileGrantRequest &request : next.permissions.files)
        added << (std::ranges::find(more.files, request.id) != more.files.end());
    const QStringList lines = permissionLines(next);
    QVariantList rows;
    for (qsizetype i = 0; i < lines.size(); ++i)
        rows << QVariantMap{{QStringLiteral("text"), lines.at(i)}, {QStringLiteral("added"), added.value(i)}};
    return rows;
}

}

QStringList permissionLines(bool unrestricted, const plugins::Permissions &p, bool qmlPanels)
{
    QStringList lines;
    if (unrestricted)
        lines << QCoreApplication::translate("HikariSub.Plugins", "Full trust: it may run native code outside HikariSub’s limits.");
    if (unrestricted && qmlPanels)
        lines << QCoreApplication::translate("HikariSub.Plugins", "Shows its own panels inside HikariSub");
    if (p.allDocuments)
        lines << QCoreApplication::translate("HikariSub.Plugins", "Read and edit every open subtitle file: %1").arg(text(*p.allDocuments));
    if (!p.networkHosts.empty()) {
        QStringList hosts;
        for (const std::string &host : p.networkHosts)
            hosts << text(host);
        lines << QCoreApplication::translate("HikariSub.Plugins", "Connect to %1: %2").arg(hosts.join(QStringLiteral(", ")), text(p.networkReason));
    }
    for (const plugins::FileGrantRequest &f : p.files)
        lines << (f.writable ? QCoreApplication::translate("HikariSub.Plugins", "Ask you for a folder to read and write, %1: %2")
                             : QCoreApplication::translate("HikariSub.Plugins", "Ask you for a folder to read, %1: %2"))
                     .arg(text(f.title), text(f.reason));
    return lines;
}

PluginManager::PluginManager(Folders folders, QObject *parent) : QObject(parent)
{
    if (!folders.plugins.isEmpty())
        m_installer.emplace(plugins::PluginFolders{path(folders.plugins), path(folders.data), path(folders.cache),
                                                   path(folders.registry)},
                            api::kApiVersion);
    refresh();
}

void PluginManager::setOfficialAuthor(plugins::OfficialAuthor official)
{
    m_official = std::move(official);
    refresh();
    emit pluginsChanged();
}

void PluginManager::refresh()
{
    m_rows.clear();
    m_problem.clear();
    if (m_installer) {
        const auto registry = m_installer->registry();
        if (!registry) {
            m_problem = QCoreApplication::translate("HikariSub.Plugins", "plugins.json in the user folder can’t be read: %1")
                            .arg(text(registry.error()));
        } else {
            for (const auto &[id, entry] : *registry) {
                QFile file(QDir(QString::fromStdU16String(m_installer->folders().plugins.u16string()))
                               .filePath(text(id) + QStringLiteral("/hikari-plugin.json")));
                std::optional<plugins::Manifest> manifest;
                if (file.open(QIODevice::ReadOnly)) {
                    const QByteArray data = file.read(1024 * 1024);
                    if (auto parsed = plugins::parseManifest(std::string_view(data.constData(), std::size_t(data.size())),
                                                             std::numeric_limits<std::int64_t>::max()))
                        manifest = std::move(*parsed);
                }
                m_rows.append(QVariantMap{
                    {QStringLiteral("id"), text(id)},
                    {QStringLiteral("name"), manifest ? text(manifest->name) : text(id)},
                    {QStringLiteral("version"), text(entry.version)},
                    {QStringLiteral("description"), manifest ? text(manifest->description) : QString()},
                    {QStringLiteral("enabled"), entry.enabled},
                    {QStringLiteral("keyId"), keyId(entry.key)},
                    {QStringLiteral("source"), entry.source == plugins::Source::Marketplace ? QStringLiteral("marketplace")
                                                                                            : QStringLiteral("file")},
                    {QStringLiteral("unrestricted"), manifest && manifest->tier == plugins::Tier::Unrestricted},
                    {QStringLiteral("official"), m_official.owns(id, entry.key)},
                    {QStringLiteral("icon"), manifest && !manifest->icon.empty()
                                                 ? QUrl::fromLocalFile(QFileInfo(file).dir().filePath(text(manifest->icon)))
                                                 : QUrl()},
                    {QStringLiteral("authors"), manifest ? authorsText(*manifest) : QString()},
                    {QStringLiteral("homepage"), manifest ? text(manifest->homepage) : QString()},
                    {QStringLiteral("permissions"), manifest ? permissionLines(*manifest) : QStringList()},
                    {QStringLiteral("adds"), manifest ? contributionLines(*manifest) : QStringList()},
                    {QStringLiteral("settings"), manifest ? settingRows(*manifest, entry.settings) : QVariantList()},
                    {QStringLiteral("folders"), manifest ? folderRows(*manifest, entry.grants) : QVariantList()},
                    {QStringLiteral("autoUpdate"), entry.autoUpdate},
                    {QStringLiteral("previous"), text(entry.previous)},
                    {QStringLiteral("held"), text(entry.held)},
                    {QStringLiteral("revoked"), entry.revoked.has_value()},
                    {QStringLiteral("revokedReason"), entry.revoked ? text(entry.revoked->reason) : QString()},
                    {QStringLiteral("lastGood"), entry.revoked ? text(entry.revoked->lastGood) : QString()},
                });
            }
        }
    }
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::ranges::sort(m_rows, [&](const QVariant &a, const QVariant &b) {
        return collator.compare(a.toMap().value(QStringLiteral("name")).toString(),
                                b.toMap().value(QStringLiteral("name")).toString()) < 0;
    });
    emit pluginsChanged();
}

QVariantMap PluginManager::review(const QUrl &url)
{
    m_review.reset();
    const auto refused = [](const QString &message) {
        return QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("message"), message}};
    };
    if (!m_installer)
        return refused(QCoreApplication::translate("HikariSub.Plugins", "Plugins need a user folder, and this copy has none."));
    const QString file = url.isLocalFile() ? url.toLocalFile() : url.toString();
    QFile package(file);
    if (QFileInfo(file).size() > qint64(plugins::kMaxPackageBytes))
        return refused(packageMessage({PackageProblem::Kind::TooLarge, {}, {}}));
    if (!package.open(QIODevice::ReadOnly))
        return refused(QCoreApplication::translate("HikariSub.Plugins", "HikariSub can’t read %1.").arg(QDir::toNativeSeparators(file)));
    const QByteArray data = package.readAll();
    Unpacked unpacked = unpack(data);
    if (!unpacked.problem.isEmpty())
        return refused(unpacked.problem);

    std::optional<std::string> signature;
    QFile signatureFile(file + QStringLiteral(".minisig"));
    if (signatureFile.open(QIODevice::ReadOnly))
        signature = signatureFile.read(qint64(minisign::kMaxSignatureBytes) + 1).toStdString();

    return reviewed(m_installer->review(bytes(data), std::move(unpacked.files), signature));
}

QVariantMap PluginManager::reviewListed(const QByteArray &package, const plugins::Listing &listing,
                                        const plugins::ListedVersion &listed, bool allowDowngrade)
{
    m_review.reset();
    Prepared prepared = prepareListed(package, listing, listed, allowDowngrade);
    if (!prepared.review)
        return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), prepared.problem}};
    m_review = std::move(*prepared.review);
    QVariantMap review = describe(*m_review);
    review.insert(QStringLiteral("signedBy"), text(listing.authorName));
    review.insert(QStringLiteral("changelog"), text(listed.changelog));
    return review;
}

PluginManager::Prepared PluginManager::prepareListed(const QByteArray &package, const plugins::Listing &listing,
                                                     const plugins::ListedVersion &listed, bool allowDowngrade) const
{
    if (!m_installer)
        return {std::nullopt, noUserFolder()};
    if (const auto problem = plugins::listedPackageProblem(listed, bytes(package)))
        return {std::nullopt, refusalMessage({InstallRefusal::Kind::Listing, {}, {}, *problem})};
    Unpacked unpacked = unpack(package);
    if (!unpacked.problem.isEmpty())
        return {std::nullopt, unpacked.problem};
    auto result = m_installer->reviewListed(std::move(unpacked.files), listing, listed, allowDowngrade);
    if (!result)
        return {std::nullopt, refusalMessage(result.error())};
    bool more = false;
    if (const auto current = m_installer->installedManifest(result->package.manifest.id))
        more = plugins::permissionIncrease(*current, result->package.manifest).any();
    return {std::move(*result), {}, more};
}

QString PluginManager::installPrepared(const plugins::InstallReview &review)
{
    if (!m_installer)
        return noUserFolder();
    const auto registry = m_installer->registry();
    const auto it = registry ? registry->find(review.package.manifest.id) : plugins::Registry::const_iterator();
    if (!registry || it == registry->end() || it->second.version != review.installedVersion)
        return QCoreApplication::translate("HikariSub.Plugins", "The plugin changed while its update downloaded.");
    const auto done = m_installer->install(review, false);
    refresh();
    return done ? QString() : problemText(done.error());
}

std::expected<plugins::Registry, std::string> PluginManager::registry() const
{
    if (!m_installer)
        return plugins::Registry{};
    return m_installer->registry();
}

QString PluginManager::name(const QString &id) const
{
    for (const QVariant &row : m_rows)
        if (row.toMap().value(QStringLiteral("id")).toString() == id)
            return row.toMap().value(QStringLiteral("name")).toString();
    return id;
}

QVariantMap PluginManager::reviewed(std::expected<plugins::InstallReview, plugins::InstallRefusal> result)
{
    if (!result)
        return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), refusalMessage(result.error())}};
    m_review = std::move(*result);
    return describe(*m_review);
}

QVariantMap PluginManager::describe(const plugins::InstallReview &review) const
{
    const plugins::Manifest &m = review.package.manifest;
    QVariantMap out{
        {QStringLiteral("ok"), true},
        {QStringLiteral("id"), text(m.id)},
        {QStringLiteral("name"), text(m.name)},
        {QStringLiteral("version"), text(m.version)},
        {QStringLiteral("change"), changeName(review.change)},
        {QStringLiteral("installedVersion"), text(review.installedVersion)},
        {QStringLiteral("keyId"), keyId(review.signer)},
        {QStringLiteral("pinnedKeyId"), keyId(review.pinned)},
        {QStringLiteral("needsRetrust"), review.needsRetrust},
        {QStringLiteral("unrestricted"), m.tier == plugins::Tier::Unrestricted},
        {QStringLiteral("permissions"), permissionLines(m)},
        {QStringLiteral("marketplace"), review.source == plugins::Source::Marketplace},
        {QStringLiteral("changes"), QVariantList()},
        {QStringLiteral("asksForMore"), false},
    };
    if (review.installedVersion.empty() || !m_installer)
        return out;
    if (const auto current = m_installer->installedManifest(m.id)) {
        out.insert(QStringLiteral("changes"), permissionChanges(*current, m));
        out.insert(QStringLiteral("asksForMore"), plugins::permissionIncrease(*current, m).any());
    }
    return out;
}

QVariantMap PluginManager::reviewUpdate(const QUrl &file, const QString &id)
{
    QVariantMap review = this->review(file);
    if (review.value(QStringLiteral("ok")).toBool() && review.value(QStringLiteral("id")).toString() != id) {
        m_review.reset();
        QString installed = id;
        for (const QVariant &row : std::as_const(m_rows))
            if (row.toMap().value(QStringLiteral("id")).toString() == id)
                installed = row.toMap().value(QStringLiteral("name")).toString();
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("message"), QCoreApplication::translate("HikariSub.Plugins", "This package is “%1”, not an update of “%2”.")
                                                .arg(review.value(QStringLiteral("name")).toString(), installed)}};
    }
    return review;
}

QString PluginManager::setSetting(const QString &id, const QString &setting, const QString &json)
{
    const auto value = api::Json::parse(json.toStdString());
    if (!m_installer || !value)
        return {};
    const auto done = m_installer->setSetting(id.toStdString(), setting.toStdString(), *value);
    if (!done)
        return problemText(done.error());
    refresh();
    emit settingChanged(id, setting, json);
    return {};
}

QString PluginManager::install(bool retrusted)
{
    if (!m_installer || !m_review)
        return QCoreApplication::translate("HikariSub.Plugins", "Choose a plugin package first.");
    const auto done = m_installer->install(*m_review, retrusted);
    m_review.reset();
    refresh();
    return done ? QString() : problemText(done.error());
}

QString PluginManager::edit(const QString &id, const std::function<void(plugins::InstalledPlugin &)> &change)
{
    if (!m_installer)
        return noUserFolder();
    const auto done = m_installer->edit(id.toStdString(), change);
    refresh();
    return done ? QString() : problemText(done.error());
}

QString PluginManager::setAutoUpdate(const QString &id, bool on)
{
    return edit(id, [on](plugins::InstalledPlugin &plugin) { plugin.autoUpdate = on; });
}

QString PluginManager::rollBack(const QString &id)
{
    if (!m_installer)
        return noUserFolder();
    const auto left = m_installer->rollBack(id.toStdString(), true);
    refresh();
    if (!left)
        return problemText(left.error());
    const auto registry = m_installer->registry();
    const auto entry = registry ? registry->find(id.toStdString()) : plugins::Registry::const_iterator();
    emit rolledBack(id, name(id), registry && entry != registry->end() ? text(entry->second.version) : QString());
    return {};
}

QString PluginManager::undoRollBack(const QString &id)
{
    if (!m_installer)
        return noUserFolder();
    const auto done = m_installer->rollBack(id.toStdString(), false);
    refresh();
    return done ? QString() : problemText(done.error());
}

QString PluginManager::setEnabled(const QString &id, bool enabled)
{
    if (!m_installer)
        return {};
    if (const auto registry = m_installer->registry(); enabled && registry) {
        const auto entry = registry->find(id.toStdString());
        if (entry != registry->end() && entry->second.revoked)
            return QCoreApplication::translate("HikariSub.Plugins", "The marketplace revoked this version. Install another one to turn it on.");
    }
    const auto done = m_installer->setEnabled(id.toStdString(), enabled);
    refresh();
    return done ? QString() : problemText(done.error());
}

QUrl PluginManager::dataFolder(const QString &id) const
{
    if (!m_installer)
        return {};
    const QString folder = QString::fromStdU16String((m_installer->folders().data / path(id)).u16string());
    if (!QDir().mkpath(folder))
        return {};
    return QUrl::fromLocalFile(folder);
}

bool PluginManager::openDataFolder(const QString &id) const
{
    const QUrl folder = dataFolder(id);
    return !folder.isEmpty() && backends::desktop::openUrl(folder);
}

bool PluginManager::hasData(const QString &id) const
{
    if (!m_installer)
        return false;
    const QDir data(QString::fromStdU16String((m_installer->folders().data / path(id)).u16string()));
    return data.exists() && !data.isEmpty();
}

QString PluginManager::uninstall(const QString &id, bool removeData)
{
    if (!m_installer)
        return {};
    const auto done = m_installer->uninstall(id.toStdString(), removeData);
    refresh();
    return done ? QString() : problemText(done.error());
}

QString PluginManager::forgetFolder(const QString &id, const QString &folder)
{
    if (!m_installer)
        return {};
    const auto done = m_installer->setGrant(id.toStdString(), folder.toStdString(), std::nullopt);
    refresh();
    if (!done)
        return problemText(done.error());
    emit folderForgotten(id);
    return {};
}

}
