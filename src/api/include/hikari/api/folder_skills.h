#pragma once

#include "hikari/api/host.h"
#include "hikari/api/skill_check.h"

#include <filesystem>

namespace hikari::api {

// A skill name as the Agent Skills format allows it: 1-64 of a-z and 0-9, in words joined by single hyphens.
bool isSkillName(std::string_view name);

// One folder under a skills folder, as Settings shows it: offered, or why not.
struct SkillReport {
    std::string folder;
    SkillOrigin origin = SkillOrigin::Mine;
    SkillCheck check;
    std::size_t files = 0; // offered, at most FolderSkills::kMaxFiles
    std::size_t found = 0; // in the folder, kMaxDepth deep
};

// The skills under one folder: each subfolder named as a skill, holding a SKILL.md. Hidden entries and symlinks are
// skipped, so a skill reaches no file outside its folder. Folders are walked in name order, and SKILL.md is always
// among the files kept.
class FolderSkills {
public:
    static constexpr std::size_t kMaxSkills = 100;
    static constexpr std::size_t kMaxFiles = 100;
    static constexpr int kMaxDepth = 4;

    explicit FolderSkills(std::filesystem::path root, SkillOrigin origin = SkillOrigin::Mine)
        : m_root(std::move(root)), m_origin(origin)
    {
    }

    std::vector<SkillFolder> skills() const;
    // A skill folder whose SKILL.md passes checkSkill.
    bool offers(std::string_view skill) const;
    // Every folder, broken ones too, until kMaxSkills are offered.
    std::vector<SkillReport> report() const;
    std::optional<std::string> read(std::string_view skill, std::string_view file, std::size_t atMost) const;

private:
    std::optional<SkillFolder> folder(const std::filesystem::path &dir) const;

    std::filesystem::path m_root;
    SkillOrigin m_origin;
};

// The user's skills, with a caller's Project skills (`<project>/.hikari/skills`) over them: a Project skill hides
// the user's of the same name. A `.hikari` or `skills` folder that is a symlink gives no Project skills.
class SkillTiers final : public SkillHost {
public:
    explicit SkillTiers(std::filesystem::path userRoot) : m_mine(std::move(userRoot)) {}

    static std::filesystem::path projectFolder(const std::filesystem::path &project);
    // The Project's folders first, then the user's.
    std::vector<SkillReport> report(const std::filesystem::path &project) const;

    std::vector<SkillFolder> skills(const std::filesystem::path &project) const override;
    std::optional<std::string> read(const std::filesystem::path &project, std::string_view skill, std::string_view file,
                                    std::size_t atMost) const override;

private:
    std::optional<FolderSkills> projectSkills(const std::filesystem::path &project) const;

    FolderSkills m_mine;
};

}
