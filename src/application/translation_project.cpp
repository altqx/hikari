#include "hikari/application/translation_project.h"

namespace hikari::application {

namespace {

std::u8string_view trimmed(std::u8string_view text)
{
    while (!text.empty() && (text.front() == u8' ' || text.front() == u8'\t'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == u8' ' || text.back() == u8'\t'))
        text.remove_suffix(1);
    return text;
}

bool inside(const std::filesystem::path &folder, const std::filesystem::path &root)
{
    const std::filesystem::path relative = folder.lexically_relative(root);
    return !relative.empty() && *relative.begin() != "..";
}

}

std::optional<std::filesystem::path> honyaFolder(const std::optional<std::filesystem::path> &projectRoot,
                                                 std::u8string_view glossary,
                                                 const std::optional<std::filesystem::path> &subtitle,
                                                 std::u8string_view pointer)
{
    if (projectRoot) {
        const std::filesystem::path root = projectRoot->lexically_normal();
        const std::filesystem::path sub(trimmed(glossary));
        if (sub.empty())
            return root;
        if (sub.is_absolute() || sub.has_root_name())
            return std::nullopt;
        const std::filesystem::path folder = (root / sub).lexically_normal();
        return inside(folder, root) ? std::optional(folder) : std::nullopt;
    }
    const std::filesystem::path named(trimmed(pointer));
    if (!subtitle || named.empty() || named.has_root_name())
        return std::nullopt;
    return named.is_absolute() ? named.lexically_normal() : (subtitle->parent_path() / named).lexically_normal();
}

}
