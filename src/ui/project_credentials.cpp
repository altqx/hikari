#include "project_credentials.h"

#include "toml_file.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <filesystem>
#include <variant>

namespace hikari::ui {

namespace {

std::filesystem::path pathOf(const QString &path)
{
    return std::filesystem::path(path.toStdU16String());
}

constexpr std::string_view kIgnored[] = {"credentials.toml", "project.local.toml"};

}

ProjectCredentials::ProjectCredentials(QString root) : m_root(std::move(root)) {}

QString ProjectCredentials::path() const
{
    return m_root + QStringLiteral("/.hikari/credentials.toml");
}

QString ProjectCredentials::gitignorePath() const
{
    return m_root + QStringLiteral("/.hikari/.gitignore");
}

bool ProjectCredentials::exists() const
{
    return QFileInfo(path()).isFile();
}

ProjectCredentials::Keys ProjectCredentials::keys(std::optional<application::ParseFailure> *problem) const
{
    Keys out;
    const auto bytes = application::readOwnedFile(pathOf(path()));
    if (!bytes)
        return out;
    auto parsed = parseTomlFile(*bytes, kVersion);
    if (auto *failure = std::get_if<application::ParseFailure>(&parsed)) {
        if (problem)
            *problem = *failure;
        return out;
    }
    if (const auto *table = std::get<toml::table>(parsed)["keys"].as_table())
        for (const auto &[entry, node] : *table)
            if (const auto *key = node.as_string())
                out.emplace(std::string(entry.str()), key->get());
    return out;
}

bool ProjectCredentials::create(const std::vector<std::string> &entries) const
{
    if (!QDir().mkpath(m_root + QStringLiteral("/.hikari")) || !ignoreInGit())
        return false;
    if (exists())
        return true;
    std::string text = "# AI keys for this project. Git ignores this file.\n"
                       "# HikariSub never sends it to Hikari AI.\n"
                       "version = 1\n\n[keys]\n";
    for (const auto &entry : entries) {
        toml::table holder;
        holder.insert(entry, "");
        text += formatToml(holder) + "\n";
    }
    if (!application::writeFileAtomically(pathOf(path()), text))
        return false;
    QFile::setPermissions(path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

bool ProjectCredentials::put(std::string_view entry, std::string_view key) const
{
    if (!create({}))
        return false;
    const auto bytes = application::readOwnedFile(pathOf(path()));
    if (!bytes)
        return false;
    const std::string id = "keys." + std::string(entry);
    auto patched = patchTomlSetting(*bytes, id, application::SettingValue(std::string(key)));
    if (!patched) {
        auto parsed = parseTomlFile(*bytes, kVersion);
        auto *table = std::get_if<toml::table>(&parsed);
        if (!table)
            return false;
        putToml(*tomlParent(*table, id, true), entry, application::SettingValue(std::string(key)));
        patched = formatTomlFile(*table, kVersion);
    }
    return application::writeFileAtomically(pathOf(path()), *patched);
}

bool ProjectCredentials::ignoreInGit() const
{
    const auto file = pathOf(gitignorePath());
    const std::string text = application::readOwnedFile(file).value_or(std::string());
    std::vector<std::string_view> lines;
    for (std::size_t start = 0; start < text.size();) {
        auto end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        std::string_view line(text.data() + start, end - start);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            line.remove_suffix(1);
        if (line.starts_with('/'))
            line.remove_prefix(1);
        lines.push_back(line);
        start = end + 1;
    }
    std::string out = text;
    for (const auto name : kIgnored) {
        if (std::ranges::find(lines, name) != lines.end())
            continue;
        if (!out.empty() && !out.ends_with('\n'))
            out += '\n';
        out += std::string(name) + '\n';
    }
    return out == text || application::writeFileAtomically(file, out);
}

}
