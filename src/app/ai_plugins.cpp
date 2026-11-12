#include "hikari/app/ai_plugins.h"

#include "hikari/api/api.h"
#include "hikari/app/plugin_manager.h"
#include "hikari/app/plugin_runtime.h"
#include "hikari/plugins/manifest.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUrl>
#include <QtCore/private/qzipreader_p.h>
#include <QtCore/private/qzipwriter_p.h>

#include <algorithm>
#include <limits>

namespace hikari::app {

namespace {

constexpr qint64 kMaxAiPackage = 1024 * 1024;

QString qs(std::string_view s)
{
    return QString::fromUtf8(s.data(), qsizetype(s.size()));
}

std::string utf8(const QString &s)
{
    return s.toUtf8().toStdString();
}

bool isPluginId(std::string_view id)
{
    return !id.empty() && id.find('/') == std::string_view::npos && id.find('\\') == std::string_view::npos &&
           id.find("..") == std::string_view::npos && id.front() != '.';
}

QString translated(const char *text)
{
    return QCoreApplication::translate("HikariSub.AiPlugins", text);
}

bool textFile(const QString &path)
{
    return path.endsWith(QStringLiteral(".lua")) || path.endsWith(QStringLiteral(".json")) ||
           path.endsWith(QStringLiteral(".md")) || path.endsWith(QStringLiteral(".txt"));
}

struct Package {
    QByteArray data;
    std::vector<api::PluginFile> files;
    std::vector<std::string> commands;
};

std::expected<Package, std::string> readPackage(const QString &path)
{
    QFile file(path);
    if (QFileInfo(path).size() > kMaxAiPackage)
        return std::unexpected(utf8(translated("The package is larger than 1 MiB, so an agent can't install it.")));
    if (!file.open(QIODevice::ReadOnly))
        return std::unexpected(utf8(translated("HikariSub can't read %1.").arg(QDir::toNativeSeparators(path))));
    Package out;
    out.data = file.readAll();
    QBuffer buffer(&out.data);
    buffer.open(QIODevice::ReadOnly);
    QZipReader zip(&buffer);
    std::string main;
    for (const QZipReader::FileInfo &entry : zip.fileInfoList()) {
        if (!entry.isFile || !textFile(entry.filePath))
            continue;
        const QByteArray text = zip.fileData(entry.filePath);
        if (entry.filePath == QLatin1String(plugins::kManifestName.data(), qsizetype(plugins::kManifestName.size())))
            if (auto manifest = plugins::parseManifest(std::string_view(text.constData(), std::size_t(text.size())),
                                                       std::numeric_limits<std::int64_t>::max())) {
                main = manifest->main;
                for (const plugins::Contribution &c : manifest->commands)
                    out.commands.push_back(c.title);
            }
        out.files.push_back({utf8(entry.filePath), text.toStdString()});
    }
    std::stable_partition(out.files.begin(), out.files.end(), [&](const api::PluginFile &f) { return f.path == main; });
    return out;
}

std::string digestOf(const QByteArray &data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex().toStdString();
}

std::vector<std::string> strings(const QVariant &list)
{
    std::vector<std::string> out;
    for (const QVariant &v : list.toList())
        out.push_back(utf8(v.toString()));
    return out;
}

api::PluginReview reviewOf(const QVariantMap &review, Package package)
{
    api::PluginReview out;
    out.id = utf8(review.value(QStringLiteral("id")).toString());
    out.name = utf8(review.value(QStringLiteral("name")).toString());
    out.version = utf8(review.value(QStringLiteral("version")).toString());
    out.change = utf8(review.value(QStringLiteral("change")).toString());
    out.installedVersion = utf8(review.value(QStringLiteral("installedVersion")).toString());
    out.fullTrust = review.value(QStringLiteral("unrestricted")).toBool();
    out.needsRetrust = review.value(QStringLiteral("needsRetrust")).toBool();
    out.keyId = utf8(review.value(QStringLiteral("keyId")).toString());
    out.pinnedKeyId = utf8(review.value(QStringLiteral("pinnedKeyId")).toString());
    out.permissions = strings(review.value(QStringLiteral("permissions")));
    out.commands = std::move(package.commands);
    out.files = std::move(package.files);
    out.digest = digestOf(package.data);
    return out;
}

}

AiPlugins::AiPlugins(Wiring wiring)
    : m_wiring(std::move(wiring))
{
}

std::vector<api::PluginState> AiPlugins::plugins() const
{
    std::vector<api::PluginState> out;
    PluginManager *manager = m_wiring.manager ? m_wiring.manager() : nullptr;
    if (!manager)
        return out;
    const QVariantList commands = m_wiring.runtime && m_wiring.runtime() ? m_wiring.runtime()->commands() : QVariantList();
    for (const QVariant &row : manager->plugins()) {
        const QVariantMap p = row.toMap();
        api::PluginState state;
        state.id = utf8(p.value(QStringLiteral("id")).toString());
        state.name = utf8(p.value(QStringLiteral("name")).toString());
        state.version = utf8(p.value(QStringLiteral("version")).toString());
        state.description = utf8(p.value(QStringLiteral("description")).toString());
        state.source = utf8(p.value(QStringLiteral("source")).toString());
        state.signedPackage = !p.value(QStringLiteral("keyId")).toString().isEmpty();
        state.enabled = p.value(QStringLiteral("enabled")).toBool();
        state.fullTrust = p.value(QStringLiteral("unrestricted")).toBool();
        state.revoked = p.value(QStringLiteral("revoked")).toBool();
        for (const QVariant &c : commands) {
            const QVariantMap command = c.toMap();
            if (command.value(QStringLiteral("plugin")).toString() == qs(state.id))
                state.commands.push_back({utf8(command.value(QStringLiteral("id")).toString()),
                                          utf8(command.value(QStringLiteral("title")).toString())});
        }
        out.push_back(std::move(state));
    }
    return out;
}

std::optional<std::string> AiPlugins::manifest(std::string_view plugin) const
{
    if (!isPluginId(plugin) || m_wiring.installed.isEmpty())
        return std::nullopt;
    const auto installed = plugins();
    if (std::ranges::none_of(installed, [&](const api::PluginState &p) { return p.id == plugin; }))
        return std::nullopt;
    QFile file(QDir(m_wiring.installed).filePath(qs(plugin) + u'/' + qs(plugins::kManifestName)));
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    return file.read(kMaxAiPackage).toStdString();
}

api::PluginRun AiPlugins::run(std::string_view plugin, std::string_view command)
{
    const auto installed = plugins();
    const auto it = std::ranges::find(installed, plugin, &api::PluginState::id);
    if (it == installed.end())
        return api::PluginRun::NotInstalled;
    if (it->commands.empty())
        return api::PluginRun::NotRunning;
    const std::string id = "plugin." + std::string(plugin) + "." + std::string(command);
    if (std::ranges::none_of(it->commands, [&](const api::PluginCommand &c) { return c.command == id; }))
        return api::PluginRun::UnknownCommand;
    PluginRuntime *runtime = m_wiring.runtime ? m_wiring.runtime() : nullptr;
    return runtime && runtime->run(qs(id)) ? api::PluginRun::Ran : api::PluginRun::NotRunning;
}

std::expected<api::ScaffoldedPlugin, std::string> AiPlugins::scaffold(std::string_view plugin,
                                                                      const std::vector<api::PluginFile> &files)
{
    if (m_wiring.folder.isEmpty())
        return std::unexpected(utf8(translated("There is no user folder to write the plugin to.")));
    if (!isPluginId(plugin))
        return std::unexpected(utf8(translated("That is not a plugin id.")));
    for (const api::PluginFile &f : files)
        if (f.path == plugins::kManifestName) {
            const auto parsed = plugins::parseManifest(f.text, api::kApiVersion);
            if (!parsed)
                return std::unexpected(utf8(translated("The manifest is not valid: %1").arg(qs(parsed.error().detail))));
        }
    const QString folder = QDir(m_wiring.folder).filePath(qs(plugin));
    const QString package = folder + QStringLiteral(".hikariplugin");
    QDir(folder).removeRecursively();
    QFile::remove(package);
    QZipWriter zip(package);
    zip.setCompressionPolicy(QZipWriter::AutoCompress);
    for (const api::PluginFile &f : files) {
        const QString path = QDir(folder).filePath(qs(f.path));
        const QByteArray text = QByteArray::fromStdString(f.text);
        QDir().mkpath(QFileInfo(path).path());
        QSaveFile out(path);
        if (!out.open(QIODevice::WriteOnly) || out.write(text) != text.size() || !out.commit())
            return std::unexpected(utf8(translated("HikariSub can't write %1.").arg(QDir::toNativeSeparators(path))));
        zip.addFile(qs(f.path), text);
    }
    zip.close();
    if (zip.status() != QZipWriter::NoError)
        return std::unexpected(utf8(translated("HikariSub can't write %1.").arg(QDir::toNativeSeparators(package))));
    return api::ScaffoldedPlugin{utf8(folder), utf8(package)};
}

std::optional<std::string> AiPlugins::scaffoldedPackage(std::string_view plugin) const
{
    if (m_wiring.folder.isEmpty() || !isPluginId(plugin))
        return std::nullopt;
    const QString package = QDir(m_wiring.folder).filePath(qs(plugin) + QStringLiteral(".hikariplugin"));
    return QFileInfo(package).isFile() ? std::optional(utf8(package)) : std::nullopt;
}

std::expected<api::PluginReview, std::string> AiPlugins::review(const std::string &package)
{
    PluginManager *manager = m_wiring.manager ? m_wiring.manager() : nullptr;
    if (!manager)
        return std::unexpected(utf8(translated("Plugins are not available.")));
    auto read = readPackage(qs(package));
    if (!read)
        return std::unexpected(read.error());
    const QVariantMap review = manager->review(QUrl::fromLocalFile(qs(package)));
    manager->cancelReview();
    if (!review.value(QStringLiteral("ok")).toBool())
        return std::unexpected(utf8(review.value(QStringLiteral("message")).toString()));
    return reviewOf(review, std::move(*read));
}

std::expected<api::PluginReview, std::string> AiPlugins::install(const std::string &package, const std::string &digest)
{
    PluginManager *manager = m_wiring.manager ? m_wiring.manager() : nullptr;
    if (!manager)
        return std::unexpected(utf8(translated("Plugins are not available.")));
    auto read = readPackage(qs(package));
    if (!read)
        return std::unexpected(read.error());
    if (digestOf(read->data) != digest)
        return std::unexpected(utf8(translated("The package changed after the user approved it, so it wasn't installed.")));
    const QVariantMap review = manager->review(QUrl::fromLocalFile(qs(package)));
    if (!review.value(QStringLiteral("ok")).toBool())
        return std::unexpected(utf8(review.value(QStringLiteral("message")).toString()));
    if (const QString problem = manager->install(review.value(QStringLiteral("needsRetrust")).toBool()); !problem.isEmpty())
        return std::unexpected(utf8(problem));
    return reviewOf(review, std::move(*read));
}

}
