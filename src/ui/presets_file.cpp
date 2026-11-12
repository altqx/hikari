#include "presets_file.h"

#include "settings_files.h"
#include "toml_file.h"

#include "hikari/application/owned_file.h"
#include "hikari/application/tiers.h"

#include <QDir>
#include <QFileInfo>

#include <filesystem>
#include <string>
#include <variant>
#include <vector>

namespace hikari::ui {

namespace {

constexpr std::string_view kDialogPresets = "automation.dialogPresets";

std::filesystem::path pathOf(const QString &path)
{
    return std::filesystem::path(path.toStdU16String());
}

}

PresetsFile::PresetsFile(QString root) : m_root(std::move(root))
{
    if (m_root.isEmpty())
        return;
    auto loaded = application::loadOwnedFile(pathOf(path()), [&](std::string_view text) {
        auto parsed = parseTomlFile(text, kVersion);
        if (auto *failure = std::get_if<application::ParseFailure>(&parsed))
            return std::optional(*failure);
        m_table = std::move(std::get<toml::table>(parsed));
        return std::optional<application::ParseFailure>();
    });
    if (loaded.problem)
        m_writable = !loaded.problem->keptAs.empty();
}

QString PresetsFile::path() const
{
    const auto file = application::projectPathOf("shiftPresets", pathOf(m_root));
    return file ? QString::fromStdU16String(file->u16string()) : QString();
}

QStringList PresetsFile::list(std::string_view id) const
{
    QStringList out;
    const auto *node = tomlNode(m_table, id);
    if (!node)
        return out;
    std::vector<std::string> rows;
    if (const auto *table = node->as_table(); table && id == kDialogPresets)
        rows = dialogPresetRows(*table);
    else if (const auto value = fromToml(*node))
        if (const auto *list = std::get_if<std::vector<std::string>>(&*value))
            rows = *list;
    for (const auto &row : rows)
        out << QString::fromStdString(row);
    return out;
}

bool PresetsFile::set(std::string_view id, const QStringList &rows)
{
    if (m_root.isEmpty() || !m_writable)
        return false;
    std::vector<std::string> list;
    for (const QString &row : rows)
        list.push_back(row.toStdString());
    toml::table table = m_table;
    if (list.empty()) {
        eraseToml(table, id);
    } else {
        auto *parent = tomlParent(table, id, true);
        const auto leaf = idSegments(id).back();
        if (auto presets = id == kDialogPresets ? dialogPresetTable(list) : std::nullopt)
            parent->insert_or_assign(leaf, std::move(*presets));
        else
            putToml(*parent, leaf, list);
    }
    QDir().mkpath(QFileInfo(path()).absolutePath());
    if (!application::writeFileAtomically(pathOf(path()), formatTomlFile(table, kVersion)))
        return false;
    m_table = std::move(table);
    return true;
}

}
