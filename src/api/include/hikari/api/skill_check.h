#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace hikari::api {

// The rules of src/ai-sidecar/src/skills/skill.ts, so HikariSub can say why a skill isn't offered without the
// sidecar. tests/api/scripts/skill_texts.jsonl holds the cases both sides must agree on.

inline constexpr std::size_t kMaxSkillDescription = 1024;

struct SkillText {
    std::map<std::string, std::string> fields;
    std::string body;
};

// The YAML frontmatter as far as skills use it: `key: value` lines with plain, quoted or block (`>`, `|`) values.
std::optional<SkillText> parseSkill(std::string_view text);

enum class SkillProblem { None, NotASkillName, NoSkillFile, NotText, NoFrontmatter, NameMismatch, NoDescription };

struct SkillCheck {
    SkillProblem problem = SkillProblem::None;
    std::string description; // with None
    std::string namedAs;     // with NameMismatch
};

SkillCheck checkSkill(std::string_view folder, std::string_view text);

}
