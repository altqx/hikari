#include "hikari/api/folder_skills.h"

#include "hikari/api/json.h"

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

std::vector<fs::directory_entry> sortedEntries(const fs::path &dir)
{
    std::vector<fs::directory_entry> entries;
    std::error_code error;
    for (const fs::directory_entry &entry : fs::directory_iterator(dir, error))
        if (!hidden(entry.path()))
            entries.push_back(entry);
    std::sort(entries.begin(), entries.end(),
              [](const auto &a, const auto &b) { return utf8(a.path().filename()) < utf8(b.path().filename()); });
    return entries;
}

void collect(const fs::path &dir, const fs::path &relative, int depth, std::vector<std::string> &files)
{
    std::error_code error;
    for (const fs::directory_entry &entry : sortedEntries(dir)) {
        if (files.size() >= FolderSkills::kMaxFiles)
            return;
        if (entry.is_symlink(error))
            continue;
        const fs::path name = relative / entry.path().filename();
        if (depth == 0 && name == kSkillFile)
            continue;
        if (entry.is_regular_file(error))
            files.push_back(utf8(name));
        else if (entry.is_directory(error) && depth + 1 < FolderSkills::kMaxDepth)
            collect(entry.path(), name, depth + 1, files);
    }
}

std::size_t countFiles(const fs::path &dir, int depth)
{
    std::size_t count = 0;
    std::error_code error;
    for (const fs::directory_entry &entry : sortedEntries(dir)) {
        if (entry.is_symlink(error))
            continue;
        if (entry.is_regular_file(error))
            ++count;
        else if (entry.is_directory(error) && depth + 1 < FolderSkills::kMaxDepth)
            count += countFiles(entry.path(), depth + 1);
    }
    return count;
}

constexpr std::size_t kMaxSkillMdBytes = 256 * 1024;

bool realFolder(const fs::path &dir)
{
    std::error_code error;
    const fs::directory_entry entry(dir, error);
    return !error && !entry.is_symlink(error) && entry.is_directory(error);
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
    SkillFolder skill{name, {std::string(kSkillFile)}, m_origin};
    collect(dir, {}, 0, skill.files);
    std::sort(skill.files.begin(), skill.files.end());
    return skill;
}

bool FolderSkills::offers(std::string_view skill) const
{
    const auto text = read(skill, kSkillFile, kMaxSkillMdBytes + 1);
    return text && text->size() <= kMaxSkillMdBytes && isUtf8(*text) &&
           checkSkill(skill, *text).problem == SkillProblem::None;
}

std::vector<SkillFolder> FolderSkills::skills() const
{
    std::vector<SkillFolder> found;
    for (const fs::directory_entry &entry : sortedEntries(m_root)) {
        if (found.size() >= kMaxSkills)
            break;
        if (auto skill = folder(entry.path()))
            found.push_back(std::move(*skill));
    }
    return found;
}

std::vector<SkillReport> FolderSkills::report() const
{
    std::vector<SkillReport> out;
    std::size_t offered = 0;
    std::error_code error;
    for (const fs::directory_entry &entry : sortedEntries(m_root)) {
        if (offered >= kMaxSkills)
            break;
        if (entry.is_symlink(error) || !entry.is_directory(error))
            continue;
        SkillReport report{utf8(entry.path().filename()), m_origin, {}, 0, 0};
        const auto skill = isSkillName(report.folder) ? folder(entry.path()) : std::nullopt;
        if (!isSkillName(report.folder)) {
            report.check.problem = SkillProblem::NotASkillName;
        } else if (!skill) {
            report.check.problem = SkillProblem::NoSkillFile;
        } else {
            const auto text = read(report.folder, kSkillFile, kMaxSkillMdBytes + 1);
            report.check = !text || text->size() > kMaxSkillMdBytes || !isUtf8(*text)
                               ? SkillCheck{SkillProblem::NotText, {}, {}}
                               : checkSkill(report.folder, *text);
            report.files = skill->files.size();
            report.found = countFiles(entry.path(), 0);
            ++offered;
        }
        out.push_back(std::move(report));
    }
    return out;
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

fs::path SkillTiers::projectFolder(const fs::path &project)
{
    return project / ".hikari" / "skills";
}

std::optional<FolderSkills> SkillTiers::projectSkills(const fs::path &project) const
{
    if (project.empty() || !realFolder(project / ".hikari") || !realFolder(projectFolder(project)))
        return std::nullopt;
    return FolderSkills(projectFolder(project), SkillOrigin::Project);
}

std::vector<SkillReport> SkillTiers::report(const fs::path &project) const
{
    const auto tier = projectSkills(project);
    std::vector<SkillReport> out = tier ? tier->report() : std::vector<SkillReport>();
    std::vector<SkillReport> mine = m_mine.report();
    out.insert(out.end(), std::make_move_iterator(mine.begin()), std::make_move_iterator(mine.end()));
    return out;
}

std::vector<SkillFolder> SkillTiers::skills(const fs::path &project) const
{
    std::vector<SkillFolder> found = m_mine.skills();
    if (const auto tier = projectSkills(project)) {
        std::vector<SkillFolder> ours = tier->skills();
        std::erase_if(ours, [&](const SkillFolder &s) { return !tier->offers(s.name); });
        std::erase_if(found, [&](const SkillFolder &mine) {
            return std::any_of(ours.begin(), ours.end(), [&](const SkillFolder &s) { return s.name == mine.name; });
        });
        found.insert(found.end(), std::make_move_iterator(ours.begin()), std::make_move_iterator(ours.end()));
        std::sort(found.begin(), found.end(), [](const SkillFolder &a, const SkillFolder &b) { return a.name < b.name; });
    }
    return found;
}

std::optional<std::string> SkillTiers::read(const fs::path &project, std::string_view skill, std::string_view file,
                                            std::size_t atMost) const
{
    if (const auto tier = projectSkills(project); tier && tier->offers(skill))
        return tier->read(skill, file, atMost);
    return m_mine.read(skill, file, atMost);
}

}
