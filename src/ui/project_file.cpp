#include "project_file.h"

#include "toml_file.h"

#include <QDir>

#include <filesystem>
#include <sstream>
#include <utility>
#include <variant>

namespace hikari::ui {

namespace {

using application::SettingDefinition;
using application::SettingValue;

std::filesystem::path pathOf(const QString &path)
{
    return std::filesystem::path(path.toStdU16String());
}

void collectRefused(const toml::table &table, const std::string &prefix, std::vector<std::string> &out)
{
    for (const auto &[key, node] : table) {
        const std::string id = prefix + std::string(key.str());
        if (const auto *inner = node.as_table()) {
            collectRefused(*inner, id + ".", out);
            continue;
        }
        if (const auto *setting = application::findSetting(id); setting && !setting->projectable)
            out.push_back(id);
    }
}

}

ProjectFile::ProjectFile(QString root) : m_root(std::move(root))
{
    load(Shared);
    load(Local);
}

QString ProjectFile::path(File file) const
{
    return m_root + (file == Shared ? QStringLiteral("/.hikari/project.toml") : QStringLiteral("/.hikari/project.local.toml"));
}

void ProjectFile::load(File file)
{
    auto loaded = application::loadOwnedFile(pathOf(path(file)), [&](std::string_view text) {
        auto parsed = parseTomlFile(text, kVersion);
        if (auto *failure = std::get_if<application::ParseFailure>(&parsed))
            return std::optional(*failure);
        m_tables[file] = std::move(std::get<toml::table>(parsed));
        return std::optional<application::ParseFailure>();
    });
    if (loaded.problem) {
        if (file == Shared)
            m_writable = !loaded.problem->keptAs.empty();
        m_problems.push_back(std::move(*loaded.problem));
    }
    collectRefused(m_tables[file], {}, m_refused);
}

std::optional<ProjectFile::File> ProjectFile::source(const SettingDefinition &setting) const
{
    if (!setting.projectable)
        return std::nullopt;
    for (const File file : {Local, Shared})
        if (const auto *node = tomlNode(m_tables[file], setting.id); node && fromToml(*node))
            return file;
    return std::nullopt;
}

bool ProjectFile::hasTable(std::string_view name) const
{
    for (const auto &table : m_tables)
        if (const auto *node = table.get(name); node && node->is_table())
            return true;
    return false;
}

std::vector<QString> ProjectFile::fontFolders() const
{
    const QDir root(m_root);
    for (const File file : {Local, Shared}) {
        const auto *list = m_tables[file]["fonts"].as_array();
        if (!list)
            continue;
        std::vector<QString> out;
        for (const auto &node : *list)
            if (const auto *folder = node.as_string(); folder && !folder->get().empty())
                out.push_back(QDir::cleanPath(root.absoluteFilePath(QString::fromStdString(folder->get()))));
        return out;
    }
    return {};
}

ProjectFile::PluginTable ProjectFile::plugins() const
{
    PluginTable out;
    const auto names = [](const toml::array *list) {
        std::vector<std::string> ids;
        if (list)
            for (const auto &node : *list)
                if (const auto *id = node.as_string(); id && !id->get().empty())
                    ids.push_back(id->get());
        return ids;
    };
    std::map<std::string, toml::table> settings;
    for (const File file : {Shared, Local}) {
        const auto *table = m_tables[file]["plugins"].as_table();
        if (!table)
            continue;
        if (const auto *list = (*table)["enabled"].as_array())
            out.enabled = names(list);
        if (const auto *list = (*table)["disabled"].as_array())
            out.disabled = names(list);
        for (const auto &[key, node] : *table)
            if (const auto *values = node.as_table())
                for (const auto &[setting, value] : *values)
                    settings[std::string(key.str())].insert_or_assign(setting, value);
    }
    for (const auto &[id, table] : settings) {
        std::ostringstream json;
        json << toml::json_formatter(table, toml::format_flags::none);
        out.settingsJson.emplace(id, json.str());
    }
    return out;
}

std::optional<SettingValue> ProjectFile::read(const SettingDefinition &setting) const
{
    const auto file = source(setting);
    if (!file)
        return std::nullopt;
    return application::convertSetting(*fromToml(*tomlNode(m_tables[*file], setting.id)), setting.type);
}

void ProjectFile::write(const SettingDefinition &setting, const SettingValue &value)
{
    store(setting, application::convertSetting(value, setting.type));
}

void ProjectFile::remove(const SettingDefinition &setting)
{
    store(setting, std::nullopt);
}

void ProjectFile::store(const SettingDefinition &setting, const std::optional<SettingValue> &value)
{
    m_writeFailed = true;
    if (!setting.projectable || !m_writable)
        return;
    const auto file = pathOf(path(Shared));
    const auto bytes = application::readOwnedFile(file);
    if (!bytes && !value) {
        m_writeFailed = false;
        return;
    }
    const std::string text = bytes ? *bytes : formatTomlFile({}, kVersion);
    auto patched = patchTomlSetting(text, setting.id, value);
    if (!patched) {
        auto parsed = parseTomlFile(text, kVersion);
        auto *table = std::get_if<toml::table>(&parsed);
        if (!table)
            return;
        if (value)
            putToml(*tomlParent(*table, setting.id, true), idSegments(setting.id).back(), *value);
        else
            eraseToml(*table, setting.id);
        patched = formatTomlFile(*table, kVersion);
    }
    QDir().mkpath(m_root + QStringLiteral("/.hikari"));
    if (!application::writeFileAtomically(file, *patched))
        return;
    if (auto parsed = parseTomlFile(*patched, kVersion); auto *table = std::get_if<toml::table>(&parsed))
        m_tables[Shared] = std::move(*table);
    m_writeFailed = false;
}

}
