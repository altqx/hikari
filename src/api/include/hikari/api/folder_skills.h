#pragma once

#include "hikari/api/host.h"

#include <filesystem>

namespace hikari::api {

// A skill name as the Agent Skills format allows it: 1-64 of a-z and 0-9, in words joined by single hyphens.
bool isSkillName(std::string_view name);

// The skills under one folder: each subfolder named as a skill, holding a SKILL.md. Hidden entries and symlinks are
// skipped, so a skill reaches no file outside its folder.
class FolderSkills final : public SkillHost {
public:
    static constexpr std::size_t kMaxSkills = 100;
    static constexpr std::size_t kMaxFiles = 100;
    static constexpr int kMaxDepth = 4;

    explicit FolderSkills(std::filesystem::path root) : m_root(std::move(root)) {}

    std::vector<SkillFolder> skills() const override;
    std::optional<std::string> read(std::string_view skill, std::string_view file, std::size_t atMost) const override;

private:
    std::optional<SkillFolder> folder(const std::filesystem::path &dir) const;

    std::filesystem::path m_root;
};

}
