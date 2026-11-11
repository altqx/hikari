#include "hikari/api/folder_skills.h"

#include <algorithm>
#include <fstream>

namespace hikari::api {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view kSkillFile = "SKILL.md";

std::string utf8(const fs::path &path)
{
    const std::u8string text = path.generic_u8string();
    return {reinterpret_cast<const char *>(text.data()), text.size()};
}

fs::path fromUtf8(std::string_view text)
{
    return fs::path(std::u8string(reinterpret_cast<const char8_t *>(text.data()), text.size()));
}

bool hidden(const fs::path &path)
{
    return utf8(path.filename()).starts_with('.');
}

void collect(const fs::path &dir, const fs::path &relative, int depth, std::vector<std::string> &files)
{
    std::error_code error;
    for (const fs::directory_entry &entry : fs::directory_iterator(dir, error)) {
        if (files.size() >= FolderSkills::kMaxFiles)
            return;
        if (hidden(entry.path()) || entry.is_symlink(error))
            continue;
        const fs::path name = relative / entry.path().filename();
        if (entry.is_regular_file(error))
            files.push_back(utf8(name));
        else if (entry.is_directory(error) && depth + 1 < FolderSkills::kMaxDepth)
            collect(entry.path(), name, depth + 1, files);
    }
}

}

bool isSkillName(std::string_view name)
{
    if (name.empty() || name.size() > 64 || name.front() == '-' || name.back() == '-')
        return false;
    char previous = 0;
    for (const char c : name) {
        const bool allowed = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
        if (!allowed || (c == '-' && previous == '-'))
            return false;
        previous = c;
    }
    return true;
}

std::optional<SkillFolder> FolderSkills::folder(const fs::path &dir) const
{
    std::error_code error;
    const fs::directory_entry entry(dir, error);
    if (error || entry.is_symlink(error) || !entry.is_directory(error))
        return std::nullopt;
    const std::string name = utf8(dir.filename());
    if (!isSkillName(name))
        return std::nullopt;
    const fs::directory_entry skillFile(dir / kSkillFile, error);
    if (error || skillFile.is_symlink(error) || !skillFile.is_regular_file(error))
        return std::nullopt;
    SkillFolder skill{name, {}};
    collect(dir, {}, 0, skill.files);
    std::sort(skill.files.begin(), skill.files.end());
    if (std::find(skill.files.begin(), skill.files.end(), kSkillFile) == skill.files.end())
        return std::nullopt;
    return skill;
}

std::vector<SkillFolder> FolderSkills::skills() const
{
    std::vector<fs::path> dirs;
    std::error_code error;
    for (const fs::directory_entry &entry : fs::directory_iterator(m_root, error))
        if (!hidden(entry.path()))
            dirs.push_back(entry.path());
    std::sort(dirs.begin(), dirs.end());
    std::vector<SkillFolder> found;
    for (const fs::path &dir : dirs) {
        if (found.size() >= kMaxSkills)
            break;
        if (auto skill = folder(dir))
            found.push_back(std::move(*skill));
    }
    return found;
}

std::optional<std::string> FolderSkills::read(std::string_view skill, std::string_view file, std::size_t atMost) const
{
    if (!isSkillName(skill))
        return std::nullopt;
    const auto listed = folder(m_root / fromUtf8(skill));
    if (!listed || std::find(listed->files.begin(), listed->files.end(), file) == listed->files.end())
        return std::nullopt;
    std::ifstream in(m_root / fromUtf8(skill) / fromUtf8(file), std::ios::binary);
    if (!in)
        return std::nullopt;
    std::string bytes(atMost, '\0');
    in.read(bytes.data(), static_cast<std::streamsize>(atMost));
    bytes.resize(static_cast<std::size_t>(in.gcount()));
    return bytes;
}

}
