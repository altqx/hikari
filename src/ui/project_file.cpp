#include "project_file.h"

#include "toml_file.h"

#include <QDir>

#include <filesystem>
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

application::ai::KeyName keyNameOf(const toml::table &table)
{
    return {table["env"].value_or(std::string()), table["entry"].value_or(std::string())};
}

bool namesOnly(const toml::table &table)
{
    for (const auto &[field, node] : table)
        if (field.str() != "env" && field.str() != "entry")
            return false;
    return true;
}

std::string firstString(const toml::table &table)
{
    for (const auto &[field, node] : table)
        if (field.str() != "env" && field.str() != "entry")
            if (const auto *text = node.as_string())
                return text->get();
    return {};
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

application::ai::ProjectKeyNames ProjectFile::aiKeyNames() const
{
    application::ai::ProjectKeyNames out;
    for (const File file : {Shared, Local}) {
        const auto *keys = m_tables[file]["ai"]["keys"].as_table();
        if (!keys)
            continue;
        for (const auto &[provider, node] : *keys) {
            const std::string id(provider.str());
            const auto *table = node.as_table();
            if (table && namesOnly(*table)) {
                if (auto name = keyNameOf(*table); !name.empty())
                    out.byProvider[id] = std::move(name);
                continue;
            }
            if (file == Shared)
                out.valuesFound.push_back(id);
        }
    }
    if (m_tables[Shared]["keys"].is_table())
        out.valuesFound.push_back("keys");
    return out;
}

std::vector<ProjectFile::KeyValue> ProjectFile::aiKeyValues() const
{
    std::vector<KeyValue> out;
    if (const auto *keys = m_tables[Shared]["ai"]["keys"].as_table())
        for (const auto &[provider, node] : *keys) {
            if (const auto *text = node.as_string())
                out.push_back({std::string(provider.str()), false, text->get()});
            else if (const auto *table = node.as_table(); table && !namesOnly(*table))
                out.push_back({std::string(provider.str()), false, firstString(*table)});
        }
    if (const auto *keys = m_tables[Shared]["keys"].as_table())
        for (const auto &[entry, node] : *keys)
            if (const auto *text = node.as_string())
                out.push_back({std::string(entry.str()), true, text->get()});
    return out;
}

bool ProjectFile::setAiKeyName(const std::string &providerId, const application::ai::KeyName &name)
{
    const std::string id = "ai.keys." + providerId;
    if (name.empty())
        return storeNode(id, nullptr);
    toml::table value;
    value.is_inline(true);
    if (!name.env.empty())
        value.insert("env", name.env);
    if (!name.entry.empty())
        value.insert("entry", name.entry);
    return storeNode(id, &value);
}

bool ProjectFile::removeKeysEntry(const std::string &entry)
{
    return storeNode("keys." + entry, nullptr);
}

bool ProjectFile::storeNode(std::string_view id, const toml::table *value)
{
    if (!m_writable)
        return false;
    const auto file = pathOf(path(Shared));
    const auto bytes = application::readOwnedFile(file);
    if (!bytes && !value)
        return true;
    const std::string text = bytes ? *bytes : formatTomlFile({}, kVersion);
    auto patched = patchTomlNode(text, id, value);
    if (!patched) {
        auto parsed = parseTomlFile(text, kVersion);
        auto *table = std::get_if<toml::table>(&parsed);
        if (!table)
            return false;
        if (value)
            tomlParent(*table, id, true)->insert_or_assign(idSegments(id).back(), *value);
        else
            eraseToml(*table, id);
        patched = formatTomlFile(*table, kVersion);
    }
    QDir().mkpath(m_root + QStringLiteral("/.hikari"));
    if (!application::writeFileAtomically(file, *patched))
        return false;
    if (auto parsed = parseTomlFile(*patched, kVersion); auto *table = std::get_if<toml::table>(&parsed))
        m_tables[Shared] = std::move(*table);
    return true;
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
